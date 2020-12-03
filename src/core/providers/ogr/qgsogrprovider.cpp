/***************************************************************************
           qgsogrprovider.cpp Data provider for OGR supported formats
                    Formerly known as qgsshapefileprovider.cpp
begin                : Oct 29, 2003
copyright            : (C) 2003 by Gary E.Sherman
email                : sherman at mrcc.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsogrprovider.h"
///@cond PRIVATE

#include "qgscplerrorhandler.h"
#include "qgsgdalutils.h"
#include "qgsogrfeatureiterator.h"
#include "qgslogger.h"
#include "qgsmessagelog.h"
#include "qgslocalec.h"
#include "qgsfeedback.h"
#include "qgssettings.h"
#include "qgsapplication.h"
#include "qgsauthmanager.h"
#include "qgscplhttpfetchoverrider.h"
#include "qgsdataitem.h"
#include "qgsdataprovider.h"
#include "qgsfeature.h"
#include "qgsfields.h"
#include "qgsgeometry.h"
#include "qgscoordinatereferencesystem.h"
#include "qgsvectorlayerexporter.h"
#include "qgsdataitemprovider.h"
#include "qgsogrdataitems.h"
#include "qgsgeopackagedataitems.h"
#include "qgswkbtypes.h"
#include "qgsnetworkaccessmanager.h"
#include "qgsogrtransaction.h"
#include "qgsgeopackageprojectstorage.h"
#include "qgsprojectstorageregistry.h"
#include "qgsprovidermetadata.h"
#include "qgsogrdbconnection.h"
#include "qgsgeopackageproviderconnection.h"
#include "qgis.h"


#define CPL_SUPRESS_CPLUSPLUS  //#spellok
#include <gdal.h>         // to collect version information
#include <ogr_api.h>
#include <ogr_srs_api.h>
#include <cpl_string.h>

// Temporary solution until GDAL Unique support is available
#include "qgssqliteutils.h"
#include <sqlite3.h>
// end temporary

#include <limits>
#include <memory>

#include <QtDebug>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QMessageBox>
#include <QString>
#include <QTextCodec>


#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifdef Q_OS_LINUX
#include <sys/vfs.h>
#endif

// Starting with GDAL 2.2, there are 2 concepts: unset fields and null fields
// whereas previously there was only unset fields. For QGIS purposes, both
// states (unset/null) are equivalent.
#ifndef OGRNullMarker
#define OGR_F_IsFieldSetAndNotNull OGR_F_IsFieldSet
#endif

#define TEXT_PROVIDER_KEY QStringLiteral( "ogr" )
#define TEXT_PROVIDER_DESCRIPTION QStringLiteral( "OGR data provider" )

static OGRwkbGeometryType ogrWkbGeometryTypeFromName( const QString &typeName );

static bool IsLocalFile( const QString &path );

Q_GLOBAL_STATIC_WITH_ARGS( QMutex, sGlobalMutex, ( QMutex::Recursive ) )

//! Map a dataset name to the number of opened GDAL dataset objects on it (if opened with GDALOpenWrapper, only for GPKG)
typedef QMap< QString, int > OpenedDsCountMap;
Q_GLOBAL_STATIC( OpenedDsCountMap, sMapCountOpenedDS )

QMap< QgsOgrProviderUtils::DatasetIdentification,
      QList<QgsOgrProviderUtils::DatasetWithLayers *> > QgsOgrProviderUtils::sMapSharedDS;

typedef QHash< GDALDatasetH, bool> DsHandleToUpdateModeHash;
Q_GLOBAL_STATIC( DsHandleToUpdateModeHash, sMapDSHandleToUpdateMode )

typedef QMap< QString, QDateTime > DsNameToLastModifiedDateMap;
Q_GLOBAL_STATIC( DsNameToLastModifiedDateMap, sMapDSNameToLastModifiedDate )

bool QgsOgrProvider::convertField( QgsField &field, const QTextCodec &encoding )
{
  OGRFieldType ogrType = OFTString; //default to string
  OGRFieldSubType ogrSubType = OFSTNone;
  int ogrWidth = field.length();
  int ogrPrecision = field.precision();
  if ( ogrPrecision > 0 )
    ogrWidth += 1;
  switch ( field.type() )
  {
    case QVariant::LongLong:
      ogrType = OFTInteger64;
      ogrPrecision = 0;
      ogrWidth = ogrWidth > 0 && ogrWidth <= 21 ? ogrWidth : 21;
      break;

    case QVariant::String:
      ogrType = OFTString;
      if ( ogrWidth < 0 || ogrWidth > 255 )
        ogrWidth = 255;
      break;

    case QVariant::Int:
      ogrType = OFTInteger;
      ogrWidth = ogrWidth > 0 && ogrWidth <= 10 ? ogrWidth : 10;
      ogrPrecision = 0;
      break;

    case QVariant::Bool:
      ogrType = OFTInteger;
      ogrSubType = OFSTBoolean;
      ogrWidth = 1;
      ogrPrecision = 0;
      break;

    case QVariant::Double:
      ogrType = OFTReal;
      break;

    case QVariant::Date:
      ogrType = OFTDate;
      break;

    case QVariant::Time:
      ogrType = OFTTime;
      break;

    case QVariant::DateTime:
      ogrType = OFTDateTime;
      break;

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,4,0)
    case QVariant::List:
      if ( field.subType() == QVariant::String )
      {
        ogrType = OFTStringList;
      }
      else
      {
        // only string lists are supported at this moment
        return false;
      }
      break;
#endif

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,4,0)
    case QVariant::Map:
      ogrType = OFTString;
      ogrSubType = OFSTJSON;
      break;
#endif
    default:
      return false;
  }

  if ( ogrSubType != OFSTNone )
    field.setTypeName( encoding.toUnicode( OGR_GetFieldSubTypeName( ogrSubType ) ) );
  else
    field.setTypeName( encoding.toUnicode( OGR_GetFieldTypeName( ogrType ) ) );

  field.setLength( ogrWidth );
  field.setPrecision( ogrPrecision );
  return true;
}

void QgsOgrProvider::repack()
{
  if ( !mValid || mGDALDriverName != QLatin1String( "ESRI Shapefile" ) || !mOgrOrigLayer )
    return;

  // run REPACK on shape files
  QByteArray sql = QByteArray( "REPACK " ) + mOgrOrigLayer->name();   // don't quote the layer name as it works with spaces in the name and won't work if the name is quoted
  QgsDebugMsgLevel( QStringLiteral( "SQL: %1" ).arg( QString::fromUtf8( sql ) ), 2 );
  CPLErrorReset();
  mOgrOrigLayer->ExecuteSQLNoReturn( sql );
  if ( CPLGetLastErrorType() != CE_None )
  {
    pushError( tr( "OGR[%1] error %2: %3" ).arg( CPLGetLastErrorType() ).arg( CPLGetLastErrorNo() ).arg( CPLGetLastErrorMsg() ) );
  }

  if ( mFilePath.endsWith( QLatin1String( ".shp" ), Qt::CaseInsensitive ) || mFilePath.endsWith( QLatin1String( ".dbf" ), Qt::CaseInsensitive ) )
  {
    QString packedDbf( mFilePath.left( mFilePath.size() - 4 ) + "_packed.dbf" );
    if ( QFile::exists( packedDbf ) )
    {
      QgsMessageLog::logMessage( tr( "Possible corruption after REPACK detected. %1 still exists. This may point to a permission or locking problem of the original DBF." ).arg( packedDbf ), tr( "OGR" ), Qgis::Critical );

      mOgrSqlLayer.reset();
      mOgrOrigLayer.reset();

      QString errCause;
      if ( mLayerName.isNull() )
      {
        mOgrOrigLayer = QgsOgrProviderUtils::getLayer( mFilePath, true, mOpenOptions, mLayerIndex, errCause, true );
      }
      else
      {
        mOgrOrigLayer = QgsOgrProviderUtils::getLayer( mFilePath, true, mOpenOptions, mLayerName, errCause, true );
      }

      if ( !mOgrOrigLayer )
      {
        QgsMessageLog::logMessage( tr( "Original layer could not be reopened." ) + " " + errCause, tr( "OGR" ), Qgis::Critical );
        mValid = false;
      }

      mOgrLayer = mOgrOrigLayer.get();
    }

  }

  if ( mFeaturesCounted != QgsVectorDataProvider::Uncounted &&
       mFeaturesCounted != QgsVectorDataProvider::UnknownCount )
  {
    long oldcount = mFeaturesCounted;
    recalculateFeatureCount();
    if ( oldcount != mFeaturesCounted )
      emit dataChanged();
  }
}


QgsVectorLayerExporter::ExportError QgsOgrProviderMetadata::createEmptyLayer( const QString &uri,
    const QgsFields &fields,
    QgsWkbTypes::Type wkbType,
    const QgsCoordinateReferenceSystem &srs,
    bool overwrite,
    QMap<int, int> &oldToNewAttrIdxMap,
    QString &errorMessage,
    const QMap<QString, QVariant> *options )
{
  return QgsOgrProvider::createEmptyLayer(
           uri, fields, wkbType, srs, overwrite,
           &oldToNewAttrIdxMap, &errorMessage, options
         );
}

static QString AnalyzeURI( QString const &uri,
                           bool &isSubLayer,
                           int &layerIndex,
                           QString &layerName,
                           QString &subsetString,
                           OGRwkbGeometryType &ogrGeometryTypeFilter,
                           QStringList &openOptions )
{
  isSubLayer = false;
  layerIndex = 0;
  layerName = QString();
  subsetString = QString();
  ogrGeometryTypeFilter = wkbUnknown;
  openOptions.clear();

  QgsDebugMsgLevel( "Data source uri is [" + uri + ']', 2 );

  QVariantMap parts = QgsOgrProviderMetadata().decodeUri( uri );

  if ( parts.contains( QStringLiteral( "layerName" ) ) )
  {
    layerName = parts.value( QStringLiteral( "layerName" ) ).toString();
    isSubLayer = !layerName.isEmpty();
  }

  if ( parts.contains( QStringLiteral( "layerId" ) ) &&
       parts.value( QStringLiteral( "layerId" ) ) != QVariant() )
  {
    bool ok;
    layerIndex = parts.value( QStringLiteral( "layerId" ) ).toInt( &ok );
    if ( ok && layerIndex >= 0 )
      isSubLayer = true;
    else
      layerIndex = -1;
  }

  if ( parts.contains( QStringLiteral( "subset" ) ) )
  {
    subsetString = parts.value( QStringLiteral( "subset" ) ).toString();
  }

  if ( parts.contains( QStringLiteral( "geometryType" ) ) )
  {
    ogrGeometryTypeFilter = ogrWkbGeometryTypeFromName( parts.value( QStringLiteral( "geometryType" ) ).toString() );
  }

  if ( parts.contains( QStringLiteral( "openOptions" ) ) )
  {
    openOptions = parts.value( QStringLiteral( "openOptions" ) ).toStringList();
  }

  return parts.value( QStringLiteral( "path" ) ).toString();
}

QgsVectorLayerExporter::ExportError QgsOgrProvider::createEmptyLayer( const QString &uri,
    const QgsFields &fields,
    QgsWkbTypes::Type wkbType,
    const QgsCoordinateReferenceSystem &srs,
    bool overwrite,
    QMap<int, int> *oldToNewAttrIdxMap,
    QString *errorMessage,
    const QMap<QString, QVariant> *options )
{
  QString encoding;
  QString driverName = QStringLiteral( "GPKG" );
  QStringList dsOptions, layerOptions;
  QString layerName;

  if ( options )
  {
    if ( options->contains( QStringLiteral( "fileEncoding" ) ) )
      encoding = options->value( QStringLiteral( "fileEncoding" ) ).toString();

    if ( options->contains( QStringLiteral( "driverName" ) ) )
      driverName = options->value( QStringLiteral( "driverName" ) ).toString();

    if ( options->contains( QStringLiteral( "datasourceOptions" ) ) )
      dsOptions << options->value( QStringLiteral( "datasourceOptions" ) ).toStringList();

    if ( options->contains( QStringLiteral( "layerOptions" ) ) )
      layerOptions << options->value( QStringLiteral( "layerOptions" ) ).toStringList();

    if ( options->contains( QStringLiteral( "layerName" ) ) )
      layerName = options->value( QStringLiteral( "layerName" ) ).toString();
  }

  oldToNewAttrIdxMap->clear();
  if ( errorMessage )
    errorMessage->clear();

  QgsVectorFileWriter::ActionOnExistingFile action( QgsVectorFileWriter::CreateOrOverwriteFile );

  bool update = false;
  if ( options && options->contains( QStringLiteral( "update" ) ) )
  {
    update = options->value( QStringLiteral( "update" ) ).toBool();
    if ( update )
    {
      if ( !overwrite && !layerName.isEmpty() )
      {
        gdal::dataset_unique_ptr hDS( GDALOpenEx( uri.toUtf8().constData(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr ) );
        if ( hDS )
        {
          if ( GDALDatasetGetLayerByName( hDS.get(), layerName.toUtf8().constData() ) )
          {
            if ( errorMessage )
              *errorMessage += QObject::tr( "Layer %2 of %1 exists and overwrite flag is false." )
                               .arg( uri, layerName );
            return QgsVectorLayerExporter::ErrCreateDataSource;
          }
        }
      }
      if ( QFileInfo::exists( uri ) )
        action = QgsVectorFileWriter::CreateOrOverwriteLayer;
    }
  }

  if ( !overwrite && !update )
  {
    if ( QFileInfo::exists( uri ) )
    {
      if ( errorMessage )
        *errorMessage += QObject::tr( "Unable to create the datasource. %1 exists and overwrite flag is false." )
                         .arg( uri );
      return QgsVectorLayerExporter::ErrCreateDataSource;
    }
  }

  QString newLayerName( layerName );

  QgsVectorFileWriter::SaveVectorOptions saveOptions;
  saveOptions.layerName = layerName;
  saveOptions.fileEncoding = encoding;
  saveOptions.driverName = driverName;
  saveOptions.datasourceOptions = dsOptions;
  saveOptions.layerOptions = layerOptions;
  saveOptions.actionOnExistingFile = action;
  saveOptions.symbologyExport = QgsVectorFileWriter::NoSymbology;
  std::unique_ptr< QgsVectorFileWriter > writer( QgsVectorFileWriter::create( uri, fields, wkbType, srs, QgsCoordinateTransformContext(), saveOptions, QgsFeatureSink::SinkFlags(), nullptr, &newLayerName ) );
  layerName = newLayerName;

  QgsVectorFileWriter::WriterError error = writer->hasError();
  if ( error )
  {
    if ( errorMessage )
      *errorMessage += writer->errorMessage();

    return static_cast<QgsVectorLayerExporter::ExportError>( error );
  }

  QMap<int, int> attrIdxMap = writer->attrIdxToOgrIdx();
  writer.reset();

  {
    bool firstFieldIsFid = false;
    bool fidColumnIsField = false;
    if ( !layerName.isEmpty() )
    {
      gdal::dataset_unique_ptr hDS( GDALOpenEx( uri.toUtf8().constData(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr ) );
      if ( hDS )
      {
        OGRLayerH hLayer = GDALDatasetGetLayerByName( hDS.get(), layerName.toUtf8().constData() );
        if ( hLayer )
        {
          // Expose the OGR FID if it comes from a "real" column (typically GPKG)
          // and make sure that this FID column is not exposed as a regular OGR field (shouldn't happen normally)
          const QString ogrFidColumnName { OGR_L_GetFIDColumn( hLayer ) };
          firstFieldIsFid = !( EQUAL( OGR_L_GetFIDColumn( hLayer ), "" ) ) &&
                            OGR_FD_GetFieldIndex( OGR_L_GetLayerDefn( hLayer ), ogrFidColumnName.toUtf8() ) < 0 &&
                            fields.indexFromName( ogrFidColumnName.toUtf8() ) < 0;
          // At this point we must check if there is a real FID field in the the fields argument,
          // because in that case we don't want to shift all fields (see issue GH #34333)
          // Check for unique values should be performed in client code.
          for ( const auto &f : qgis::as_const( fields ) )
          {
            if ( f.name().compare( ogrFidColumnName, Qt::CaseSensitivity::CaseInsensitive ) == 0 )
            {
              fidColumnIsField = true;
              break;
            }
          }
        }
      }
    }

    const bool shiftColumnsByOne { firstFieldIsFid &&( ! fidColumnIsField ) };

    for ( QMap<int, int>::const_iterator attrIt = attrIdxMap.constBegin(); attrIt != attrIdxMap.constEnd(); ++attrIt )
    {
      oldToNewAttrIdxMap->insert( attrIt.key(), *attrIt + ( shiftColumnsByOne ? 1 : 0 ) );
    }
  }

  QgsOgrProviderUtils::invalidateCachedLastModifiedDate( uri );

  return QgsVectorLayerExporter::NoError;
}

QgsOgrProvider::QgsOgrProvider( QString const &uri, const ProviderOptions &options, QgsDataProvider::ReadFlags flags )
  : QgsVectorDataProvider( uri, options, flags )
{
  QgsApplication::registerOgrDrivers();

  QgsSettings settings;
  // we always disable GDAL side shapefile encoding handling, and do it on the QGIS side.
  // why? it's not the ideal choice, but...
  // - if we DON'T disable GDAL side encoding support, then there's NO way to change the encoding used when reading
  //   shapefiles. And unfortunately the embedded encoding (which is read by GDAL) is sometimes wrong, so we need
  //   to expose support for users to be able to change and correct this
  // - we can't change this setting on-the-fly. If we don't set it upfront, we can't reverse this decision later when
  //   a user does want/need to manually specify the encoding
  CPLSetConfigOption( "SHAPE_ENCODING", "" );

#ifndef QT_NO_NETWORKPROXY
  QgsGdalUtils::setupProxy();
#endif

  // make connection to the data source

  QgsDebugMsgLevel( "Data source uri is [" + uri + ']', 2 );

  mFilePath = AnalyzeURI( uri,
                          mIsSubLayer,
                          mLayerIndex,
                          mLayerName,
                          mSubsetString,
                          mOgrGeometryTypeFilter,
                          mOpenOptions );

  if ( mFilePath.contains( QLatin1String( "authcfg" ) ) )
  {
    QRegularExpression authcfgRe( " authcfg='([^']+)'" );
    QRegularExpressionMatch match;
    if ( mFilePath.contains( authcfgRe, &match ) )
    {
      mAuthCfg = match.captured( 1 );
    }
  }
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  open( OpenModeInitial );

  int nMaxIntLen = 11;
  int nMaxInt64Len = 21;
  int nMaxDoubleLen = 20;
  int nMaxDoublePrec = 15;
  int nDateLen = 8;
  if ( mGDALDriverName == QLatin1String( "GPKG" ) )
  {
    // GPKG only supports field length for text (and binary)
    nMaxIntLen = 0;
    nMaxInt64Len = 0;
    nMaxDoubleLen = 0;
    nMaxDoublePrec = 0;
    nDateLen = 0;
  }

  QList<NativeType> nativeTypes;
  nativeTypes
      << QgsVectorDataProvider::NativeType( tr( "Whole number (integer)" ), QStringLiteral( "integer" ), QVariant::Int, 0, nMaxIntLen )
      << QgsVectorDataProvider::NativeType( tr( "Whole number (integer 64 bit)" ), QStringLiteral( "integer64" ), QVariant::LongLong, 0, nMaxInt64Len )
      << QgsVectorDataProvider::NativeType( tr( "Decimal number (real)" ), QStringLiteral( "double" ), QVariant::Double, 0, nMaxDoubleLen, 0, nMaxDoublePrec )
      << QgsVectorDataProvider::NativeType( tr( "Text (string)" ), QStringLiteral( "string" ), QVariant::String, 0, 65535 );

  if ( mGDALDriverName == QLatin1String( "GPKG" ) )
    nativeTypes << QgsVectorDataProvider::NativeType( tr( "JSON (string)" ), QStringLiteral( "JSON" ), QVariant::Map, 0, 0, 0, 0, QVariant::String );

  bool supportsDate = true;
  bool supportsTime = mGDALDriverName != QLatin1String( "ESRI Shapefile" ) && mGDALDriverName != QLatin1String( "GPKG" );
  bool supportsDateTime = mGDALDriverName != QLatin1String( "ESRI Shapefile" );
  bool supportsBinary = false;
  bool supportsStringList = false;
  const char *pszDataTypes = nullptr;
  if ( mOgrOrigLayer )
  {
    pszDataTypes = GDALGetMetadataItem( mOgrOrigLayer->driver(), GDAL_DMD_CREATIONFIELDDATATYPES, nullptr );
  }
  // For drivers that advertise their data type, use that instead of the
  // above hardcoded defaults.
  if ( pszDataTypes )
  {
    char **papszTokens = CSLTokenizeString2( pszDataTypes, " ", 0 );
    supportsDate = CSLFindString( papszTokens, "Date" ) >= 0;
    supportsTime = CSLFindString( papszTokens, "Time" ) >= 0;
    supportsDateTime = CSLFindString( papszTokens, "DateTime" ) >= 0;
    supportsBinary = CSLFindString( papszTokens, "Binary" ) >= 0;
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,4,0)
    supportsStringList = CSLFindString( papszTokens, "StringList" ) >= 0;
#endif
    CSLDestroy( papszTokens );
  }

  // Older versions of GDAL incorrectly report that shapefiles support
  // DateTime.
#if GDAL_VERSION_NUM < GDAL_COMPUTE_VERSION(3,2,0)
  if ( mGDALDriverName == QLatin1String( "ESRI Shapefile" ) )
  {
    supportsDateTime = false;
  }
#endif

  if ( supportsDate )
  {
    nativeTypes
        << QgsVectorDataProvider::NativeType( tr( "Date" ), QStringLiteral( "date" ), QVariant::Date, nDateLen, nDateLen );
  }
  if ( supportsTime )
  {
    nativeTypes
        << QgsVectorDataProvider::NativeType( tr( "Time" ), QStringLiteral( "time" ), QVariant::Time );
  }
  if ( supportsDateTime )
  {
    nativeTypes
        << QgsVectorDataProvider::NativeType( tr( "Date & Time" ), QStringLiteral( "datetime" ), QVariant::DateTime );
  }
  if ( supportsBinary )
  {
    nativeTypes
        << QgsVectorDataProvider::NativeType( tr( "Binary object (BLOB)" ), QStringLiteral( "binary" ), QVariant::ByteArray );
  }
  if ( supportsStringList )
  {
    nativeTypes
        << QgsVectorDataProvider::NativeType( tr( "String List" ), QStringLiteral( "stringlist" ), QVariant::List, 0, 0, 0, 0, QVariant::String );
  }

  bool supportsBoolean = false;

  // layer metadata
  mLayerMetadata.setType( QStringLiteral( "dataset" ) );
  if ( mOgrOrigLayer )
  {
    QMutex *mutex = nullptr;
    OGRLayerH layer = mOgrOrigLayer->getHandleAndMutex( mutex );
    QMutexLocker locker( mutex );
    const QString identifier = GDALGetMetadataItem( layer, "IDENTIFIER", nullptr );
    if ( !identifier.isEmpty() )
      mLayerMetadata.setTitle( identifier ); // see geopackage specs -- "'identifier' is analogous to 'title'"
    const QString abstract = GDALGetMetadataItem( layer, "DESCRIPTION", nullptr );
    if ( !abstract.isEmpty() )
      mLayerMetadata.setAbstract( abstract );
  }

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,3,0)
  if ( mOgrOrigLayer )
  {
    const char *pszDataSubTypes = GDALGetMetadataItem( mOgrOrigLayer->driver(), GDAL_DMD_CREATIONFIELDDATASUBTYPES, nullptr );
    if ( pszDataSubTypes && strstr( pszDataSubTypes, "Boolean" ) )
      supportsBoolean = true;
  }
#else
  if ( mGDALDriverName == QLatin1String( "GeoJSON" ) ||
       mGDALDriverName == QLatin1String( "GML" ) ||
       mGDALDriverName == QLatin1String( "CSV" ) ||
       mGDALDriverName == QLatin1String( "PostgreSQL" ) ||
       mGDALDriverName == QLatin1String( "PGDump" ) ||
       mGDALDriverName == QLatin1String( "SQLite" ) ||
       mGDALDriverName == QLatin1String( "GPKG" ) )
  {
    supportsBoolean = true;
  }
#endif

  if ( supportsBoolean )
  {
    // boolean data type
    nativeTypes
        << QgsVectorDataProvider::NativeType( tr( "Boolean" ), QStringLiteral( "bool" ), QVariant::Bool );
  }

  setNativeTypes( nativeTypes );

  QgsOgrConnPool::instance()->ref( QgsOgrProviderUtils::connectionPoolId( dataSourceUri( true ), mShareSameDatasetAmongLayers ) );
}

QgsOgrProvider::~QgsOgrProvider()
{
  QgsOgrConnPool::instance()->unref( QgsOgrProviderUtils::connectionPoolId( dataSourceUri( true ), mShareSameDatasetAmongLayers ) );
  // We must also make sure to flush unusef cached connections so that
  // the file can be removed (#15137)
  QgsOgrConnPool::instance()->invalidateConnections( QgsOgrProviderUtils::connectionPoolId( dataSourceUri( true ), mShareSameDatasetAmongLayers ) );

  // Do that as last step for final cleanup that might be prevented by
  // still opened datasets.
  close();
}

QString QgsOgrProvider::dataSourceUri( bool expandAuthConfig ) const
{
  if ( expandAuthConfig && QgsDataProvider::dataSourceUri( ).contains( QLatin1String( "authcfg" ) ) )
  {
    return QgsOgrProviderUtils::expandAuthConfig( QgsDataProvider::dataSourceUri( ) );
  }
  else
  {
    return QgsDataProvider::dataSourceUri( );
  }
}

QgsTransaction *QgsOgrProvider::transaction() const
{
  return static_cast<QgsTransaction *>( mTransaction );
}

void QgsOgrProvider::setTransaction( QgsTransaction *transaction )
{
  QgsDebugMsgLevel( QStringLiteral( "set transaction %1" ).arg( transaction != nullptr ), 1 );
  // static_cast since layers cannot be added to a transaction of a non-matching provider
  mTransaction = static_cast<QgsOgrTransaction *>( transaction );
}

QgsAbstractFeatureSource *QgsOgrProvider::featureSource() const
{
  return new QgsOgrFeatureSource( this );
}

bool QgsOgrProvider::setSubsetString( const QString &theSQL, bool updateFeatureCount )
{
  return _setSubsetString( theSQL, updateFeatureCount, true );
}

QString QgsOgrProvider::subsetString() const
{
  return mSubsetString;
}

QString QgsOgrProvider::ogrWkbGeometryTypeName( OGRwkbGeometryType type ) const
{
  QString geom;

  // GDAL 2.1 can return M/ZM geometries
  if ( wkbHasM( type ) )
  {
    geom = ogrWkbGeometryTypeName( wkbFlatten( type ) );
    if ( wkbHasZ( type ) )
      geom += QLatin1Char( 'Z' );
    if ( wkbHasM( type ) )
      geom += QLatin1Char( 'M' );
    return geom;
  }

  switch ( static_cast<unsigned>( type ) )
  {
    case wkbUnknown:
      geom = QStringLiteral( "Unknown" );
      break;
    case wkbPoint:
      geom = QStringLiteral( "Point" );
      break;
    case wkbLineString:
      geom = QStringLiteral( "LineString" );
      break;
    case wkbPolygon:
      geom = QStringLiteral( "Polygon" );
      break;
    case wkbMultiPoint:
      geom = QStringLiteral( "MultiPoint" );
      break;
    case wkbMultiLineString:
      geom = QStringLiteral( "MultiLineString" );
      break;
    case wkbMultiPolygon:
      geom = QStringLiteral( "MultiPolygon" );
      break;
    case wkbGeometryCollection:
      geom = QStringLiteral( "GeometryCollection" );
      break;
    case wkbCircularString:
      geom = QStringLiteral( "CircularString" );
      break;
    case wkbCompoundCurve:
      geom = QStringLiteral( "CompoundCurve" );
      break;
    case wkbCurvePolygon:
      geom = QStringLiteral( "CurvePolygon" );
      break;
    case wkbMultiCurve:
      geom = QStringLiteral( "MultiCurve" );
      break;
    case wkbMultiSurface:
      geom = QStringLiteral( "MultiSurface" );
      break;
    case wkbCircularStringZ:
      geom = QStringLiteral( "CircularStringZ" );
      break;
    case wkbCompoundCurveZ:
      geom = QStringLiteral( "CompoundCurveZ" );
      break;
    case wkbCurvePolygonZ:
      geom = QStringLiteral( "CurvePolygonZ" );
      break;
    case wkbMultiCurveZ:
      geom = QStringLiteral( "MultiCurveZ" );
      break;
    case wkbMultiSurfaceZ:
      geom = QStringLiteral( "MultiSurfaceZ" );
      break;
    case wkbNone:
      geom = QStringLiteral( "None" );
      break;
    case static_cast<unsigned>( wkbUnknown ) | static_cast<unsigned>( wkb25DBit ):
      geom = QStringLiteral( "Unknown25D" );
      break;
    case static_cast<unsigned>( wkbPoint25D ):
      geom = QStringLiteral( "Point25D" );
      break;
    case static_cast<unsigned>( wkbLineString25D ):
      geom = QStringLiteral( "LineString25D" );
      break;
    case static_cast<unsigned>( wkbPolygon25D ):
      geom = QStringLiteral( "Polygon25D" );
      break;
    case static_cast<unsigned>( wkbMultiPoint25D ):
      geom = QStringLiteral( "MultiPoint25D" );
      break;
    case static_cast<unsigned>( wkbMultiLineString25D ):
      geom = QStringLiteral( "MultiLineString25D" );
      break;
    case static_cast<unsigned>( wkbMultiPolygon25D ):
      geom = QStringLiteral( "MultiPolygon25D" );
      break;
    case static_cast<unsigned>( wkbGeometryCollection25D ):
      geom = QStringLiteral( "GeometryCollection25D" );
      break;
    default:
      // Do not use ':', as it will mess with the separator used by QgsSublayersDialog::populateLayers()
      geom = QStringLiteral( "Unknown WKB (%1)" ).arg( type );
  }
  return geom;
}

static OGRwkbGeometryType ogrWkbGeometryTypeFromName( const QString &typeName )
{
  if ( typeName == QLatin1String( "Point" ) ) return wkbPoint;
  else if ( typeName == QLatin1String( "LineString" ) ) return wkbLineString;
  else if ( typeName == QLatin1String( "Polygon" ) ) return wkbPolygon;
  else if ( typeName == QLatin1String( "MultiPoint" ) ) return wkbMultiPoint;
  else if ( typeName == QLatin1String( "MultiLineString" ) ) return wkbMultiLineString;
  else if ( typeName == QLatin1String( "MultiPolygon" ) ) return wkbMultiPolygon;
  else if ( typeName == QLatin1String( "GeometryCollection" ) ) return wkbGeometryCollection;
  else if ( typeName == QLatin1String( "None" ) ) return wkbNone;
  else if ( typeName == QLatin1String( "Point25D" ) ) return wkbPoint25D;
  else if ( typeName == QLatin1String( "LineString25D" ) ) return wkbLineString25D;
  else if ( typeName == QLatin1String( "Polygon25D" ) ) return wkbPolygon25D;
  else if ( typeName == QLatin1String( "MultiPoint25D" ) ) return wkbMultiPoint25D;
  else if ( typeName == QLatin1String( "MultiLineString25D" ) ) return wkbMultiLineString25D;
  else if ( typeName == QLatin1String( "MultiPolygon25D" ) ) return wkbMultiPolygon25D;
  else if ( typeName == QLatin1String( "GeometryCollection25D" ) ) return wkbGeometryCollection25D;
  QgsDebugMsg( QStringLiteral( "unknown geometry type: %1" ).arg( typeName ) );
  return wkbUnknown;
}

void QgsOgrProvider::addSubLayerDetailsToSubLayerList( int i, QgsOgrLayer *layer, bool withFeatureCount ) const
{
  QString layerName = QString::fromUtf8( layer->name() );

  if ( !mIsSubLayer && ( layerName == QLatin1String( "layer_styles" ) ||
                         layerName == QLatin1String( "qgis_projects" ) ) )
  {
    // Ignore layer_styles (coming from QGIS styling support) and
    // qgis_projects (coming from http://plugins.qgis.org/plugins/QgisGeopackage/)
    return;
  }
  // Get first column name,
  // TODO: add support for multiple
  QString geometryColumnName;
  OGRwkbGeometryType layerGeomType = wkbUnknown;
  const bool slowGeomTypeRetrieval =
    mGDALDriverName == QLatin1String( "OAPIF" ) || mGDALDriverName == QLatin1String( "WFS3" ) || mGDALDriverName == QLatin1String( "PGeo" );
  if ( !slowGeomTypeRetrieval )
  {
    QgsOgrFeatureDefn &fdef = layer->GetLayerDefn();
    if ( fdef.GetGeomFieldCount() )
    {
      OGRGeomFieldDefnH geomH = fdef.GetGeomFieldDefn( 0 );
      geometryColumnName = QString::fromUtf8( OGR_GFld_GetNameRef( geomH ) );
    }
    layerGeomType = fdef.GetGeomType();
  }

  QString longDescription;
  if ( mGDALDriverName == QLatin1String( "OAPIF" ) || mGDALDriverName == QLatin1String( "WFS3" ) )
  {
    longDescription = layer->GetMetadataItem( "TITLE" );
  }

  QgsDebugMsgLevel( QStringLiteral( "id = %1 name = %2 layerGeomType = %3 longDescription = %4" ).arg( i ).arg( layerName ).arg( layerGeomType ). arg( longDescription ), 2 );

  if ( slowGeomTypeRetrieval || wkbFlatten( layerGeomType ) != wkbUnknown )
  {
    int layerFeatureCount = withFeatureCount ? layer->GetApproxFeatureCount() : -1;

    QString geom = ogrWkbGeometryTypeName( layerGeomType );

    // For feature count, -1 indicates an unknown count state
    QStringList parts = QStringList()
                        << QString::number( i )
                        << layerName
                        << QString::number( layerFeatureCount )
                        << geom
                        << geometryColumnName
                        << longDescription;

    mSubLayerList << parts.join( sublayerSeparator() );
  }
  else
  {
    QgsDebugMsgLevel( QStringLiteral( "Unknown geometry type, count features for each geometry type" ), 2 );
    // Add virtual sublayers for supported geometry types if layer type is unknown
    // Count features for geometry types
    QMap<OGRwkbGeometryType, int> fCount;
    // TODO: avoid reading attributes, setRelevantFields cannot be called here because it is not constant

    layer->ResetReading();
    gdal::ogr_feature_unique_ptr fet;
    while ( fet.reset( layer->GetNextFeature() ), fet )
    {
      OGRGeometryH geom = OGR_F_GetGeometryRef( fet.get() );
      if ( geom )
      {
        OGRwkbGeometryType gType = ogrWkbSingleFlatten( OGR_G_GetGeometryType( geom ) );
        fCount[gType] = fCount.value( gType ) + 1;
      }
    }
    layer->ResetReading();
    // it may happen that there are no features in the layer, in that case add unknown type
    // to show to user that the layer exists but it is empty
    if ( fCount.isEmpty() )
    {
      fCount[wkbUnknown] = 0;
    }

    // List TIN and PolyhedralSurface as Polygon
    if ( fCount.contains( wkbTIN ) )
    {
      fCount[wkbPolygon] = fCount.value( wkbPolygon ) + fCount[wkbTIN];
      fCount.remove( wkbTIN );
    }
    if ( fCount.contains( wkbPolyhedralSurface ) )
    {
      fCount[wkbPolygon] = fCount.value( wkbPolygon ) + fCount[wkbPolyhedralSurface];
      fCount.remove( wkbPolyhedralSurface );
    }
    // When there are CurvePolygons, promote Polygons
    if ( fCount.contains( wkbPolygon ) && fCount.contains( wkbCurvePolygon ) )
    {
      fCount[wkbCurvePolygon] += fCount.value( wkbPolygon );
      fCount.remove( wkbPolygon );
    }
    // When there are CompoundCurves, promote LineStrings and CircularStrings
    if ( fCount.contains( wkbLineString ) && fCount.contains( wkbCompoundCurve ) )
    {
      fCount[wkbCompoundCurve] += fCount.value( wkbLineString );
      fCount.remove( wkbLineString );
    }
    if ( fCount.contains( wkbCircularString ) && fCount.contains( wkbCompoundCurve ) )
    {
      fCount[wkbCompoundCurve] += fCount.value( wkbCircularString );
      fCount.remove( wkbCircularString );
    }

    bool bIs25D = wkbHasZ( layerGeomType );
    QMap<OGRwkbGeometryType, int>::const_iterator countIt = fCount.constBegin();
    for ( ; countIt != fCount.constEnd(); ++countIt )
    {
      QString geom = ogrWkbGeometryTypeName( ( bIs25D ) ? wkbSetZ( countIt.key() ) : countIt.key() );

      QStringList parts = QStringList()
                          << QString::number( i )
                          << layerName
                          << QString::number( fCount.value( countIt.key() ) )
                          << geom
                          << geometryColumnName
                          << longDescription;

      QString sl = parts.join( sublayerSeparator() );
      QgsDebugMsgLevel( "sub layer: " + sl, 2 );
      mSubLayerList << sl;
    }
  }
}

QStringList QgsOgrProvider::subLayers() const
{
  const bool withFeatureCount = ( mReadFlags & QgsDataProvider::SkipFeatureCount ) == 0;
  return _subLayers( withFeatureCount );
}

QgsLayerMetadata QgsOgrProvider::layerMetadata() const
{
  return mLayerMetadata;
}

QStringList QgsOgrProvider::subLayersWithoutFeatureCount() const
{
  return _subLayers( false );
}

QStringList QgsOgrProvider::_subLayers( bool withFeatureCount )  const
{
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  if ( !mValid )
  {
    return QStringList();
  }

  if ( !mSubLayerList.isEmpty() )
    return mSubLayerList;

  if ( mOgrLayer && ( mIsSubLayer || layerCount() == 1 ) )
  {
    addSubLayerDetailsToSubLayerList( mLayerIndex, mOgrLayer, withFeatureCount );
  }
  else
  {
    // In case there is no free opened dataset in the cache, keep the first
    // layer alive while we iterate over the other layers, so that we can
    // reuse the same dataset. Can help in a particular with a FileGDB with
    // the FileGDB driver
    QgsOgrLayerUniquePtr firstLayer;
    for ( unsigned int i = 0; i < layerCount() ; i++ )
    {
      QString errCause;
      QgsOgrLayerUniquePtr layer = QgsOgrProviderUtils::getLayer( mOgrOrigLayer->datasetName(),
                                   mOgrOrigLayer->updateMode(),
                                   mOgrOrigLayer->options(),
                                   i,
                                   errCause,
                                   // do not check timestamp beyond the first
                                   // layer
                                   firstLayer == nullptr );
      if ( !layer )
        continue;

      addSubLayerDetailsToSubLayerList( i, layer.get(), withFeatureCount );
      if ( firstLayer == nullptr )
      {
        firstLayer = std::move( layer );
      }
    }
  }
  return mSubLayerList;
}

void QgsOgrProvider::setEncoding( const QString &e )
{
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  QgsSettings settings;

  // if the layer has the OLCStringsAsUTF8 capability, we CANNOT override the
  // encoding on the QGIS side!
  if ( mOgrLayer && !mOgrLayer->TestCapability( OLCStringsAsUTF8 ) )
  {
    QgsVectorDataProvider::setEncoding( e );
  }
  else
  {
    QgsVectorDataProvider::setEncoding( QStringLiteral( "UTF-8" ) );
  }
  loadFields();
}

// This is reused by dataItem
OGRwkbGeometryType QgsOgrProvider::getOgrGeomType( const QString &driverName, OGRLayerH ogrLayer )
{
  OGRFeatureDefnH fdef = OGR_L_GetLayerDefn( ogrLayer );
  OGRwkbGeometryType geomType = wkbUnknown;
  if ( fdef )
  {
    geomType = OGR_FD_GetGeomType( fdef );

    // Handle wkbUnknown and its Z/M variants. QGIS has no unknown Z/M variants,
    // so just use flat wkbUnknown
    if ( wkbFlatten( geomType ) == wkbUnknown )
      geomType = wkbUnknown;

    // Some ogr drivers (e.g. GML) are not able to determine the geometry type of a layer like this.
    // In such cases, we use virtual sublayers for each geometry if the layer contains
    // multiple geometries (see subLayers) otherwise we guess geometry type from the first
    // feature that has a geometry (limit us to a few features, not the whole layer)
    if ( geomType == wkbUnknown )
    {
      geomType = wkbNone;
      OGR_L_ResetReading( ogrLayer );
      for ( int i = 0; i < 10; i++ )
      {
        gdal::ogr_feature_unique_ptr nextFeature( OGR_L_GetNextFeature( ogrLayer ) );
        if ( !nextFeature )
          break;

        OGRGeometryH geometry = OGR_F_GetGeometryRef( nextFeature.get() );
        if ( geometry )
        {
          geomType = OGR_G_GetGeometryType( geometry );

          // Shapefile MultiPatch can be reported as GeometryCollectionZ of TINZ
          if ( wkbFlatten( geomType ) == wkbGeometryCollection &&
               driverName == QLatin1String( "ESRI Shapefile" )  &&
               OGR_G_GetGeometryCount( geometry ) >= 1 &&
               wkbFlatten( OGR_G_GetGeometryType( OGR_G_GetGeometryRef( geometry, 0 ) ) ) == wkbTIN )
          {
            geomType = wkbMultiPolygon25D;
          }
        }
        if ( geomType != wkbNone )
          break;
      }
      OGR_L_ResetReading( ogrLayer );
    }
  }
  return geomType;
}

void QgsOgrProvider::loadFields()
{
  QgsOgrConnPool::instance()->invalidateConnections( QgsOgrProviderUtils::connectionPoolId( dataSourceUri( true ), mShareSameDatasetAmongLayers ) );
  //the attribute fields need to be read again when the encoding changes
  mAttributeFields.clear();
  mDefaultValues.clear();
  mPrimaryKeyAttrs.clear();
  if ( !mOgrLayer )
    return;

  if ( mOgrGeometryTypeFilter != wkbUnknown )
  {
    mOGRGeomType = mOgrGeometryTypeFilter;
  }
  else
  {
    QMutex *mutex = nullptr;
    OGRLayerH ogrLayer = mOgrLayer->getHandleAndMutex( mutex );
    QMutexLocker locker( mutex );
    mOGRGeomType = getOgrGeomType( mGDALDriverName, ogrLayer );
  }
  QgsOgrFeatureDefn &fdef = mOgrLayer->GetLayerDefn();

  // Expose the OGR FID if it comes from a "real" column (typically GPKG)
  // and make sure that this FID column is not exposed as a regular OGR field (shouldn't happen normally)
  QByteArray fidColumn( mOgrLayer->GetFIDColumn() );
  mFirstFieldIsFid = !fidColumn.isEmpty() &&
                     fdef.GetFieldIndex( fidColumn ) < 0;

  // This is a temporary solution until GDAL Unique support is available
  QSet<QString> uniqueFieldNames;


  if ( mGDALDriverName == QLatin1String( "GPKG" ) )
  {
    sqlite3_database_unique_ptr dsPtr;
    if ( dsPtr.open_v2( mFilePath, SQLITE_OPEN_READONLY, nullptr ) == SQLITE_OK )
    {
      QString errMsg;
      uniqueFieldNames = QgsSqliteUtils::uniqueFields( dsPtr.get(), mOgrLayer->name(), errMsg );
      if ( ! errMsg.isEmpty() )
      {
        QgsMessageLog::logMessage( tr( "GPKG error searching for unique constraints on fields for table %1. (%2)" ).arg( QString( mOgrLayer->name() ), errMsg ), tr( "OGR" ) );
      }
    }
  }

  int createdFields = 0;
  if ( mFirstFieldIsFid )
  {
    QgsField fidField(
      fidColumn,
      QVariant::LongLong,
      QStringLiteral( "Integer64" )
    );
    // Set constraints for feature id
    QgsFieldConstraints constraints = fidField.constraints();
    constraints.setConstraint( QgsFieldConstraints::ConstraintUnique, QgsFieldConstraints::ConstraintOriginProvider );
    constraints.setConstraint( QgsFieldConstraints::ConstraintNotNull, QgsFieldConstraints::ConstraintOriginProvider );
    fidField.setConstraints( constraints );
    mAttributeFields.append(
      fidField
    );
    mDefaultValues.insert( 0, tr( "Autogenerate" ) );
    createdFields++;
    mPrimaryKeyAttrs << 0;
  }

  for ( int i = 0; i < fdef.GetFieldCount(); ++i )
  {
    OGRFieldDefnH fldDef = fdef.GetFieldDefn( i );
    OGRFieldType ogrType = OGR_Fld_GetType( fldDef );
    OGRFieldSubType ogrSubType = OFSTNone;

    QVariant::Type varType;
    QVariant::Type varSubType = QVariant::Invalid;
    switch ( ogrType )
    {
      case OFTInteger:
        if ( OGR_Fld_GetSubType( fldDef ) == OFSTBoolean )
        {
          varType = QVariant::Bool;
          ogrSubType = OFSTBoolean;
        }
        else
          varType = QVariant::Int;
        break;
      case OFTInteger64:
        varType = QVariant::LongLong;
        break;
      case OFTReal:
        varType = QVariant::Double;
        break;
      case OFTDate:
        varType = QVariant::Date;
        break;
      case OFTTime:
        varType = QVariant::Time;
        break;
      case OFTDateTime:
        varType = QVariant::DateTime;
        break;

      case OFTBinary:
        varType = QVariant::ByteArray;
        break;

      case OFTString:
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,4,0)
        if ( OGR_Fld_GetSubType( fldDef ) == OFSTJSON )
        {
          ogrSubType = OFSTJSON;
          varType = QVariant::Map;
          varSubType = QVariant::String;
        }
        else
        {
          varType = QVariant::String;
        }
        break;
#endif

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,4,0)
      case OFTStringList:
        varType = QVariant::List;
        varSubType = QVariant::String;
        break;
#endif

      default:
        varType = QVariant::String; // other unsupported, leave it as a string
    }

    //TODO: fix this hack
#ifdef ANDROID
    QString name = OGR_Fld_GetNameRef( fldDef );
#else
    QString name = textEncoding()->toUnicode( OGR_Fld_GetNameRef( fldDef ) );
#endif

    if ( mAttributeFields.indexFromName( name ) != -1 )
    {

      QString tmpname = name + "_%1";
      int fix = 0;

      while ( mAttributeFields.indexFromName( name ) != -1 )
      {
        name = tmpname.arg( ++fix );
      }
    }

    int width = OGR_Fld_GetWidth( fldDef );
    int prec = OGR_Fld_GetPrecision( fldDef );
    if ( prec > 0 )
      width -= 1;

    QString typeName = OGR_GetFieldTypeName( ogrType );
    if ( ogrSubType != OFSTNone )
      typeName = OGR_GetFieldSubTypeName( ogrSubType );

    QgsField newField = QgsField(
                          name,
                          varType,
#ifdef ANDROID
                          typeName,
#else
                          textEncoding()->toUnicode( typeName.toStdString().c_str() ),
#endif
                          width, prec, QString(), varSubType
                        );

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(3,2,0)
    const QString alias = textEncoding()->toUnicode( OGR_Fld_GetAlternativeNameRef( fldDef ) );
    if ( !alias.isEmpty() )
    {
      newField.setAlias( alias );
    }
#endif

    // check if field is nullable
    bool nullable = OGR_Fld_IsNullable( fldDef );
    if ( !nullable )
    {
      QgsFieldConstraints constraints;
      constraints.setConstraint( QgsFieldConstraints::ConstraintNotNull, QgsFieldConstraints::ConstraintOriginProvider );
      newField.setConstraints( constraints );
    }

    if ( uniqueFieldNames.contains( OGR_Fld_GetNameRef( fldDef ) ) )
    {
      QgsFieldConstraints constraints = newField.constraints();
      constraints.setConstraint( QgsFieldConstraints::ConstraintUnique, QgsFieldConstraints::ConstraintOriginProvider );
      newField.setConstraints( constraints );
    }

    // check if field has default value
    QString defaultValue = textEncoding()->toUnicode( OGR_Fld_GetDefault( fldDef ) );
    if ( !defaultValue.isEmpty() && !OGR_Fld_IsDefaultDriverSpecific( fldDef ) )
    {
      if ( defaultValue.startsWith( '\'' ) )
      {
        defaultValue = defaultValue.remove( 0, 1 );
        defaultValue.chop( 1 );
        defaultValue.replace( QLatin1String( "''" ), QLatin1String( "'" ) );
      }
      mDefaultValues.insert( createdFields, defaultValue );
    }

    mAttributeFields.append( newField );
    createdFields++;
  }
}


QString QgsOgrProvider::storageType() const
{
  // Delegate to the driver loaded in by OGR
  return mGDALDriverName;
}


void QgsOgrProvider::setRelevantFields( bool fetchGeometry, const QgsAttributeList &fetchAttributes ) const
{
  QMutex *mutex = nullptr;
  OGRLayerH ogrLayer = mOgrLayer->getHandleAndMutex( mutex );
  QMutexLocker locker( mutex );
  QgsOgrProviderUtils::setRelevantFields( ogrLayer, mAttributeFields.count(), fetchGeometry, fetchAttributes, mFirstFieldIsFid, mSubsetString );
}


void QgsOgrProviderUtils::setRelevantFields( OGRLayerH ogrLayer, int fieldCount,
    bool fetchGeometry,
    const QgsAttributeList &fetchAttributes,
    bool firstAttrIsFid,
    const QString &subsetString )
{
  if ( OGR_L_TestCapability( ogrLayer, OLCIgnoreFields ) )
  {
    QVector<const char *> ignoredFields;
    OGRFeatureDefnH featDefn = OGR_L_GetLayerDefn( ogrLayer );
    for ( int i = ( firstAttrIsFid ? 1 : 0 ); i < fieldCount; i++ )
    {
      if ( !fetchAttributes.contains( i ) )
      {
        // add to ignored fields
        if ( OGRFieldDefnH field = OGR_FD_GetFieldDefn( featDefn, firstAttrIsFid ? i - 1 : i ) )
        {
          const char *fieldName = OGR_Fld_GetNameRef( field );
          // This is implemented a bit in a hacky way, but in case we are acting on a layer
          // with a subset filter, do not ignore fields that are found in the
          // where clause. We do this in a rough way, by looking, in a case
          // insensitive way, if the current field name is in the subsetString,
          // so we potentially don't ignore fields we could, in situations like
          // subsetFilter == "foobar = 2", and there's a "foo" or "bar" field.
          // Better be safe than sorry.
          // We could argue that OGR_L_SetIgnoredFields() should be aware of
          // the fields of the attribute filter, and do not ignore them.
          if ( subsetString.isEmpty() ||
               subsetString.indexOf( QString::fromUtf8( fieldName ), 0, Qt::CaseInsensitive ) < 0 )
          {
            ignoredFields.append( fieldName );
          }
        }
      }
    }

    if ( !fetchGeometry )
      ignoredFields.append( "OGR_GEOMETRY" );
    ignoredFields.append( "OGR_STYLE" ); // not used by QGIS
    ignoredFields.append( nullptr );

    OGR_L_SetIgnoredFields( ogrLayer, ignoredFields.data() );
  }
}

QgsFeatureIterator QgsOgrProvider::getFeatures( const QgsFeatureRequest &request ) const
{
  return QgsFeatureIterator( new QgsOgrFeatureIterator( static_cast<QgsOgrFeatureSource *>( featureSource() ), true, request, mTransaction ) );
}


unsigned char *QgsOgrProvider::getGeometryPointer( OGRFeatureH fet )
{
  OGRGeometryH geom = OGR_F_GetGeometryRef( fet );
  unsigned char *gPtr = nullptr;

  if ( !geom )
    return nullptr;

  // get the wkb representation
  gPtr = new unsigned char[OGR_G_WkbSize( geom )];

  OGR_G_ExportToWkb( geom, ( OGRwkbByteOrder ) QgsApplication::endian(), gPtr );
  return gPtr;
}


QgsRectangle QgsOgrProvider::extent() const
{
  if ( !mExtent )
  {
    QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
    QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

    mExtent.reset( new OGREnvelope() );

    // get the extent_ (envelope) of the layer
    QgsDebugMsgLevel( QStringLiteral( "Starting get extent" ), 3 );

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,1,2)
    if ( mForceRecomputeExtent && mValid && mGDALDriverName == QLatin1String( "GPKG" ) && mOgrOrigLayer )
    {
      // works with unquoted layerName
      QByteArray sql = QByteArray( "RECOMPUTE EXTENT ON " ) + mOgrOrigLayer->name();
      QgsDebugMsgLevel( QStringLiteral( "SQL: %1" ).arg( QString::fromUtf8( sql ) ), 2 );
      mOgrOrigLayer->ExecuteSQLNoReturn( sql );
    }
#endif

    mExtent->MinX = std::numeric_limits<double>::max();
    mExtent->MinY = std::numeric_limits<double>::max();
    mExtent->MaxX = -std::numeric_limits<double>::max();
    mExtent->MaxY = -std::numeric_limits<double>::max();

    // TODO: This can be expensive, do we really need it!
    if ( mOgrLayer == mOgrOrigLayer.get() && mSubsetString.isEmpty() )
    {
      if ( ( mGDALDriverName == QLatin1String( "OAPIF" ) || mGDALDriverName == QLatin1String( "WFS3" ) ) &&
           !mOgrLayer->TestCapability( OLCFastGetExtent ) )
      {
        // When the extent is not in the metadata, retrieving it would be
        // super slow
        mExtent->MinX = -180;
        mExtent->MinY = -90;
        mExtent->MaxX = 180;
        mExtent->MaxY = 90;
      }
      else
      {
        mOgrLayer->GetExtent( mExtent.get(), true );
      }
    }
    else
    {
      gdal::ogr_feature_unique_ptr f;

      mOgrLayer->ResetReading();
      while ( f.reset( mOgrLayer->GetNextFeature() ), f )
      {
        OGRGeometryH g = OGR_F_GetGeometryRef( f.get() );
        if ( g && !OGR_G_IsEmpty( g ) )
        {
          OGREnvelope env;
          OGR_G_GetEnvelope( g, &env );

          mExtent->MinX = std::min( mExtent->MinX, env.MinX );
          mExtent->MinY = std::min( mExtent->MinY, env.MinY );
          mExtent->MaxX = std::max( mExtent->MaxX, env.MaxX );
          mExtent->MaxY = std::max( mExtent->MaxY, env.MaxY );
        }
      }
      mOgrLayer->ResetReading();
    }

    QgsDebugMsgLevel( QStringLiteral( "Finished get extent" ), 4 );
  }

  mExtentRect.set( mExtent->MinX, mExtent->MinY, mExtent->MaxX, mExtent->MaxY );
  return mExtentRect;
}

QVariant QgsOgrProvider::defaultValue( int fieldId ) const
{
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  if ( fieldId < 0 || fieldId >= mAttributeFields.count() )
    return QVariant();

  QString defaultVal = mDefaultValues.value( fieldId, QString() );
  if ( defaultVal.isEmpty() )
    return QVariant();

  QVariant resultVar = defaultVal;
  if ( defaultVal == QLatin1String( "CURRENT_TIMESTAMP" ) )
    resultVar = QDateTime::currentDateTime();
  else if ( defaultVal == QLatin1String( "CURRENT_DATE" ) )
    resultVar = QDate::currentDate();
  else if ( defaultVal == QLatin1String( "CURRENT_TIME" ) )
    resultVar = QTime::currentTime();

  // Get next sequence value for sqlite in case we are inside a transaction
  if ( mOgrOrigLayer &&
       mTransaction &&
       mDefaultValues.value( fieldId, QString() ) == tr( "Autogenerate" ) &&
       providerProperty( EvaluateDefaultValues, false ).toBool() &&
       ( mGDALDriverName == QLatin1String( "GPKG" ) ||
         mGDALDriverName == QLatin1String( "SQLite" ) ) &&
       mFirstFieldIsFid &&
       fieldId == 0 )
  {
    QgsOgrLayerUniquePtr resultLayer = mOgrOrigLayer->ExecuteSQL( QByteArray( "SELECT seq FROM sqlite_sequence WHERE name = " ) +  QgsSqliteUtils::quotedValue( mOgrOrigLayer->name() ).toUtf8() );
    if ( resultLayer )
    {
      gdal::ogr_feature_unique_ptr f;
      if ( f.reset( resultLayer->GetNextFeature() ), f )
      {
        bool ok { true };
        const QVariant res = QgsOgrUtils::getOgrFeatureAttribute( f.get(),
                             fields().at( 0 ),
                             0, textEncoding(), &ok );
        if ( ok )
        {
          long long nextVal { res.toLongLong( &ok ) };
          if ( ok )
          {
            // Increment
            resultVar = ++nextVal;
            mOgrOrigLayer->ExecuteSQLNoReturn( QByteArray( "UPDATE sqlite_sequence SET seq = seq + 1 WHERE name = " ) +  QgsSqliteUtils::quotedValue( mOgrOrigLayer->name() ).toUtf8() );
          }
        }

        if ( ! ok )
        {
          QgsMessageLog::logMessage( tr( "Error retrieving next sequence value for %1" ).arg( QString::fromUtf8( mOgrOrigLayer->name() ) ), tr( "OGR" ) );
        }
      }
      else  // no sequence!
      {
        resultVar = 1;
        mOgrOrigLayer->ExecuteSQLNoReturn( QByteArray( "INSERT INTO sqlite_sequence (name, seq) VALUES( " +
                                           QgsSqliteUtils::quotedValue( mOgrOrigLayer->name() ).toUtf8() ) + ", 1)" );
      }
    }
    else
    {
      QgsMessageLog::logMessage( tr( "Error retrieving default value for %1" ).arg( mLayerName ), tr( "OGR" ) );
    }
  }

  ( void )mAttributeFields.at( fieldId ).convertCompatible( resultVar );
  return resultVar;
}

QString QgsOgrProvider::defaultValueClause( int fieldIndex ) const
{
  // Return empty clause to force defaultValue calls for sqlite in case we are inside a transaction
  if ( mTransaction &&
       mDefaultValues.value( fieldIndex, QString() ) == tr( "Autogenerate" ) &&
       providerProperty( EvaluateDefaultValues, false ).toBool() &&
       ( mGDALDriverName == QLatin1String( "GPKG" ) ||
         mGDALDriverName == QLatin1String( "SQLite" ) ) &&
       mFirstFieldIsFid &&
       fieldIndex == 0 )
    return QString();
  else
    return mDefaultValues.value( fieldIndex, QString() );
}

bool QgsOgrProvider::skipConstraintCheck( int fieldIndex, QgsFieldConstraints::Constraint constraint, const QVariant &value ) const
{
  Q_UNUSED( constraint )
  if ( providerProperty( EvaluateDefaultValues, false ).toBool() )
  {
    return ! mDefaultValues.value( fieldIndex ).isEmpty();
  }
  else
  {
    // stricter check
    return mDefaultValues.contains( fieldIndex ) && mDefaultValues.value( fieldIndex ) == value.toString() && !value.isNull();
  }
}

void QgsOgrProvider::updateExtents()
{
  invalidateCachedExtent( true );
}

void QgsOgrProvider::invalidateCachedExtent( bool bForceRecomputeExtent )
{
  mForceRecomputeExtent = bForceRecomputeExtent;
  mExtent.reset();
}

size_t QgsOgrProvider::layerCount() const
{
  if ( !mValid )
    return 0;
  return mOgrLayer->GetLayerCount();
}

/**
 * Returns the feature type
 */
