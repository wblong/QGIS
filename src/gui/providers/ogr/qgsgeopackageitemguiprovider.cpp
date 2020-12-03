/***************************************************************************
      qgsgeopackageitemguiprovider.h
      -------------------
    begin                : June, 2019
    copyright            : (C) 2019 by Peter Petrik
    email                : zilolv at gmail dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsgeopackageitemguiprovider.h"
///@cond PRIVATE

#include <QAction>
#include <QMenu>
#include <QString>
#include <QMessageBox>

#include "qgsvectorlayer.h"
#include "qgsrasterlayer.h"
#include "qgsgeopackagedataitems.h"
#include "qgsnewnamedialog.h"
#include "qgsnewgeopackagelayerdialog.h"
#include "qgsmessageoutput.h"
#include "qgsapplication.h"
#include "qgsgeopackagerasterwritertask.h"
#include "qgsvectorlayerexporter.h"
#include "qgsproviderregistry.h"
#include "qgsproject.h"
#include "gdal.h"
#include "qgsogrdataitems.h"
#include "qgsogrdbconnection.h"
#include "qgsgeopackageproviderconnection.h"
#include "qgsmessagebar.h"

void QgsGeoPackageItemGuiProvider::populateContextMenu( QgsDataItem *item, QMenu *menu,
    const QList<QgsDataItem *> &,
    QgsDataItemGuiContext context )
{
  if ( QgsGeoPackageVectorLayerItem *layerItem = qobject_cast< QgsGeoPackageVectorLayerItem * >( item ) )
  {
    // Check capabilities
    if ( layerItem->capabilities2() & QgsDataItem::Capability::Rename )
    {
      QAction *actionRenameLayer = new QAction( tr( "Rename Layer '%1'…" ).arg( layerItem->name() ), this );
      QVariantMap data;
      const QString uri = layerItem->uri();
      const QString providerKey = layerItem->providerKey();
      const QStringList tableNames = layerItem->tableNames();
      QPointer< QgsDataItem > itemPointer( layerItem );
      connect( actionRenameLayer, &QAction::triggered, this, [this, uri, providerKey, tableNames, itemPointer, context ]
      {
        renameVectorLayer( uri, providerKey, tableNames, itemPointer, context );
      } );
      menu->addAction( actionRenameLayer );
    }
  }

  if ( QgsGeoPackageRootItem *rootItem = qobject_cast< QgsGeoPackageRootItem * >( item ) )
  {
    QAction *actionNew = new QAction( tr( "New Connection…" ), rootItem->parent() );
    connect( actionNew, &QAction::triggered, rootItem, &QgsGeoPackageRootItem::newConnection );
    menu->addAction( actionNew );

    QAction *actionCreateDatabase = new QAction( tr( "Create Database…" ), rootItem->parent() );
    QPointer< QgsGeoPackageRootItem > rootItemPointer( rootItem );
    connect( actionCreateDatabase, &QAction::triggered, this, [this, rootItemPointer ]
    {
      createDatabase( rootItemPointer );
    } );
    menu->addAction( actionCreateDatabase );
  }

  if ( QgsGeoPackageCollectionItem *collectionItem = qobject_cast< QgsGeoPackageCollectionItem * >( item ) )
  {
    if ( QgsOgrDbConnection::connectionList( QStringLiteral( "GPKG" ) ).contains( collectionItem->name() ) )
    {
      QAction *actionDeleteConnection = new QAction( tr( "Remove Connection" ), collectionItem->parent() );
      connect( actionDeleteConnection, &QAction::triggered, collectionItem, &QgsGeoPackageConnectionItem::deleteConnection );
      menu->addAction( actionDeleteConnection );
    }
    else
    {
      // Add to stored connections
      QAction *actionAddConnection = new QAction( tr( "Add Connection" ), collectionItem->parent() );
      connect( actionAddConnection, &QAction::triggered, collectionItem, &QgsGeoPackageCollectionItem::addConnection );
      menu->addAction( actionAddConnection );
    }

    QAction *sep = new QAction( collectionItem->parent() );
    sep->setSeparator( true );
    menu->addAction( sep );

    QString message = QObject::tr( "Delete %1…" ).arg( collectionItem->name() );
    QAction *actionDelete = new QAction( message, collectionItem->parent() );
    QString collectionPath = collectionItem->path();
    QString collectionName = collectionItem->name();
    QPointer< QgsDataItem > parent( collectionItem->parent() );
    connect( actionDelete, &QAction::triggered, this, [this, collectionPath, collectionName, parent, context ]()
    {
      deleteGpkg( collectionPath, collectionName, parent, context );
    } );
    menu->addAction( actionDelete );

    // Run VACUUM
    QAction *actionVacuumDb = new QAction( tr( "Compact Database (VACUUM)" ), collectionItem->parent() );
    QVariantMap dataVacuum;
    const QString name = collectionItem->name();
    const QString path = collectionItem->path();
    actionVacuumDb->setData( dataVacuum );
    connect( actionVacuumDb, &QAction::triggered, this, [this, context, name, path]
    {
      vacuum( path, name, context );
    } );
    menu->addAction( actionVacuumDb );
  }
}

void QgsGeoPackageItemGuiProvider::deleteGpkg( const QString &itemPath, const QString &name, QPointer< QgsDataItem > parent, const QgsDataItemGuiContext &context )
{
  QString path = itemPath;
  path = path.remove( QStringLiteral( "gpkg:/" ) );
  if ( parent )
  {
    const QString title = QObject::tr( "Delete GeoPackage" );
    // Check if the layer is in the project
    const QgsMapLayer *projectLayer = nullptr;
    const auto mapLayers = QgsProject::instance()->mapLayers();
    for ( auto it = mapLayers.constBegin(); it != mapLayers.constEnd(); ++it )
    {
      const QVariantMap parts = QgsProviderRegistry::instance()->decodeUri( it.value()->providerType(), it.value()->source() );
      if ( parts.value( QStringLiteral( "path" ) ).toString() == path )
      {
        projectLayer = it.value();
      }
    }
    if ( ! projectLayer )
    {
      const QString confirmMessage = QObject::tr( "Are you sure you want to delete '%1'?" ).arg( path );

      if ( QMessageBox::question( nullptr, title,
                                  confirmMessage,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No ) != QMessageBox::Yes )
        return;

      if ( !QFile::remove( path ) )
      {
        notify( title, tr( "Could not delete GeoPackage." ), context, Qgis::Critical );
      }
      else
      {
        notify( title, tr( "GeoPackage deleted successfully." ), context, Qgis::Success );
        // If the deleted file was a stored connection, remove it too
        if ( ! name.isEmpty() )
        {
          QgsProviderMetadata *md { QgsProviderRegistry::instance()->providerMetadata( QStringLiteral( "ogr" ) ) };
          try
          {
            md->deleteConnection( name );
          }
          catch ( QgsProviderConnectionException &ex )
          {
            QgsDebugMsg( QStringLiteral( "Could not remove GPKG connection %1: %2" ).arg( name, ex.what() ) );
          }
        }
        if ( parent )
          parent->refresh();
      }
    }
    else
    {
      notify( title, QObject::tr( "The GeoPackage '%1' cannot be deleted because it is in the current project as '%2',"
                                  " remove it from the project and retry." ).arg( path, projectLayer->name() ), context, Qgis::Critical );
    }
  }
}

bool QgsGeoPackageItemGuiProvider::rename( QgsDataItem *item, const QString &newName, QgsDataItemGuiContext context )
{
  if ( QgsGeoPackageVectorLayerItem *layerItem = qobject_cast< QgsGeoPackageVectorLayerItem * >( item ) )
  {
    // Checks that name does not exist yet
    if ( layerItem->tableNames().contains( newName ) )
    {
      return true;
    }
    // Check if the layer(s) are in the registry
    const QList<QgsMapLayer *> layersList( layerItem->layersInProject() );
    if ( ! layersList.isEmpty( ) )
    {
      if ( QMessageBox::question( nullptr, QObject::tr( "Rename Layer" ), QObject::tr( "The layer <b>%1</b> is loaded in the current project with name <b>%2</b>,"
                                  " do you want to remove it from the project and rename it?" ).arg( layerItem->name(), layersList.at( 0 )->name() ), QMessageBox::Yes | QMessageBox::No, QMessageBox::No ) != QMessageBox::Yes )
      {
        return true;
      }
    }
    if ( ! layersList.isEmpty() )
    {
      QgsProject::instance()->removeMapLayers( layersList );
    }

    const QVariantMap parts = QgsProviderRegistry::instance()->decodeUri( layerItem->providerKey(), layerItem->uri() );
    QString errCause;
    if ( parts.empty() || parts.value( QStringLiteral( "path" ) ).isNull() || parts.value( QStringLiteral( "layerName" ) ).isNull() )
    {
      errCause = QObject::tr( "Layer URI %1 is not valid!" ).arg( layerItem->uri() );
    }
    else
    {
      const QList<QgsMapLayer *> layersList( layerItem->layersInProject() );
      if ( ! layersList.isEmpty( ) )
      {
        if ( QMessageBox::question( nullptr, QObject::tr( "Rename Layer" ), QObject::tr( "The layer <b>%1</b> exists in the current project <b>%2</b>,"
                                    " do you want to remove it from the project and rename it?" ).arg( layerItem->name(), layersList.at( 0 )->name() ), QMessageBox::Yes | QMessageBox::No, QMessageBox::No ) != QMessageBox::Yes )
        {
          return true;
        }
      }
      if ( ! layersList.isEmpty() )
      {
        QgsProject::instance()->removeMapLayers( layersList );
      }

      // Actually rename
      QgsProviderMetadata *md { QgsProviderRegistry::instance()->providerMetadata( QStringLiteral( "ogr" ) ) };
      std::unique_ptr<QgsGeoPackageProviderConnection> conn( static_cast<QgsGeoPackageProviderConnection *>( md->createConnection( layerItem->collection()->path(), QVariantMap() ) ) );
      QString oldName = parts.value( QStringLiteral( "layerName" ) ).toString();
      if ( ! conn->tableExists( QString(), oldName ) )
      {
        errCause = QObject::tr( "There was an error retrieving the connection %1!" ).arg( layerItem->collection()->name() );
      }
      else
      {
        try
        {
          conn->renameVectorTable( QString(), oldName, newName );
        }
        catch ( QgsProviderConnectionException &ex )
        {
          errCause = ex.what();
        }
      }
      if ( errCause.isEmpty() && context.messageBar() )
      {
        context.messageBar()->pushMessage( tr( "Rename Layer" ), tr( "The layer <b>%1</b> was successfully renamed." ).arg( oldName ), Qgis::Success );
      }
    }

    if ( ! errCause.isEmpty() )
    {
      notify( QObject::tr( "Error renaming layer" ), errCause, context, Qgis::Critical );
    }
    else if ( layerItem->parent() )
    {
      layerItem->parent()->refresh();
    }

    return errCause.isEmpty();
  }

  return false;
}

void QgsGeoPackageItemGuiProvider::renameVectorLayer( const QString &uri, const QString &key, const QStringList &tableNames,
    QPointer< QgsDataItem > item, const QgsDataItemGuiContext &context )
{
  // Get layer name from layer URI
  QVariantMap pieces( QgsProviderRegistry::instance()->decodeUri( key, uri ) );
  QString layerName = pieces[QStringLiteral( "layerName" )].toString();

  // Allow any character, except |, which could create confusion, due to it being
  // the URI componenent separator. And ideally we should remove that restriction
  // by using proper escaping of |
  const QRegExp checkRe( QStringLiteral( R"re([^|]+)re" ) );
  QgsNewNameDialog dlg( uri, layerName, QStringList(), tableNames, checkRe );
  dlg.setOverwriteEnabled( false );

  if ( dlg.exec() != dlg.Accepted || dlg.name().isEmpty() || dlg.name() == layerName )
    return;

  rename( item, dlg.name(), context );
}


bool QgsGeoPackageItemGuiProvider::deleteLayer( QgsLayerItem *layerItem, QgsDataItemGuiContext context )
{
  if ( QgsGeoPackageAbstractLayerItem *item = qobject_cast< QgsGeoPackageAbstractLayerItem * >( layerItem ) )
  {
    // Check if the layer(s) are in the registry
    const QList<QgsMapLayer *> layersList( item->layersInProject( ) );
    if ( ! layersList.isEmpty( ) )
    {
      if ( QMessageBox::question( nullptr, QObject::tr( "Delete Layer" ), QObject::tr( "The layer <b>%1</b> exists in the current project <b>%2</b>,"
                                  " do you want to remove it from the project and delete it?" ).arg( item->name(),
                                      layersList.at( 0 )->name() ), QMessageBox::Yes | QMessageBox::No, QMessageBox::No ) != QMessageBox::Yes )
      {
        return false;
      }
    }
    else if ( QMessageBox::question( nullptr, QObject::tr( "Delete Layer" ),
                                     QObject::tr( "Are you sure you want to delete layer <b>%1</b> from GeoPackage?" ).arg( item->name() ),
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No ) != QMessageBox::Yes )
    {
      return false;
    }

    if ( ! layersList.isEmpty() )
    {
      QgsProject::instance()->removeMapLayers( layersList );
    }

    QString errCause;
    bool res = item->executeDeleteLayer( errCause );
    if ( !res )
    {
      notify( tr( "Delete Layer" ), errCause, context, Qgis::Critical );
    }
    else
    {

      QgsGeoPackageConnectionItem *connectionParentItem = qobject_cast<QgsGeoPackageConnectionItem *>( item->parent() );
      if ( connectionParentItem )
      {
        if ( QMessageBox::question( nullptr, QObject::tr( "Delete Layer" ), QObject::tr( "The layer <b>%1</b> was successfully deleted."
                                    " Compact database (VACUUM) <b>%2</b> now?" ).arg( item->name(), connectionParentItem->name() ), QMessageBox::Yes | QMessageBox::No, QMessageBox::No ) == QMessageBox::Yes )
        {
          vacuumGeoPackageDbAction( connectionParentItem->path(), connectionParentItem->name(), context );
        }
      }
      else
      {
        notify( tr( "Delete Layer" ), tr( "The layer <b>%1</b> was successfully deleted." ).arg( item->name() ), context, Qgis::Success );
      }
      if ( item->parent() )
      {
        item->parent()->refresh();
      }
    }
    return true;
  }
  else
  {
    return false;
  }
}

void QgsGeoPackageItemGuiProvider::vacuumGeoPackageDbAction( const QString &path, const QString &name, const QgsDataItemGuiContext &context )
{
  Q_UNUSED( path )
  QString errCause;
  bool result = QgsGeoPackageCollectionItem::vacuumGeoPackageDb( name, path, errCause );
  if ( !result || !errCause.isEmpty() )
  {
    notify( tr( "Database compact (VACUUM)" ), errCause, context, Qgis::Critical );
  }
  else if ( context.messageBar() )
  {
    context.messageBar()->pushMessage( tr( "Database compacted" ), Qgis::Success );
  }
}

void QgsGeoPackageItemGuiProvider::vacuum( const QString &itemPath, const QString &name, const QgsDataItemGuiContext &context )
{
  QString path = itemPath;
  path = path.remove( QStringLiteral( "gpkg:/" ) );
  vacuumGeoPackageDbAction( path, name, context );
}

void QgsGeoPackageItemGuiProvider::createDatabase( QPointer< QgsGeoPackageRootItem > item )
{
  if ( item )
  {
    QgsNewGeoPackageLayerDialog dialog( nullptr );
    dialog.setCrs( QgsProject::instance()->defaultCrsForNewLayers() );
    if ( dialog.exec() == QDialog::Accepted )
    {
      // Call QFileInfo to normalize paths, see: https://github.com/qgis/QGIS/issues/36832
      if ( QgsOgrDataCollectionItem::saveConnection( QFileInfo( dialog.databasePath() ).filePath(), QStringLiteral( "GPKG" ) ) )
      {
        item->refreshConnections();
      }
    }
  }
}

bool QgsGeoPackageItemGuiProvider::acceptDrop( QgsDataItem *item, QgsDataItemGuiContext )
{
  if ( qobject_cast< QgsGeoPackageCollectionItem * >( item ) )
  {
    return true;
  }
  return false;
}

bool QgsGeoPackageItemGuiProvider::handleDrop( QgsDataItem *item, QgsDataItemGuiContext context, const QMimeData *data, Qt::DropAction )
{
  if ( QgsGeoPackageCollectionItem *collectionItem = qobject_cast< QgsGeoPackageCollectionItem * >( item ) )
  {
    return handleDropGeopackage( collectionItem, data, context );
  }
  return false;
}

bool QgsGeoPackageItemGuiProvider::handleDropGeopackage( QgsGeoPackageCollectionItem *item, const QMimeData *data, const QgsDataItemGuiContext &context )
{
  if ( !QgsMimeDataUtils::isUriList( data ) )
    return false;

  QString uri;

  QStringList importResults;
  bool hasError = false;

  // Main task
  std::unique_ptr< QgsConcurrentFileWriterImportTask > mainTask( new QgsConcurrentFileWriterImportTask( tr( "GeoPackage import" ) ) );
  QgsTaskList importTasks;

  const auto lst = QgsMimeDataUtils::decodeUriList( data );
  for ( const QgsMimeDataUtils::Uri &dropUri : lst )
  {
    // Check that we are not copying over self
    if ( dropUri.uri.startsWith( item->path() ) )
    {
      importResults.append( tr( "You cannot import layer %1 over itself!" ).arg( dropUri.name ) );
      hasError = true;

    }
    else
    {
      QgsMapLayer *srcLayer = nullptr;
      bool owner;
      bool isVector = false;
      QString error;
      // Common checks for raster and vector
      // aspatial is treated like vector
      if ( dropUri.layerType == QLatin1String( "vector" ) )
      {
        // open the source layer
        srcLayer = dropUri.vectorLayer( owner, error );
        isVector = true;
      }
      else if ( dropUri.layerType == QLatin1String( "mesh" ) )
      {
        // unsupported
        hasError = true;
        continue;
      }
      else
      {
        srcLayer = dropUri.rasterLayer( owner, error );
      }
      if ( !srcLayer )
      {
        importResults.append( tr( "%1: %2" ).arg( dropUri.name, error ) );
        hasError = true;
        continue;
      }

      if ( srcLayer->isValid() )
      {
        uri = item->path().remove( QStringLiteral( "gpkg:/" ) );
        QgsDebugMsgLevel( "URI " + uri, 3 );

        // check if the destination layer already exists
        bool exists = false;
        const auto c( item->children() );
        for ( const QgsDataItem *child : c )
        {
          if ( child->name() == dropUri.name )
          {
            exists = true;
          }
        }

        if ( exists && !isVector )
        {
          notify( tr( "Cannot Overwrite Layer" ),
                  tr( "Destination layer <b>%1</b> already exists. Overwriting with raster layers is not currently supported." ).arg( dropUri.name ), context, Qgis::Critical );
        }
        else if ( ! exists || QMessageBox::question( nullptr, tr( "Overwrite Layer" ),
                  tr( "Destination layer <b>%1</b> already exists. Do you want to overwrite it?" ).arg( dropUri.name ), QMessageBox::Yes |  QMessageBox::No ) == QMessageBox::Yes )
        {
          if ( isVector ) // Import vectors and aspatial
          {
            QgsVectorLayer *vectorSrcLayer = qobject_cast < QgsVectorLayer * >( srcLayer );
            QVariantMap options;
            options.insert( QStringLiteral( "driverName" ), QStringLiteral( "GPKG" ) );
            options.insert( QStringLiteral( "update" ), true );
            options.insert( QStringLiteral( "overwrite" ), true );
            options.insert( QStringLiteral( "layerName" ), dropUri.name );
            QgsVectorLayerExporterTask *exportTask = new QgsVectorLayerExporterTask( vectorSrcLayer, uri, QStringLiteral( "ogr" ), vectorSrcLayer->crs(), options, owner );
            mainTask->addSubTask( exportTask, importTasks );
            importTasks << exportTask;
            // when export is successful:
            connect( exportTask, &QgsVectorLayerExporterTask::exportComplete, item, [ = ]()
            {
              notify( tr( "Import to GeoPackage database" ), tr( "Import was successful." ), context, Qgis::Success );
              item->refresh();
            } );

            // when an error occurs:
            connect( exportTask, &QgsVectorLayerExporterTask::errorOccurred, item, [ = ]( int error, const QString & errorMessage )
            {
              if ( error != QgsVectorLayerExporter::ErrUserCanceled )
              {
                QgsMessageOutput *output = QgsMessageOutput::createMessageOutput();
                output->setTitle( tr( "Import to GeoPackage database" ) );
                output->setMessage( tr( "Failed to import some vector layers!\n\n" ) + errorMessage, QgsMessageOutput::MessageText );
                output->showMessage();
              }
            } );

          }
          else  // Import raster
          {
            QgsGeoPackageRasterWriterTask  *exportTask = new QgsGeoPackageRasterWriterTask( dropUri, item->path() );
            mainTask->addSubTask( exportTask, importTasks );
            importTasks << exportTask;
            // when export is successful:
            connect( exportTask, &QgsGeoPackageRasterWriterTask::writeComplete, item, [ = ]()
            {
              notify( tr( "Import to GeoPackage database" ), tr( "Import was successful." ), context, Qgis::Success );
              item->refresh();
            } );

            // when an error occurs:
            connect( exportTask, &QgsGeoPackageRasterWriterTask::errorOccurred, item, [ = ]( QgsGeoPackageRasterWriter::WriterError error, const QString & errorMessage )
            {
              if ( error != QgsGeoPackageRasterWriter::WriterError::ErrUserCanceled )
              {
                QgsMessageOutput *output = QgsMessageOutput::createMessageOutput();
                output->setTitle( tr( "Import to GeoPackage database" ) );
                output->setMessage( tr( "Failed to import some raster layers!\n\n" ) + errorMessage, QgsMessageOutput::MessageText );
                output->showMessage();
              }
              // Always try to delete the imported raster, in case the gpkg has been left
              // in an inconsistent status. Ignore delete errors.
              QString deleteErr;
              item->deleteRasterLayer( dropUri.name, deleteErr );
            } );

          }
        } // do not overwrite
      }
      else
      {
        importResults.append( tr( "%1: Not a valid layer!" ).arg( dropUri.name ) );
        hasError = true;
      }
    } // check for self copy
  } // for each

  if ( hasError )
  {
    QgsMessageOutput *output = QgsMessageOutput::createMessageOutput();
    output->setTitle( tr( "Import to GeoPackage database" ) );
    output->setMessage( tr( "Failed to import some layers!\n\n" ) + importResults.join( QLatin1Char( '\n' ) ), QgsMessageOutput::MessageText );
    output->showMessage();
  }
  if ( ! importTasks.isEmpty() )
  {
    QgsApplication::taskManager()->addTask( mainTask.release() );
  }
  return true;
}

///@endcond
