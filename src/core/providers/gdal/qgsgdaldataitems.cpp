/***************************************************************************
    qgsgdaldataitems.cpp
    ---------------------
    begin                : October 2011
    copyright            : (C) 2011 by Martin Dobias
    email                : wonder dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsgdaldataitems.h"

///@cond PRIVATE
#include "qgsgdalprovider.h"
#include "qgslogger.h"
#include "qgsmbtiles.h"
#include "qgssettings.h"
#include "qgsogrutils.h"
#include "qgsproject.h"
#include "qgsgdalutils.h"
#include "qgsvectortiledataitems.h"
#include "qgsproviderregistry.h"
#include "symbology/qgsstyle.h"

#include <QFileInfo>
#include <QAction>
#include <mutex>
#include <QMessageBox>

// defined in qgsgdalprovider.cpp
void buildSupportedRasterFileFilterAndExtensions( QString &fileFiltersString, QStringList &extensions, QStringList &wildcards );


QgsGdalLayerItem::QgsGdalLayerItem( QgsDataItem *parent,
                                    const QString &name, const QString &path, const QString &uri,
                                    QStringList *sublayers )
  : QgsLayerItem( parent, name, path, uri, QgsLayerItem::Raster, QStringLiteral( "gdal" ) )
{
  mToolTip = uri;
  // save sublayers for subsequent access
  // if there are sublayers, set populated=false so item can be populated on demand
  if ( sublayers && !sublayers->isEmpty() )
  {
    mSublayers = *sublayers;
    // We have sublayers: we are able to create children!
    mCapabilities |= Fertile;
    setState( NotPopulated );
  }
  else
    setState( Populated );
}


bool QgsGdalLayerItem::setCrs( const QgsCoordinateReferenceSystem &crs )
{
  gdal::dataset_unique_ptr hDS( GDALOpen( mPath.toUtf8().constData(), GA_Update ) );
  if ( !hDS )
    return false;

  QString wkt = crs.toWkt( QgsCoordinateReferenceSystem::WKT_PREFERRED_GDAL );
  if ( GDALSetProjection( hDS.get(), wkt.toLocal8Bit().data() ) != CE_None )
  {
    QgsDebugMsg( QStringLiteral( "Could not set CRS" ) );
    return false;
  }

  return true;
}

QVector<QgsDataItem *> QgsGdalLayerItem::createChildren()
{
  QgsDebugMsgLevel( "Entered, path=" + path(), 3 );
  QVector<QgsDataItem *> children;

  // get children from sublayers
  if ( !mSublayers.isEmpty() )
  {
    QgsDataItem *childItem = nullptr;
    QgsDebugMsgLevel( QStringLiteral( "got %1 sublayers" ).arg( mSublayers.count() ), 3 );
    for ( int i = 0; i < mSublayers.count(); i++ )
    {
      QString name = mSublayers[i];
      // if netcdf/hdf use all text after filename
      // for hdf4 it would be best to get description, because the subdataset_index is not very practical
      if ( name.startsWith( QLatin1String( "netcdf" ), Qt::CaseInsensitive ) ||
           name.startsWith( QLatin1String( "hdf" ), Qt::CaseInsensitive ) )
        name = name.mid( name.indexOf( mPath ) + mPath.length() + 1 );
      else
      {
        // remove driver name and file name and initial ':'
        name.remove( name.split( QgsDataProvider::sublayerSeparator() )[0] + ':' );
        name.remove( mPath );
      }
      // remove any : or " left over
      if ( name.startsWith( ':' ) ) name.remove( 0, 1 );
      if ( name.startsWith( '\"' ) ) name.remove( 0, 1 );
      if ( name.endsWith( ':' ) ) name.chop( 1 );
      if ( name.endsWith( '\"' ) ) name.chop( 1 );

      childItem = new QgsGdalLayerItem( this, name, mSublayers[i], mSublayers[i] );
      if ( childItem )
      {
        children.append( childItem );
      }
    }
  }

  return children;
}

QString QgsGdalLayerItem::layerName() const
{
  QFileInfo info( name() );
  if ( info.suffix() == QLatin1String( "gz" ) )
    return info.baseName();
  else
    return info.completeBaseName();
}

// ---------------------------------------------------------------------------

QString QgsGdalDataItemProvider::name()
{
  return QStringLiteral( "GDAL" );
}

int QgsGdalDataItemProvider::capabilities() const
{
  return QgsDataProvider::File | QgsDataProvider::Dir | QgsDataProvider::Net;
}

QgsDataItem *QgsGdalDataItemProvider::createDataItem( const QString &pathIn, QgsDataItem *parentItem )
{
  static QString sFilterString;
  static QStringList sExtensions = QStringList();
  static QStringList sWildcards = QStringList();
  static QMutex sBuildingFilters;

  QString path( pathIn );
  if ( path.isEmpty() )
    return nullptr;

  // if another provider has preference for this path, let it win. This allows us to hide known files
  // more strongly associated with another provider from showing duplicate entries for the ogr provider.
  // e.g. in particular this hides "ept.json" files from showing as a non-functional ogr data item, and
  // instead ONLY shows them as the functional EPT point cloud provider items
  if ( QgsProviderRegistry::instance()->shouldDeferUriForOtherProviders( path, QStringLiteral( "gdal" ) ) )
  {
    return nullptr;
  }

  // hide blocklisted URIs, such as .aux.xml files
  if ( QgsProviderRegistry::instance()->uriIsBlocklisted( path ) )
    return nullptr;

  QgsDebugMsgLevel( "thePath = " + path, 2 );

  // zip settings + info
  QgsSettings settings;
  QString scanZipSetting = settings.value( QStringLiteral( "qgis/scanZipInBrowser2" ), "basic" ).toString();
  QString vsiPrefix = QgsZipItem::vsiPrefix( path );
  bool is_vsizip = ( vsiPrefix == QLatin1String( "/vsizip/" ) );
  bool is_vsigzip = ( vsiPrefix == QLatin1String( "/vsigzip/" ) );
  bool is_vsitar = ( vsiPrefix == QLatin1String( "/vsitar/" ) );

  // should we check ext. only?
  // check if scanItemsInBrowser2 == extension or parent dir in scanItemsFastScanUris
  // TODO - do this in dir item, but this requires a way to inform which extensions are supported by provider
  // maybe a callback function or in the provider registry?
  bool scanExtSetting = false;
  if ( ( settings.value( QStringLiteral( "qgis/scanItemsInBrowser2" ),
                         "extension" ).toString() == QLatin1String( "extension" ) ) ||
       ( parentItem && settings.value( QStringLiteral( "qgis/scanItemsFastScanUris" ),
                                       QStringList() ).toStringList().contains( parentItem->path() ) ) ||
       ( ( is_vsizip || is_vsitar ) && parentItem && parentItem->parent() &&
         settings.value( QStringLiteral( "qgis/scanItemsFastScanUris" ),
                         QStringList() ).toStringList().contains( parentItem->parent()->path() ) ) )
  {
    scanExtSetting = true;
  }

  if ( path.endsWith( QLatin1String( ".shp.zip" ), Qt::CaseInsensitive ) )
  {
    // .shp.zip are vector datasets
    return nullptr;
  }

  // get suffix, removing .gz if present
  QString tmpPath = path; //path used for testing, not for layer creation
  if ( is_vsigzip )
    tmpPath.chop( 3 );
  QFileInfo info( tmpPath );
  QString suffix = info.suffix().toLower();
  // extract basename with extension
  info.setFile( path );
  QString name = info.fileName();

  // If a .tab exists, then the corresponding .map/.dat is very likely a
  // side-car file of the .tab
  if ( suffix == QLatin1String( "map" ) || suffix == QLatin1String( "dat" ) )
  {
    if ( QFileInfo( QDir( info.path() ), info.baseName() + ".tab" ).exists() )
      return nullptr;
  }

  QgsDebugMsgLevel( "path= " + path + " tmpPath= " + tmpPath + " name= " + name
                    + " suffix= " + suffix + " vsiPrefix= " + vsiPrefix, 3 );

  // allow only normal files or VSIFILE items to continue
  if ( !info.isFile() && vsiPrefix.isEmpty() )
    return nullptr;

  // get supported extensions
  static std::once_flag initialized;
  std::call_once( initialized, [ = ]
  {
    buildSupportedRasterFileFilterAndExtensions( sFilterString, sExtensions, sWildcards );
    QgsDebugMsgLevel( QStringLiteral( "extensions: " ) + sExtensions.join( ' ' ), 2 );
    QgsDebugMsgLevel( QStringLiteral( "wildcards: " ) + sWildcards.join( ' ' ), 2 );
  } );

  // skip QGIS style xml files
  if ( path.endsWith( QLatin1String( ".xml" ), Qt::CaseInsensitive ) &&
       QgsStyle::isXmlStyleFile( path ) )
    return nullptr;

  // Filter files by extension
  if ( !sExtensions.contains( suffix ) )
  {
    bool matches = false;
    const auto constSWildcards = sWildcards;
    for ( const QString &wildcard : constSWildcards )
    {
      QRegExp rx( wildcard, Qt::CaseInsensitive, QRegExp::Wildcard );
      if ( rx.exactMatch( info.fileName() ) )
      {
        matches = true;
        break;
      }
    }
    if ( !matches )
      return nullptr;
  }

  // fix vsifile path and name
  if ( !vsiPrefix.isEmpty() )
  {
    // add vsiPrefix to path if needed
    if ( !path.startsWith( vsiPrefix ) )
      path = vsiPrefix + path;
    // if this is a /vsigzip/path_to_zip.zip/file_inside_zip remove the full path from the name
    // no need to change the name I believe
#if 0
    if ( ( is_vsizip || is_vsitar ) && ( path != vsiPrefix + parentItem->path() ) )
    {
      name = path;
      name = name.replace( vsiPrefix + parentItem->path() + '/', "" );
    }
#endif
  }

  if ( suffix == QLatin1String( "mbtiles" ) )
  {
    QgsMbTiles reader( path );
    if ( reader.open() )
    {
      if ( reader.metadataValue( "format" ) == QLatin1String( "pbf" ) )
      {
        // these are vector tiles
        QUrlQuery uq;
        uq.addQueryItem( QStringLiteral( "type" ), QStringLiteral( "mbtiles" ) );
        uq.addQueryItem( QStringLiteral( "url" ), path );
        QString encodedUri = uq.toString();
        return new QgsVectorTileLayerItem( parentItem, name, path, encodedUri );
      }
      else
      {
        // handled by WMS provider
        QUrlQuery uq;
        uq.addQueryItem( QStringLiteral( "type" ), QStringLiteral( "mbtiles" ) );
        uq.addQueryItem( QStringLiteral( "url" ), QUrl::fromLocalFile( path ).toString() );
        QString encodedUri = uq.toString();
        QgsLayerItem *item = new QgsLayerItem( parentItem, name, path, encodedUri, QgsLayerItem::Raster, QStringLiteral( "wms" ) );
        item->setState( QgsDataItem::Populated );
        return item;
      }
    }
  }

  // Filters out the OGR/GDAL supported formats that can contain multiple layers
  // and should be treated like a DB: GeoPackage and SQLite
  // NOTE: this formats are scanned for rasters too and they are handled
  //       by the "ogr" provider. For this reason they must
  //       be skipped by "gdal" provider or the rasters will be listed
  //       twice. ogrSupportedDbLayersExtensions must be kept in sync
  //       with the companion variable (same name) in the ogr provider
  //       class
  // TODO: add more OGR supported multiple layers formats here!
  QStringList ogrSupportedDbLayersExtensions;
  ogrSupportedDbLayersExtensions << QStringLiteral( "gpkg" ) << QStringLiteral( "sqlite" ) << QStringLiteral( "db" ) << QStringLiteral( "gdb" );
  QStringList ogrSupportedDbDriverNames;
  ogrSupportedDbDriverNames << QStringLiteral( "GPKG" ) << QStringLiteral( "db" ) << QStringLiteral( "gdb" );

  // return item without testing if:
  // scanExtSetting
  // or zipfile and scan zip == "Basic scan"
  if ( ( scanExtSetting ||
         ( ( is_vsizip || is_vsitar ) && scanZipSetting == QLatin1String( "basic" ) ) ) &&
       suffix != QLatin1String( "nc" ) )
  {
    // Skip this layer if it's handled by ogr:
    if ( ogrSupportedDbLayersExtensions.contains( suffix ) )
    {
      return nullptr;
    }

    // if this is a VRT file make sure it is raster VRT to avoid duplicates
    if ( suffix == QLatin1String( "vrt" ) )
    {
      // do not print errors, but write to debug
      CPLPushErrorHandler( CPLQuietErrorHandler );
      CPLErrorReset();
      GDALDriverH hDriver = GDALIdentifyDriver( path.toUtf8().constData(), nullptr );
      CPLPopErrorHandler();
      if ( !hDriver || GDALGetDriverShortName( hDriver ) == QLatin1String( "OGR_VRT" ) )
      {
        QgsDebugMsgLevel( QStringLiteral( "Skipping VRT file because root is not a GDAL VRT" ), 2 );
        return nullptr;
      }
    }
    // add the item
    QStringList sublayers;
    QgsDebugMsgLevel( QStringLiteral( "adding item name=%1 path=%2" ).arg( name, path ), 2 );
    QgsLayerItem *item = new QgsGdalLayerItem( parentItem, name, path, path, &sublayers );
    if ( item )
      return item;
  }

  // test that file is valid with GDAL
  GDALAllRegister();
  // do not print errors, but write to debug
  CPLPushErrorHandler( CPLQuietErrorHandler );
  CPLErrorReset();
  gdal::dataset_unique_ptr hDS( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  CPLPopErrorHandler();

  if ( ! hDS )
  {
    QgsDebugMsg( QStringLiteral( "GDALOpen error # %1 : %2 " ).arg( CPLGetLastErrorNo() ).arg( CPLGetLastErrorMsg() ) );
    return nullptr;
  }

  GDALDriverH hDriver = GDALGetDatasetDriver( hDS.get() );
  QString ogrDriverName = GDALGetDriverShortName( hDriver );

  // Skip this layer if it's handled by ogr:
  if ( ogrSupportedDbDriverNames.contains( ogrDriverName ) )
  {
    return nullptr;
  }

  QStringList sublayers = QgsGdalProvider::subLayers( hDS.get() );
  hDS.reset();

  QgsDebugMsgLevel( "GdalDataset opened " + path, 2 );

  QgsLayerItem *item = new QgsGdalLayerItem( parentItem, name, path, path,
      &sublayers );

  return item;
}

///@endcond