QgsWkbTypes::Type QgsOgrProvider::wkbType() const
{
  QgsWkbTypes::Type wkb = QgsOgrUtils::ogrGeometryTypeToQgsWkbType( mOGRGeomType );
  const QgsWkbTypes::Type wkbFlat = QgsWkbTypes::flatType( wkb );
  if ( mGDALDriverName == QLatin1String( "ESRI Shapefile" ) && ( wkbFlat == QgsWkbTypes::LineString || wkbFlat == QgsWkbTypes::Polygon ) )
  {
    wkb = QgsWkbTypes::multiType( wkb );
  }
  if ( mOGRGeomType % 1000 == wkbPolyhedralSurface ) // is PolyhedralSurface, PolyhedralSurfaceZ, PolyhedralSurfaceM or PolyhedralSurfaceZM => map to MultiPolygon
  {
    wkb = static_cast<QgsWkbTypes::Type>( mOGRGeomType - ( wkbPolyhedralSurface - wkbMultiPolygon ) );
  }
  else if ( mOGRGeomType % 1000 == wkbTIN ) // is TIN, TINZ, TINM or TINZM => map to MultiPolygon
  {
    wkb = static_cast<QgsWkbTypes::Type>( mOGRGeomType - ( wkbTIN - wkbMultiPolygon ) );
  }
  return wkb;
}

/**
 * Returns the feature count
 */
long QgsOgrProvider::featureCount() const
{
  if ( ( mReadFlags & QgsDataProvider::SkipFeatureCount ) != 0 )
  {
    return QgsVectorDataProvider::UnknownCount;
  }
  if ( mRefreshFeatureCount )
  {
    mRefreshFeatureCount = false;
    recalculateFeatureCount();
  }
  return mFeaturesCounted;
}


QgsFields QgsOgrProvider::fields() const
{
  return mAttributeFields;
}


//TODO - add sanity check for shape file layers, to include checking to
//       see if the .shp, .dbf, .shx files are all present and the layer
//       actually has features
bool QgsOgrProvider::isValid() const
{
  return mValid;
}

// Drivers may be more tolerant than we really wish (e.g. GeoPackage driver
// may accept any geometry type)
OGRGeometryH QgsOgrProvider::ConvertGeometryIfNecessary( OGRGeometryH hGeom )
{
  if ( !hGeom )
    return hGeom;
  OGRwkbGeometryType layerGeomType = mOgrLayer->GetLayerDefn().GetGeomType();
  OGRwkbGeometryType flattenLayerGeomType = wkbFlatten( layerGeomType );
  OGRwkbGeometryType geomType = OGR_G_GetGeometryType( hGeom );
  OGRwkbGeometryType flattenGeomType = wkbFlatten( geomType );

  if ( flattenLayerGeomType == wkbUnknown || flattenLayerGeomType == flattenGeomType )
  {
    return hGeom;
  }
  if ( flattenLayerGeomType == wkbMultiPolygon && flattenGeomType == wkbPolygon )
  {
    return OGR_G_ForceToMultiPolygon( hGeom );
  }
  if ( flattenLayerGeomType == wkbMultiLineString && flattenGeomType == wkbLineString )
  {
    return OGR_G_ForceToMultiLineString( hGeom );
  }

  return OGR_G_ForceTo( hGeom, layerGeomType, nullptr );
}

QString QgsOgrProvider::jsonStringValue( const QVariant &value ) const
{
  QString stringValue = QString::fromUtf8( QJsonDocument::fromVariant( value ).toJson().constData() );
  if ( stringValue.isEmpty() )
  {
    //store as string, because it's no valid QJson value
    stringValue = value.toString();
  }
  return stringValue;
}

bool QgsOgrProvider::addFeaturePrivate( QgsFeature &f, Flags flags )
{
  bool returnValue = true;
  QgsOgrFeatureDefn &featureDefinition = mOgrLayer->GetLayerDefn();
  gdal::ogr_feature_unique_ptr feature( featureDefinition.CreateFeature() );

  if ( f.hasGeometry() )
  {
    QByteArray wkb( f.geometry().asWkb() );
    OGRGeometryH geom = nullptr;

    if ( !wkb.isEmpty() )
    {
      if ( OGR_G_CreateFromWkb( reinterpret_cast<unsigned char *>( const_cast<char *>( wkb.constData() ) ), nullptr, &geom, wkb.length() ) != OGRERR_NONE )
      {
        pushError( tr( "OGR error creating wkb for feature %1: %2" ).arg( f.id() ).arg( CPLGetLastErrorMsg() ) );
        return false;
      }

      geom = ConvertGeometryIfNecessary( geom );

      OGR_F_SetGeometryDirectly( feature.get(), geom );
    }
  }

  QgsAttributes attributes = f.attributes();
  const QgsFields qgisFields { f.fields() };

  QgsLocaleNumC l;

  int qgisAttributeId = ( mFirstFieldIsFid ) ? 1 : 0;
  // If the first attribute is the FID and the user has set it, then use it
  if ( mFirstFieldIsFid && attributes.count() > 0 )
  {
    QVariant attrFid = attributes.at( 0 );
    if ( !attrFid.isNull() )
    {
      bool ok = false;
      qlonglong id = attrFid.toLongLong( &ok );
      if ( ok )
      {
        OGR_F_SetFID( feature.get(), static_cast<GIntBig>( id ) );
      }
    }
  }

  //add possible attribute information
  for ( int ogrAttributeId = 0; qgisAttributeId < attributes.count(); ++qgisAttributeId, ++ogrAttributeId )
  {
    // Skip fields that have no provider origin
    if ( qgisFields.exists( qgisAttributeId ) && qgisFields.fieldOrigin( qgisAttributeId ) != QgsFields::FieldOrigin::OriginProvider )
    {
      qgisAttributeId++;
      continue;
    }

    // don't try to set field from attribute map if it's not present in layer
    if ( ogrAttributeId >= featureDefinition.GetFieldCount() )
    {
      pushError( tr( "Feature has too many attributes (expecting %1, received %2)" ).arg( featureDefinition.GetFieldCount() ).arg( f.attributes().count() ) );
      continue;
    }

    //if(!s.isEmpty())
    // continue;
    //
    OGRFieldDefnH fldDef = featureDefinition.GetFieldDefn( ogrAttributeId );
    OGRFieldType type = OGR_Fld_GetType( fldDef );

    QVariant attrVal = attributes.at( qgisAttributeId );
    // The field value is equal to the default (that might be a provider-side expression)
    if ( mDefaultValues.contains( qgisAttributeId ) && attrVal.toString() == mDefaultValues.value( qgisAttributeId ) )
    {
      OGR_F_UnsetField( feature.get(), ogrAttributeId );
    }
    else if ( attrVal.isNull() || ( type != OFTString && attrVal.toString().isEmpty() ) )
    {
// Starting with GDAL 2.2, there are 2 concepts: unset fields and null fields
// whereas previously there was only unset fields. For a GeoJSON output,
// leaving a field unset will cause it to not appear at all in the output
// feature.
// When all features of a layer have a field unset, this would cause the
// field to not be present at all in the output, and thus on reading to
// have disappeared. #16812
#ifdef OGRNullMarker
      OGR_F_SetFieldNull( feature.get(), ogrAttributeId );
#else
      OGR_F_UnsetField( feature.get(), ogrAttId );
#endif
    }
    else
    {
      switch ( type )
      {
        case OFTInteger:
          OGR_F_SetFieldInteger( feature.get(), ogrAttributeId, attrVal.toInt() );
          break;


        case OFTInteger64:
          OGR_F_SetFieldInteger64( feature.get(), ogrAttributeId, attrVal.toLongLong() );
          break;

        case OFTReal:
          OGR_F_SetFieldDouble( feature.get(), ogrAttributeId, attrVal.toDouble() );
          break;

        case OFTDate:
          OGR_F_SetFieldDateTime( feature.get(), ogrAttributeId,
                                  attrVal.toDate().year(),
                                  attrVal.toDate().month(),
                                  attrVal.toDate().day(),
                                  0, 0, 0,
                                  0 );
          break;

        case OFTTime:
          OGR_F_SetFieldDateTime( feature.get(), ogrAttributeId,
                                  0, 0, 0,
                                  attrVal.toTime().hour(),
                                  attrVal.toTime().minute(),
                                  attrVal.toTime().second(),
                                  0 );
          break;

        case OFTDateTime:
          OGR_F_SetFieldDateTime( feature.get(), ogrAttributeId,
                                  attrVal.toDateTime().date().year(),
                                  attrVal.toDateTime().date().month(),
                                  attrVal.toDateTime().date().day(),
                                  attrVal.toDateTime().time().hour(),
                                  attrVal.toDateTime().time().minute(),
                                  attrVal.toDateTime().time().second(),
                                  0 );
          break;

        case OFTString:
        {
          QString stringValue;

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,4,0)
          if ( OGR_Fld_GetSubType( fldDef ) == OFSTJSON )
            stringValue = jsonStringValue( attrVal );
          else
          {
            stringValue = attrVal.toString();
          }
#else
          stringValue = attrVal.toString();
#endif
          QgsDebugMsgLevel( QStringLiteral( "Writing string attribute %1 with %2, encoding %3" )
                            .arg( qgisAttributeId )
                            .arg( attrVal.toString(),
                                  textEncoding()->name().data() ), 3 );
          OGR_F_SetFieldString( feature.get(), ogrAttributeId, textEncoding()->fromUnicode( stringValue ).constData() );
          break;
        }
        case OFTBinary:
        {
          const QByteArray ba = attrVal.toByteArray();
          OGR_F_SetFieldBinary( feature.get(), ogrAttributeId, ba.size(), const_cast< GByte * >( reinterpret_cast< const GByte * >( ba.data() ) ) );
          break;
        }

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,4,0)
        case OFTStringList:
        {
          QStringList list = attrVal.toStringList();
          int count = list.count();
          char **lst = new char *[count + 1];
          if ( count > 0 )
          {
            int pos = 0;
            for ( QString string : list )
            {
              lst[pos] = textEncoding()->fromUnicode( string ).data();
              pos++;
            }
          }
          lst[count] = nullptr;
          OGR_F_SetFieldStringList( feature.get(), ogrAttributeId, lst );
          break;
        }
#endif

        default:
          QgsMessageLog::logMessage( tr( "type %1 for attribute %2 not found" ).arg( type ).arg( qgisAttributeId ), tr( "OGR" ) );
          break;
      }
    }
  }

  if ( mOgrLayer->CreateFeature( feature.get() ) != OGRERR_NONE )
  {
    pushError( tr( "OGR error creating feature %1: %2" ).arg( f.id() ).arg( CPLGetLastErrorMsg() ) );
    returnValue = false;
  }
  else if ( !( flags & QgsFeatureSink::FastInsert ) )
  {
    QgsFeatureId id = static_cast<QgsFeatureId>( OGR_F_GetFID( feature.get() ) );
    if ( id >= 0 )
    {
      f.setId( id );

      if ( mFirstFieldIsFid && attributes.count() > 0 )
      {
        f.setAttribute( 0, id );
      }
    }
  }

  return returnValue;
}


