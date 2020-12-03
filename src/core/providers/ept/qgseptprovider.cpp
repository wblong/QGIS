/***************************************************************************
                         qgseptdataprovider.cpp
                         -----------------------
    begin                : October 2020
    copyright            : (C) 2020 by Peter Petrik
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

#include "qgis.h"
#include "qgseptprovider.h"
#include "qgseptpointcloudindex.h"
#include "qgseptdataitems.h"
#include "qgsruntimeprofiler.h"
#include "qgsapplication.h"

///@cond PRIVATE

#define PROVIDER_KEY QStringLiteral( "ept" )
#define PROVIDER_DESCRIPTION QStringLiteral( "EPT point cloud data provider" )

QgsEptProvider::QgsEptProvider(
  const QString &uri,
  const QgsDataProvider::ProviderOptions &options,
  QgsDataProvider::ReadFlags flags )
  : QgsPointCloudDataProvider( uri, options, flags )
  , mIndex( new QgsEptPointCloudIndex )
{
  std::unique_ptr< QgsScopedRuntimeProfile > profile;
  if ( QgsApplication::profiler()->groupIsActive( QStringLiteral( "projectload" ) ) )
    profile = qgis::make_unique< QgsScopedRuntimeProfile >( tr( "Open data source" ), QStringLiteral( "projectload" ) );

  mIsValid = mIndex->load( uri );
}

QgsEptProvider::~QgsEptProvider() = default;

QgsCoordinateReferenceSystem QgsEptProvider::crs() const
{
  return mIndex->crs();
}

QgsRectangle QgsEptProvider::extent() const
{
  return mIndex->extent();
}

QgsPointCloudAttributeCollection QgsEptProvider::attributes() const
{
  return mIndex->attributes();
}

bool QgsEptProvider::isValid() const
{
  return mIsValid;
}

QString QgsEptProvider::name() const
{
  return QStringLiteral( "ept" );
}

QString QgsEptProvider::description() const
{
  return QStringLiteral( "Point Clouds EPT" );
}

QgsPointCloudIndex *QgsEptProvider::index() const
{
  return mIndex.get();
}

int QgsEptProvider::pointCount() const
{
  return mIndex->pointCount();
}

QVariantList QgsEptProvider::metadataClasses( const QString &attribute ) const
{
  return mIndex->metadataClasses( attribute );
}

QVariant QgsEptProvider::metadataClassStatistic( const QString &attribute, const QVariant &value, QgsStatisticalSummary::Statistic statistic ) const
{
  return mIndex->metadataClassStatistic( attribute, value, statistic );
}

QVariant QgsEptProvider::metadataStatistic( const QString &attribute, QgsStatisticalSummary::Statistic statistic ) const
{
  return mIndex->metadataStatistic( attribute, statistic );
}

QgsEptProviderMetadata::QgsEptProviderMetadata():
  QgsProviderMetadata( PROVIDER_KEY, PROVIDER_DESCRIPTION )
{
}

QgsEptProvider *QgsEptProviderMetadata::createProvider( const QString &uri, const QgsDataProvider::ProviderOptions &options, QgsDataProvider::ReadFlags flags )
{
  return new QgsEptProvider( uri, options, flags );
}

QList<QgsDataItemProvider *> QgsEptProviderMetadata::dataItemProviders() const
{
  QList< QgsDataItemProvider * > providers;
  providers << new QgsEptDataItemProvider;
  return providers;
}

int QgsEptProviderMetadata::priorityForUri( const QString &uri ) const
{
  const QVariantMap parts = decodeUri( uri );
  QFileInfo fi( parts.value( QStringLiteral( "path" ) ).toString() );
  if ( fi.fileName().compare( QLatin1String( "ept.json" ), Qt::CaseInsensitive ) == 0 )
    return 100;

  return 0;
}

QList<QgsMapLayerType> QgsEptProviderMetadata::validLayerTypesForUri( const QString &uri ) const
{
  const QVariantMap parts = decodeUri( uri );
  QFileInfo fi( parts.value( QStringLiteral( "path" ) ).toString() );
  if ( fi.fileName().compare( QLatin1String( "ept.json" ), Qt::CaseInsensitive ) == 0 )
    return QList< QgsMapLayerType>() << QgsMapLayerType::PointCloudLayer;

  return QList< QgsMapLayerType>();
}

bool QgsEptProviderMetadata::uriIsBlocklisted( const QString &uri ) const
{
  const QVariantMap parts = decodeUri( uri );
  if ( !parts.contains( QStringLiteral( "path" ) ) )
    return false;

  QFileInfo fi( parts.value( QStringLiteral( "path" ) ).toString() );

  // internal details only
  if ( fi.fileName().compare( QLatin1String( "ept-build.json" ), Qt::CaseInsensitive ) == 0 )
    return true;

  return false;
}

QVariantMap QgsEptProviderMetadata::decodeUri( const QString &uri ) const
{
  const QString path = uri;
  QVariantMap uriComponents;
  uriComponents.insert( QStringLiteral( "path" ), path );
  return uriComponents;
}

QString QgsEptProviderMetadata::filters( QgsProviderMetadata::FilterType type )
{
  switch ( type )
  {
    case QgsProviderMetadata::FilterType::FilterVector:
    case QgsProviderMetadata::FilterType::FilterRaster:
    case QgsProviderMetadata::FilterType::FilterMesh:
    case QgsProviderMetadata::FilterType::FilterMeshDataset:
      return QString();

    case QgsProviderMetadata::FilterType::FilterPointCloud:
      return QObject::tr( "Entwine Point Clouds" ) + QStringLiteral( " (ept.json EPT.JSON)" );
  }
  return QString();
}

QString QgsEptProviderMetadata::encodeUri( const QVariantMap &parts ) const
{
  const QString path = parts.value( QStringLiteral( "path" ) ).toString();
  return path;
}

QgsProviderMetadata::ProviderMetadataCapabilities QgsEptProviderMetadata::capabilities() const
{
  return ProviderMetadataCapability::LayerTypesForUri
         | ProviderMetadataCapability::PriorityForUri;
}
///@endcond