bool QgsOgrProvider::addFeatures( QgsFeatureList &flist, Flags flags )
{
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  if ( !doInitialActionsForEdition() )
    return false;

  setRelevantFields( true, attributeIndexes() );

  const bool inTransaction = startTransaction();

  bool returnvalue = true;
  for ( QgsFeatureList::iterator it = flist.begin(); it != flist.end(); ++it )
  {
    if ( !addFeaturePrivate( *it, flags ) )
    {
      returnvalue = false;
    }
  }

  if ( inTransaction )
  {
    if ( returnvalue )
      returnvalue = commitTransaction();
    else
      rollbackTransaction();
  }

  if ( !syncToDisc() )
  {
    returnvalue = false;
  }

  if ( mFeaturesCounted != QgsVectorDataProvider::Uncounted &&
       mFeaturesCounted != QgsVectorDataProvider::UnknownCount )
  {
    if ( returnvalue )
      mFeaturesCounted += flist.size();
    else
      recalculateFeatureCount();
  }

  if ( returnvalue )
    clearMinMaxCache();

  if ( mTransaction )
    mTransaction->dirtyLastSavePoint();

  return returnvalue;
}

bool QgsOgrProvider::addAttributeOGRLevel( const QgsField &field, bool &ignoreErrorOut )
{
  ignoreErrorOut = false;

  OGRFieldType type;

  switch ( field.type() )
  {
    case QVariant::Int:
    case QVariant::Bool:
      type = OFTInteger;
      break;
    case QVariant::LongLong:
    {
      const char *pszDataTypes = GDALGetMetadataItem( mOgrLayer->driver(), GDAL_DMD_CREATIONFIELDDATATYPES, nullptr );
      if ( pszDataTypes && strstr( pszDataTypes, "Integer64" ) )
        type = OFTInteger64;
      else
      {
        type = OFTReal;
      }
      break;
    }
    case QVariant::Double:
      type = OFTReal;
      break;
    case QVariant::Date:
      type = OFTDate;
      break;
    case QVariant::Time:
      type = OFTTime;
      break;
    case QVariant::DateTime:
      type = OFTDateTime;
      break;
    case QVariant::String:
      type = OFTString;
      break;
    case QVariant::ByteArray:
      type = OFTBinary;
      break;
    case QVariant::Map:
      type = OFTString;
      break;
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,4,0)
    case QVariant::List:
      // only string list supported at the moment, fall through to default for other types
      if ( field.subType() == QVariant::String )
      {
        type = OFTStringList;
        break;
      }
      //intentional fall-through
      FALLTHROUGH
#endif
    default:
      pushError( tr( "type %1 for field %2 not found" ).arg( field.typeName(), field.name() ) );
      ignoreErrorOut = true;
      return false;
  }

  gdal::ogr_field_def_unique_ptr fielddefn( OGR_Fld_Create( textEncoding()->fromUnicode( field.name() ).constData(), type ) );
  int width = field.length();
  // Increase width by 1 for OFTReal to make room for the decimal point
  if ( type == OFTReal && field.precision() )
    width += 1;
  OGR_Fld_SetWidth( fielddefn.get(), width );
  OGR_Fld_SetPrecision( fielddefn.get(), field.precision() );

  switch ( field.type() )
  {
    case QVariant::Bool:
      OGR_Fld_SetSubType( fielddefn.get(), OFSTBoolean );
      break;
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,4,0)
    case QVariant::Map:
      OGR_Fld_SetSubType( fielddefn.get(), OFSTJSON );
      break;
#endif
    default:
      break;
  }

  if ( mOgrLayer->CreateField( fielddefn.get(), true ) != OGRERR_NONE )
  {
    pushError( tr( "OGR error creating field %1: %2" ).arg( field.name(), CPLGetLastErrorMsg() ) );
    return false;
  }
  return true;
}

bool QgsOgrProvider::addAttributes( const QList<QgsField> &attributes )
{
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  if ( !doInitialActionsForEdition() )
    return false;

  if ( mGDALDriverName == QLatin1String( "MapInfo File" ) )
  {
    // adding attributes in mapinfo requires to be able to delete the .dat file
    // so drop any cached connections.
    QgsOgrConnPool::instance()->invalidateConnections( QgsOgrProviderUtils::connectionPoolId( dataSourceUri( true ), mShareSameDatasetAmongLayers ) );
  }

  bool returnvalue = true;

  QMap< QString, QgsField > mapFieldNameToOriginalField;

  for ( const auto &field : attributes )
  {
    mapFieldNameToOriginalField[ field.name()] = field;

    bool ignoreErrorOut = false;
    if ( !addAttributeOGRLevel( field, ignoreErrorOut ) )
    {
      returnvalue = false;
      if ( !ignoreErrorOut )
      {
        break;
      }
    }
  }

  // Backup existing fields. We need them to 'restore' field type, length, precision
  QgsFields oldFields = mAttributeFields;

  loadFields();

  // The check in QgsVectorLayerEditBuffer::commitChanges() is questionable with
  // real-world drivers that might only be able to satisfy request only partially.
  // So to avoid erroring out, patch field type, width and precision to match
  // what was requested.
  // For example in case of Integer64->Real mapping so that QVariant::LongLong is
  // still returned to the caller
  // Or if a field width was specified but not strictly enforced by the driver (#15614)
  for ( QMap< QString, QgsField >::const_iterator it = mapFieldNameToOriginalField.constBegin();
        it != mapFieldNameToOriginalField.constEnd(); ++it )
  {
    int idx = mAttributeFields.lookupField( it.key() );
    if ( idx >= 0 )
    {
      mAttributeFields[ idx ].setType( it->type() );
      mAttributeFields[ idx ].setLength( it->length() );
      mAttributeFields[ idx ].setPrecision( it->precision() );
    }
  }

  // Restore field type, length, precision of existing fields as well
  // We need that in scenarios where the user adds a int field with length != 0
  // in a editing session, and repeat that again in another editing session
  // Without the below hack, the length of the first added field would have
  // been reset to zero, and QgsVectorLayerEditBuffer::commitChanges() would
  // error out because of this.
  // See https://github.com/qgis/QGIS/issues/26840
  for ( auto field : oldFields )
  {
    int idx = mAttributeFields.lookupField( field.name() );
    if ( idx >= 0 )
    {
      mAttributeFields[ idx ].setType( field.type() );
      mAttributeFields[ idx ].setLength( field.length() );
      mAttributeFields[ idx ].setPrecision( field.precision() );
    }
  }

  if ( mTransaction )
    mTransaction->dirtyLastSavePoint();

  return returnvalue;
}

bool QgsOgrProvider::deleteAttributes( const QgsAttributeIds &attributes )
{
  if ( !doInitialActionsForEdition() )
    return false;

  bool res = true;
  QList<int> attrsLst = qgis::setToList( attributes );
  // sort in descending order
  std::sort( attrsLst.begin(), attrsLst.end(), std::greater<int>() );
  const auto constAttrsLst = attrsLst;
  for ( int attr : constAttrsLst )
  {
    if ( mFirstFieldIsFid )
    {
      if ( attr == 0 )
      {
        pushError( tr( "Cannot delete feature id column" ) );
        res = false;
        break;
      }
      else
      {
        --attr;
      }
    }
    if ( mOgrLayer->DeleteField( attr ) != OGRERR_NONE )
    {
      pushError( tr( "OGR error deleting field %1: %2" ).arg( attr ).arg( CPLGetLastErrorMsg() ) );
      res = false;
    }
  }
  loadFields();

  if ( mTransaction )
    mTransaction->dirtyLastSavePoint();

  return res;
}

bool QgsOgrProvider::renameAttributes( const QgsFieldNameMap &renamedAttributes )
{
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  if ( !doInitialActionsForEdition() )
    return false;

  QgsFieldNameMap::const_iterator renameIt = renamedAttributes.constBegin();
  bool result = true;
  for ( ; renameIt != renamedAttributes.constEnd(); ++renameIt )
  {
    int fieldIndex = renameIt.key();
    if ( fieldIndex < 0 || fieldIndex >= mAttributeFields.count() )
    {
      pushError( tr( "Invalid attribute index" ) );
      result = false;
      continue;
    }
    if ( mAttributeFields.indexFromName( renameIt.value() ) >= 0 )
    {
      //field name already in use
      pushError( tr( "Error renaming field %1: name '%2' already exists" ).arg( fieldIndex ).arg( renameIt.value() ) );
      result = false;
      continue;
    }
    int ogrFieldIndex = fieldIndex;
    if ( mFirstFieldIsFid )
    {
      ogrFieldIndex -= 1;
      if ( ogrFieldIndex < 0 )
      {
        pushError( tr( "Invalid attribute index" ) );
        result = false;
        continue;
      }
    }

    //type does not matter, it will not be used
    gdal::ogr_field_def_unique_ptr fld( OGR_Fld_Create( textEncoding()->fromUnicode( renameIt.value() ), OFTReal ) );
    if ( mOgrLayer->AlterFieldDefn( ogrFieldIndex, fld.get(), ALTER_NAME_FLAG ) != OGRERR_NONE )
    {
      pushError( tr( "OGR error renaming field %1: %2" ).arg( fieldIndex ).arg( CPLGetLastErrorMsg() ) );
      result = false;
    }
  }
  loadFields();

  if ( mTransaction )
    mTransaction->dirtyLastSavePoint();

  return result;
}

bool QgsOgrProvider::startTransaction()
{
  bool inTransaction = false;
  if ( mTransaction == nullptr && mOgrLayer->TestCapability( OLCTransactions ) )
  {
    // A transaction might already be active, so be robust on failed
    // StartTransaction.
    CPLPushErrorHandler( CPLQuietErrorHandler );
    inTransaction = ( mOgrLayer->StartTransaction() == OGRERR_NONE );
    CPLPopErrorHandler();
  }
  return inTransaction;
}


bool QgsOgrProvider::commitTransaction()
{
  if ( mOgrLayer->CommitTransaction() != OGRERR_NONE )
  {
    pushError( tr( "OGR error committing transaction: %1" ).arg( CPLGetLastErrorMsg() ) );
    return false;
  }
  return true;
}


bool QgsOgrProvider::rollbackTransaction()
{
  if ( mOgrLayer->RollbackTransaction() != OGRERR_NONE )
  {
    pushError( tr( "OGR error rolling back transaction: %1" ).arg( CPLGetLastErrorMsg() ) );
    return false;
  }
  return true;
}

bool QgsOgrProvider::_setSubsetString( const QString &theSQL, bool updateFeatureCount, bool updateCapabilities, bool hasExistingRef )
{
  QgsCPLErrorHandler handler;

  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  if ( !mOgrOrigLayer )
    return false;

  if ( theSQL == mSubsetString && mFeaturesCounted != QgsVectorDataProvider::Uncounted )
    return true;

  if ( !theSQL.isEmpty() )
  {
    QMutex *mutex = nullptr;
    OGRLayerH layer = mOgrOrigLayer->getHandleAndMutex( mutex );
    GDALDatasetH ds = mOgrOrigLayer->getDatasetHandleAndMutex( mutex );
    OGRLayerH subsetLayerH;
    {
      QMutexLocker locker( mutex );
      subsetLayerH = QgsOgrProviderUtils::setSubsetString( layer, ds, textEncoding(), theSQL );
    }
    if ( !subsetLayerH )
    {
      pushError( tr( "OGR[%1] error %2: %3" ).arg( CPLGetLastErrorType() ).arg( CPLGetLastErrorNo() ).arg( CPLGetLastErrorMsg() ) );
      return false;
    }
    if ( layer != subsetLayerH )
    {
      mOgrSqlLayer = QgsOgrProviderUtils::getSqlLayer( mOgrOrigLayer.get(), subsetLayerH, theSQL );
      Q_ASSERT( mOgrSqlLayer.get() );
      mOgrLayer = mOgrSqlLayer.get();
    }
    else
    {
      mOgrSqlLayer.reset();
      mOgrLayer = mOgrOrigLayer.get();
    }
  }
  else
  {
    mOgrSqlLayer.reset();
    mOgrLayer = mOgrOrigLayer.get();
    QMutex *mutex = nullptr;
    OGRLayerH layer = mOgrOrigLayer->getHandleAndMutex( mutex );
    {
      QMutexLocker locker( mutex );
      OGR_L_SetAttributeFilter( layer, nullptr );
    }
  }
  mSubsetString = theSQL;

  QVariantMap parts;
  parts.insert( QStringLiteral( "path" ), mFilePath );

  if ( !mLayerName.isNull() )
  {
    parts.insert( QStringLiteral( "layerName" ), mLayerName );
  }
  else if ( mIsSubLayer && mLayerIndex >= 0 )
  {
    parts.insert( QStringLiteral( "layerId" ), mLayerIndex );
  }

  if ( !mSubsetString.isEmpty() )
  {
    parts.insert( QStringLiteral( "subset" ), mSubsetString );
  }

  if ( mOgrGeometryTypeFilter != wkbUnknown )
  {
    parts.insert( QStringLiteral( "geometryType" ), ogrWkbGeometryTypeName( mOgrGeometryTypeFilter ) );
  }

  if ( !mOpenOptions.isEmpty() )
  {
    parts.insert( QStringLiteral( "openOptions" ), mOpenOptions );
  }

  QString uri = QgsOgrProviderMetadata().encodeUri( parts );
  if ( uri != dataSourceUri() )
  {
    if ( hasExistingRef )
      QgsOgrConnPool::instance()->unref( QgsOgrProviderUtils::connectionPoolId( dataSourceUri( true ), mShareSameDatasetAmongLayers ) );
    setDataSourceUri( uri );
    if ( hasExistingRef )
      QgsOgrConnPool::instance()->ref( QgsOgrProviderUtils::connectionPoolId( dataSourceUri( true ), mShareSameDatasetAmongLayers ) );
  }

  mOgrLayer->ResetReading();

  mRefreshFeatureCount = updateFeatureCount;

  // check the validity of the layer
  QgsDebugMsgLevel( QStringLiteral( "checking validity" ), 4 );
  loadFields();
  QgsDebugMsgLevel( QStringLiteral( "Done checking validity" ), 4 );

  invalidateCachedExtent( false );

  // Changing the filter may change capabilities
  if ( updateCapabilities )
    computeCapabilities();

  emit dataChanged();

  return true;

}


bool QgsOgrProvider::changeAttributeValues( const QgsChangedAttributesMap &attr_map )
{
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  if ( !doInitialActionsForEdition() )
    return false;

  if ( attr_map.isEmpty() )
    return true;

  bool returnValue = true;

  clearMinMaxCache();

  setRelevantFields( true, attributeIndexes() );

  const bool inTransaction = startTransaction();

  // Some drivers may need to call ResetReading() after GetFeature(), such
  // as GPKG in GDAL < 2.3.0 to avoid letting the database in a locked state.
  // But this is undesirable in general, so don't do this when we know that
  // we don't need to.
  bool mayNeedResetReadingAfterGetFeature = true;
  if ( mGDALDriverName == QLatin1String( "ESRI Shapefile" ) )
  {
    mayNeedResetReadingAfterGetFeature = false;
  }
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,3,0)
  else if ( mGDALDriverName == QLatin1String( "GPKG" ) )
  {
    mayNeedResetReadingAfterGetFeature = false;
  }
#endif

  for ( QgsChangedAttributesMap::const_iterator it = attr_map.begin(); it != attr_map.end(); ++it )
  {
    QgsFeatureId fid = it.key();

    const QgsAttributeMap &attr = it.value();
    if ( attr.isEmpty() )
      continue;

    gdal::ogr_feature_unique_ptr of( mOgrLayer->GetFeature( FID_TO_NUMBER( fid ) ) );
    if ( !of )
    {
      pushError( tr( "Feature %1 for attribute update not found." ).arg( fid ) );
      continue;
    }

    if ( mayNeedResetReadingAfterGetFeature )
    {
      mOgrLayer->ResetReading();
    }

    QgsLocaleNumC l;

    for ( QgsAttributeMap::const_iterator it2 = attr.begin(); it2 != attr.end(); ++it2 )
    {
      int f = it2.key();
      if ( mFirstFieldIsFid )
      {
        if ( f == 0 )
        {
          if ( it2->toLongLong() != fid )
          {
            pushError( tr( "Changing feature id of feature %1 is not allowed." ).arg( fid ) );
            continue;
          }
        }
        else
        {
          --f;
        }
      }

      OGRFieldDefnH fd = OGR_F_GetFieldDefnRef( of.get(), f );
      if ( !fd )
      {
        pushError( tr( "Field %1 of feature %2 doesn't exist." ).arg( f ).arg( fid ) );
        continue;
      }

      OGRFieldType type = OGR_Fld_GetType( fd );

      if ( it2->isNull() || ( type != OFTString && it2->toString().isEmpty() ) )
      {
// Starting with GDAL 2.2, there are 2 concepts: unset fields and null fields
// whereas previously there was only unset fields. For a GeoJSON output,
// leaving a field unset will cause it to not appear at all in the output
// feature.
// When all features of a layer have a field unset, this would cause the
// field to not be present at all in the output, and thus on reading to
// have disappeared. #16812
#ifdef OGRNullMarker
        OGR_F_SetFieldNull( of.get(), f );
#else
        OGR_F_UnsetField( of.get(), f );
#endif
      }
      else
      {

        switch ( type )
        {
          case OFTInteger:
            OGR_F_SetFieldInteger( of.get(), f, it2->toInt() );
            break;
          case OFTInteger64:
            OGR_F_SetFieldInteger64( of.get(), f, it2->toLongLong() );
            break;
          case OFTReal:
            OGR_F_SetFieldDouble( of.get(), f, it2->toDouble() );
            break;
          case OFTDate:
            OGR_F_SetFieldDateTime( of.get(), f,
                                    it2->toDate().year(),
                                    it2->toDate().month(),
                                    it2->toDate().day(),
                                    0, 0, 0,
                                    0 );
            break;
          case OFTTime:
            OGR_F_SetFieldDateTime( of.get(), f,
                                    0, 0, 0,
                                    it2->toTime().hour(),
                                    it2->toTime().minute(),
                                    it2->toTime().second(),
                                    0 );
            break;
          case OFTDateTime:
            OGR_F_SetFieldDateTime( of.get(), f,
                                    it2->toDateTime().date().year(),
                                    it2->toDateTime().date().month(),
                                    it2->toDateTime().date().day(),
                                    it2->toDateTime().time().hour(),
                                    it2->toDateTime().time().minute(),
                                    it2->toDateTime().time().second(),
                                    0 );
            break;
          case OFTString:
          {
            QString stringValue;
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,4,0)
            if ( OGR_Fld_GetSubType( fd ) == OFSTJSON )
              stringValue = jsonStringValue( it2.value() );
            else
              stringValue = it2->toString();
#else
            stringValue = it2->toString();
#endif
            OGR_F_SetFieldString( of.get(), f, textEncoding()->fromUnicode( stringValue ).constData() );
            break;
          }

          case OFTBinary:
          {
            const QByteArray ba = it2->toByteArray();
            OGR_F_SetFieldBinary( of.get(), f, ba.size(), const_cast< GByte * >( reinterpret_cast< const GByte * >( ba.data() ) ) );
            break;
          }

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,4,0)
          case OFTStringList:
          {
            QStringList list = it2->toStringList();
            int count = list.count();
            char **lst = new char *[count + 1];
            if ( count > 0 )
            {
              int pos = 0;
              for ( QString string : list )
              {
                lst[pos] = textEncoding()->fromUnicode( string ).data();
                pos++;
              }
            }
            lst[count] = nullptr;
            OGR_F_SetFieldStringList( of.get(), f, lst );
            break;
          }
#endif

          default:
            pushError( tr( "Type %1 of attribute %2 of feature %3 unknown." ).arg( type ).arg( fid ).arg( f ) );
            break;
        }
      }
    }

    if ( mOgrLayer->SetFeature( of.get() ) != OGRERR_NONE )
    {
      pushError( tr( "OGR error setting feature %1: %2" ).arg( fid ).arg( CPLGetLastErrorMsg() ) );
      returnValue = false;
    }
  }

  if ( inTransaction )
  {
    if ( returnValue )
      returnValue = commitTransaction();
    else
      rollbackTransaction();
  }

  if ( mTransaction )
    mTransaction->dirtyLastSavePoint();

  if ( !syncToDisc() )
  {
    pushError( tr( "OGR error syncing to disk: %1" ).arg( CPLGetLastErrorMsg() ) );
  }
  return returnValue;
}

bool QgsOgrProvider::changeGeometryValues( const QgsGeometryMap &geometry_map )
{
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  if ( !doInitialActionsForEdition() )
    return false;

  setRelevantFields( true, attributeIndexes() );

  const bool inTransaction = startTransaction();

  // Some drivers may need to call ResetReading() after GetFeature(), such
  // as GPKG in GDAL < 2.3.0 to avoid letting the database in a locked state.
  // But this is undesirable in general, so don't do this when we know that
  // we don't need to.
  bool mayNeedResetReadingAfterGetFeature = true;
  if ( mGDALDriverName == QLatin1String( "ESRI Shapefile" ) )
  {
    mayNeedResetReadingAfterGetFeature = false;
  }
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,3,0)
  else if ( mGDALDriverName == QLatin1String( "GPKG" ) )
  {
    mayNeedResetReadingAfterGetFeature = false;
  }
#endif

  bool returnvalue = true;
  for ( QgsGeometryMap::const_iterator it = geometry_map.constBegin(); it != geometry_map.constEnd(); ++it )
  {
    gdal::ogr_feature_unique_ptr theOGRFeature( mOgrLayer->GetFeature( FID_TO_NUMBER( it.key() ) ) );
    if ( !theOGRFeature )
    {
      pushError( tr( "OGR error changing geometry: feature %1 not found" ).arg( it.key() ) );
      continue;
    }

    if ( mayNeedResetReadingAfterGetFeature )
    {
      mOgrLayer->ResetReading(); // needed for SQLite-based to clear iterator, which could let the database in a locked state otherwise
    }

    OGRGeometryH newGeometry = nullptr;
    QByteArray wkb = it->asWkb();
    // We might receive null geometries. It is OK, but don't go through the
    // OGR_G_CreateFromWkb() route then
    if ( !wkb.isEmpty() )
    {
      //create an OGRGeometry
      if ( OGR_G_CreateFromWkb( reinterpret_cast<unsigned char *>( const_cast<char *>( wkb.constData() ) ),
                                mOgrLayer->GetSpatialRef(),
                                &newGeometry,
                                wkb.length() ) != OGRERR_NONE )
      {
        pushError( tr( "OGR error creating geometry for feature %1: %2" ).arg( it.key() ).arg( CPLGetLastErrorMsg() ) );
        OGR_G_DestroyGeometry( newGeometry );
        newGeometry = nullptr;
        continue;
      }

      if ( !newGeometry )
      {
        pushError( tr( "OGR error in feature %1: geometry is null" ).arg( it.key() ) );
        continue;
      }

      newGeometry = ConvertGeometryIfNecessary( newGeometry );
    }

    //set the new geometry
    if ( OGR_F_SetGeometryDirectly( theOGRFeature.get(), newGeometry ) != OGRERR_NONE )
    {
      pushError( tr( "OGR error setting geometry of feature %1: %2" ).arg( it.key() ).arg( CPLGetLastErrorMsg() ) );
      // Shouldn't happen normally. If it happens, ownership of the geometry
      // may be not really well defined, so better not destroy it, but just
      // the feature.
      continue;
    }


    if ( mOgrLayer->SetFeature( theOGRFeature.get() ) != OGRERR_NONE )
    {
      pushError( tr( "OGR error setting feature %1: %2" ).arg( it.key() ).arg( CPLGetLastErrorMsg() ) );
      returnvalue = false;
      continue;
    }
    mShapefileMayBeCorrupted = true;

    invalidateCachedExtent( true );
  }

  if ( inTransaction )
  {
    if ( returnvalue )
      returnvalue = commitTransaction();
    else
      rollbackTransaction();
  }

  if ( mTransaction )
    mTransaction->dirtyLastSavePoint();

  if ( !syncToDisc() )
  {
    pushError( tr( "OGR error syncing to disk: %1" ).arg( CPLGetLastErrorMsg() ) );
  }
  return returnvalue;
}

bool QgsOgrProvider::createSpatialIndex()
{
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  if ( !mOgrOrigLayer )
    return false;
  if ( !doInitialActionsForEdition() )
    return false;

  QByteArray layerName = mOgrOrigLayer->name();
  if ( mGDALDriverName == QLatin1String( "ESRI Shapefile" ) )
  {
    QByteArray sql = QByteArray( "CREATE SPATIAL INDEX ON " ) + quotedIdentifier( layerName );  // quote the layer name so spaces are handled
    QgsDebugMsgLevel( QStringLiteral( "SQL: %1" ).arg( QString::fromUtf8( sql ) ), 2 );
    mOgrOrigLayer->ExecuteSQLNoReturn( sql );

    if ( !mFilePath.endsWith( QLatin1String( ".shp" ), Qt::CaseInsensitive ) )
      return true;

    QFileInfo fi( mFilePath );     // to get the base name
    //find out, if the .qix file is there
    return QFileInfo::exists( fi.path().append( '/' ).append( fi.completeBaseName() ).append( ".qix" ) );
  }
  else if ( mGDALDriverName == QLatin1String( "GPKG" ) ||
            mGDALDriverName == QLatin1String( "SQLite" ) )
  {
    QMutex *mutex = nullptr;
    OGRLayerH layer = mOgrOrigLayer->getHandleAndMutex( mutex );
    QByteArray sql = QByteArray( "SELECT CreateSpatialIndex(" + quotedIdentifier( layerName ) + ","
                                 + quotedIdentifier( OGR_L_GetGeometryColumn( layer ) ) + ") " ); // quote the layer name so spaces are handled
    mOgrOrigLayer->ExecuteSQLNoReturn( sql );
    return true;
  }
  return false;
}

QString QgsOgrProvider::createIndexName( QString tableName, QString field )
{
  QRegularExpression safeExp( QStringLiteral( "[^a-zA-Z0-9]" ) );
  tableName.replace( safeExp, QStringLiteral( "_" ) );
  field.replace( safeExp, QStringLiteral( "_" ) );
  return tableName + "_" + field + "_idx";
}

bool QgsOgrProvider::createAttributeIndex( int field )
{
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  if ( field < 0 || field >= mAttributeFields.count() )
    return false;

  if ( !doInitialActionsForEdition() )
    return false;

  QByteArray quotedLayerName = quotedIdentifier( mOgrOrigLayer->name() );
  if ( mGDALDriverName == QLatin1String( "GPKG" ) ||
       mGDALDriverName == QLatin1String( "SQLite" ) )
  {
    if ( field == 0 && mFirstFieldIsFid )
    {
      // already an index on this field, no need to re-created
      return false;
    }

    QString indexName = createIndexName( mOgrOrigLayer->name(), fields().at( field ).name() );
    QByteArray createSql = "CREATE INDEX IF NOT EXISTS " + textEncoding()->fromUnicode( indexName ) + " ON " + quotedLayerName + " (" + textEncoding()->fromUnicode( fields().at( field ).name() ) + ")";
    mOgrOrigLayer->ExecuteSQLNoReturn( createSql );
    return true;
  }
  else
  {
    QByteArray dropSql = "DROP INDEX ON " + quotedLayerName;
    mOgrOrigLayer->ExecuteSQLNoReturn( dropSql );
    QByteArray createSql = "CREATE INDEX ON " + quotedLayerName + " USING " + textEncoding()->fromUnicode( fields().at( field ).name() );
    mOgrOrigLayer->ExecuteSQLNoReturn( createSql );

    QFileInfo fi( mFilePath );     // to get the base name
    //find out, if the .idm/.ind file is there
    QString idmFile( fi.path().append( '/' ).append( fi.completeBaseName() ).append( ".idm" ) );
    QString indFile( fi.path().append( '/' ).append( fi.completeBaseName() ).append( ".ind" ) );
    return QFile::exists( idmFile ) || QFile::exists( indFile );
  }
}

bool QgsOgrProvider::deleteFeatures( const QgsFeatureIds &id )
{
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  if ( !doInitialActionsForEdition() )
    return false;

  const bool inTransaction = startTransaction();

  bool returnvalue = true;
  for ( QgsFeatureIds::const_iterator it = id.begin(); it != id.end(); ++it )
  {
    if ( !deleteFeature( *it ) )
    {
      returnvalue = false;
    }
  }

  if ( inTransaction )
  {
    if ( returnvalue )
      returnvalue = commitTransaction();
    else
      rollbackTransaction();
  }

  if ( mTransaction )
    mTransaction->dirtyLastSavePoint();

  if ( !syncToDisc() )
  {
    returnvalue = false;
  }

  if ( mGDALDriverName == QLatin1String( "ESRI Shapefile" ) )
  {
    // Shapefile behaves in a special way due to possible recompaction
    recalculateFeatureCount();
  }
  else
  {
    if ( mFeaturesCounted != QgsVectorDataProvider::Uncounted &&
         mFeaturesCounted != QgsVectorDataProvider::UnknownCount )
    {
      if ( returnvalue )
        mFeaturesCounted -= id.size();
      else
        recalculateFeatureCount();
    }
  }

  clearMinMaxCache();

  invalidateCachedExtent( true );

  return returnvalue;
}

bool QgsOgrProvider::deleteFeature( QgsFeatureId id )
{
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  if ( !doInitialActionsForEdition() )
    return false;

  if ( mOgrLayer->DeleteFeature( FID_TO_NUMBER( id ) ) != OGRERR_NONE )
  {
    pushError( tr( "OGR error deleting feature %1: %2" ).arg( id ).arg( CPLGetLastErrorMsg() ) );
    return false;
  }

  if ( mTransaction )
    mTransaction->dirtyLastSavePoint();

  mShapefileMayBeCorrupted = true;

  return true;
}

bool QgsOgrProvider::doInitialActionsForEdition()
{
  if ( !mValid )
    return false;

  // If mUpdateModeStackDepth > 0, it means that an updateMode is already active and that we have write access
  if ( mUpdateModeStackDepth == 0 )
  {
    QgsDebugMsgLevel( QStringLiteral( "Enter update mode implicitly" ), 1 );
    if ( !_enterUpdateMode( true ) )
      return false;
  }

  return true;
}

QgsVectorDataProvider::Capabilities QgsOgrProvider::capabilities() const
{
  return mCapabilities;
}

void QgsOgrProvider::computeCapabilities()
{
  QgsVectorDataProvider::Capabilities ability = QgsVectorDataProvider::Capabilities();
  bool updateModeActivated = false;

  // collect abilities reported by OGR
  if ( mOgrLayer )
  {

    // We want the layer in rw mode or capabilities will be wrong
    // If mUpdateModeStackDepth > 0, it means that an updateMode is already active and that we have write access
    if ( mUpdateModeStackDepth == 0 )
    {
      updateModeActivated = _enterUpdateMode( true );
    }

    // Whilst the OGR documentation (e.g. at
    // https://gdal.org/doxygen/classOGRLayer.html#aeedbda1a62f9b89b8e5f24332cf22286) states "The capability
    // codes that can be tested are represented as strings, but #defined
    // constants exists to ensure correct spelling", we always use strings
    // here.  This is because older versions of OGR don't always have all
    // the #defines we want to test for here.

    if ( mOgrLayer->TestCapability( "RandomRead" ) )
      // true if the GetFeature() method works *efficiently* for this layer.
      // TODO: Perhaps influence if QGIS caches into memory
      //       (vs read from disk every time) based on this setting.
    {
      // the latter flag is here just for compatibility
      ability |= QgsVectorDataProvider::SelectAtId;
    }

    if ( mWriteAccessPossible && mOgrLayer->TestCapability( "SequentialWrite" ) )
      // true if the CreateFeature() method works for this layer.
    {
      ability |= QgsVectorDataProvider::AddFeatures;
    }

    if ( mWriteAccessPossible && mOgrLayer->TestCapability( "DeleteFeature" ) )
      // true if this layer can delete its features
    {
      ability |= DeleteFeatures;
    }

    if ( mWriteAccessPossible && mOgrLayer->TestCapability( "RandomWrite" ) )
      // true if the SetFeature() method is operational on this layer.
    {
      // TODO According to http://shapelib.maptools.org/ (Shapefile C Library V1.2)
      // TODO "You can't modify the vertices of existing structures".
      // TODO Need to work out versions of shapelib vs versions of GDAL/OGR
      // TODO And test appropriately.

      ability |= ChangeAttributeValues;
      ability |= ChangeGeometries;
    }

#if 0
    if ( mOgrLayer->TestCapability( "FastSpatialFilter" ) )
      // true if this layer implements spatial filtering efficiently.
      // Layers that effectively read all features, and test them with the
      // OGRFeature intersection methods should return false.
      // This can be used as a clue by the application whether it should build
      // and maintain it's own spatial index for features in this layer.
    {
      // TODO: Perhaps use as a clue by QGIS whether it should build and maintain it's own spatial index for features in this layer.
    }

    if ( mOgrLayer->TestCapability( "FastFeatureCount" ) )
      // true if this layer can return a feature count
      // (via OGRLayer::GetFeatureCount()) efficiently ... ie. without counting
      // the features. In some cases this will return true until a spatial
      // filter is installed after which it will return false.
    {
      // TODO: Perhaps use as a clue by QGIS whether it should spawn a thread to count features.
    }

    if ( mOgrLayer->TestCapability( "FastGetExtent" ) )
      // true if this layer can return its data extent
      // (via OGRLayer::GetExtent()) efficiently ... ie. without scanning
      // all the features. In some cases this will return true until a
      // spatial filter is installed after which it will return false.
    {
      // TODO: Perhaps use as a clue by QGIS whether it should spawn a thread to calculate extent.
    }

    if ( mOgrLayer->TestCapability( "FastSetNextByIndex" ) )
      // true if this layer can perform the SetNextByIndex() call efficiently.
    {
      // No use required for this QGIS release.
    }
#endif

    if ( mWriteAccessPossible && mOgrLayer->TestCapability( "CreateField" ) )
    {
      ability |= AddAttributes;
    }

    if ( mWriteAccessPossible && mOgrLayer->TestCapability( "DeleteField" ) )
    {
      ability |= DeleteAttributes;
    }

    if ( mWriteAccessPossible && mOgrLayer->TestCapability( "AlterFieldDefn" ) )
    {
      ability |= RenameAttributes;
    }

    if ( !mOgrLayer->TestCapability( OLCStringsAsUTF8 ) )
    {
      ability |= SelectEncoding;
    }

    // OGR doesn't handle shapefiles without attributes, ie. missing DBFs well, fixes #803
    if ( mGDALDriverName == QLatin1String( "ESRI Shapefile" ) )
    {
      ability |= CreateSpatialIndex;
      ability |= CreateAttributeIndex;

      if ( mAttributeFields.size() == 0 )
      {
        QgsMessageLog::logMessage( tr( "Shapefiles without attribute are considered read-only." ), tr( "OGR" ) );
        ability &= ~( AddFeatures | DeleteFeatures | ChangeAttributeValues | AddAttributes | DeleteAttributes );
      }

      if ( ( ability & ChangeAttributeValues ) == 0 )
      {
        // on readonly shapes OGR reports that it can delete features although it can't RandomWrite
        ability &= ~( AddAttributes | DeleteFeatures );
      }
    }
    else if ( mGDALDriverName == QLatin1String( "GPKG" ) ||
              mGDALDriverName == QLatin1String( "SQLite" ) )
    {
      ability |= CreateSpatialIndex;
      ability |= CreateAttributeIndex;
    }

    /* Curve geometries are available in some drivers starting with GDAL 2.0 */
    if ( mOgrLayer->TestCapability( "CurveGeometries" ) )
    {
      ability |= CircularGeometries;
    }

    if ( mGDALDriverName == QLatin1String( "GPKG" ) )
    {
      //supports transactions
      ability |= TransactionSupport;
    }
  }

  ability |= ReadLayerMetadata;

  if ( updateModeActivated )
    leaveUpdateMode();

  mCapabilities = ability;
}


QString QgsOgrProvider::name() const
{
  return TEXT_PROVIDER_KEY;
}

QString QgsOgrProvider::providerKey()
{
  return TEXT_PROVIDER_KEY;
}

QString  QgsOgrProvider::description() const
{
  return TEXT_PROVIDER_DESCRIPTION;
} //  QgsOgrProvider::description()


/**

  Convenience function for readily creating file filters.

  Given a long name for a file filter and a regular expression, return
  a file filter string suitable for use in a QFileDialog::OpenFiles()
  call.  The regular express, glob, will have both all lower and upper
  case versions added.

  \note

  Copied from qgisapp.cpp.

  \todo XXX This should probably be generalized and moved to a standard
            utility type thingy.

*/
static QString createFileFilter_( QString const &longName, QString const &glob )
{
  // return longName + " [OGR] (" + glob.toLower() + ' ' + glob.toUpper() + ");;";
  return longName + " (" + glob.toLower() + ' ' + glob.toUpper() + ");;";
} // createFileFilter_


QString createFilters( const QString &type )
{
  //! Database drivers available
  static QString sDatabaseDrivers;
  //! Protocol drivers available
  static QString sProtocolDrivers;
  //! File filters
  static QString sFileFilters;
  //! Directory drivers
  static QString sDirectoryDrivers;
  //! Extensions
  static QStringList sExtensions;
  //! Directory extensions
  static QStringList sDirectoryExtensions;
  //! Wildcards
  static QStringList sWildcards;

  // if we've already built the supported vector string, just return what
  // we've already built

  if ( sFileFilters.isEmpty() || sFileFilters.isNull() )
  {
    // register ogr plugins
    QgsApplication::registerOgrDrivers();

    // first get the GDAL driver manager
    GDALDriverH driver;          // current driver
    QString driverName;           // current driver name

    // Grind through all the drivers and their respective metadata.
    // We'll add a file filter for those drivers that have a file
    // extension defined for them; the others, welll, even though
    // theoreticaly we can open those files because there exists a
    // driver for them, the user will have to use the "All Files" to
    // open datasets with no explicitly defined file name extension.
    QgsDebugMsgLevel( QStringLiteral( "Driver count: %1" ).arg( OGRGetDriverCount() ), 3 );

    bool kmlFound = false;
    bool dwgFound = false;
    bool dgnFound = false;

    for ( int i = 0; i < OGRGetDriverCount(); ++i )
    {
      driver = OGRGetDriver( i );

      Q_CHECK_PTR( driver ); // NOLINT

      if ( !driver )
      {
        QgsMessageLog::logMessage( QObject::tr( "Unable to get driver %1" ).arg( i ), QObject::tr( "OGR" ) );
        continue;
      }

      driverName = GDALGetDriverShortName( driver );

      if ( driverName.startsWith( QLatin1String( "AVCBin" ) ) )
      {
        sDirectoryDrivers += QObject::tr( "Arc/Info Binary Coverage" ) + ",AVCBin;";
      }
      else if ( driverName.startsWith( QLatin1String( "AVCE00" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Arc/Info ASCII Coverage" ), QStringLiteral( "*.e00" ) );
        sExtensions << QStringLiteral( "e00" );
      }
      else if ( driverName.startsWith( QLatin1String( "BNA" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Atlas BNA" ), QStringLiteral( "*.bna" ) );
        sExtensions << QStringLiteral( "bna" );
      }
      else if ( driverName.startsWith( QLatin1String( "CSV" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Comma Separated Value" ), QStringLiteral( "*.csv" ) );
        sExtensions << QStringLiteral( "csv" );
      }
      else if ( driverName.startsWith( QObject::tr( "DODS" ) ) )
      {
        sProtocolDrivers += QLatin1String( "DODS/OPeNDAP,DODS;" );
      }
      else if ( driverName.startsWith( QObject::tr( "CouchDB" ) ) )
      {
        sProtocolDrivers += QLatin1String( "CouchDB;" );
      }
      else if ( driverName.startsWith( QLatin1String( "FileGDB" ) ) )
      {
        sDirectoryDrivers += QObject::tr( "ESRI FileGDB" ) + ",FileGDB;";
        if ( !sDirectoryExtensions.contains( QStringLiteral( "gdb" ) ) )
          sDirectoryExtensions << QStringLiteral( "gdb" );
      }
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(3,1,0)
      else if ( driverName.startsWith( QLatin1String( "FlatGeobuf" ) ) )
      {
        sProtocolDrivers += QLatin1String( "FlatGeobuf;" );
        sFileFilters += createFileFilter_( QObject::tr( "FlatGeobuf" ), QStringLiteral( "*.fgb" ) );
        sExtensions << QStringLiteral( "fgb" );
      }
#endif
      else if ( driverName.startsWith( QLatin1String( "PGeo" ) ) )
      {
        sDatabaseDrivers += QObject::tr( "ESRI Personal GeoDatabase" ) + ",PGeo;";
#ifdef Q_OS_WIN
        sFileFilters += createFileFilter_( QObject::tr( "ESRI Personal GeoDatabase" ), "*.mdb" );
        sExtensions << "mdb";
#endif
      }
      else if ( driverName.startsWith( QLatin1String( "SDE" ) ) )
      {
        sDatabaseDrivers += QObject::tr( "ESRI ArcSDE" ) + ",SDE;";
      }
      else if ( driverName.startsWith( QLatin1String( "ESRI Shapefile" ) ) )
      {
        QString exts = GDALGetMetadataItem( driver, GDAL_DMD_EXTENSIONS, "" );
        sFileFilters += createFileFilter_( QObject::tr( "ESRI Shapefiles" ), exts.contains( "shz" ) ? QStringLiteral( "*.shp *.shz *.shp.zip" ) : QStringLiteral( "*.shp" ) );
        sExtensions << QStringLiteral( "shp" ) << QStringLiteral( "dbf" );
        if ( exts.contains( "shz" ) )
          sExtensions << QStringLiteral( "shz" ) << QStringLiteral( "shp.zip" );
      }
      else if ( driverName.startsWith( QObject::tr( "FMEObjects Gateway" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "FMEObjects Gateway" ), QStringLiteral( "*.fdd" ) );
        sExtensions << QStringLiteral( "fdd" );
      }
      else if ( driverName.startsWith( QLatin1String( "GeoJSONSeq" ) ) )
      {
        sProtocolDrivers += QLatin1String( "GeoJSON - Newline Delimited;" );
        sFileFilters += createFileFilter_( QObject::tr( "GeoJSON Newline Delimited JSON" ), QStringLiteral( "*.geojsonl *.geojsons *.nlgeojson *.json" ) );
        sExtensions << QStringLiteral( "geojsonl" ) << QStringLiteral( "geojsons" ) << QStringLiteral( "nlgeojson" ) << QStringLiteral( "json" );
      }
      else if ( driverName.startsWith( QLatin1String( "GeoJSON" ) ) )
      {
        sProtocolDrivers += QLatin1String( "GeoJSON;" );
        sFileFilters += createFileFilter_( QObject::tr( "GeoJSON" ), QStringLiteral( "*.geojson" ) );
        sExtensions << QStringLiteral( "geojson" );
      }
      else if ( driverName.startsWith( QLatin1String( "GeoRSS" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "GeoRSS" ), QStringLiteral( "*.xml" ) );
        sExtensions << QStringLiteral( "xml" );
      }
      else if ( driverName == QLatin1String( "GML" ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Geography Markup Language [GML]" ), QStringLiteral( "*.gml" ) );
        sExtensions << QStringLiteral( "gml" );
      }
      else if ( driverName == QLatin1String( "GMLAS" ) )
      {
        continue;
      }
      else if ( driverName.startsWith( QLatin1String( "GMT" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Generic Mapping Tools [GMT]" ), QStringLiteral( "*.gmt" ) );
        sExtensions << QStringLiteral( "gmt" );
      }
      else if ( driverName.startsWith( QLatin1String( "GPX" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "GPS eXchange Format [GPX]" ), QStringLiteral( "*.gpx" ) );
        sExtensions << QStringLiteral( "gpx" );
      }
      else if ( driverName.startsWith( QLatin1String( "GPKG" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "GeoPackage" ), QStringLiteral( "*.gpkg" ) );
        sExtensions << QStringLiteral( "gpkg" );
      }
      else if ( driverName.startsWith( QLatin1String( "GRASS" ) ) )
      {
        sDirectoryDrivers += QObject::tr( "Grass Vector" ) + ",GRASS;";
      }
      else if ( driverName.startsWith( QLatin1String( "IDB" ) ) )
      {
        sDatabaseDrivers += QObject::tr( "Informix DataBlade" ) + ",IDB;";
      }
      else if ( driverName.startsWith( QLatin1String( "Interlis 1" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "INTERLIS 1" ), QStringLiteral( "*.itf *.xml *.ili" ) );
        sExtensions << QStringLiteral( "itf" ) << QStringLiteral( "xml" ) << QStringLiteral( "ili" );
      }
      else if ( driverName.startsWith( QLatin1String( "Interlis 2" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "INTERLIS 2" ), QStringLiteral( "*.xtf *.xml *.ili" ) );
        sExtensions << QStringLiteral( "xtf" ) << QStringLiteral( "xml" ) << QStringLiteral( "ili" );
      }
      else if ( driverName.startsWith( QLatin1String( "Ingres" ) ) )
      {
        sDatabaseDrivers += QObject::tr( "Ingres" ) + ",Ingres;";
      }
      else if ( driverName == QLatin1String( "KML" ) || driverName == QLatin1String( "LIBKML" ) )
      {
        if ( kmlFound )
          continue;
        kmlFound = true;
        sFileFilters += createFileFilter_( QObject::tr( "Keyhole Markup Language [KML]" ), QStringLiteral( "*.kml *.kmz" ) );
        sExtensions << QStringLiteral( "kml" ) << QStringLiteral( "kmz" );
      }
      else if ( driverName.startsWith( QLatin1String( "MapInfo File" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Mapinfo File" ), QStringLiteral( "*.mif *.tab" ) );
        sExtensions << QStringLiteral( "mif" ) << QStringLiteral( "tab" );
      }
      else if ( driverName == QLatin1String( "DGN" ) || driverName == QLatin1String( "DGNV8" ) )
      {
        if ( dgnFound )
          continue;
        dgnFound = true;
        sFileFilters += createFileFilter_( QObject::tr( "Microstation DGN" ), QStringLiteral( "*.dgn" ) );
        sExtensions << QStringLiteral( "dgn" );
      }
      else if ( driverName.startsWith( QLatin1String( "MySQL" ) ) )
      {
        sDatabaseDrivers += QObject::tr( "MySQL" ) + ",MySQL;";
      }
      else if ( driverName.startsWith( QLatin1String( "MSSQL" ) ) )
      {
        sDatabaseDrivers += QObject::tr( "MSSQL" ) + ",MSSQL;";
      }
      else if ( driverName.startsWith( QLatin1String( "OCI" ) ) )
      {
        sDatabaseDrivers += QObject::tr( "Oracle Spatial" ) + ",OCI;";
      }
      else if ( driverName.startsWith( QLatin1String( "ODBC" ) ) )
      {
        sDatabaseDrivers += QObject::tr( "ODBC" ) + ",ODBC;";
      }
      else if ( driverName.startsWith( QLatin1String( "OGDI" ) ) )
      {
        sDatabaseDrivers += QObject::tr( "OGDI Vectors" ) + ",OGDI;";
      }
      else if ( driverName.startsWith( QLatin1String( "OpenFileGDB" ) ) )
      {
        sDirectoryDrivers += QObject::tr( "OpenFileGDB" ) + ",OpenFileGDB;";
        if ( !sDirectoryExtensions.contains( QStringLiteral( "gdb" ) ) )
          sDirectoryExtensions << QStringLiteral( "gdb" );
      }
      else if ( driverName.startsWith( QLatin1String( "PostgreSQL" ) ) )
      {
        sDatabaseDrivers += QObject::tr( "PostgreSQL" ) + ",PostgreSQL;";
      }
      else if ( driverName.startsWith( QLatin1String( "S57" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "S-57 Base file" ),
                                           QStringLiteral( "*.000" ) );
        sExtensions << QStringLiteral( "000" );
      }
      else if ( driverName.startsWith( QLatin1String( "SDTS" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Spatial Data Transfer Standard [SDTS]" ),
                                           QStringLiteral( "*catd.ddf" ) );
        sWildcards << QStringLiteral( "*catd.ddf" );
      }
      else if ( driverName.startsWith( QLatin1String( "SOSI" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Systematic Organization of Spatial Information [SOSI]" ), QStringLiteral( "*.sos" ) );
        sExtensions << QStringLiteral( "sos" );
      }
      else if ( driverName.startsWith( QLatin1String( "SQLite" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "SQLite/SpatiaLite" ), QStringLiteral( "*.sqlite *.db *.sqlite3 *.db3 *.s3db *.sl3" ) );
        sExtensions << QStringLiteral( "sqlite" ) << QStringLiteral( "db" ) << QStringLiteral( "sqlite3" ) << QStringLiteral( "db3" ) << QStringLiteral( "s3db" ) << QStringLiteral( "sl3" );
      }
      else if ( driverName.startsWith( QLatin1String( "SXF" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Storage and eXchange Format" ), QStringLiteral( "*.sxf" ) );
        sExtensions << QStringLiteral( "sxf" );
      }
      else if ( driverName.startsWith( QLatin1String( "UK .NTF" ) ) )
      {
        sDirectoryDrivers += QObject::tr( "UK. NTF2" ) + ",UK. NTF;";
      }
      else if ( driverName.startsWith( QLatin1String( "TIGER" ) ) )
      {
        sDirectoryDrivers += QObject::tr( "U.S. Census TIGER/Line" ) + ",TIGER;";
      }
      else if ( driverName.startsWith( QLatin1String( "OGR_VRT" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "VRT - Virtual Datasource" ),
                                           QStringLiteral( "*.vrt *.ovf" ) );
        sExtensions << QStringLiteral( "vrt" ) << QStringLiteral( "ovf" );
      }
      else if ( driverName.startsWith( QLatin1String( "XPlane" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "X-Plane/Flightgear" ),
                                           QStringLiteral( "apt.dat nav.dat fix.dat awy.dat" ) );
        sWildcards << QStringLiteral( "apt.dat" ) << QStringLiteral( "nav.dat" ) << QStringLiteral( "fix.dat" ) << QStringLiteral( "awy.dat" );
      }
      else if ( driverName.startsWith( QLatin1String( "Geoconcept" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Geoconcept" ), QStringLiteral( "*.gxt *.txt" ) );
        sExtensions << QStringLiteral( "gxt" ) << QStringLiteral( "txt" );
      }
      else if ( driverName.startsWith( QLatin1String( "DXF" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "AutoCAD DXF" ), QStringLiteral( "*.dxf" ) );
        sExtensions << QStringLiteral( "dxf" );
      }
      else if ( driverName.startsWith( QLatin1String( "ODS" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Open Document Spreadsheet" ), QStringLiteral( "*.ods" ) );
        sExtensions << QStringLiteral( "ods" );
      }
      else if ( driverName.startsWith( QLatin1String( "XLSX" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "MS Office Open XML spreadsheet" ), QStringLiteral( "*.xlsx" ) );
        sExtensions << QStringLiteral( "xlsx" );
      }
      else if ( driverName.endsWith( QLatin1String( "XLS" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "MS Excel format" ), QStringLiteral( "*.xls" ) );
        sExtensions << QStringLiteral( "xls" );
      }
      else if ( driverName.startsWith( QLatin1String( "EDIGEO" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "EDIGEO" ), QStringLiteral( "*.thf" ) );
        sExtensions << QStringLiteral( "thf" );
      }
      else if ( driverName.startsWith( QLatin1String( "NAS" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "NAS - ALKIS" ), QStringLiteral( "*.xml" ) );
        sExtensions << QStringLiteral( "xml" );
      }
      else if ( driverName.startsWith( QLatin1String( "WAsP" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "WAsP" ), QStringLiteral( "*.map" ) );
        sExtensions << QStringLiteral( "map" );
      }
      else if ( driverName.startsWith( QLatin1String( "PCIDSK" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "PCI Geomatics Database File" ), QStringLiteral( "*.pix" ) );
        sExtensions << QStringLiteral( "pix" );
      }
      else if ( driverName.startsWith( QLatin1String( "GPSTrackMaker" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "GPSTrackMaker" ), QStringLiteral( "*.gtm *.gtz" ) );
        sExtensions << QStringLiteral( "gtm" ) << QStringLiteral( "gtz" );
      }
      else if ( driverName.startsWith( QLatin1String( "VFK" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Czech Cadastral Exchange Data Format" ), QStringLiteral( "*.vfk" ) );
        sExtensions << QStringLiteral( "vfk" );
      }
      else if ( driverName.startsWith( QLatin1String( "OSM" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "OpenStreetMap" ), QStringLiteral( "*.osm *.pbf" ) );
        sExtensions << QStringLiteral( "osm" ) << QStringLiteral( "pbf" );
      }
      else if ( driverName.startsWith( QLatin1String( "SUA" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Special Use Airspace Format" ), QStringLiteral( "*.sua" ) );
        sExtensions << QStringLiteral( "sua" );
      }
      else if ( driverName.startsWith( QLatin1String( "OpenAir" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "OpenAir Special Use Airspace Format" ), QStringLiteral( "*.txt" ) );
        sExtensions << QStringLiteral( "txt" );
      }
      else if ( driverName.startsWith( QLatin1String( "PDS" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Planetary Data Systems TABLE" ), QStringLiteral( "*.xml" ) );
        sExtensions << QStringLiteral( "xml" );
      }
      else if ( driverName.startsWith( QLatin1String( "HTF" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Hydrographic Transfer Format" ), QStringLiteral( "*.htf" ) );
        sExtensions << QStringLiteral( "htf" );
      }
      else if ( driverName.startsWith( QLatin1String( "SVG" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Scalable Vector Graphics" ), QStringLiteral( "*.svg" ) );
        sExtensions << QStringLiteral( "svg" );
      }
      else if ( driverName.startsWith( QLatin1String( "ARCGEN" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Arc/Info Generate" ), QStringLiteral( "*.gen" ) );
        sExtensions << QStringLiteral( "gen" );
      }
      else if ( driverName.startsWith( QLatin1String( "PDF" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "Geospatial PDF" ), QStringLiteral( "*.pdf" ) );
        sExtensions << QStringLiteral( "pdf" );
      }
      else if ( driverName.startsWith( QLatin1String( "SEGY" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "SEG-Y" ), QStringLiteral( "*.sgy *.segy" ) );
        sExtensions << QStringLiteral( "sgy" ) << QStringLiteral( "segy" );
      }
      else if ( driverName.startsWith( QLatin1String( "SEGUKOOA" ) ) )
      {
        sFileFilters += createFileFilter_( QObject::tr( "SEG-P1" ), QStringLiteral( "*.seg *.seg1 *.sp1" ) );
        sFileFilters += createFileFilter_( QObject::tr( "UKOOA P1/90" ), QStringLiteral( "*.uko *.ukooa" ) );
        sExtensions << QStringLiteral( "seg" ) << QStringLiteral( "seg1" ) << QStringLiteral( "sp1" ) << QStringLiteral( "uko" ) << QStringLiteral( "ukooa" );
      }
      else
      {
        if ( driverName == QLatin1String( "CAD" ) || driverName == QLatin1String( "DWG" ) )
        {
          if ( dwgFound )
            continue;
          dwgFound = true;
        }

        QString myGdalDriverExtensions = GDALGetMetadataItem( driver, GDAL_DMD_EXTENSIONS, "" );
        QString myGdalDriverLongName = GDALGetMetadataItem( driver, GDAL_DMD_LONGNAME, "" );
        if ( !( myGdalDriverExtensions.isEmpty() || myGdalDriverLongName.isEmpty() ) )
        {
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
          const QStringList splitExtensions = myGdalDriverExtensions.split( ' ', QString::SkipEmptyParts );
#else
          const QStringList splitExtensions = myGdalDriverExtensions.split( ' ', Qt::SkipEmptyParts );
#endif
          QString glob;

          for ( const QString &ext : splitExtensions )
          {
            sExtensions << ext;
            if ( !glob.isEmpty() )
              glob += QLatin1Char( ' ' );
            glob += "*." + ext;
          }

          if ( driverName == QLatin1String( "JPEG2000" ) ||
               driverName.startsWith( QLatin1String( "JP2" ) ) )
          {
            // Skip over JPEG2000 drivers, as their vector capabilities are just
            // a marginal use case
            continue;
          }

          sFileFilters += createFileFilter_( myGdalDriverLongName, glob );

        }
        else
        {
          // NOP, we don't know anything about the current driver
          // with regards to a proper file filter string
          QgsDebugMsgLevel( QStringLiteral( "Unknown driver %1 for file filters." ).arg( driverName ), 2 );
        }
      }

    }                          // each loaded OGR driver

    // sort file filters alphabetically
    QgsDebugMsgLevel( "myFileFilters: " + sFileFilters, 2 );
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    QStringList filters = sFileFilters.split( QStringLiteral( ";;" ), QString::SkipEmptyParts );
#else
    QStringList filters = sFileFilters.split( QStringLiteral( ";;" ), Qt::SkipEmptyParts );
#endif
    filters.sort();
    sFileFilters = filters.join( QLatin1String( ";;" ) ) + ";;";
    QgsDebugMsgLevel( "myFileFilters: " + sFileFilters, 2 );

    // VSIFileHandler (.zip and .gz files) - second
    //   see http://trac.osgeo.org/gdal/wiki/UserDocs/ReadInZip
    // Requires GDAL>=1.6.0 with libz support, let's assume we have it.
    // This does not work for some file types, see VSIFileHandler doc.
    QgsSettings settings;
    if ( settings.value( QStringLiteral( "qgis/scanZipInBrowser2" ), "basic" ).toString() != QLatin1String( "no" ) )
    {
      sFileFilters.prepend( createFileFilter_( QObject::tr( "GDAL/OGR VSIFileHandler" ), QStringLiteral( "*.zip *.gz *.tar *.tar.gz *.tgz" ) ) );
      sExtensions << QStringLiteral( "zip" ) << QStringLiteral( "gz" ) << QStringLiteral( "tar" ) << QStringLiteral( "tar.gz" ) << QStringLiteral( "tgz" );
    }

    // can't forget the all supported case
    QStringList exts;
    for ( const QString &ext : qgis::as_const( sExtensions ) )
      exts << QStringLiteral( "*.%1 *.%2" ).arg( ext, ext.toUpper() );
    sFileFilters.prepend( QObject::tr( "All supported files" ) + QStringLiteral( " (%1);;" ).arg( exts.join( QLatin1Char( ' ' ) ) ) );

    // can't forget the default case - first
    sFileFilters.prepend( QObject::tr( "All files" ) + " (*);;" );


    // cleanup
    if ( sFileFilters.endsWith( QLatin1String( ";;" ) ) ) sFileFilters.chop( 2 );

    QgsDebugMsgLevel( "myFileFilters: " + sFileFilters, 2 );
  }

  if ( type == QLatin1String( "file" ) )
  {
    return sFileFilters;
  }
  if ( type == QLatin1String( "database" ) )
  {
    return sDatabaseDrivers;
  }
  if ( type == QLatin1String( "protocol" ) )
  {
    return sProtocolDrivers;
  }
  if ( type == QLatin1String( "directory" ) )
  {
    return sDirectoryDrivers;
  }
  if ( type == QLatin1String( "extensions" ) )
  {
    return sExtensions.join( QLatin1Char( '|' ) );
  }
  if ( type == QLatin1String( "directory_extensions" ) )
  {
    return sDirectoryExtensions.join( QLatin1Char( '|' ) );
  }
  if ( type == QLatin1String( "wildcards" ) )
  {
    return sWildcards.join( QLatin1Char( '|' ) );
  }
  else
  {
    return QString();
  }
}

QVariantMap QgsOgrProviderMetadata::decodeUri( const QString &uri ) const
{
  QString path = uri;
  QString layerName;
  QString subset;
  QString geometryType;
  QStringList openOptions;
  QString databaseName;

  int layerId = -1;

  if ( path.contains( '|' ) )
  {
    const QRegularExpression geometryTypeRegex( QStringLiteral( "\\|geometrytype=([a-zA-Z0-9]*)" ), QRegularExpression::PatternOption::CaseInsensitiveOption );
    const QRegularExpression layerNameRegex( QStringLiteral( "\\|layername=([^|]*)" ), QRegularExpression::PatternOption::CaseInsensitiveOption );
    const QRegularExpression layerIdRegex( QStringLiteral( "\\|layerid=([^|]*)" ), QRegularExpression::PatternOption::CaseInsensitiveOption );
    const QRegularExpression subsetRegex( QStringLiteral( "\\|subset=((?:.*[\r\n]*)*)\\Z" ) );
    const QRegularExpression openOptionRegex( QStringLiteral( "\\|option:([^|]*)" ) );


    // we first try to split off the geometry type component, if that's present. That's a known quantity which
    // will never be more than a-z characters
    QRegularExpressionMatch match = geometryTypeRegex.match( path );
    if ( match.hasMatch() )
    {
      geometryType = match.captured( 1 );
      path = path.remove( match.capturedStart( 0 ), match.capturedLength( 0 ) );
    }

    // next, we try to find and strip out the layerid/layername component. (This logic is based on the assumption
    // that a layer name doesn't contain a | character!)
    // we prefer layer names over layer ids, since they are persistent..
    match = layerNameRegex.match( path );
    if ( match.hasMatch() )
    {
      layerName = match.captured( 1 );
      path = path.remove( match.capturedStart( 0 ), match.capturedLength( 0 ) );
    }

    match = layerIdRegex.match( path );
    if ( match.hasMatch() )
    {
      layerId = match.captured( 1 ).toInt();
      path = path.remove( match.capturedStart( 0 ), match.capturedLength( 0 ) );
    }

    while ( true )
    {
      match = openOptionRegex.match( path );
      if ( match.hasMatch() )
      {
        openOptions << match.captured( 1 );
        path = path.remove( match.capturedStart( 0 ), match.capturedLength( 0 ) );
      }
      else
      {
        break;
      }
    }

    // lastly, try to parse out the subset component. This is the biggest unknown, because it's
    // quite possible that a subset string will contain a | character. If we've already parsed
    // out all the other known |xxx=yyy tags, then we can safely assume that everything from "|subset=" to the
    // end of the path is part of the subset filter
    match = subsetRegex.match( path );
    if ( match.hasMatch() )
    {
      subset = match.captured( 1 );
      path = path.remove( match.capturedStart( 0 ), match.capturedLength( 0 ) );
    }
  }

  // Handles DB connections extracting database name if possible
  // Example: MySQL:database_name,host=localhost,port=3306 authcfg='f8wwfx8'
  if ( uri.startsWith( QStringLiteral( "MySQL" ), Qt::CaseSensitivity::CaseInsensitive ) ||
       uri.startsWith( QStringLiteral( "PostgreSQL" ), Qt::CaseSensitivity::CaseInsensitive ) ||
       uri.startsWith( QStringLiteral( "MSSQL" ), Qt::CaseSensitivity::CaseInsensitive ) ||
       uri.startsWith( QStringLiteral( "ODBC" ), Qt::CaseSensitivity::CaseInsensitive ) ||
       uri.startsWith( QStringLiteral( "PGeo" ), Qt::CaseSensitivity::CaseInsensitive ) ||
       uri.startsWith( QStringLiteral( "SDE" ), Qt::CaseSensitivity::CaseInsensitive ) ||
       uri.startsWith( QStringLiteral( "OGDI" ), Qt::CaseSensitivity::CaseInsensitive ) ||
       uri.startsWith( QStringLiteral( "Ingres" ), Qt::CaseSensitivity::CaseInsensitive ) ||
       uri.startsWith( QStringLiteral( "IDB" ), Qt::CaseSensitivity::CaseInsensitive ) ||
       uri.startsWith( QStringLiteral( "OCI" ), Qt::CaseSensitivity::CaseInsensitive ) )
  {
    auto parts( path.split( ':' ) );
    if ( parts.count( ) > 1 )
    {
      auto dataParts( parts.at( 1 ).split( ',' ) );
      if ( dataParts.count() > 0 )
        databaseName = dataParts.at( 0 );
    }
  }

  QVariantMap uriComponents;
  uriComponents.insert( QStringLiteral( "path" ), path );
  uriComponents.insert( QStringLiteral( "layerName" ), layerName );
  uriComponents.insert( QStringLiteral( "layerId" ), layerId > -1 && layerName.isEmpty() ? layerId : QVariant() ) ;
  if ( !subset.isEmpty() )
    uriComponents.insert( QStringLiteral( "subset" ), subset );
  if ( !geometryType.isEmpty() )
    uriComponents.insert( QStringLiteral( "geometryType" ), geometryType );
  if ( !databaseName.isEmpty() )
    uriComponents.insert( QStringLiteral( "databaseName" ), databaseName );
  if ( !openOptions.isEmpty() )
    uriComponents.insert( QStringLiteral( "openOptions" ), openOptions );
  return uriComponents;
}

QString QgsOgrProviderMetadata::encodeUri( const QVariantMap &parts ) const
{
  const QString path = parts.value( QStringLiteral( "path" ) ).toString();
  const QString layerName = parts.value( QStringLiteral( "layerName" ) ).toString();
  const QString layerId = parts.value( QStringLiteral( "layerId" ) ).toString();
  const QString subset = parts.value( QStringLiteral( "subset" ) ).toString();
  const QString geometryType = parts.value( QStringLiteral( "geometryType" ) ).toString();
  const QStringList openOptions = parts.value( QStringLiteral( "openOptions" ) ).toStringList();
  QString uri = path
                + ( !layerName.isEmpty() ? QStringLiteral( "|layername=%1" ).arg( layerName ) : !layerId.isEmpty() ? QStringLiteral( "|layerid=%1" ).arg( layerId ) : QString() )
                + ( !geometryType.isEmpty() ? QStringLiteral( "|geometrytype=%1" ).arg( geometryType ) : QString() );
  for ( const QString &openOption : openOptions )
  {
    uri += QLatin1String( "|option:" );
    uri += openOption;
  }
  if ( !subset.isEmpty() )
    uri += QStringLiteral( "|subset=%1" ).arg( subset );
  return uri;
}

QString QgsOgrProviderUtils::fileVectorFilters()
{
  return createFilters( QStringLiteral( "file" ) );
}

QString QgsOgrProviderUtils::databaseDrivers()
{
  return createFilters( QStringLiteral( "database" ) );
}

QString QgsOgrProviderUtils::protocolDrivers()
{
  return createFilters( QStringLiteral( "protocol" ) );
}


QString QgsOgrProviderUtils::directoryDrivers()
{
  return  createFilters( QStringLiteral( "directory" ) );
}

QStringList QgsOgrProviderUtils::fileExtensions()
{
  return  createFilters( QStringLiteral( "extensions" ) ).split( '|' );
}

QStringList QgsOgrProviderUtils::directoryExtensions()
{
  return createFilters( QStringLiteral( "directory_extensions" ) ).split( '|' );
}

QStringList QgsOgrProviderUtils::wildcards()
{
  return  createFilters( QStringLiteral( "wildcards" ) ).split( '|' );
}


/**
 * Class factory to return a pointer to a newly created
 * QgsOgrProvider object
 */
QgsOgrProvider *QgsOgrProviderMetadata::createProvider( const QString &uri, const QgsDataProvider::ProviderOptions &options,
    QgsDataProvider::ReadFlags flags )
{
  return new QgsOgrProvider( uri, options, flags );
}

/**
 * Creates an empty data source
\param uri location to store the file(s)
\param format data format (e.g. "ESRI Shapefile")
\param vectortype point/line/polygon or multitypes
\param attributes a list of name/type pairs for the initial attributes
\return true in case of success*/
bool QgsOgrProviderUtils::createEmptyDataSource( const QString &uri,
    const QString &format,
    const QString &encoding,
    QgsWkbTypes::Type vectortype,
    const QList< QPair<QString, QString> > &attributes,
    const QgsCoordinateReferenceSystem &srs,
    QString &errorMessage )
{
  QgsDebugMsgLevel( QStringLiteral( "Creating empty vector layer with format: %1" ).arg( format ), 2 );
  errorMessage.clear();

  QgsApplication::registerOgrDrivers();
  OGRSFDriverH driver = OGRGetDriverByName( format.toLatin1() );
  if ( !driver )
  {
    return false;
  }

  QString driverName = GDALGetDriverShortName( driver );

  if ( driverName == QLatin1String( "ESRI Shapefile" ) )
  {
    if ( !uri.endsWith( QLatin1String( ".shp" ), Qt::CaseInsensitive ) &&
         !uri.endsWith( QLatin1String( ".shz" ), Qt::CaseInsensitive ) &&
         !uri.endsWith( QLatin1String( ".dbf" ), Qt::CaseInsensitive ) )
    {
      errorMessage = QObject::tr( "URI %1 doesn't end with .shp or .dbf" ).arg( uri );
      QgsDebugMsg( errorMessage );
      return false;
    }

    // check for duplicate fieldnames
    QSet<QString> fieldNames;
    QList<QPair<QString, QString> >::const_iterator fldIt;
    for ( fldIt = attributes.begin(); fldIt != attributes.end(); ++fldIt )
    {
      QString name = fldIt->first.left( 10 );
      if ( fieldNames.contains( name ) )
      {
        errorMessage = QObject::tr( "Duplicate field (10 significant characters): %1" ).arg( name );
        QgsMessageLog::logMessage( errorMessage, QObject::tr( "OGR" ) );
        return false;
      }
      fieldNames << name;
    }

    QgsVectorFileWriter::deleteShapeFile( uri );
  }
  else
  {
    QFile::remove( uri );
  }

  gdal::dataset_unique_ptr dataSource;
  dataSource.reset( OGR_Dr_CreateDataSource( driver, uri.toUtf8().constData(), nullptr ) );
  if ( !dataSource )
  {
    errorMessage = QObject::tr( "Creating the data source %1 failed: %2" ).arg( uri, QString::fromUtf8( CPLGetLastErrorMsg() ) );
    QgsMessageLog::logMessage( errorMessage, QObject::tr( "OGR" ) );
    return false;
  }

  //consider spatial reference system
  OGRSpatialReferenceH reference = nullptr;

  QgsCoordinateReferenceSystem mySpatialRefSys;
  if ( srs.isValid() )
  {
    mySpatialRefSys = srs;
  }
  else
  {
    mySpatialRefSys.validate();
  }

  QString myWkt = mySpatialRefSys.toWkt( QgsCoordinateReferenceSystem::WKT_PREFERRED_GDAL );

  if ( !myWkt.isNull()  &&  myWkt.length() != 0 )
  {
    reference = OSRNewSpatialReference( myWkt.toLocal8Bit().data() );
  }

  // Map the qgis geometry type to the OGR geometry type
  OGRwkbGeometryType OGRvectortype = wkbUnknown;
  switch ( vectortype )
  {
    case QgsWkbTypes::GeometryCollection:
    case QgsWkbTypes::GeometryCollectionZ:
    case QgsWkbTypes::GeometryCollectionM:
    case QgsWkbTypes::GeometryCollectionZM:
    case QgsWkbTypes::Unknown:
    {
      errorMessage = QObject::tr( "Unknown vector type of %1" ).arg( static_cast< int >( vectortype ) );
      QgsMessageLog::logMessage( errorMessage, QObject::tr( "OGR" ) );
      return false;
    }

    default:
      OGRvectortype = QgsOgrProviderUtils::ogrTypeFromQgisType( vectortype );
  }

  char **papszOptions = nullptr;
  if ( driverName == QLatin1String( "ESRI Shapefile" ) )
  {
    papszOptions = CSLSetNameValue( papszOptions, "ENCODING", QgsVectorFileWriter::convertCodecNameForEncodingOption( encoding ).toLocal8Bit().data() );
    // OGR Shapefile fails to create fields if given encoding is not supported by its side
    // so disable encoding conversion of OGR Shapefile layer
    CPLSetConfigOption( "SHAPE_ENCODING", "" );
  }

  OGRLayerH layer;
  layer = GDALDatasetCreateLayer( dataSource.get(), QFileInfo( uri ).completeBaseName().toUtf8().constData(), reference, OGRvectortype, papszOptions );
  CSLDestroy( papszOptions );

  if ( !layer )
  {
    errorMessage = QString::fromUtf8( CPLGetLastErrorMsg() );
    QgsMessageLog::logMessage( errorMessage, QObject::tr( "OGR" ) );
    return false;
  }

  //create the attribute fields

  QTextCodec *codec = QTextCodec::codecForName( encoding.toLocal8Bit().data() );
  if ( !codec )
  {
    // fall back to "System" codec
    codec = QTextCodec::codecForLocale();
    Q_ASSERT( codec );
  }

  for ( QList<QPair<QString, QString> >::const_iterator it = attributes.begin(); it != attributes.end(); ++it )
  {
    QStringList fields = it->second.split( ';' );

    if ( fields.isEmpty() )
      continue;

    int width = fields.size() > 1 ? fields[1].toInt() : -1;
    int precision = fields.size() > 2 ? fields[2].toInt() : -1;
    if ( precision > 0 )
      width += 1;

    OGRFieldDefnH field;
    if ( fields[0] == QLatin1String( "Real" ) )
    {
      if ( width < 0 )
        width = 32;
      if ( precision < 0 )
        precision = 3;

      field = OGR_Fld_Create( codec->fromUnicode( it->first ).constData(), OFTReal );
      OGR_Fld_SetWidth( field, width );
      OGR_Fld_SetPrecision( field, precision );
    }
    else if ( fields[0] == QLatin1String( "Integer" ) )
    {
      if ( width < 0 || width > 10 )
        width = 10;

      field = OGR_Fld_Create( codec->fromUnicode( it->first ).constData(), OFTInteger );
      // limit to 10.  otherwise OGR sets it to 11 and recognizes as OFTDouble later
      OGR_Fld_SetWidth( field, width );
    }
    else if ( fields[0] == QLatin1String( "String" ) )
    {
      if ( width < 0 || width > 255 )
        width = 255;

      field = OGR_Fld_Create( codec->fromUnicode( it->first ).constData(), OFTString );
      OGR_Fld_SetWidth( field, width );
    }
    else if ( fields[0] == QLatin1String( "Date" ) )
    {
      field = OGR_Fld_Create( codec->fromUnicode( it->first ).constData(), OFTDate );
    }
    else if ( fields[0] == QLatin1String( "Time" ) )
    {
      field = OGR_Fld_Create( codec->fromUnicode( it->first ).constData(), OFTTime );
    }
    else if ( fields[0] == QLatin1String( "DateTime" ) )
    {
      field = OGR_Fld_Create( codec->fromUnicode( it->first ).constData(), OFTDateTime );
    }
    else
    {
      QgsMessageLog::logMessage( QObject::tr( "field %1 with unsupported type %2 skipped" ).arg( it->first, fields[0] ), QObject::tr( "OGR" ) );
      continue;
    }

    if ( OGR_L_CreateField( layer, field, true ) != OGRERR_NONE )
    {
      QgsMessageLog::logMessage( QObject::tr( "creation of field %1 failed" ).arg( it->first ), QObject::tr( "OGR" ) );
    }
  }

  dataSource.reset();

  if ( driverName == QLatin1String( "ESRI Shapefile" ) )
  {
    int index = uri.indexOf( QLatin1String( ".shp" ), Qt::CaseInsensitive );
    if ( index > 0 )
    {
      QString layerName = uri.left( index );
      QFile prjFile( layerName + ".qpj" );
#if PROJ_VERSION_MAJOR<6
      if ( prjFile.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
      {
        QTextStream prjStream( &prjFile );
        prjStream << myWkt.toLocal8Bit().data() << endl;
        prjFile.close();
      }
      else
      {
        QgsMessageLog::logMessage( QObject::tr( "Couldn't create file %1.qpj" ).arg( layerName ), QObject::tr( "OGR" ) );
      }
#else
      if ( prjFile.exists() )
        prjFile.remove();
#endif
    }
  }

  QgsDebugMsgLevel( QStringLiteral( "GDAL Version number %1" ).arg( GDAL_VERSION_NUM ), 2 );
  if ( reference )
  {
    OSRRelease( reference );
  }
  return true;
}

QList<QgsDataItemProvider *> QgsOgrProviderMetadata::dataItemProviders() const
{
  QList< QgsDataItemProvider * > providers;
  providers << new QgsOgrDataItemProvider;
  providers << new QgsGeoPackageDataItemProvider;
  return providers;
}

QgsCoordinateReferenceSystem QgsOgrProvider::crs() const
{
  QgsCoordinateReferenceSystem srs;
  if ( !mValid || ( mOGRGeomType == wkbNone ) )
    return srs;

#if PROJ_VERSION_MAJOR<6
  if ( mGDALDriverName == QLatin1String( "ESRI Shapefile" ) )
  {
    int index = mFilePath.indexOf( QLatin1String( ".shp" ), Qt::CaseInsensitive );
    if ( index > 0 )
    {
      QString layerName = mFilePath.left( index );
      QFile prjFile( layerName + ".qpj" );
      if ( prjFile.open( QIODevice::ReadOnly ) )
      {
        QTextStream prjStream( &prjFile );
        QString myWktString = prjStream.readLine();
        prjFile.close();

        srs = QgsCoordinateReferenceSystem::fromWkt( myWktString.toUtf8().constData() );
        if ( srs.isValid() )
          return srs;
      }
    }
  }

  // add towgs84 parameter
  Q_NOWARN_DEPRECATED_PUSH
  QgsCoordinateReferenceSystem::setupESRIWktFix();
  Q_NOWARN_DEPRECATED_POP
#endif

  if ( OGRSpatialReferenceH spatialRefSys = mOgrLayer->GetSpatialRef() )
  {
    srs = QgsOgrUtils::OGRSpatialReferenceToCrs( spatialRefSys );
  }
  else
  {
    QgsDebugMsg( QStringLiteral( "no spatial reference found" ) );
  }

  return srs;
}

QSet<QVariant> QgsOgrProvider::uniqueValues( int index, int limit ) const
{
  QSet<QVariant> uniqueValues;

  if ( !mValid || index < 0 || index >= mAttributeFields.count() )
    return uniqueValues;

  const QgsField fld = mAttributeFields.at( index );
  if ( fld.name().isNull() )
  {
    return uniqueValues; //not a provider field
  }

  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  QByteArray sql = "SELECT DISTINCT " + quotedIdentifier( textEncoding()->fromUnicode( fld.name() ) );

  // GPKG/SQLite fid
  // For GPKG and SQLITE drivers PK fields are not exposed as real fields, (and OGR_F_GetFID only
  // works with GPKG), so we are adding an extra column that will become index 0
  // See https://github.com/qgis/QGIS/issues/29129
  if ( ( mGDALDriverName == QLatin1String( "GPKG" ) || mGDALDriverName == QLatin1String( "SQLite" ) )
       && mFirstFieldIsFid && index == 0 )
  {
    sql += ", " + quotedIdentifier( textEncoding()->fromUnicode( fld.name() ) ) + " AS fid2";
  }

  sql += " FROM " + quotedIdentifier( mOgrLayer->name() );

  if ( !mSubsetString.isEmpty() )
  {
    sql += " WHERE " + textEncoding()->fromUnicode( mSubsetString );
  }

  sql += " ORDER BY " + quotedIdentifier( textEncoding()->fromUnicode( fld.name() ) ) + " ASC";

  QgsDebugMsgLevel( QStringLiteral( "SQL: %1" ).arg( textEncoding()->toUnicode( sql ) ), 2 );
  QgsOgrLayerUniquePtr l = mOgrLayer->ExecuteSQL( sql );
  if ( !l )
  {
    QgsDebugMsg( QStringLiteral( "Failed to execute SQL" ) );
    return QgsVectorDataProvider::uniqueValues( index, limit );
  }

  gdal::ogr_feature_unique_ptr f;
  bool ok = false;
  while ( f.reset( l->GetNextFeature() ), f )
  {
    const QVariant res = QgsOgrUtils::getOgrFeatureAttribute( f.get(), fld, 0, textEncoding(), &ok );
    if ( ok )
      uniqueValues << res;

    if ( limit >= 0 && uniqueValues.size() >= limit )
      break;
  }

  return uniqueValues;
}

QStringList QgsOgrProvider::uniqueStringsMatching( int index, const QString &substring, int limit, QgsFeedback *feedback ) const
{
  QStringList results;

  if ( !mValid || index < 0 || index >= mAttributeFields.count() )
    return results;

  QgsField fld = mAttributeFields.at( index );
  if ( fld.name().isNull() )
  {
    return results; //not a provider field
  }

  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  // uniqueStringsMatching() is supposed to be case insensitive, so use the
  // ILIKE operator when it is available.
  // Prior to GDAL 3.1, with OGR SQL, LIKE behaved like ILIKE
  bool supportsILIKE = false;
  {
    QByteArray sql = "SELECT 1 FROM ";
    sql += quotedIdentifier( mOgrLayer->name() );
    sql += " WHERE 'a' ILIKE 'A' LIMIT 1";
    QgsOgrLayerUniquePtr l = mOgrLayer->ExecuteSQL( sql );
    if ( l )
    {
      gdal::ogr_feature_unique_ptr f;
      f.reset( l->GetNextFeature() );
      supportsILIKE = f != nullptr;
    }
  }

  QByteArray sql = "SELECT DISTINCT " + quotedIdentifier( textEncoding()->fromUnicode( fld.name() ) );
  sql += " FROM " + quotedIdentifier( mOgrLayer->name() );

  sql += " WHERE " + quotedIdentifier( textEncoding()->fromUnicode( fld.name() ) );
  if ( supportsILIKE )
    sql += " ILIKE '%";
  else
    sql += " LIKE '%";
  sql += textEncoding()->fromUnicode( substring ) + "%'";

  if ( !mSubsetString.isEmpty() )
  {
    sql += " AND (" + textEncoding()->fromUnicode( mSubsetString ) + ')';
  }

  sql += " ORDER BY " + quotedIdentifier( textEncoding()->fromUnicode( fld.name() ) ) + " ASC";

  QgsDebugMsgLevel( QStringLiteral( "SQL: %1" ).arg( textEncoding()->toUnicode( sql ) ), 2 );
  QgsOgrLayerUniquePtr l = mOgrLayer->ExecuteSQL( sql );
  if ( !l )
  {
    QgsDebugMsg( QStringLiteral( "Failed to execute SQL" ) );
    return QgsVectorDataProvider::uniqueStringsMatching( index, substring, limit, feedback );
  }

  gdal::ogr_feature_unique_ptr f;
  while ( f.reset( l->GetNextFeature() ), f )
  {
    if ( OGR_F_IsFieldSetAndNotNull( f.get(), 0 ) )
      results << textEncoding()->toUnicode( OGR_F_GetFieldAsString( f.get(), 0 ) );

    if ( ( limit >= 0 && results.size() >= limit ) || ( feedback && feedback->isCanceled() ) )
      break;
  }

  return results;
}

QgsFeatureSource::SpatialIndexPresence QgsOgrProvider::hasSpatialIndex() const
{
  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  if ( mOgrLayer && mOgrLayer->TestCapability( OLCFastSpatialFilter ) )
    return QgsFeatureSource::SpatialIndexPresent;
  else if ( mOgrLayer )
    return QgsFeatureSource::SpatialIndexNotPresent;
  else
    return QgsFeatureSource::SpatialIndexUnknown;
}

QVariant QgsOgrProvider::minimumValue( int index ) const
{
  if ( !mValid || index < 0 || index >= mAttributeFields.count() )
  {
    return QVariant();
  }

  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  const QgsField originalField = mAttributeFields.at( index );
  QgsField fld = originalField;

  // can't use native date/datetime types -- OGR converts these to string in the MAX return value
  if ( fld.type() == QVariant::DateTime || fld.type() == QVariant::Date )
  {
    fld.setType( QVariant::String );
  }

  // Don't quote column name (see https://trac.osgeo.org/gdal/ticket/5799#comment:9)
  QByteArray sql = "SELECT MIN(" + quotedIdentifier( textEncoding()->fromUnicode( fld.name() ) );
  sql += ") FROM " + quotedIdentifier( mOgrLayer->name() );

  if ( !mSubsetString.isEmpty() )
  {
    sql += " WHERE " + textEncoding()->fromUnicode( mSubsetString );
  }

  QgsOgrLayerUniquePtr l = mOgrLayer->ExecuteSQL( sql );
  if ( !l )
  {
    QgsDebugMsg( QStringLiteral( "Failed to execute SQL: %1" ).arg( textEncoding()->toUnicode( sql ) ) );
    return QgsVectorDataProvider::minimumValue( index );
  }

  gdal::ogr_feature_unique_ptr f( l->GetNextFeature() );
  if ( !f )
  {
    return QVariant();
  }

  bool ok = false;
  QVariant res = QgsOgrUtils::getOgrFeatureAttribute( f.get(), fld, 0, textEncoding(), &ok );
  if ( !ok )
    return QVariant();

  if ( res.type() != originalField.type() )
    res = convertValue( originalField.type(), res.toString() );

  if ( originalField.type() == QVariant::DateTime )
  {
    // ensure that we treat times as local time, to match behavior when iterating features
    QDateTime temp = res.toDateTime();
    temp.setTimeSpec( Qt::LocalTime );
    res = temp;
  }

  return res;
}

QVariant QgsOgrProvider::maximumValue( int index ) const
{
  if ( !mValid || index < 0 || index >= mAttributeFields.count() )
  {
    return QVariant();
  }

  QgsCPLHTTPFetchOverrider oCPLHTTPFetcher( mAuthCfg );
  QgsSetCPLHTTPFetchOverriderInitiatorClass( oCPLHTTPFetcher, QStringLiteral( "QgsOgrProvider" ) );

  const QgsField originalField = mAttributeFields.at( index );
  QgsField fld = originalField;

  // can't use native date/datetime types -- OGR converts these to string in the MAX return value
  if ( fld.type() == QVariant::DateTime || fld.type() == QVariant::Date )
  {
    fld.setType( QVariant::String );
  }

  // Don't quote column name (see https://trac.osgeo.org/gdal/ticket/5799#comment:9)
  QByteArray sql = "SELECT MAX(" + quotedIdentifier( textEncoding()->fromUnicode( fld.name() ) );
  sql += ") FROM " + quotedIdentifier( mOgrLayer->name() );

  if ( !mSubsetString.isEmpty() )
  {
    sql += " WHERE " + textEncoding()->fromUnicode( mSubsetString );
  }

  QgsOgrLayerUniquePtr l = mOgrLayer->ExecuteSQL( sql );
  if ( !l )
  {
    QgsDebugMsg( QStringLiteral( "Failed to execute SQL: %1" ).arg( textEncoding()->toUnicode( sql ) ) );
    return QgsVectorDataProvider::maximumValue( index );
  }

  gdal::ogr_feature_unique_ptr f( l->GetNextFeature() );
  if ( !f )
  {
    return QVariant();
  }

  bool ok = false;
  QVariant res = QgsOgrUtils::getOgrFeatureAttribute( f.get(), fld, 0, textEncoding(), &ok );
  if ( !ok )
    return QVariant();

  if ( res.type() != originalField.type() )
    res = convertValue( originalField.type(), res.toString() );

  if ( originalField.type() == QVariant::DateTime )
  {
    // ensure that we treat times as local time, to match behavior when iterating features
    QDateTime temp = res.toDateTime();
    temp.setTimeSpec( Qt::LocalTime );
    res = temp;
  }

  return res;
}

QByteArray QgsOgrProvider::quotedIdentifier( const QByteArray &field ) const
{
  return QgsOgrProviderUtils::quotedIdentifier( field, mGDALDriverName );
}

QString QgsOgrProviderUtils::connectionPoolId( const QString &dataSourceURI, bool shareSameDatasetAmongLayers )
{
  if ( shareSameDatasetAmongLayers )
  {
    // If the file part of the URI is really a file, then use it as the
    // connection pool id (for example, so that all layers of a .gpkg file can
    // use the same GDAL dataset object)
    // Otherwise use the datasourceURI
    // Not completely sure about this logic. But at least, for GeoPackage this
    // works fine with multi layer datasets.
    QString filePath = dataSourceURI.left( dataSourceURI.indexOf( QLatin1Char( '|' ) ) );
    QFileInfo fi( filePath );
    if ( fi.isFile() )
      return filePath;
  }
  return dataSourceURI;
}

GDALDatasetH QgsOgrProviderUtils::GDALOpenWrapper( const char *pszPath, bool bUpdate, char **papszOpenOptionsIn, GDALDriverH *phDriver )
{
  CPLErrorReset();

  char **papszOpenOptions = CSLDuplicate( papszOpenOptionsIn );

#if GDAL_VERSION_NUM < GDAL_COMPUTE_VERSION(2,3,0)
  {
    // Workaround for a bug in the GML driver that was fixed in 2.3.0 (and 2.2.X)
    // See https://trac.osgeo.org/gdal/ticket/7046
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,2,0)
    const char *apszAllowedDrivers[] = { "GML", nullptr };
    GDALDriverH hIdentifiedDriver =
      GDALIdentifyDriverEx( pszPath, GDAL_OF_VECTOR, apszAllowedDrivers, nullptr );
#else
    GDALDriverH hIdentifiedDriver =
      GDALIdentifyDriver( pszPath, nullptr );
#endif
    if ( hIdentifiedDriver &&
         strcmp( GDALGetDriverShortName( hIdentifiedDriver ), "GML" ) == 0 )
    {
      VSIStatBufL sStat;
      if ( VSIStatL( CPLResetExtension( pszPath, "gfs" ), &sStat ) != 0 )
      {
        papszOpenOptions = CSLSetNameValue( papszOpenOptions, "FORCE_SRS_DETECTION", "YES" );
      }
    }
  }
#endif

  QString filePath( QString::fromUtf8( pszPath ) );

  bool bIsGpkg = QFileInfo( filePath ).suffix().compare( QLatin1String( "gpkg" ), Qt::CaseInsensitive ) == 0;
  bool bIsLocalGpkg = false;
  if ( bIsGpkg &&
       IsLocalFile( filePath ) &&
       !CPLGetConfigOption( "OGR_SQLITE_JOURNAL", nullptr ) &&
       QgsSettings().value( QStringLiteral( "qgis/walForSqlite3" ), true ).toBool() )
  {
    // For GeoPackage, we force opening of the file in WAL (Write Ahead Log)
    // mode so as to avoid readers blocking writer(s), and vice-versa.
    // https://www.sqlite.org/wal.html
    // But only do that on a local file since WAL is advertised not to work
    // on network shares
    CPLSetThreadLocalConfigOption( "OGR_SQLITE_JOURNAL", "WAL" );
    bIsLocalGpkg = true;
  }
  else if ( bIsGpkg )
  {
    // If WAL isn't set, we explicitly disable it, as it is persistent and it
    // may have been set on a previous connection.
    CPLSetThreadLocalConfigOption( "OGR_SQLITE_JOURNAL", "DELETE" );
  }

  bool modify_OGR_GPKG_FOREIGN_KEY_CHECK = !CPLGetConfigOption( "OGR_GPKG_FOREIGN_KEY_CHECK", nullptr );
  if ( modify_OGR_GPKG_FOREIGN_KEY_CHECK )
  {
    CPLSetThreadLocalConfigOption( "OGR_GPKG_FOREIGN_KEY_CHECK", "NO" );
  }

  const int nOpenFlags = GDAL_OF_VECTOR | ( bUpdate ? GDAL_OF_UPDATE : 0 );
  GDALDatasetH hDS = GDALOpenEx( pszPath, nOpenFlags, nullptr, papszOpenOptions, nullptr );
  CSLDestroy( papszOpenOptions );

  CPLSetThreadLocalConfigOption( "OGR_SQLITE_JOURNAL", nullptr );
  if ( modify_OGR_GPKG_FOREIGN_KEY_CHECK )
  {
    CPLSetThreadLocalConfigOption( "OGR_GPKG_FOREIGN_KEY_CHECK", nullptr );
  }

  if ( !hDS )
  {
    if ( phDriver )
      *phDriver = nullptr;
    return nullptr;
  }
  GDALDriverH hDrv = GDALGetDatasetDriver( hDS );
  if ( bIsLocalGpkg && strcmp( GDALGetDriverShortName( hDrv ), "GPKG" ) == 0 )
  {
    QMutexLocker locker( sGlobalMutex() );
    ( *sMapCountOpenedDS() )[ filePath ]++;
    ( *sMapDSHandleToUpdateMode() )[ hDS ] = bUpdate;
  }
  if ( phDriver )
    *phDriver = hDrv;

  return hDS;
}

static bool IsLocalFile( const QString &path )
{
  QString dirName( QFileInfo( path ).absolutePath() );
  // Start with the OS specific methods since the QT >= 5.4 method just
  // return a string and not an enumerated type.
#if defined(Q_OS_WIN)
  if ( dirName.startsWith( "\\\\" ) || dirName.startsWith( "//" ) )
    return false;
  if ( dirName.length() >= 3 && dirName[1] == ':' &&
       ( dirName[2] == '\\' || dirName[2] == '/' ) )
  {
    dirName.resize( 3 );
    return GetDriveType( dirName.toLatin1().constData() ) != DRIVE_REMOTE;
  }
  return true;
#elif defined(Q_OS_LINUX)
  struct statfs sStatFS;
  if ( statfs( dirName.toLatin1().constData(), &sStatFS ) == 0 )
  {
    // Codes from http://man7.org/linux/man-pages/man2/statfs.2.html
    if ( sStatFS.f_type == 0x6969 /* NFS */ ||
         sStatFS.f_type == 0x517b /* SMB */ ||
         sStatFS.f_type == 0xff534d42 /* CIFS */ ||
         sStatFS.f_type == 0xfe534d42 /* CIFS */ )
    {
      return false;
    }
  }
  return true;
#else
  QStorageInfo info( dirName );
  const QString fileSystem( info.fileSystemType() );
  bool isLocal = path != QLatin1String( "nfs" ) && path != QLatin1String( "smbfs" );
  if ( !isLocal )
    QgsDebugMsgLevel( QStringLiteral( "Filesystem for %1 is %2" ).arg( path, fileSystem ), 2 );
  return isLocal;
#endif
}

void QgsOgrProviderUtils::GDALCloseWrapper( GDALDatasetH hDS )
{
  if ( !hDS )
    return;
  GDALDriverH mGDALDriver = GDALGetDatasetDriver( hDS );
  QString mGDALDriverName = GDALGetDriverShortName( mGDALDriver );
  QString datasetName( QString::fromUtf8( GDALGetDescription( hDS ) ) );
  if ( mGDALDriverName == QLatin1String( "GPKG" ) &&
       IsLocalFile( datasetName ) &&
       !CPLGetConfigOption( "OGR_SQLITE_JOURNAL", nullptr ) )
  {
    bool openedAsUpdate = false;
    bool tryReturnToWall = false;
    {
      QMutexLocker locker( sGlobalMutex() );
      ( *sMapCountOpenedDS() )[ datasetName ] --;
      if ( ( *sMapCountOpenedDS() )[ datasetName ] == 0 )
      {
        sMapCountOpenedDS()->remove( datasetName );
        openedAsUpdate = ( *sMapDSHandleToUpdateMode() )[hDS];
        tryReturnToWall = true;
      }
      sMapDSHandleToUpdateMode()->remove( hDS );
    }
    if ( tryReturnToWall )
    {
      bool bSuccess = false;
      if ( openedAsUpdate )
      {
        // We need to reset all iterators on layers, otherwise we will not
        // be able to change journal_mode.
        int layerCount = GDALDatasetGetLayerCount( hDS );
        for ( int i = 0; i < layerCount; i ++ )
        {
          OGR_L_ResetReading( GDALDatasetGetLayer( hDS, i ) );
        }

        CPLPushErrorHandler( CPLQuietErrorHandler );
        QgsDebugMsgLevel( QStringLiteral( "GPKG: Trying to return to delete mode" ), 2 );
        OGRLayerH hSqlLyr = GDALDatasetExecuteSQL( hDS,
                            "PRAGMA journal_mode = delete",
                            nullptr, nullptr );
        if ( hSqlLyr )
        {
          gdal::ogr_feature_unique_ptr hFeat( OGR_L_GetNextFeature( hSqlLyr ) );
          if ( hFeat )
          {
            const char *pszRet = OGR_F_GetFieldAsString( hFeat.get(), 0 );
            bSuccess = EQUAL( pszRet, "delete" );
            QgsDebugMsgLevel( QStringLiteral( "Return: %1" ).arg( pszRet ), 2 );
          }
        }
        else if ( CPLGetLastErrorType() != CE_None )
        {
          QgsDebugMsg( QStringLiteral( "Return: %1" ).arg( CPLGetLastErrorMsg() ) );
        }
        GDALDatasetReleaseResultSet( hDS, hSqlLyr );
        CPLPopErrorHandler();
      }
      GDALClose( hDS );

      // If the file was opened in read-only mode, or if the above failed,
      // we need to reopen it in update mode
      if ( !bSuccess )
      {
        if ( openedAsUpdate )
        {
          QgsDebugMsgLevel( QStringLiteral( "GPKG: Trying again" ), 1 );
        }
        else
        {
          QgsDebugMsgLevel( QStringLiteral( "GPKG: Trying to return to delete mode" ), 1 );
        }
        CPLSetThreadLocalConfigOption( "OGR_SQLITE_JOURNAL", "DELETE" );
        hDS = GDALOpenEx( datasetName.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_UPDATE, nullptr, nullptr, nullptr );
        CPLSetThreadLocalConfigOption( "OGR_SQLITE_JOURNAL", nullptr );
        if ( hDS )
        {
#ifdef QGISDEBUG
          CPLPushErrorHandler( CPLQuietErrorHandler );
          OGRLayerH hSqlLyr = GDALDatasetExecuteSQL( hDS,
                              "PRAGMA journal_mode",
                              nullptr, nullptr );
          CPLPopErrorHandler();
          if ( hSqlLyr != nullptr )
          {
            gdal::ogr_feature_unique_ptr hFeat( OGR_L_GetNextFeature( hSqlLyr ) );
            if ( hFeat != nullptr )
            {
              const char *pszRet = OGR_F_GetFieldAsString( hFeat.get(), 0 );
              QgsDebugMsgLevel( QStringLiteral( "Return: %1" ).arg( pszRet ), 1 );
            }
            GDALDatasetReleaseResultSet( hDS, hSqlLyr );
          }
#endif
          GDALClose( hDS );
        }
      }
    }
    else
    {
      GDALClose( hDS );
    }
  }

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(3,1,0) && GDAL_VERSION_NUM <= GDAL_COMPUTE_VERSION(3,1,3)
  else if ( mGDALDriverName == QLatin1String( "XLSX" ) ||
            mGDALDriverName == QLatin1String( "ODS" ) )
  {
    // Workaround bug in GDAL 3.1.0 to 3.1.3 that creates XLSX and ODS files incompatible with LibreOffice due to use of ZIP64
    CPLSetThreadLocalConfigOption( "CPL_CREATE_ZIP64", "NO" );
    GDALClose( hDS );
    CPLSetThreadLocalConfigOption( "CPL_CREATE_ZIP64", nullptr );
  }
#endif

  else
  {
    GDALClose( hDS );
  }
}

QByteArray QgsOgrProviderUtils::quotedIdentifier( QByteArray field, const QString &driverName )
{
  if ( driverName == QLatin1String( "MySQL" ) )
  {
    field.replace( '\\', "\\\\" );
    field.replace( '`', "``" );
    return field.prepend( '`' ).append( '`' );
  }
  else
  {
    field.replace( '\\', "\\\\" );
    field.replace( '"', "\\\"" );
    field.replace( '\'', "\\'" );
    return field.prepend( '\"' ).append( '\"' );
  }
}

QString QgsOgrProviderUtils::quotedValue( const QVariant &value )
{
  if ( value.isNull() )
    return QStringLiteral( "NULL" );

  switch ( value.type() )
  {
    case QVariant::Int:
    case QVariant::LongLong:
    case QVariant::Double:
      return value.toString();

    case QVariant::Bool:
      //OGR does not support boolean literals
      return value.toBool() ? "1" : "0";

    default:
    case QVariant::String:
      QString v = value.toString();
      v.replace( '\'', QLatin1String( "''" ) );
      if ( v.contains( '\\' ) )
        return v.replace( '\\', QLatin1String( "\\\\" ) ).prepend( "E'" ).append( '\'' );
      else
        return v.prepend( '\'' ).append( '\'' );
  }
}

bool QgsOgrProvider::syncToDisc()
{
  QgsOgrConnPool::instance()->invalidateConnections( QgsOgrProviderUtils::connectionPoolId( dataSourceUri( true ), mShareSameDatasetAmongLayers ) );

  //for shapefiles, remove spatial index files and create a new index
  QgsOgrConnPool::instance()->unref( QgsOgrProviderUtils::connectionPoolId( dataSourceUri( true ), mShareSameDatasetAmongLayers ) );
  bool shapeIndex = false;
  if ( mGDALDriverName == QLatin1String( "ESRI Shapefile" ) )
  {
    QString sbnIndexFile;
    QFileInfo fi( mFilePath );
    int suffixLength = fi.suffix().length();
    sbnIndexFile = mFilePath;
    sbnIndexFile.chop( suffixLength );
    sbnIndexFile.append( "sbn" );

    if ( QFile::exists( sbnIndexFile ) )
    {
      shapeIndex = true;
      close();
      QgsOgrConnPool::instance()->invalidateConnections( QgsOgrProviderUtils::connectionPoolId( dataSourceUri( true ), mShareSameDatasetAmongLayers ) );
      QFile::remove( sbnIndexFile );
      open( OpenModeSameAsCurrent );
      if ( !mValid )
        return false;
    }
  }

  if ( mOgrLayer->SyncToDisk() != OGRERR_NONE )
  {
    pushError( tr( "OGR error syncing to disk: %1" ).arg( CPLGetLastErrorMsg() ) );
  }

  // Repack is done automatically on OGR_L_SyncToDisk with gdal-2.2.0+
#if !defined(GDAL_VERSION_NUM) || GDAL_VERSION_NUM < 2020000
  if ( !mDeferRepack )
  {
    if ( mShapefileMayBeCorrupted )
      repack();

    mShapefileMayBeCorrupted = false;
  }
#endif

  QgsOgrConnPool::instance()->ref( QgsOgrProviderUtils::connectionPoolId( dataSourceUri( true ), mShareSameDatasetAmongLayers ) );
  if ( shapeIndex )
  {
    return createSpatialIndex();
  }

  return true;
}

void QgsOgrProvider::recalculateFeatureCount() const
{
  if ( !mOgrLayer )
  {
    mFeaturesCounted = QgsVectorDataProvider::Uncounted;
    return;
  }

  OGRGeometryH filter = mOgrLayer->GetSpatialFilter();
  if ( filter )
  {
    filter = OGR_G_Clone( filter );
    mOgrLayer->SetSpatialFilter( nullptr );
  }

  // feature count returns number of features within current spatial filter
  // so we remove it if there's any and then put it back
  if ( mOgrGeometryTypeFilter == wkbUnknown )
  {
    mFeaturesCounted = mOgrLayer->GetApproxFeatureCount();
    if ( mFeaturesCounted == -1 )
    {
      mFeaturesCounted = QgsVectorDataProvider::UnknownCount;
    }
  }
  else
  {
    mFeaturesCounted = 0;
    setRelevantFields( true, QgsAttributeList() );
    mOgrLayer->ResetReading();
    gdal::ogr_feature_unique_ptr fet;
    const OGRwkbGeometryType flattenGeomTypeFilter =
      QgsOgrProvider::ogrWkbSingleFlatten( mOgrGeometryTypeFilter );
    while ( fet.reset( mOgrLayer->GetNextFeature() ), fet )
    {
      OGRGeometryH geom = OGR_F_GetGeometryRef( fet.get() );
      if ( geom )
      {
        OGRwkbGeometryType gType = OGR_G_GetGeometryType( geom );
        gType = QgsOgrProvider::ogrWkbSingleFlatten( gType );
        if ( gType == flattenGeomTypeFilter ) mFeaturesCounted++;
      }
    }
    mOgrLayer->ResetReading();
    setRelevantFields( true, attributeIndexes() );
  }

  if ( filter )
  {
    mOgrLayer->SetSpatialFilter( filter );
  }

  QgsOgrConnPool::instance()->invalidateConnections( QgsOgrProviderUtils::connectionPoolId( dataSourceUri( true ), mShareSameDatasetAmongLayers ) );
}

bool QgsOgrProvider::doesStrictFeatureTypeCheck() const
{
  // FIXME probably other drivers too...
  return mGDALDriverName != QLatin1String( "ESRI Shapefile" ) || ( mOGRGeomType == wkbPoint || mOGRGeomType == wkbPoint25D );
}

OGRwkbGeometryType QgsOgrProvider::ogrWkbSingleFlatten( OGRwkbGeometryType type )
{
  type = wkbFlatten( type );
  switch ( type )
  {
    case wkbMultiPoint:
      return wkbPoint;
    case wkbMultiLineString:
      return wkbLineString;
    case wkbMultiPolygon:
      return wkbPolygon;
    case wkbMultiCurve:
      return wkbCompoundCurve;
    case wkbMultiSurface:
      return wkbCurvePolygon;
    default:
      return type;
  }
}

OGRLayerH QgsOgrProviderUtils::setSubsetString( OGRLayerH layer, GDALDatasetH ds, QTextCodec *encoding, const QString &subsetString )
{
  QByteArray layerName = OGR_FD_GetName( OGR_L_GetLayerDefn( layer ) );
  GDALDriverH driver = GDALGetDatasetDriver( ds );
  QString driverName = GDALGetDriverShortName( driver );

  if ( driverName == QLatin1String( "ODBC" ) ) //the odbc driver does not like schema names for subset
  {
    QString layerNameString = encoding->toUnicode( layerName );
    int dotIndex = layerNameString.indexOf( '.' );
    if ( dotIndex > 1 )
    {
      QString modifiedLayerName = layerNameString.right( layerNameString.size() - dotIndex - 1 );
      layerName = encoding->fromUnicode( modifiedLayerName );
    }
  }
  OGRLayerH subsetLayer = nullptr;
  if ( subsetString.startsWith( QLatin1String( "SELECT " ), Qt::CaseInsensitive ) )
  {
    QByteArray sql = encoding->fromUnicode( subsetString );

    QgsDebugMsgLevel( QStringLiteral( "SQL: %1" ).arg( encoding->toUnicode( sql ) ), 1 );
    subsetLayer = GDALDatasetExecuteSQL( ds, sql.constData(), nullptr, nullptr );
  }
  else
  {
    if ( OGR_L_SetAttributeFilter( layer, encoding->fromUnicode( subsetString ).constData() ) != OGRERR_NONE )
    {
      return nullptr;
    }
    subsetLayer = layer;
  }

  return subsetLayer;
}

void QgsOgrProvider::open( OpenMode mode )
{
  bool openReadOnly = false;
  Q_ASSERT( !mOgrSqlLayer );
  Q_ASSERT( !mOgrOrigLayer );

  // Try to open using VSIFileHandler
  //   see http://trac.osgeo.org/gdal/wiki/UserDocs/ReadInZip
  QString vsiPrefix = QgsZipItem::vsiPrefix( dataSourceUri() );
  if ( !vsiPrefix.isEmpty() )
  {
    // GDAL>=1.8.0 has write support for zip, but read and write operations
    // cannot be interleaved, so for now just use read-only.
    openReadOnly = true;
    if ( !mFilePath.startsWith( vsiPrefix ) )
    {
      mFilePath = vsiPrefix + mFilePath;
      setDataSourceUri( mFilePath );
    }
    QgsDebugMsgLevel( QStringLiteral( "Trying %1 syntax, mFilePath= %2" ).arg( vsiPrefix, mFilePath ), 1 );
  }

  QgsDebugMsgLevel( "mFilePath: " + mFilePath, 3 );
  QgsDebugMsgLevel( "mLayerIndex: " + QString::number( mLayerIndex ), 3 );
  QgsDebugMsgLevel( "mLayerName: " + mLayerName, 3 );
  QgsDebugMsgLevel( "mSubsetString: " + mSubsetString, 3 );
  CPLSetConfigOption( "OGR_ORGANIZE_POLYGONS", "ONLY_CCW" );  // "SKIP" returns MULTIPOLYGONs for multiringed POLYGONs
  CPLSetConfigOption( "GPX_ELE_AS_25D", "YES" );  // use GPX elevation as z values
  if ( !CPLGetConfigOption( "OSM_USE_CUSTOM_INDEXING", nullptr ) )
  {
    // Disable custom/fast indexing by default, as it can prevent some .osm.pbf
    // files to be loaded.
    // See https://github.com/qgis/QGIS/issues/31062
    CPLSetConfigOption( "OSM_USE_CUSTOM_INDEXING", "NO" );
  }

  if ( mFilePath.startsWith( QLatin1String( "MySQL:" ) ) && !mLayerName.isEmpty() && !mFilePath.endsWith( ",tables=" + mLayerName ) )
  {
    mFilePath += ",tables=" + mLayerName;
  }

  if ( mode == OpenModeForceReadOnly )
    openReadOnly = true;
  else if ( mode == OpenModeSameAsCurrent && !mWriteAccess )
    openReadOnly = true;

  // first try to open in update mode (unless specified otherwise)
  QString errCause;
  if ( !openReadOnly )
  {
    QStringList options( mOpenOptions );
    if ( mode == OpenModeForceUpdateRepackOff || ( mDeferRepack && OpenModeSameAsCurrent ) )
    {
      options << "AUTO_REPACK=OFF";
    }
    // We get the layer which was requested by the uri. The layername
    // has precedence over the layerid if both are given.
    if ( !mLayerName.isNull() )
    {
      mOgrOrigLayer = QgsOgrProviderUtils::getLayer( mFilePath, true, options, mLayerName, errCause, true );
    }
    else
    {
      mOgrOrigLayer = QgsOgrProviderUtils::getLayer( mFilePath, true, options, mLayerIndex, errCause, true );
    }
  }

  mValid = false;
  if ( mOgrOrigLayer )
  {
    mWriteAccess = true;
    mWriteAccessPossible = true;
  }
  else
  {
    mWriteAccess = false;
    if ( !openReadOnly )
    {
      QgsDebugMsg( QStringLiteral( "OGR failed to opened in update mode, trying in read-only mode" ) );
    }

    QStringList options( mOpenOptions );
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(3,1,0)
    // assume trusted data to get more speed
    if ( mGDALDriverName == QLatin1String( "FlatGeobuf" ) &&
         !options.contains( "VERIFY_BUFFERS=YES" ) )
    {
      options << "VERIFY_BUFFERS=NO";
    }
#endif

    // try to open read-only
    if ( !mLayerName.isNull() )
    {
      mOgrOrigLayer = QgsOgrProviderUtils::getLayer( mFilePath, false, options, mLayerName, errCause, true );
    }
    else
    {
      mOgrOrigLayer = QgsOgrProviderUtils::getLayer( mFilePath, false, options, mLayerIndex, errCause, true );
    }
  }

  if ( mOgrOrigLayer )
  {
    mGDALDriverName = mOgrOrigLayer->driverName();
    mShareSameDatasetAmongLayers = QgsOgrProviderUtils::canDriverShareSameDatasetAmongLayers( mGDALDriverName );

    QgsDebugMsgLevel( "OGR opened using Driver " + mGDALDriverName, 2 );

    mOgrLayer = mOgrOrigLayer.get();

    // check that the initial encoding setting is fit for this layer

    if ( mode == OpenModeInitial && mGDALDriverName == QLatin1String( "ESRI Shapefile" ) )
    {
      // determine encoding from shapefile cpg or LDID information, if possible
      QString shpEncoding;
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(3,1,0)
      shpEncoding = mOgrLayer->GetMetadataItem( QStringLiteral( "ENCODING_FROM_CPG" ), QStringLiteral( "SHAPEFILE" ) );
      if ( shpEncoding.isEmpty() )
        shpEncoding = mOgrLayer->GetMetadataItem( QStringLiteral( "ENCODING_FROM_LDID" ), QStringLiteral( "SHAPEFILE" ) );
#else
      shpEncoding = QgsOgrUtils::readShapefileEncoding( mFilePath );
#endif

      if ( !shpEncoding.isEmpty() )
        setEncoding( shpEncoding );
      else
        setEncoding( encoding() );
    }
    else
    {
      setEncoding( encoding() );
    }

    // Ensure subset is set (setSubsetString does nothing if the passed sql subset string is equal to mSubsetString, which is the case when reloading the dataset)
    QString origSubsetString = mSubsetString;
    mSubsetString.clear();
    // Block signals to avoid endless recursion reloadData -> emit dataChanged -> reloadData
    blockSignals( true );

    // Do not update capabilities: it will be done later

    // WARNING if this is the initial open - we don't already have a connection ref, and will be creating one later. So we *mustn't* grab an extra connection ref
    // while setting the subset string, or we'll be left with an extra reference which is never cleared.
    mValid = _setSubsetString( origSubsetString, false, false, mode != OpenModeInitial );

    blockSignals( false );
    if ( mValid )
    {
      if ( mode == OpenModeInitial )
      {
        computeCapabilities();
      }
      QgsDebugMsgLevel( QStringLiteral( "Data source is valid" ), 2 );
    }
    else
    {
      QgsMessageLog::logMessage( tr( "Data source is invalid (%1)" ).arg( QString::fromUtf8( CPLGetLastErrorMsg() ) ), tr( "OGR" ) );
    }
  }
  else
  {
    QgsMessageLog::logMessage( errCause + "(" + QString::fromUtf8( CPLGetLastErrorMsg() ) + ")", tr( "OGR" ) );
  }

  // For shapefiles or MapInfo .tab, so as to allow concurrent opening between
  // QGIS and MapInfo, we go back to read-only mode for now.
  // We limit to those drivers as re-opening is relatively cheap (other drivers
  // like GeoJSON might do full content ingestion for example)
  if ( mValid && mode == OpenModeInitial && mWriteAccess &&
       ( mGDALDriverName == QLatin1String( "ESRI Shapefile" ) || mGDALDriverName == QLatin1String( "MapInfo File" ) ) )
  {
    mOgrSqlLayer.reset();
    mOgrOrigLayer.reset();
    mOgrLayer = nullptr;
    mValid = false;

    // In the case where we deal with a shapefile, it is possible that it has
    // pre-existing holes in the DBF (see #15407), so if using a GDAL version
    // recent enough to have reliable packing, do a packing at the first edit
    // action.
    if ( mGDALDriverName == QLatin1String( "ESRI Shapefile" )  &&
         atoi( GDALVersionInfo( "VERSION_NUM" ) ) >= GDAL_COMPUTE_VERSION( 2, 1, 2 ) )
    {
      mShapefileMayBeCorrupted = true;
    }

    // try to open read-only
    if ( !mLayerName.isNull() )
    {
      mOgrOrigLayer = QgsOgrProviderUtils::getLayer( mFilePath, false, mOpenOptions, mLayerName, errCause, true );
    }
    else
    {
      mOgrOrigLayer = QgsOgrProviderUtils::getLayer( mFilePath, false, mOpenOptions, mLayerIndex, errCause, true );
    }

    mWriteAccess = false;
    mOgrLayer = mOgrOrigLayer.get();
    if ( mOgrLayer )
    {
      mValid = true;
      mDynamicWriteAccess = true;

      if ( !mSubsetString.isEmpty() )
      {
        // Do not update capabilities here
        // but ensure subset is set (setSubsetString does nothing if the passed sql subset string is equal to
        // mSubsetString, which is the case when reloading the dataset)
        QString origSubsetString = mSubsetString;
        mSubsetString.clear();
        mValid = _setSubsetString( origSubsetString, false, false );
      }
    }
  }

  // For debug/testing purposes
  if ( !mValid )
    setProperty( "_debug_open_mode", "invalid" );
  else if ( mWriteAccess )
    setProperty( "_debug_open_mode", "read-write" );
  else
    setProperty( "_debug_open_mode", "read-only" );

  mRefreshFeatureCount = true;
}

void QgsOgrProvider::close()
{
  mOgrSqlLayer.reset();
  mOgrOrigLayer.reset();
  mOgrLayer = nullptr;
  mValid = false;
  setProperty( "_debug_open_mode", "invalid" );

  invalidateCachedExtent( false );
}

void QgsOgrProvider::reloadProviderData()
{
  mFeaturesCounted = QgsVectorDataProvider::Uncounted;
  bool wasValid = mValid;
  QgsOgrConnPool::instance()->invalidateConnections( QgsOgrProviderUtils::connectionPoolId( dataSourceUri( true ), mShareSameDatasetAmongLayers ) );
  close();
  open( OpenModeSameAsCurrent );
  if ( !mValid && wasValid )
    pushError( tr( "Cannot reopen datasource %1" ).arg( dataSourceUri() ) );
}

bool QgsOgrProvider::_enterUpdateMode( bool implicit )
{
  if ( !mWriteAccessPossible )
  {
    return false;
  }
  if ( mWriteAccess )
  {
    ++mUpdateModeStackDepth;
    return true;
  }
  if ( mUpdateModeStackDepth == 0 )
  {
    Q_ASSERT( mDynamicWriteAccess );
    QgsDebugMsgLevel( QStringLiteral( "Reopening %1 in update mode" ).arg( dataSourceUri() ), 1 );
    close();
    open( implicit ? OpenModeForceUpdate : OpenModeForceUpdateRepackOff );
    if ( !mOgrLayer || !mWriteAccess )
    {
      QgsMessageLog::logMessage( tr( "Cannot reopen datasource %1 in update mode" ).arg( dataSourceUri() ), tr( "OGR" ) );
      pushError( tr( "Cannot reopen datasource %1 in update mode" ).arg( dataSourceUri() ) );
      return false;
    }
  }
  ++mUpdateModeStackDepth;
  // For implicitly entered updateMode, don't defer repacking
  mDeferRepack = !implicit;
  return true;
}

bool QgsOgrProvider::leaveUpdateMode()
{
  if ( !mWriteAccessPossible )
  {
    return false;
  }
  --mUpdateModeStackDepth;
  if ( mUpdateModeStackDepth < 0 )
  {
    QgsMessageLog::logMessage( tr( "Unbalanced call to leaveUpdateMode() w.r.t. enterUpdateMode()" ), tr( "OGR" ) );
    mUpdateModeStackDepth = 0;
    return false;
  }
  if ( mDeferRepack && mUpdateModeStackDepth == 0 )
  {
    // Only repack once update mode is inactive
    if ( mShapefileMayBeCorrupted )
      repack();

    mShapefileMayBeCorrupted = false;
    mDeferRepack = false;
  }
  if ( !mDynamicWriteAccess )
  {
    // The GeoJSON driver only properly flushes stuff in all situations by
    // closing and re-opening. Starting with GDAL 2.3.1, it should be safe to
    // use GDALDatasetFlush().
    if ( mGDALDriverName == QLatin1String( "GeoJSON" ) )
    {
      // Backup fields since if we created new fields, but didn't populate it
      // with any feature yet, it will disappear.
      QgsFields oldFields = mAttributeFields;
      reloadData();
      if ( mValid )
      {
        // Make sure that new fields we added, but didn't populate yet, are
        // recreated at the OGR level, otherwise we won't be able to populate
        // them.
        for ( const auto &field : oldFields )
        {
          int idx = mAttributeFields.lookupField( field.name() );
          if ( idx < 0 )
          {
            bool ignoreErrorOut = false;
            addAttributeOGRLevel( field, ignoreErrorOut );
          }
        }
        mAttributeFields = oldFields;
      }
    }
    return true;
  }
  if ( mUpdateModeStackDepth == 0 )
  {
    QgsDebugMsgLevel( QStringLiteral( "Reopening %1 in read-only mode" ).arg( dataSourceUri() ), 1 );
    close();
    open( OpenModeForceReadOnly );
    if ( !mOgrLayer )
    {
      QgsMessageLog::logMessage( tr( "Cannot reopen datasource %1 in read-only mode" ).arg( dataSourceUri() ), tr( "OGR" ) );
      pushError( tr( "Cannot reopen datasource %1 in read-only mode" ).arg( dataSourceUri() ) );
      return false;
    }
  }
  return true;
}

bool QgsOgrProvider::isSaveAndLoadStyleToDatabaseSupported() const
{
  // We could potentially extend support for styling to other drivers
  // with multiple layer support.
  return mGDALDriverName == QLatin1String( "GPKG" ) ||
         mGDALDriverName == QLatin1String( "SQLite" );
}

bool QgsOgrProvider::isDeleteStyleFromDatabaseSupported() const
{
  return isSaveAndLoadStyleToDatabaseSupported();
}

QString QgsOgrProvider::fileVectorFilters() const
{
  return QgsOgrProviderUtils::fileVectorFilters();
}

QString QgsOgrProviderUtils::DatasetIdentification::toString() const
{
  return dsName +
         ( updateMode ?
           QStringLiteral( "update" ) : QStringLiteral( "read-only" ) ) +
         options.join( ',' );
}

bool QgsOgrProviderUtils::DatasetIdentification::operator<
( const QgsOgrProviderUtils::DatasetIdentification &other ) const
{
  return toString() < other.toString();
}

static GDALDatasetH OpenHelper( const QString &dsName,
                                bool updateMode,
                                const QStringList &options )
{
  char **papszOpenOptions = nullptr;
  const auto constOptions = options;
  for ( QString option : constOptions )
  {
    papszOpenOptions = CSLAddString( papszOpenOptions,
                                     option.toUtf8().constData() );
  }
  GDALDatasetH hDS = QgsOgrProviderUtils::GDALOpenWrapper(
                       QgsOgrProviderUtils::expandAuthConfig( dsName ).toUtf8().constData(), updateMode, papszOpenOptions, nullptr );
  CSLDestroy( papszOpenOptions );
  return hDS;
}

void QgsOgrProviderUtils::invalidateCachedDatasets( const QString &dsName )
{
  QMutexLocker locker( sGlobalMutex() );
  while ( true )
  {
    bool erased = false;
    for ( auto iter = sMapSharedDS.begin(); iter != sMapSharedDS.end(); ++iter )
    {
      if ( iter.key().dsName == dsName )
      {
        sMapSharedDS.erase( iter );
        erased = true;
        break;
      }
    }
    if ( !erased )
      break;
  }
}


QgsOgrDatasetSharedPtr QgsOgrProviderUtils::getAlreadyOpenedDataset( const QString &dsName )
{
  QMutexLocker locker( sGlobalMutex() );
  for ( auto iter = sMapSharedDS.begin(); iter != sMapSharedDS.end(); ++iter )
  {
    auto ident = iter.key();
    if ( ident.dsName == dsName && ident.updateMode )
    {
      // Browse through this list, to look for the first available DatasetWithLayers*
      // instance that is in update mode (hoping there's only one...)
      auto &datasetList = iter.value();
      for ( const auto &ds : datasetList )
      {
        Q_ASSERT( ds->refCount > 0 );
        return QgsOgrDataset::create( ident, ds );
      }
    }
  }
  return nullptr;
}


QgsOgrLayerUniquePtr QgsOgrProviderUtils::getLayer( const QString &dsName,
    int layerIndex,
    QString &errCause )
{
  QMutexLocker locker( sGlobalMutex() );
  for ( auto iter = sMapSharedDS.begin(); iter != sMapSharedDS.end(); ++iter )
  {
    if ( iter.key().dsName == dsName )
    {
      // Browse through this list, to look for a DatasetWithLayers*
      // instance that don't use yet our layer of interest
      auto &datasetList = iter.value();
      const auto constDatasetList = datasetList;
      for ( QgsOgrProviderUtils::DatasetWithLayers *ds : constDatasetList )
      {
        if ( !ds->canBeShared )
          continue;
        Q_ASSERT( ds->refCount > 0 );

        QString layerName;
        OGRLayerH hLayer;
        {
          QMutexLocker lockerDS( &ds->mutex );
          hLayer = GDALDatasetGetLayer(
                     ds->hDS, layerIndex );
          if ( hLayer )
          {
            OGR_L_SetAttributeFilter( hLayer, nullptr );
            layerName = QString::fromUtf8( OGR_L_GetName( hLayer ) );
          }
        }
        if ( !hLayer )
        {
          errCause = QObject::tr( "Cannot find layer %1." ).arg( layerIndex );
          return nullptr;
        }
        return getLayer( dsName, iter.key().updateMode, iter.key().options, layerName, errCause, true );
      }
    }
  }
  return getLayer( dsName, false, QStringList(), layerIndex, errCause, true );
}

QgsOgrLayerUniquePtr QgsOgrProviderUtils::getLayer( const QString &dsName,
    bool updateMode,
    const QStringList &options,
    int layerIndex,
    QString &errCause,
    bool checkModificationDateAgainstCache )
{
  QMutexLocker locker( sGlobalMutex() );

  // The idea is that we want to minimize the number of GDALDatasetH
  // handles openeded. But we have constraints. We do not want that 2
  // callers of getLayer() with the same input parameters get the same
  // GDALDatasetH since iterators over features of that layer would conflict

  QgsOgrProviderUtils::DatasetIdentification ident;
  ident.dsName = dsName;
  ident.updateMode = updateMode;
  ident.options = options;
  // Find if there's a list of DatasetWithLayers* that match our
  // (dsName, updateMode, options) criteria
  auto iter = sMapSharedDS.find( ident );
  if ( iter != sMapSharedDS.end() )
  {
    // Browse through this list, to look for a DatasetWithLayers*
    // instance that don't use yet our layer of interest
    auto datasetList = iter.value();
    const auto constDatasetList = datasetList;
    for ( QgsOgrProviderUtils::DatasetWithLayers *ds : constDatasetList )
    {
      if ( !ds->canBeShared )
        continue;
      Q_ASSERT( ds->refCount > 0 );

      QString layerName;
      OGRLayerH hLayer;
      {
        QMutexLocker lockerDS( &ds->mutex );
        hLayer = GDALDatasetGetLayer(
                   ds->hDS, layerIndex );
        if ( hLayer )
        {
          OGR_L_SetAttributeFilter( hLayer, nullptr );
          layerName = QString::fromUtf8( OGR_L_GetName( hLayer ) );
        }
      }
      if ( !hLayer )
      {
        errCause = QObject::tr( "Cannot find layer %1." ).arg( layerIndex );
        return nullptr;
      }
      return getLayer( dsName, updateMode, options, layerName, errCause,
                       checkModificationDateAgainstCache );
    }
  }

  GDALDatasetH hDS = OpenHelper( dsName, updateMode, options );
  if ( !hDS )
  {
    errCause = QObject::tr( "Cannot open %1." ).arg( dsName );
    return nullptr;
  }

  OGRLayerH hLayer = GDALDatasetGetLayer(
                       hDS, layerIndex );
  if ( !hLayer )
  {
    QgsOgrProviderUtils::GDALCloseWrapper( hDS );
    errCause = QObject::tr( "Cannot find layer %1." ).arg( layerIndex );
    return nullptr;
  }
  QString layerName = QString::fromUtf8( OGR_L_GetName( hLayer ) );

  QgsOgrProviderUtils::DatasetWithLayers *ds =
    new QgsOgrProviderUtils::DatasetWithLayers;
  ds->hDS = hDS;

  GDALDriverH driver = GDALGetDatasetDriver( hDS );
  QString driverName = GDALGetDriverShortName( driver );
  ds->canBeShared = canDriverShareSameDatasetAmongLayers( driverName, updateMode, dsName );

  QgsOgrLayerUniquePtr layer = QgsOgrLayer::CreateForLayer(
                                 ident, layerName, ds, hLayer );
  ds->setLayers[layerName] = layer.get();

  QList<DatasetWithLayers *> datasetList;
  datasetList.push_back( ds );
  sMapSharedDS[ident] = datasetList;

  return layer;
}

QgsOgrLayerUniquePtr QgsOgrProviderUtils::getLayer( const QString &dsName,
    const QString &layerName,
    QString &errCause )
{
  QMutexLocker locker( sGlobalMutex() );

  for ( auto iter = sMapSharedDS.begin(); iter != sMapSharedDS.end(); ++iter )
  {
    if ( iter.key().dsName == dsName )
    {
      // Browse through this list, to look for a DatasetWithLayers*
      // instance that don't use yet our layer of interest
      auto &datasetList = iter.value();
      const auto constDatasetList = datasetList;
      for ( QgsOgrProviderUtils::DatasetWithLayers *ds : constDatasetList )
      {
        if ( !ds->canBeShared )
          continue;
        Q_ASSERT( ds->refCount > 0 );

        auto iter2 = ds->setLayers.find( layerName );
        if ( iter2 == ds->setLayers.end() )
        {
          OGRLayerH hLayer;
          {
            QMutexLocker lockerDS( &ds->mutex );
            hLayer = GDALDatasetGetLayerByName(
                       ds->hDS, layerName.toUtf8().constData() );
          }
          if ( !hLayer )
          {
            // Shouldn't happen really !
            errCause = QObject::tr( "Cannot find layer %1." ).arg( layerName );
            return nullptr;
          }
          OGR_L_SetAttributeFilter( hLayer, nullptr );

          QgsOgrLayerUniquePtr layer = QgsOgrLayer::CreateForLayer(
                                         iter.key(), layerName, ds, hLayer );
          ds->setLayers[layerName] = layer.get();
          return layer;
        }
      }
    }
  }
  return getLayer( dsName, false, QStringList(), layerName, errCause, true );
}

static QDateTime getLastModified( const QString &dsName )
{
  if ( dsName.endsWith( ".gpkg", Qt::CaseInsensitive ) )
  {
    QFileInfo info( dsName + "-wal" );
    if ( info.exists() )
      return info.lastModified();
  }
  return QFileInfo( dsName ).lastModified();
}

// In case we do very fast structural changes within the same second,
// the last modified date might not change enough, so artificially
// decrement the cache modified date, so that the file appears newer to it
void QgsOgrProviderUtils::invalidateCachedLastModifiedDate( const QString &dsName )
{
  QMutexLocker locker( sGlobalMutex() );

  auto iter = sMapDSNameToLastModifiedDate()->find( dsName );
  if ( iter != sMapDSNameToLastModifiedDate()->end() )
  {
    QgsDebugMsgLevel( QStringLiteral( "invalidating last modified date for %1" ).arg( dsName ), 1 );
    iter.value() = iter.value().addSecs( -10 );
  }
}

OGRwkbGeometryType QgsOgrProviderUtils::ogrTypeFromQgisType( QgsWkbTypes::Type type )
{
  switch ( type )
  {
    case QgsWkbTypes::Point:
      return wkbPoint;
    case QgsWkbTypes::Point25D:
    case QgsWkbTypes::PointZ:
      return wkbPoint25D;
    case QgsWkbTypes::PointM:
      return wkbPointM;
    case QgsWkbTypes::PointZM:
      return wkbPointZM;

    case QgsWkbTypes::LineString:
      return wkbLineString;
    case QgsWkbTypes::LineString25D:
    case QgsWkbTypes::LineStringZ:
      return wkbLineString25D;
    case QgsWkbTypes::LineStringM:
      return wkbLineStringM;
    case QgsWkbTypes::LineStringZM:
      return wkbLineStringZM;

    case QgsWkbTypes::Polygon:
      return wkbPolygon;
    case QgsWkbTypes::Polygon25D:
    case QgsWkbTypes::PolygonZ:
      return wkbPolygon25D;
    case QgsWkbTypes::PolygonM:
      return wkbPolygonM;
    case QgsWkbTypes::PolygonZM:
      return wkbPolygonZM;

    case QgsWkbTypes::MultiPoint:
      return wkbMultiPoint;
    case QgsWkbTypes::MultiPoint25D:
    case QgsWkbTypes::MultiPointZ:
      return wkbMultiPoint25D;
    case QgsWkbTypes::MultiPointM:
      return wkbMultiPointM;
    case QgsWkbTypes::MultiPointZM:
      return wkbMultiPointZM;

    case QgsWkbTypes::MultiLineString:
      return wkbMultiLineString;
    case QgsWkbTypes::MultiLineString25D:
    case QgsWkbTypes::MultiLineStringZ:
      return wkbMultiLineString25D;
    case QgsWkbTypes::MultiLineStringM:
      return wkbMultiLineStringM;
    case QgsWkbTypes::MultiLineStringZM:
      return wkbMultiLineStringZM;

    case QgsWkbTypes::MultiPolygon:
      return wkbMultiPolygon;
    case QgsWkbTypes::MultiPolygon25D:
    case QgsWkbTypes::MultiPolygonZ:
      return wkbMultiPolygon25D;
    case QgsWkbTypes::MultiPolygonM:
      return wkbMultiPolygonM;
    case QgsWkbTypes::MultiPolygonZM:
      return wkbMultiPolygonZM;

    case QgsWkbTypes::CircularString:
      return wkbCircularString;
    case QgsWkbTypes::CircularStringZ:
      return wkbCircularStringZ;
    case QgsWkbTypes::CircularStringM:
      return wkbCircularStringM;
    case QgsWkbTypes::CircularStringZM:
      return wkbCircularStringZM;

    case QgsWkbTypes::CompoundCurve:
      return wkbCompoundCurve;
    case QgsWkbTypes::CompoundCurveZ:
      return wkbCompoundCurveZ;
    case QgsWkbTypes::CompoundCurveM:
      return wkbCompoundCurveM;
    case QgsWkbTypes::CompoundCurveZM:
      return wkbCompoundCurveZM;

    case QgsWkbTypes::CurvePolygon:
      return wkbCurvePolygon;
    case QgsWkbTypes::CurvePolygonZ:
      return wkbCurvePolygonZ;
    case QgsWkbTypes::CurvePolygonM:
      return wkbCurvePolygonM;
    case QgsWkbTypes::CurvePolygonZM:
      return wkbCurvePolygonZM;

    case QgsWkbTypes::MultiCurve:
      return wkbMultiCurve;
    case QgsWkbTypes::MultiCurveZ:
      return wkbMultiCurveZ;
    case QgsWkbTypes::MultiCurveM:
      return wkbMultiCurveM;
    case QgsWkbTypes::MultiCurveZM:
      return wkbMultiCurveZM;

    case QgsWkbTypes::MultiSurface:
      return wkbMultiSurface;
    case QgsWkbTypes::MultiSurfaceZ:
      return wkbMultiSurfaceZ;
    case QgsWkbTypes::MultiSurfaceM:
      return wkbMultiSurfaceM;
    case QgsWkbTypes::MultiSurfaceZM:
      return wkbMultiSurfaceZM;

    case QgsWkbTypes::Triangle:
      return wkbTriangle;
    case QgsWkbTypes::TriangleZ:
      return wkbTriangleZ;
    case QgsWkbTypes::TriangleM:
      return wkbTriangleM;
    case QgsWkbTypes::TriangleZM:
      return wkbTriangleZM;

    case QgsWkbTypes::NoGeometry:
      return wkbNone;

    case QgsWkbTypes::GeometryCollection:
      return wkbGeometryCollection;
    case QgsWkbTypes::GeometryCollectionZ:
      return wkbGeometryCollection25D;
    case QgsWkbTypes::GeometryCollectionM:
      return wkbGeometryCollectionM;
    case QgsWkbTypes::GeometryCollectionZM:
      return wkbGeometryCollectionZM;

    case QgsWkbTypes::Unknown:
      return wkbUnknown;
  }
  // no warnings!
  return wkbUnknown;
}

QgsWkbTypes::Type QgsOgrProviderUtils::qgisTypeFromOgrType( OGRwkbGeometryType type )
{
  switch ( type )
  {
    case wkbUnknown:
      return QgsWkbTypes::Unknown;
    case wkbPoint:
      return QgsWkbTypes::Point;
    case wkbLineString:
      return QgsWkbTypes::LineString;
    case wkbPolygon:
      return QgsWkbTypes::Polygon;
    case wkbMultiPoint:
      return QgsWkbTypes::MultiPoint;
    case wkbMultiLineString:
      return QgsWkbTypes::MultiLineString;
    case wkbMultiPolygon:
      return QgsWkbTypes::MultiPolygon;
    case wkbGeometryCollection:
      return QgsWkbTypes::GeometryCollection;
    case wkbCircularString:
      return QgsWkbTypes::CircularString;
    case wkbCompoundCurve:
      return QgsWkbTypes::CompoundCurve;
    case wkbCurvePolygon:
      return QgsWkbTypes::CurvePolygon;
    case wkbMultiCurve:
      return QgsWkbTypes::MultiCurve;
    case wkbMultiSurface:
      return QgsWkbTypes::MultiSurface;
    case wkbTriangle:
      return QgsWkbTypes::Triangle;
    case wkbNone:
      return QgsWkbTypes::NoGeometry;

    case wkbCircularStringZ:
      return QgsWkbTypes::CircularStringZ;
    case wkbCompoundCurveZ:
      return QgsWkbTypes::CompoundCurveZ;
    case wkbCurvePolygonZ:
      return QgsWkbTypes::PolygonZ;
    case wkbMultiCurveZ:
      return QgsWkbTypes::MultiCurveZ;
    case wkbMultiSurfaceZ:
      return QgsWkbTypes::MultiSurfaceZ;
    case wkbTriangleZ:
      return QgsWkbTypes::TriangleZ;

    case wkbPointM:
      return QgsWkbTypes::PointM;
    case wkbLineStringM:
      return QgsWkbTypes::LineStringM;
    case wkbPolygonM:
      return QgsWkbTypes::PolygonM;
    case wkbMultiPointM:
      return QgsWkbTypes::PointM;
    case wkbMultiLineStringM:
      return QgsWkbTypes::LineStringM;
    case wkbMultiPolygonM:
      return QgsWkbTypes::PolygonM;
    case wkbGeometryCollectionM:
      return QgsWkbTypes::GeometryCollectionM;
    case wkbCircularStringM:
      return QgsWkbTypes::CircularStringM;
    case wkbCompoundCurveM:
      return QgsWkbTypes::CompoundCurveM;
    case wkbCurvePolygonM:
      return QgsWkbTypes::PolygonM;
    case wkbMultiCurveM:
      return QgsWkbTypes::MultiCurveM;
    case wkbMultiSurfaceM:
      return QgsWkbTypes::MultiSurfaceM;
    case wkbTriangleM:
      return QgsWkbTypes::TriangleM;

    case wkbPointZM:
      return QgsWkbTypes::PointZM;
    case wkbLineStringZM:
      return QgsWkbTypes::LineStringZM;
    case wkbPolygonZM:
      return QgsWkbTypes::PolygonZM;
    case wkbMultiPointZM:
      return QgsWkbTypes::MultiPointZM;
    case wkbMultiLineStringZM:
      return QgsWkbTypes::MultiLineStringZM;
    case wkbMultiPolygonZM:
      return QgsWkbTypes::MultiPolygonZM;
    case wkbGeometryCollectionZM:
      return QgsWkbTypes::GeometryCollectionZM;
    case wkbCircularStringZM:
      return QgsWkbTypes::CircularStringZM;
    case wkbCompoundCurveZM:
      return QgsWkbTypes::CompoundCurveZM;
    case wkbCurvePolygonZM:
      return QgsWkbTypes::CurvePolygonZM;
    case wkbMultiCurveZM:
      return QgsWkbTypes::MultiCurveZM;
    case wkbMultiSurfaceZM:
      return QgsWkbTypes::MultiSurfaceZM;
    case wkbTriangleZM:
      return QgsWkbTypes::TriangleZM;

    case wkbPoint25D:
      return QgsWkbTypes::Point25D;
    case wkbLineString25D:
      return QgsWkbTypes::LineString25D;
    case wkbPolygon25D:
      return QgsWkbTypes::Polygon25D;
    case wkbMultiPoint25D:
      return QgsWkbTypes::MultiPoint25D;
    case wkbMultiLineString25D:
      return QgsWkbTypes::MultiLineString25D;
    case wkbMultiPolygon25D:
      return QgsWkbTypes::MultiPolygon25D;
    case wkbGeometryCollection25D:
      return QgsWkbTypes::GeometryCollectionZ;

    case wkbCurve:
    case wkbSurface:
    case wkbCurveZ:
    case wkbSurfaceZ:
    case wkbCurveM:
    case wkbSurfaceM:
    case wkbCurveZM:
    case wkbSurfaceZM:
      return QgsWkbTypes::Unknown; // abstract types - no direct mapping to QGIS types

    case wkbLinearRing:
    case wkbTIN:
    case wkbTINZ:
    case wkbTINM:
    case wkbTINZM:
    case wkbPolyhedralSurface:
    case wkbPolyhedralSurfaceZ:
    case wkbPolyhedralSurfaceM:
    case wkbPolyhedralSurfaceZM:
      return QgsWkbTypes::Unknown; // unsupported types
  }
  return QgsWkbTypes::Unknown;
}


QString QgsOgrProviderUtils::expandAuthConfig( const QString &dsName )
{
  QString uri( dsName );
  // Check for authcfg
  QRegularExpression authcfgRe( " authcfg='([^']+)'" );
  QRegularExpressionMatch match;
  if ( uri.contains( authcfgRe, &match ) )
  {
    uri = uri.replace( match.captured( 0 ), QString() );
    QString configId( match.captured( 1 ) );
    QStringList connectionItems;
    connectionItems << uri;
    if ( QgsApplication::authManager()->updateDataSourceUriItems( connectionItems, configId, QStringLiteral( "ogr" ) ) )
    {
      uri = connectionItems.first( );
    }
  }
  return uri;
}

// Must be called under the globalMutex
bool QgsOgrProviderUtils::canUseOpenedDatasets( const QString &dsName )
{
  auto iter = sMapDSNameToLastModifiedDate()->find( dsName );
  if ( iter == sMapDSNameToLastModifiedDate()->end() )
    return true;
  return getLastModified( dsName ) <= iter.value();
}

QgsOgrProviderUtils::DatasetWithLayers *QgsOgrProviderUtils::createDatasetWithLayers(
  const QString &dsName,
  bool updateMode,
  const QStringList &options,
  const QString &layerName,
  const DatasetIdentification &ident,
  QgsOgrLayerUniquePtr &layer,
  QString &errCause )
{
  GDALDatasetH hDS = OpenHelper( dsName, updateMode, options );
  if ( !hDS )
  {
    errCause = QObject::tr( "Cannot open %1." ).arg( dsName );
    return nullptr;
  }
  ( *sMapDSNameToLastModifiedDate() )[dsName] = getLastModified( dsName );

  OGRLayerH hLayer = GDALDatasetGetLayerByName(
                       hDS, layerName.toUtf8().constData() );
  if ( !hLayer )
  {
    errCause = QObject::tr( "Cannot find layer %1." ).arg( layerName );
    QgsOgrProviderUtils::GDALCloseWrapper( hDS );
    return nullptr;
  }

  QgsOgrProviderUtils::DatasetWithLayers *ds =
    new QgsOgrProviderUtils::DatasetWithLayers;
  ds->hDS = hDS;

  GDALDriverH driver = GDALGetDatasetDriver( hDS );
  QString driverName = GDALGetDriverShortName( driver );
  ds->canBeShared = canDriverShareSameDatasetAmongLayers( driverName, updateMode, dsName );

  layer = QgsOgrLayer::CreateForLayer(
            ident, layerName, ds, hLayer );
  ds->setLayers[layerName] = layer.get();
  return ds;
}

QgsOgrLayerUniquePtr QgsOgrProviderUtils::getLayer( const QString &dsName,
    bool updateMode,
    const QStringList &options,
    const QString &layerName,
    QString &errCause,
    bool checkModificationDateAgainstCache )
{
  QMutexLocker locker( sGlobalMutex() );

  // The idea is that we want to minimize the number of GDALDatasetH
  // handles openeded. But we have constraints. We do not want that 2
  // callers of getLayer() with the same input parameters get the same
  // GDALDatasetH since iterators over features of that layer would conflict

  QgsOgrProviderUtils::DatasetIdentification ident;
  ident.dsName = dsName;
  ident.updateMode = updateMode;
  ident.options = options;
  // Find if there's a list of DatasetWithLayers* that match our
  // (dsName, updateMode, options) criteria
  auto iter = sMapSharedDS.find( ident );
  if ( iter != sMapSharedDS.end() )
  {
    if ( checkModificationDateAgainstCache && !canUseOpenedDatasets( dsName ) )
    {
      QgsDebugMsg( QStringLiteral( "Cannot reuse existing opened dataset(s) on %1 since it has been modified" ).arg( dsName ) );
      invalidateCachedDatasets( dsName );
      iter = sMapSharedDS.find( ident );
      Q_ASSERT( iter == sMapSharedDS.end() );
    }
  }
  if ( iter != sMapSharedDS.end() )
  {
    // Browse through this list, to look for a DatasetWithLayers*
    // instance that don't use yet our layer of interest
    auto &datasetList = iter.value();
    const auto constDatasetList = datasetList;
    for ( QgsOgrProviderUtils::DatasetWithLayers *ds : constDatasetList )
    {
      if ( !ds->canBeShared )
        continue;
      Q_ASSERT( ds->refCount > 0 );

      auto iter2 = ds->setLayers.find( layerName );
      if ( iter2 == ds->setLayers.end() )
      {
        OGRLayerH hLayer;
        {
          QMutexLocker lockerDS( &ds->mutex );
          hLayer = GDALDatasetGetLayerByName(
                     ds->hDS, layerName.toUtf8().constData() );
        }
        if ( !hLayer )
        {
          // Shouldn't happen really !
          errCause = QObject::tr( "Cannot find layer %1." ).arg( layerName );
          return nullptr;
        }
        OGR_L_SetAttributeFilter( hLayer, nullptr );

        QgsOgrLayerUniquePtr layer = QgsOgrLayer::CreateForLayer(
                                       ident, layerName, ds, hLayer );
        ds->setLayers[layerName] = layer.get();
        return layer;
      }
    }

    // All existing DatasetWithLayers* already reference our layer of
    // interest, so instantiate a new DatasetWithLayers*
    QgsOgrLayerUniquePtr layer;
    QgsOgrProviderUtils::DatasetWithLayers *ds =
      createDatasetWithLayers( dsName, updateMode, options, layerName, ident, layer, errCause );
    if ( !ds )
      return nullptr;

    datasetList.push_back( ds );

    return layer;
  }

  QgsOgrLayerUniquePtr layer;
  QgsOgrProviderUtils::DatasetWithLayers *ds =
    createDatasetWithLayers( dsName, updateMode, options, layerName, ident, layer, errCause );
  if ( !ds )
    return nullptr;

  QList<DatasetWithLayers *> datasetList;
  datasetList.push_back( ds );
  sMapSharedDS[ident] = datasetList;

  return layer;
}

QgsOgrLayerUniquePtr QgsOgrProviderUtils::getSqlLayer( QgsOgrLayer *baseLayer,
    OGRLayerH hSqlLayer,
    const QString &sql )
{
  QgsOgrProviderUtils::DatasetIdentification ident;
  ident.dsName = baseLayer->datasetName();
  ident.updateMode = baseLayer->updateMode();
  ident.options = baseLayer->options();
  return QgsOgrLayer::CreateForSql( ident, sql, baseLayer->ds, hSqlLayer );
}

void QgsOgrProviderUtils::releaseInternal( const DatasetIdentification &ident,
    DatasetWithLayers *ds,
    bool removeFromDatasetList )
{

  ds->refCount --;
  if ( ds->refCount == 0 )
  {
    Q_ASSERT( ds->setLayers.isEmpty() );

    if ( removeFromDatasetList )
    {
      auto iter = sMapSharedDS.find( ident );
      if ( iter != sMapSharedDS.end() )
      {
        auto &datasetList = iter.value();
        int i = 0;

        // Normally there should be a match, except for datasets that
        // have been invalidated
        const auto constDatasetList = datasetList;
        for ( QgsOgrProviderUtils::DatasetWithLayers *dsIter : constDatasetList )
        {
          if ( dsIter == ds )
          {
            datasetList.removeAt( i );
            break;
          }
          i ++;
        }

        if ( datasetList.isEmpty() )
          sMapSharedDS.erase( iter );
      }
    }
    QgsOgrProviderUtils::GDALCloseWrapper( ds->hDS );
    delete ds;
  }
}

void QgsOgrProviderUtils::release( QgsOgrLayer *&layer )
{
  if ( !layer )
    return;

  QMutexLocker locker( sGlobalMutex() );

  if ( !layer->isSqlLayer )
  {
    layer->ds->setLayers.remove( layer->layerName );
  }
  else
  {
    QMutexLocker lockerDS( &layer->ds->mutex );
    GDALDatasetReleaseResultSet( layer->ds->hDS, layer->hLayer );
  }

  releaseInternal( layer->ident, layer->ds,  !layer->isSqlLayer );

  delete layer;
  layer = nullptr;
}


void QgsOgrProviderUtils::releaseDataset( QgsOgrDataset *&ds )
{
  if ( !ds )
    return;

  QMutexLocker locker( sGlobalMutex() );
  releaseInternal( ds->mIdent, ds->mDs, true );
  delete ds;
  ds = nullptr;
}

bool QgsOgrProviderUtils::canDriverShareSameDatasetAmongLayers( const QString &driverName )
{
  return driverName != QLatin1String( "OSM" );
}

bool QgsOgrProviderUtils::canDriverShareSameDatasetAmongLayers( const QString &driverName,
    bool updateMode,
    const QString &dsName )
{
  // For .shp.zip with multiple layers, in update mode, we want that each
  // layer has its own dataset, so that when its gets closed and reopened,
  // the .shp.zip is updated. Otherwise if we share the same dataset, the .shp.zip
  // would only be updated when all layers are unloaded, and thus readers will see
  // an outdated version of the .shp.zip. This works only if editing operations are
  // done separately on layers (which is how it works from the GUI)
  return canDriverShareSameDatasetAmongLayers( driverName ) &&
         !( updateMode && dsName.endsWith( QLatin1String( ".shp.zip" ), Qt::CaseInsensitive ) );
}


QgsOgrDatasetSharedPtr QgsOgrDataset::create( const QgsOgrProviderUtils::DatasetIdentification &ident,
    QgsOgrProviderUtils::DatasetWithLayers *ds )
{
  QgsOgrDatasetSharedPtr dsRet = QgsOgrDatasetSharedPtr( new QgsOgrDataset(), QgsOgrProviderUtils::releaseDataset );
  dsRet->mIdent = ident;
  dsRet->mDs = ds;
  dsRet->mDs->refCount ++;
  return dsRet;
}

bool QgsOgrDataset::executeSQLNoReturn( const QString &sql )
{
  QMutexLocker locker( &mutex() );
  CPLErrorReset();
  OGRLayerH hSqlLayer = GDALDatasetExecuteSQL(
                          mDs->hDS, sql.toUtf8().constData(), nullptr, nullptr );
  bool ret = CPLGetLastErrorType() == CE_None;
  GDALDatasetReleaseResultSet( mDs->hDS, hSqlLayer );
  return ret;
}


OGRLayerH QgsOgrDataset::getLayerFromNameOrIndex( const QString &layerName, int layerIndex )
{
  QMutexLocker locker( &mutex() );

  OGRLayerH layer;
  if ( !layerName.isEmpty() )
  {
    layer = GDALDatasetGetLayerByName( mDs->hDS, layerName.toUtf8().constData() );
  }
  else
  {
    layer = GDALDatasetGetLayer( mDs->hDS, layerIndex );
  }
  return layer;
}

void QgsOgrDataset::releaseResultSet( OGRLayerH hSqlLayer )
{
  QMutexLocker locker( &mutex() );
  GDALDatasetReleaseResultSet( mDs->hDS, hSqlLayer );
}

QgsOgrLayer::QgsOgrLayer()
{
  oFDefn.layer = this;
}

QgsOgrLayerUniquePtr QgsOgrLayer::CreateForLayer(
  const QgsOgrProviderUtils::DatasetIdentification &ident,
  const QString &layerName,
  QgsOgrProviderUtils::DatasetWithLayers *ds,
  OGRLayerH hLayer )
{
  QgsOgrLayerUniquePtr layer( new QgsOgrLayer() );
  layer->ident = ident;
  layer->isSqlLayer = false;
  layer->layerName = layerName;
  layer->ds = ds;
  layer->hLayer = hLayer;
  {
    QMutexLocker locker( &ds->mutex );
    OGR_L_ResetReading( hLayer );
  }
  ds->refCount ++;
  return layer;
}

QgsOgrLayerUniquePtr QgsOgrLayer::CreateForSql(
  const QgsOgrProviderUtils::DatasetIdentification &ident,
  const QString &sql,
  QgsOgrProviderUtils::DatasetWithLayers *ds,
  OGRLayerH hLayer )
{
  QgsOgrLayerUniquePtr layer( new QgsOgrLayer() );
  layer->ident = ident;
  layer->isSqlLayer = true;
  layer->sql = sql;
  layer->ds = ds;
  layer->hLayer = hLayer;
  {
    QMutexLocker locker( &ds->mutex );
    OGR_L_ResetReading( hLayer );
  }
  ds->refCount ++;
  return layer;
}

int QgsOgrLayer::GetLayerCount()
{
  QMutexLocker locker( &ds->mutex );
  return GDALDatasetGetLayerCount( ds->hDS );
}

GDALDriverH QgsOgrLayer::driver()
{
  return GDALGetDatasetDriver( ds->hDS );
}

QString  QgsOgrLayer::driverName()
{
  return QString::fromUtf8( GDALGetDriverShortName( GDALGetDatasetDriver( ds->hDS ) ) );
}

QByteArray QgsOgrLayer::name()
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_GetName( hLayer );
}

void QgsOgrLayer::ResetReading()
{
  QMutexLocker locker( &ds->mutex );
  OGR_L_ResetReading( hLayer );
}

QByteArray QgsOgrLayer::GetFIDColumn()
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_GetFIDColumn( hLayer );
}

OGRSpatialReferenceH QgsOgrLayer::GetSpatialRef()
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_GetSpatialRef( hLayer );
}

OGRFeatureH QgsOgrLayer::GetNextFeature()
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_GetNextFeature( hLayer );
}

OGRFeatureH QgsOgrLayer::GetFeature( GIntBig fid )
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_GetFeature( hLayer, fid );
}

QgsOgrFeatureDefn &QgsOgrLayer::GetLayerDefn()
{
  return oFDefn;
}

GIntBig QgsOgrLayer::GetFeatureCount( bool force )
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_GetFeatureCount( hLayer, force );
}

GIntBig QgsOgrLayer::GetApproxFeatureCount()
{
  QMutexLocker locker( &ds->mutex );

  // OGR_L_GetFeatureCount() can be super slow on huge geopackage files
  // so implement some approximation strategy that has reasonable runtime.
  QString driverName = GDALGetDriverShortName( GDALGetDatasetDriver( ds->hDS ) );
  if ( driverName == QLatin1String( "GPKG" ) )
  {
    CPLPushErrorHandler( CPLQuietErrorHandler );
    OGRLayerH hSqlLayer = GDALDatasetExecuteSQL(
                            ds->hDS, "SELECT 1 FROM gpkg_ogr_contents LIMIT 0", nullptr, nullptr );
    CPLPopErrorHandler();
    if ( hSqlLayer )
    {
      GDALDatasetReleaseResultSet( ds->hDS, hSqlLayer );
      return OGR_L_GetFeatureCount( hLayer, TRUE );
    }

    // Enumerate features up to a limit of 100000.
    const GIntBig nLimit = CPLAtoGIntBig(
                             CPLGetConfigOption( "QGIS_GPKG_FC_THRESHOLD", "100000" ) );
    QByteArray layerName = OGR_L_GetName( hLayer );
    QByteArray sql( "SELECT COUNT(*) FROM (SELECT 1 FROM " );
    sql += QgsOgrProviderUtils::quotedIdentifier( layerName, driverName );
    sql += " LIMIT ";
    sql += CPLSPrintf( CPL_FRMT_GIB, nLimit );
    sql += ")";
    hSqlLayer = GDALDatasetExecuteSQL( ds->hDS, sql, nullptr, nullptr );
    GIntBig res = -1;
    if ( hSqlLayer )
    {
      gdal::ogr_feature_unique_ptr fet( OGR_L_GetNextFeature( hSqlLayer ) );
      if ( fet )
      {
        res = OGR_F_GetFieldAsInteger64( fet.get(), 0 );
      }
      GDALDatasetReleaseResultSet( ds->hDS, hSqlLayer );
    }
    if ( res >= 0 && res < nLimit )
    {
      // Less than 100000 features ? This is the final count
      return res;
    }
    if ( res == nLimit )
    {
      // If we reach the threshold, then use the min and max values of the rowid
      // hoping there are not a lot of holes.
      // Do it in 2 separate SQL queries otherwise SQLite apparently does a
      // full table scan...
      sql = "SELECT MAX(ROWID) FROM ";
      sql += QgsOgrProviderUtils::quotedIdentifier( layerName, driverName );
      hSqlLayer = GDALDatasetExecuteSQL( ds->hDS, sql, nullptr, nullptr );
      GIntBig maxrowid = -1;
      if ( hSqlLayer )
      {
        gdal::ogr_feature_unique_ptr fet( OGR_L_GetNextFeature( hSqlLayer ) );
        if ( fet )
        {
          maxrowid = OGR_F_GetFieldAsInteger64( fet.get(), 0 );
        }
        GDALDatasetReleaseResultSet( ds->hDS, hSqlLayer );
      }

      sql = "SELECT MIN(ROWID) FROM ";
      sql += QgsOgrProviderUtils::quotedIdentifier( layerName, driverName );
      hSqlLayer = GDALDatasetExecuteSQL( ds->hDS, sql, nullptr, nullptr );
      GIntBig minrowid = 0;
      if ( hSqlLayer )
      {
        gdal::ogr_feature_unique_ptr fet( OGR_L_GetNextFeature( hSqlLayer ) );
        if ( fet )
        {
          minrowid = OGR_F_GetFieldAsInteger64( fet.get(), 0 );
        }
        GDALDatasetReleaseResultSet( ds->hDS, hSqlLayer );
      }

      if ( maxrowid >= minrowid )
      {
        return maxrowid - minrowid + 1;
      }
    }
  }
  if ( driverName == QLatin1String( "OAPIF" ) || driverName == QLatin1String( "OAPIF" ) )
  {
    return -1;
  }

  return OGR_L_GetFeatureCount( hLayer, TRUE );
}

#if GDAL_VERSION_NUM < GDAL_COMPUTE_VERSION(2,4,0)
static bool findMinOrMax( GDALDatasetH hDS, const QByteArray &rtreeName,
                          const char *varName, bool isMin, double &val )
{
  // We proceed by dichotomic search since unfortunately SELECT MIN(minx)
  // in a RTree is a slow operation
  double minval = -1e10;
  double maxval = 1e10;
  val = 0.0;
  double oldval = 0.0;
  for ( int i = 0; i < 100 && maxval - minval > 1e-15; i++ )
  {
    val = ( minval + maxval ) / 2;
    if ( i > 0 && val == oldval )
    {
      break;
    }
    oldval = val;
    QByteArray sql = "SELECT 1 FROM ";
    sql += rtreeName;
    sql += " WHERE ";
    sql += varName;
    sql += isMin ? " < " : " > ";
    sql += CPLSPrintf( "%.18g", val );
    sql += " LIMIT 1";
    auto hSqlLayer = GDALDatasetExecuteSQL(
                       hDS, sql, nullptr, nullptr );
    GIntBig count = -1;
    if ( hSqlLayer )
    {
      count = OGR_L_GetFeatureCount( hSqlLayer, true );
      GDALDatasetReleaseResultSet( hDS, hSqlLayer );
    }
    if ( count < 0 )
    {
      return false;
    }
    if ( ( isMin && count == 0 ) || ( !isMin && count == 1 ) )
    {
      minval = val;
    }
    else
    {
      maxval = val;
    }
  }
  return true;
}
#endif

OGRErr QgsOgrLayer::GetExtent( OGREnvelope *psExtent, bool bForce )
{
  QMutexLocker locker( &ds->mutex );

#if GDAL_VERSION_NUM < GDAL_COMPUTE_VERSION(2,4,0)
  // OGR_L_GetExtent() can be super slow on huge geopackage files
  // so implement some approximation strategy that has reasonable runtime.
  // Actually this should return a rather accurante answer.
  QString driverName = GDALGetDriverShortName( GDALGetDatasetDriver( ds->hDS ) );
  if ( driverName == QLatin1String( "GPKG" ) )
  {
    QByteArray layerName = OGR_L_GetName( hLayer );
    QByteArray rtreeName =
      QgsOgrProviderUtils::quotedIdentifier( "rtree_" + layerName + "_" + OGR_L_GetGeometryColumn( hLayer ), driverName );

    // Check if there is a non-empty RTree
    QByteArray sql( "SELECT 1 FROM " );
    sql += rtreeName;
    sql += " LIMIT 1";
    CPLPushErrorHandler( CPLQuietErrorHandler );
    OGRLayerH hSqlLayer = GDALDatasetExecuteSQL(
                            ds->hDS, sql, nullptr, nullptr );
    CPLPopErrorHandler();
    if ( !hSqlLayer )
    {
      return OGR_L_GetExtent( hLayer, psExtent, bForce );
    }
    bool hasFeatures = OGR_L_GetFeatureCount( hSqlLayer, true ) > 0;
    GDALDatasetReleaseResultSet( ds->hDS, hSqlLayer );
    if ( !hasFeatures )
    {
      return OGRERR_FAILURE;
    }

    double minx, miny, maxx, maxy;
    if ( findMinOrMax( ds->hDS, rtreeName, "MINX", true, minx ) &&
         findMinOrMax( ds->hDS, rtreeName, "MINY", true, miny ) &&
         findMinOrMax( ds->hDS, rtreeName, "MAXX", false, maxx ) &&
         findMinOrMax( ds->hDS, rtreeName, "MAXY", false, maxy ) )
    {
      psExtent->MinX = minx;
      psExtent->MinY = miny;
      psExtent->MaxX = maxx;
      psExtent->MaxY = maxy;
      return OGRERR_NONE;
    }
  }
#endif

  return OGR_L_GetExtent( hLayer, psExtent, bForce );
}

OGRGeometryH QgsOgrLayer::GetSpatialFilter()
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_GetSpatialFilter( hLayer );
}

void QgsOgrLayer::SetSpatialFilter( OGRGeometryH hGeometry )
{
  QMutexLocker locker( &ds->mutex );
  OGR_L_SetSpatialFilter( hLayer, hGeometry );
}

GDALDatasetH QgsOgrLayer::getDatasetHandleAndMutex( QMutex *&mutex )
{
  mutex = &( ds->mutex );
  return ds->hDS;
}

OGRLayerH QgsOgrLayer::getHandleAndMutex( QMutex *&mutex )
{
  mutex = &( ds->mutex );
  return hLayer;
}

OGRErr QgsOgrLayer::CreateFeature( OGRFeatureH hFeature )
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_CreateFeature( hLayer, hFeature );
}

OGRErr QgsOgrLayer::SetFeature( OGRFeatureH hFeature )
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_SetFeature( hLayer, hFeature );
}

OGRErr QgsOgrLayer::DeleteFeature( GIntBig fid )
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_DeleteFeature( hLayer, fid );
}

OGRErr QgsOgrLayer::CreateField( OGRFieldDefnH hFieldDefn, bool bStrict )
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_CreateField( hLayer, hFieldDefn, bStrict );
}

OGRErr QgsOgrLayer::DeleteField( int iField )
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_DeleteField( hLayer, iField );
}

OGRErr QgsOgrLayer::AlterFieldDefn( int iField, OGRFieldDefnH hNewFieldDefn, int flags )
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_AlterFieldDefn( hLayer, iField, hNewFieldDefn, flags );
}

int QgsOgrLayer::TestCapability( const char *cap )
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_TestCapability( hLayer, cap );
}

OGRErr QgsOgrLayer::StartTransaction()
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_StartTransaction( hLayer );
}

OGRErr QgsOgrLayer::CommitTransaction()
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_CommitTransaction( hLayer );
}

OGRErr QgsOgrLayer::RollbackTransaction()
{
  QMutexLocker locker( &ds->mutex );
  return OGR_L_RollbackTransaction( hLayer );
}

OGRErr QgsOgrLayer::SyncToDisk()
{
  QMutexLocker locker( &ds->mutex );

  OGRErr eErr;
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(3,1,0) && GDAL_VERSION_NUM <= GDAL_COMPUTE_VERSION(3,1,3)
  // Workaround bug in GDAL 3.1.0 to 3.1.3 that creates XLSX and ODS files incompatible with LibreOffice due to use of ZIP64
  QString drvName = GDALGetDriverShortName( GDALGetDatasetDriver( ds->hDS ) );
  if ( drvName == QLatin1String( "XLSX" ) ||
       drvName == QLatin1String( "ODS" ) )
  {
    CPLSetThreadLocalConfigOption( "CPL_CREATE_ZIP64", "NO" );
    eErr = OGR_L_SyncToDisk( hLayer );
    CPLSetThreadLocalConfigOption( "CPL_CREATE_ZIP64", nullptr );
  }
  else
#endif
  {
    eErr = OGR_L_SyncToDisk( hLayer );
  }

  return eErr;
}

void QgsOgrLayer::ExecuteSQLNoReturn( const QByteArray &sql )
{
  QMutexLocker locker( &ds->mutex );
  OGRLayerH hSqlLayer = GDALDatasetExecuteSQL( ds->hDS,
                        sql.constData(),
                        nullptr,
                        nullptr );
  GDALDatasetReleaseResultSet( ds->hDS, hSqlLayer );
}

QgsOgrLayerUniquePtr QgsOgrLayer::ExecuteSQL( const QByteArray &sql )
{
  QMutexLocker locker( &ds->mutex );
  OGRLayerH hSqlLayer = GDALDatasetExecuteSQL( ds->hDS,
                        sql.constData(),
                        nullptr,
                        nullptr );
  if ( !hSqlLayer )
    return nullptr;

  return QgsOgrLayer::CreateForSql( ident,
                                    QString::fromUtf8( sql ),
                                    ds,
                                    hSqlLayer );
}

QString QgsOgrLayer::GetMetadataItem( const QString &key, const QString &domain )
{
  QMutexLocker locker( &ds->mutex );
  return GDALGetMetadataItem( hLayer, key.toUtf8().constData(),
                              domain.toUtf8().constData() );
}

QMutex &QgsOgrFeatureDefn::mutex()
{
  return layer->mutex();
}

OGRFeatureDefnH QgsOgrFeatureDefn::get()
{
  if ( !hDefn )
  {
    QMutexLocker locker( &mutex() );
    hDefn = OGR_L_GetLayerDefn( layer->hLayer );
  }
  return hDefn;
}


int QgsOgrFeatureDefn::GetFieldCount()
{
  QMutexLocker locker( &mutex() );
  return OGR_FD_GetFieldCount( get() );
}

OGRFieldDefnH QgsOgrFeatureDefn::GetFieldDefn( int idx )
{
  QMutexLocker locker( &mutex() );
  return OGR_FD_GetFieldDefn( get(), idx );
}

int QgsOgrFeatureDefn::GetFieldIndex( const QByteArray &name )
{
  QMutexLocker locker( &mutex() );
  return OGR_FD_GetFieldIndex( get(), name.constData() );
}

int QgsOgrFeatureDefn::GetGeomFieldCount()
{
  QMutexLocker locker( &mutex() );
  return OGR_FD_GetGeomFieldCount( get() );
}

OGRGeomFieldDefnH  QgsOgrFeatureDefn::GetGeomFieldDefn( int idx )
{
  QMutexLocker locker( &mutex() );
  return OGR_FD_GetGeomFieldDefn( get(), idx );
}

OGRwkbGeometryType QgsOgrFeatureDefn::GetGeomType()
{
  QMutexLocker locker( &mutex() );
  return OGR_FD_GetGeomType( get() );
}

OGRFeatureH QgsOgrFeatureDefn::CreateFeature()
{
  QMutexLocker locker( &mutex() );
  return OGR_F_Create( get() );
}

// ---------------------------------------------------------------------------

static
QgsOgrLayerUniquePtr LoadDataSourceAndLayer( const QString &uri,
    QString &errCause )
{
  bool isSubLayer;
  int layerIndex;
  QString layerName;
  QString subsetString;
  OGRwkbGeometryType ogrGeometryType;
  QStringList openOptions;
  QString filePath = AnalyzeURI( uri,
                                 isSubLayer,
                                 layerIndex,
                                 layerName,
                                 subsetString,
                                 ogrGeometryType,
                                 openOptions );

  if ( !layerName.isEmpty() )
  {
    return QgsOgrProviderUtils::getLayer( filePath, true, QStringList(), layerName, errCause, true );
  }
  else
  {
    return QgsOgrProviderUtils::getLayer( filePath, true, QStringList(), layerIndex, errCause, true );
  }
}


bool QgsOgrProviderMetadata::saveStyle(
  const QString &uri, const QString &qmlStyle, const QString &sldStyle,
  const QString &styleName, const QString &styleDescription,
  const QString &uiFileContent, bool useAsDefault, QString &errCause )
{
  QgsOgrLayerUniquePtr userLayer = LoadDataSourceAndLayer( uri, errCause );
  if ( !userLayer )
    return false;

  QMutex *mutex = nullptr;
  OGRLayerH hUserLayer = userLayer->getHandleAndMutex( mutex );
  GDALDatasetH hDS = userLayer->getDatasetHandleAndMutex( mutex );
  QMutexLocker locker( mutex );

  // check if layer_styles table already exist
  OGRLayerH hLayer = GDALDatasetGetLayerByName( hDS, "layer_styles" );
  if ( !hLayer )
  {
    // if not create it
    // Note: we use the same schema as in the SpatiaLite and postgres providers
    //for cross interoperability

    char **options = nullptr;
    // TODO: might need change if other drivers than GPKG / SQLite
    options = CSLSetNameValue( options, "FID", "id" );
    hLayer = GDALDatasetCreateLayer( hDS, "layer_styles", nullptr, wkbNone, options );
    QgsOgrProviderUtils::invalidateCachedDatasets( QString::fromUtf8( GDALGetDescription( hDS ) ) );
    CSLDestroy( options );
    if ( !hLayer )
    {
      errCause = QObject::tr( "Unable to save layer style. It's not possible to create the destination table on the database." );
      return false;
    }
    bool ok = true;
    {
      gdal::ogr_field_def_unique_ptr fld( OGR_Fld_Create( "f_table_catalog", OFTString ) );
      OGR_Fld_SetWidth( fld.get(), 256 );
      ok &= OGR_L_CreateField( hLayer, fld.get(), true ) == OGRERR_NONE;
    }
    {
      gdal::ogr_field_def_unique_ptr fld( OGR_Fld_Create( "f_table_schema", OFTString ) );
      OGR_Fld_SetWidth( fld.get(), 256 );
      ok &= OGR_L_CreateField( hLayer, fld.get(), true ) == OGRERR_NONE;
    }
    {
      gdal::ogr_field_def_unique_ptr fld( OGR_Fld_Create( "f_table_name", OFTString ) );
      OGR_Fld_SetWidth( fld.get(), 256 );
      ok &= OGR_L_CreateField( hLayer, fld.get(), true ) == OGRERR_NONE;
    }
    {
      gdal::ogr_field_def_unique_ptr fld( OGR_Fld_Create( "f_geometry_column", OFTString ) );
      OGR_Fld_SetWidth( fld.get(), 256 );
      ok &= OGR_L_CreateField( hLayer, fld.get(), true ) == OGRERR_NONE;
    }
    {
      gdal::ogr_field_def_unique_ptr fld( OGR_Fld_Create( "styleName", OFTString ) );
      OGR_Fld_SetWidth( fld.get(), 30 );
      ok &= OGR_L_CreateField( hLayer, fld.get(), true ) == OGRERR_NONE;
    }
    {
      gdal::ogr_field_def_unique_ptr fld( OGR_Fld_Create( "styleQML", OFTString ) );
      ok &= OGR_L_CreateField( hLayer, fld.get(), true ) == OGRERR_NONE;
    }
    {
      gdal::ogr_field_def_unique_ptr fld( OGR_Fld_Create( "styleSLD", OFTString ) );
      ok &= OGR_L_CreateField( hLayer, fld.get(), true ) == OGRERR_NONE;
    }
    {
      gdal::ogr_field_def_unique_ptr fld( OGR_Fld_Create( "useAsDefault", OFTInteger ) );
      OGR_Fld_SetSubType( fld.get(), OFSTBoolean );
      ok &= OGR_L_CreateField( hLayer, fld.get(), true ) == OGRERR_NONE;
    }
    {
      gdal::ogr_field_def_unique_ptr fld( OGR_Fld_Create( "description", OFTString ) );
      ok &= OGR_L_CreateField( hLayer, fld.get(), true ) == OGRERR_NONE;
    }
    {
      gdal::ogr_field_def_unique_ptr fld( OGR_Fld_Create( "owner", OFTString ) );
      OGR_Fld_SetWidth( fld.get(), 30 );
      ok &= OGR_L_CreateField( hLayer, fld.get(), true ) == OGRERR_NONE;
    }
    {
      gdal::ogr_field_def_unique_ptr fld( OGR_Fld_Create( "ui", OFTString ) );
      OGR_Fld_SetWidth( fld.get(), 30 );
      ok &= OGR_L_CreateField( hLayer, fld.get(), true ) == OGRERR_NONE;
    }
    {
      gdal::ogr_field_def_unique_ptr fld( OGR_Fld_Create( "update_time", OFTDateTime ) );
      OGR_Fld_SetDefault( fld.get(), "CURRENT_TIMESTAMP" );
      ok &= OGR_L_CreateField( hLayer, fld.get(), true ) == OGRERR_NONE;
    }
    if ( !ok )
    {
      errCause = QObject::tr( "Unable to save layer style. It's not possible to create the destination table on the database." );
      return false;
    }
  }

  QString realStyleName =
    styleName.isEmpty() ? QString( OGR_L_GetName( hUserLayer ) ) : styleName;

  OGRFeatureDefnH hLayerDefn = OGR_L_GetLayerDefn( hLayer );

  if ( useAsDefault )
  {
    QString oldDefaultQuery = QStringLiteral( "useAsDefault = 1 AND f_table_schema=''"
                              " AND f_table_name=%1"
                              " AND f_geometry_column=%2" )
                              .arg( QgsOgrProviderUtils::quotedValue( QString( OGR_L_GetName( hUserLayer ) ) ) )
                              .arg( QgsOgrProviderUtils::quotedValue( QString( OGR_L_GetGeometryColumn( hUserLayer ) ) ) );
    OGR_L_SetAttributeFilter( hLayer, oldDefaultQuery.toUtf8().constData() );
    gdal::ogr_feature_unique_ptr hFeature( OGR_L_GetNextFeature( hLayer ) );
    if ( hFeature )
    {
      OGR_F_SetFieldInteger( hFeature.get(),
                             OGR_FD_GetFieldIndex( hLayerDefn, "useAsDefault" ),
                             0 );
      bool ok = OGR_L_SetFeature( hLayer, hFeature.get() ) == 0;
      if ( !ok )
      {
        QgsDebugMsg( QStringLiteral( "Could not unset previous useAsDefault style" ) );
      }
    }
  }

  QString checkQuery = QStringLiteral( "f_table_schema=''"
                                       " AND f_table_name=%1"
                                       " AND f_geometry_column=%2"
                                       " AND styleName=%3" )
                       .arg( QgsOgrProviderUtils::quotedValue( QString( OGR_L_GetName( hUserLayer ) ) ) )
                       .arg( QgsOgrProviderUtils::quotedValue( QString( OGR_L_GetGeometryColumn( hUserLayer ) ) ) )
                       .arg( QgsOgrProviderUtils::quotedValue( realStyleName ) );
  OGR_L_SetAttributeFilter( hLayer, checkQuery.toUtf8().constData() );
  OGR_L_ResetReading( hLayer );
  gdal::ogr_feature_unique_ptr hFeature( OGR_L_GetNextFeature( hLayer ) );
  bool bNew = true;

  if ( hFeature )
  {
    QgsSettings settings;
    // Only used in tests. Do not define it for interactive implication
    QVariant overwriteStyle = settings.value( QStringLiteral( "qgis/overwriteStyle" ) );
    if ( ( !overwriteStyle.isNull() && !overwriteStyle.toBool() ) ||
         ( overwriteStyle.isNull() &&
           QMessageBox::question( nullptr, QObject::tr( "Save style in database" ),
                                  QObject::tr( "A style named \"%1\" already exists in the database for this layer. Do you want to overwrite it?" )
                                  .arg( realStyleName ),
                                  QMessageBox::Yes | QMessageBox::No ) == QMessageBox::No ) )
    {
      errCause = QObject::tr( "Operation aborted" );
      return false;
    }
    bNew = false;
  }
  else
  {
    hFeature.reset( OGR_F_Create( hLayerDefn ) );
    OGR_F_SetFieldString( hFeature.get(),
                          OGR_FD_GetFieldIndex( hLayerDefn, "f_table_catalog" ),
                          "" );
    OGR_F_SetFieldString( hFeature.get(),
                          OGR_FD_GetFieldIndex( hLayerDefn, "f_table_schema" ),
                          "" );
    OGR_F_SetFieldString( hFeature.get(),
                          OGR_FD_GetFieldIndex( hLayerDefn, "f_table_name" ),
                          OGR_L_GetName( hUserLayer ) );
    OGR_F_SetFieldString( hFeature.get(),
                          OGR_FD_GetFieldIndex( hLayerDefn, "f_geometry_column" ),
                          OGR_L_GetGeometryColumn( hUserLayer ) );
    OGR_F_SetFieldString( hFeature.get(),
                          OGR_FD_GetFieldIndex( hLayerDefn, "styleName" ),
                          realStyleName.toUtf8().constData() );
    if ( !uiFileContent.isEmpty() )
    {
      OGR_F_SetFieldString( hFeature.get(),
                            OGR_FD_GetFieldIndex( hLayerDefn, "ui" ),
                            uiFileContent.toUtf8().constData() );
    }
  }
  OGR_F_SetFieldString( hFeature.get(),
                        OGR_FD_GetFieldIndex( hLayerDefn, "styleQML" ),
                        qmlStyle.toUtf8().constData() );
  OGR_F_SetFieldString( hFeature.get(),
                        OGR_FD_GetFieldIndex( hLayerDefn, "styleSLD" ),
                        sldStyle.toUtf8().constData() );
  OGR_F_SetFieldInteger( hFeature.get(),
                         OGR_FD_GetFieldIndex( hLayerDefn, "useAsDefault" ),
                         useAsDefault ? 1 : 0 );
  OGR_F_SetFieldString( hFeature.get(),
                        OGR_FD_GetFieldIndex( hLayerDefn, "description" ),
                        ( styleDescription.isEmpty() ? QDateTime::currentDateTime().toString() : styleDescription ).toUtf8().constData() );
  OGR_F_SetFieldString( hFeature.get(),
                        OGR_FD_GetFieldIndex( hLayerDefn, "owner" ),
                        "" );

  bool bFeatureOK;
  if ( bNew )
    bFeatureOK = OGR_L_CreateFeature( hLayer, hFeature.get() ) == OGRERR_NONE;
  else
    bFeatureOK = OGR_L_SetFeature( hLayer, hFeature.get() ) == OGRERR_NONE;

  if ( !bFeatureOK )
  {
    QgsMessageLog::logMessage( QObject::tr( "Error updating style" ) );
    errCause = QObject::tr( "Error looking for style. The query was logged" );
    return false;
  }

  return true;
}


bool QgsOgrProviderMetadata::deleteStyleById( const QString &uri, QString styleId, QString &errCause )
{
  QgsDataSourceUri dsUri( uri );
  bool deleted;

  QgsOgrLayerUniquePtr userLayer = LoadDataSourceAndLayer( uri, errCause );
  if ( !userLayer )
    return false;

  QMutex *mutex = nullptr;
  GDALDatasetH hDS = userLayer->getDatasetHandleAndMutex( mutex );
  QMutexLocker locker( mutex );

  // check if layer_styles table already exist
  OGRLayerH hLayer = GDALDatasetGetLayerByName( hDS, "layer_styles" );
  if ( !hLayer )
  {
    errCause = QObject::tr( "Connection to database failed: %1" ).arg( dsUri.uri() );
    deleted = false;
  }
  else
  {
    if ( OGR_L_DeleteFeature( hLayer, styleId.toInt() ) != OGRERR_NONE )
    {
      errCause = QObject::tr( "Error executing the delete query." );
      deleted = false;
    }
    else
    {
      deleted = true;
    }
  }
  return deleted;
}

static
bool LoadDataSourceLayerStylesAndLayer( const QString &uri,
                                        QgsOgrLayerUniquePtr &layerStyles,
                                        QgsOgrLayerUniquePtr &userLayer,
                                        QString &errCause )
{
  bool isSubLayer;
  int layerIndex;
  QString layerName;
  QString subsetString;
  OGRwkbGeometryType ogrGeometryType;
  QStringList openOptions;
  QString filePath = AnalyzeURI( uri,
                                 isSubLayer,
                                 layerIndex,
                                 layerName,
                                 subsetString,
                                 ogrGeometryType,
                                 openOptions );

  layerStyles =
    QgsOgrProviderUtils::getLayer( filePath, "layer_styles", errCause );
  userLayer = nullptr;
  if ( !layerStyles )
  {
    errCause = QObject::tr( "Cannot find layer_styles layer" );
    return false;
  }

  if ( !layerName.isEmpty() )
  {
    userLayer = QgsOgrProviderUtils::getLayer( filePath, layerName, errCause );
  }
  else
  {
    userLayer = QgsOgrProviderUtils::getLayer( filePath, layerIndex, errCause );
  }
  if ( !userLayer )
  {
    layerStyles.reset();
    return false;
  }
  return true;
}


QString QgsOgrProviderMetadata::loadStyle( const QString &uri, QString &errCause )
{
  QgsOgrLayerUniquePtr layerStyles;
  QgsOgrLayerUniquePtr userLayer;
  if ( !LoadDataSourceLayerStylesAndLayer( uri, layerStyles, userLayer, errCause ) )
  {
    return QString();
  }

  QMutex *mutex1 = nullptr;
  OGRLayerH hLayer = layerStyles->getHandleAndMutex( mutex1 );
  QMutex *mutex2 = nullptr;
  OGRLayerH hUserLayer = userLayer->getHandleAndMutex( mutex2 );
  QMutexLocker lock1( mutex1 );
  QMutexLocker lock2( mutex2 );

  QString selectQmlQuery = QStringLiteral( "f_table_schema=''"
                           " AND f_table_name=%1"
                           " AND f_geometry_column=%2"
                           " ORDER BY CASE WHEN useAsDefault THEN 1 ELSE 2 END"
                           ",update_time DESC LIMIT 1" )
                           .arg( QgsOgrProviderUtils::quotedValue( QString( OGR_L_GetName( hUserLayer ) ) ) )
                           .arg( QgsOgrProviderUtils::quotedValue( QString( OGR_L_GetGeometryColumn( hUserLayer ) ) ) );
  OGR_L_SetAttributeFilter( hLayer, selectQmlQuery.toUtf8().constData() );
  OGR_L_ResetReading( hLayer );
  OGRFeatureDefnH hLayerDefn = OGR_L_GetLayerDefn( hLayer );
  QString styleQML;
  qlonglong moreRecentTimestamp = 0;
  while ( true )
  {
    gdal::ogr_feature_unique_ptr hFeat( OGR_L_GetNextFeature( hLayer ) );
    if ( !hFeat )
      break;
    if ( OGR_F_GetFieldAsInteger( hFeat.get(), OGR_FD_GetFieldIndex( hLayerDefn, "useAsDefault" ) ) )
    {
      styleQML = QString::fromUtf8(
                   OGR_F_GetFieldAsString( hFeat.get(), OGR_FD_GetFieldIndex( hLayerDefn, "styleQML" ) ) );
      break;
    }

    int  year, month, day, hour, minute, second, TZ;
    OGR_F_GetFieldAsDateTime( hFeat.get(), OGR_FD_GetFieldIndex( hLayerDefn, "update_time" ),
                              &year, &month, &day, &hour, &minute, &second, &TZ );
    qlonglong ts = second + minute * 60 + hour * 3600 + day * 24 * 3600 +
                   static_cast<qlonglong>( month ) * 31 * 24 * 3600 + static_cast<qlonglong>( year ) * 12 * 31 * 24 * 3600;
    if ( ts > moreRecentTimestamp )
    {
      moreRecentTimestamp = ts;
      styleQML = QString::fromUtf8(
                   OGR_F_GetFieldAsString( hFeat.get(), OGR_FD_GetFieldIndex( hLayerDefn, "styleQML" ) ) );

    }
  }

  return styleQML;
}

int QgsOgrProviderMetadata::listStyles(
  const QString &uri, QStringList &ids, QStringList &names,
  QStringList &descriptions, QString &errCause )
{
  bool isSubLayer;
  int layerIndex;
  QString layerName;
  QString subsetString;
  OGRwkbGeometryType ogrGeometryType;
  QStringList openOptions;
  QString filePath = AnalyzeURI( uri,
                                 isSubLayer,
                                 layerIndex,
                                 layerName,
                                 subsetString,
                                 ogrGeometryType,
                                 openOptions );

  QgsOgrLayerUniquePtr userLayer;
  if ( !layerName.isEmpty() )
  {
    userLayer = QgsOgrProviderUtils::getLayer( filePath, layerName, errCause );
  }
  else
  {
    userLayer = QgsOgrProviderUtils::getLayer( filePath, layerIndex, errCause );
  }
  if ( !userLayer )
  {
    return -1;
  }

  QgsOgrLayerUniquePtr layerStyles =
    QgsOgrProviderUtils::getLayer( filePath, "layer_styles", errCause );
  if ( !layerStyles )
  {
    QgsMessageLog::logMessage( QObject::tr( "No styles available on DB" ) );
    errCause = QObject::tr( "No styles available on DB" );
    return 0;
  }

  QMutex *mutex1 = nullptr;
  OGRLayerH hLayer = layerStyles->getHandleAndMutex( mutex1 );
  QMutexLocker lock1( mutex1 );
  QMutex *mutex2 = nullptr;
  OGRLayerH hUserLayer = userLayer->getHandleAndMutex( mutex2 );
  QMutexLocker lock2( mutex2 );

  if ( OGR_L_GetFeatureCount( hLayer, TRUE ) == 0 )
  {
    QgsMessageLog::logMessage( QObject::tr( "No styles available on DB" ) );
    errCause = QObject::tr( "No styles available on DB" );
    return 0;
  }

  OGRFeatureDefnH hLayerDefn = OGR_L_GetLayerDefn( hLayer );

  OGR_L_ResetReading( hLayer );

  QList<qlonglong> listTimestamp;
  QMap<int, QString> mapIdToStyleName;
  QMap<int, QString> mapIdToDescription;
  QMap<qlonglong, QList<int> > mapTimestampToId;
  int numberOfRelatedStyles = 0;
  while ( true )
  {
    gdal::ogr_feature_unique_ptr hFeature( OGR_L_GetNextFeature( hLayer ) );
    if ( !hFeature )
      break;

    QString tableName( QString::fromUtf8(
                         OGR_F_GetFieldAsString( hFeature.get(),
                             OGR_FD_GetFieldIndex( hLayerDefn, "f_table_name" ) ) ) );
    QString geometryColumn( QString::fromUtf8(
                              OGR_F_GetFieldAsString( hFeature.get(),
                                  OGR_FD_GetFieldIndex( hLayerDefn, "f_geometry_column" ) ) ) );
    QString styleName( QString::fromUtf8(
                         OGR_F_GetFieldAsString( hFeature.get(),
                             OGR_FD_GetFieldIndex( hLayerDefn, "styleName" ) ) ) );
    QString description( QString::fromUtf8(
                           OGR_F_GetFieldAsString( hFeature.get(),
                               OGR_FD_GetFieldIndex( hLayerDefn, "description" ) ) ) );
    int fid = static_cast<int>( OGR_F_GetFID( hFeature.get() ) );
    if ( tableName == QString::fromUtf8( OGR_L_GetName( hUserLayer ) ) &&
         geometryColumn == QString::fromUtf8( OGR_L_GetGeometryColumn( hUserLayer ) ) )
    {
      // Append first all related styles
      QString id( QString::number( fid ) );
      ids.append( id );
      names.append( styleName );
      descriptions.append( description );
      ++ numberOfRelatedStyles;
    }
    else
    {
      int  year, month, day, hour, minute, second, TZ;
      OGR_F_GetFieldAsDateTime( hFeature.get(), OGR_FD_GetFieldIndex( hLayerDefn, "update_time" ),
                                &year, &month, &day, &hour, &minute, &second, &TZ );
      qlonglong ts = second + minute * 60 + hour * 3600 + day * 24 * 3600 +
                     static_cast<qlonglong>( month ) * 31 * 24 * 3600 + static_cast<qlonglong>( year ) * 12 * 31 * 24 * 3600;

      listTimestamp.append( ts );
      mapIdToStyleName[fid] = styleName;
      mapIdToDescription[fid] = description;
      mapTimestampToId[ts].append( fid );
    }
  }

  std::sort( listTimestamp.begin(), listTimestamp.end() );
  // Sort from most recent to least recent
  for ( int i = listTimestamp.size() - 1; i >= 0; i-- )
  {
    const QList<int> &listId = mapTimestampToId[listTimestamp[i]];
    for ( int j = 0; j < listId.size(); j++ )
    {
      int fid = listId[j];
      QString id( QString::number( fid ) );
      ids.append( id );
      names.append( mapIdToStyleName[fid] );
      descriptions.append( mapIdToDescription[fid] );
    }
  }

  return numberOfRelatedStyles;
}

QString QgsOgrProviderMetadata::getStyleById( const QString &uri, QString styleId, QString &errCause )
{
  QgsOgrLayerUniquePtr layerStyles;
  QgsOgrLayerUniquePtr userLayer;
  if ( !LoadDataSourceLayerStylesAndLayer( uri, layerStyles, userLayer, errCause ) )
  {
    return QString();
  }

  QMutex *mutex1 = nullptr;
  OGRLayerH hLayer = layerStyles->getHandleAndMutex( mutex1 );
  QMutexLocker lock1( mutex1 );

  bool ok;
  int id = styleId.toInt( &ok );
  if ( !ok )
  {
    errCause = QObject::tr( "Invalid style identifier" );
    return QString();
  }

  gdal::ogr_feature_unique_ptr hFeature( OGR_L_GetFeature( hLayer, id ) );
  if ( !hFeature )
  {
    errCause = QObject::tr( "No style corresponding to style identifier" );
    return QString();
  }

  OGRFeatureDefnH hLayerDefn = OGR_L_GetLayerDefn( hLayer );
  QString styleQML( QString::fromUtf8(
                      OGR_F_GetFieldAsString( hFeature.get(),
                          OGR_FD_GetFieldIndex( hLayerDefn, "styleQML" ) ) ) );

  return styleQML;
}


// ---------------------------------------------------------------------------

bool QgsOgrProviderUtils::deleteLayer( const QString &uri, QString &errCause )
{
  bool isSubLayer;
  int layerIndex;
  QString layerName;
  QString subsetString;
  OGRwkbGeometryType ogrGeometryType;
  QStringList openOptions;
  QString filePath = AnalyzeURI( uri,
                                 isSubLayer,
                                 layerIndex,
                                 layerName,
                                 subsetString,
                                 ogrGeometryType,
                                 openOptions );


  GDALDatasetH hDS = GDALOpenEx( filePath.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_VECTOR | GDAL_OF_UPDATE, nullptr, nullptr, nullptr );
  if ( hDS  && ( ! layerName.isEmpty() || layerIndex != -1 ) )
  {
    // If we have got a name we convert it into an index
    if ( ! layerName.isEmpty() )
    {
      layerIndex = -1;
      for ( int i = 0; i < GDALDatasetGetLayerCount( hDS ); i++ )
      {
        OGRLayerH hL = GDALDatasetGetLayer( hDS, i );
        if ( layerName == QString::fromUtf8( OGR_L_GetName( hL ) ) )
        {
          layerIndex = i;
          break;
        }
      }
    }
    // Do delete!
    if ( layerIndex != -1 )
    {
      OGRErr error = GDALDatasetDeleteLayer( hDS, layerIndex );
      switch ( error )
      {
        case OGRERR_NOT_ENOUGH_DATA:
          errCause = QObject::tr( "Not enough data to deserialize" );
          break;
        case OGRERR_NOT_ENOUGH_MEMORY:
          errCause = QObject::tr( "Not enough memory" );
          break;
        case OGRERR_UNSUPPORTED_GEOMETRY_TYPE:
          errCause = QObject::tr( "Unsupported geometry type" );
          break;
        case OGRERR_UNSUPPORTED_OPERATION:
          errCause = QObject::tr( "Unsupported operation" );
          break;
        case OGRERR_CORRUPT_DATA:
          errCause = QObject::tr( "Corrupt data" );
          break;
        case OGRERR_FAILURE:
          errCause = QObject::tr( "Failure" );
          break;
        case OGRERR_UNSUPPORTED_SRS:
          errCause = QObject::tr( "Unsupported SRS" );
          break;
        case OGRERR_INVALID_HANDLE:
          errCause = QObject::tr( "Invalid handle" );
          break;
        case OGRERR_NON_EXISTING_FEATURE:
          errCause = QObject::tr( "Non existing feature" );
          break;
        default:
        case OGRERR_NONE:
          errCause = QObject::tr( "Success" );
          break;
      }
      errCause = QObject::tr( "GDAL result code: %1" ).arg( errCause );
      return error == OGRERR_NONE;
    }
  }
  // This should never happen:
  errCause = QObject::tr( "Layer not found: %1" ).arg( uri );
  return false;
}

void QgsOgrLayerReleaser::operator()( QgsOgrLayer *layer )
{
  QgsOgrProviderUtils::release( layer );
}

QgsTransaction *QgsOgrProviderMetadata::createTransaction( const QString &connString )
{
  auto ds = QgsOgrProviderUtils::getAlreadyOpenedDataset( connString );
  if ( !ds )
  {
    QgsMessageLog::logMessage( QObject::tr( "Cannot open transaction on %1, since it is is not currently opened" ).arg( connString ),
                               QObject::tr( "OGR" ), Qgis::Critical );
    return nullptr;
  }

  return new QgsOgrTransaction( connString, ds );
}

QgsGeoPackageProjectStorage *gGeoPackageProjectStorage = nullptr;   // when not null it is owned by QgsApplication::projectStorageRegistry()

void QgsOgrProviderMetadata::initProvider()
{
  Q_ASSERT( !gGeoPackageProjectStorage );
  gGeoPackageProjectStorage = new QgsGeoPackageProjectStorage;
  QgsApplication::projectStorageRegistry()->registerProjectStorage( gGeoPackageProjectStorage );  // takes ownership
}


void QgsOgrProviderMetadata::cleanupProvider()
{
  QgsApplication::projectStorageRegistry()->unregisterProjectStorage( gGeoPackageProjectStorage );  // destroys the object
  gGeoPackageProjectStorage = nullptr;
  QgsOgrConnPool::cleanupInstance();
  // NOTE: QgsApplication takes care of
  // calling OGRCleanupAll();
}



QgsOgrProviderMetadata::QgsOgrProviderMetadata()
  : QgsProviderMetadata( TEXT_PROVIDER_KEY, TEXT_PROVIDER_DESCRIPTION )
{

}

QString QgsOgrProviderMetadata::filters( FilterType type )
{
  switch ( type )
  {
    case QgsProviderMetadata::FilterType::FilterVector:
      return QgsOgrProviderUtils::fileVectorFilters();

    case QgsProviderMetadata::FilterType::FilterRaster:
    case QgsProviderMetadata::FilterType::FilterMesh:
    case QgsProviderMetadata::FilterType::FilterMeshDataset:
    case QgsProviderMetadata::FilterType::FilterPointCloud:
      return QString();
  }
  return QString();
}

bool QgsOgrProviderMetadata::uriIsBlocklisted( const QString &uri ) const
{
  const QVariantMap parts = decodeUri( uri );
  if ( !parts.contains( QStringLiteral( "path" ) ) )
    return false;

  QFileInfo fi( parts.value( QStringLiteral( "path" ) ).toString() );
  const QString suffix = fi.completeSuffix();

  // internal details only
  if ( suffix.compare( QLatin1String( "shp.xml" ), Qt::CaseInsensitive ) == 0 )
    return true;

  return false;
}

QMap<QString, QgsAbstractProviderConnection *> QgsOgrProviderMetadata::connections( bool cached )
{
  return connectionsProtected<QgsGeoPackageProviderConnection, QgsOgrDbConnection>( cached );
}

QgsAbstractProviderConnection *QgsOgrProviderMetadata::createConnection( const QString &connName )
{
  return new QgsGeoPackageProviderConnection( connName );
}

QgsAbstractProviderConnection *QgsOgrProviderMetadata::createConnection( const QString &uri, const QVariantMap &configuration )
{
  return new QgsGeoPackageProviderConnection( uri, configuration );
}

void QgsOgrProviderMetadata::deleteConnection( const QString &name )
{
  deleteConnectionProtected<QgsGeoPackageProviderConnection>( name );
}

void QgsOgrProviderMetadata::saveConnection( const QgsAbstractProviderConnection *conn, const QString &name )
{
  saveConnectionProtected( conn, name );
}

///@endcond
