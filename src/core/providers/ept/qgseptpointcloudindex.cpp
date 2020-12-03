/***************************************************************************
                         qgspointcloudindex.cpp
                         --------------------
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

#include "qgseptpointcloudindex.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTime>
#include <QtDebug>
#include <QQueue>

#include "qgseptdecoder.h"
#include "qgscoordinatereferencesystem.h"
#include "qgspointcloudrequest.h"
#include "qgspointcloudattribute.h"
#include "qgslogger.h"

///@cond PRIVATE

#define PROVIDER_KEY QStringLiteral( "ept" )
#define PROVIDER_DESCRIPTION QStringLiteral( "EPT point cloud provider" )

QgsEptPointCloudIndex::QgsEptPointCloudIndex() = default;

QgsEptPointCloudIndex::~QgsEptPointCloudIndex() = default;

bool QgsEptPointCloudIndex::load( const QString &fileName )
{
  // mDirectory = directory;
  QFile f( fileName );
  if ( !f.open( QIODevice::ReadOnly ) )
    return false;

  const QDir directory = QFileInfo( fileName ).absoluteDir();
  mDirectory = directory.absolutePath();

  QByteArray dataJson = f.readAll();
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson( dataJson, &err );
  if ( err.error != QJsonParseError::NoError )
    return false;
  QJsonObject result = doc.object();
  mDataType = result.value( QLatin1String( "dataType" ) ).toString();  // "binary" or "laszip"
  if ( mDataType != "laszip" && mDataType != "binary" && mDataType != "zstandard" )
    return false;

  QString hierarchyType = result.value( QLatin1String( "hierarchyType" ) ).toString();  // "json" or "gzip"
  if ( hierarchyType != "json" )
    return false;

  mSpan = result.value( QLatin1String( "span" ) ).toInt();
  mPointCount = result.value( QLatin1String( "points" ) ).toInt();

  // WKT
  QJsonObject srs = result.value( QLatin1String( "srs" ) ).toObject();
  mWkt = srs.value( QLatin1String( "wkt" ) ).toString();

  // rectangular
  QJsonArray bounds = result.value( QLatin1String( "bounds" ) ).toArray();
  if ( bounds.size() != 6 )
    return false;

  QJsonArray bounds_conforming = result.value( QLatin1String( "boundsConforming" ) ).toArray();
  if ( bounds.size() != 6 )
    return false;
  mExtent.set( bounds_conforming[0].toDouble(), bounds_conforming[1].toDouble(),
               bounds_conforming[3].toDouble(), bounds_conforming[4].toDouble() );
  mZMin = bounds_conforming[2].toDouble();
  mZMax = bounds_conforming[5].toDouble();

  QJsonArray schemaArray = result.value( QLatin1String( "schema" ) ).toArray();
  QgsPointCloudAttributeCollection attributes;


  for ( QJsonValue schemaItem : schemaArray )
  {
    const QJsonObject schemaObj = schemaItem.toObject();
    QString name = schemaObj.value( QLatin1String( "name" ) ).toString();
    QString type = schemaObj.value( QLatin1String( "type" ) ).toString();

    int size = schemaObj.value( QLatin1String( "size" ) ).toInt();

    if ( type == QLatin1String( "float" ) && ( size == 4 ) )
    {
      attributes.push_back( QgsPointCloudAttribute( name, QgsPointCloudAttribute::Float ) );
    }
    else if ( type == QLatin1String( "float" ) && ( size == 8 ) )
    {
      attributes.push_back( QgsPointCloudAttribute( name, QgsPointCloudAttribute::Double ) );
    }
    else if ( size == 1 )
    {
      attributes.push_back( QgsPointCloudAttribute( name, QgsPointCloudAttribute::Char ) );
    }
    else if ( type == QLatin1String( "unsigned" ) && size == 2 )
    {
      attributes.push_back( QgsPointCloudAttribute( name, QgsPointCloudAttribute::UShort ) );
    }
    else if ( size == 2 )
    {
      attributes.push_back( QgsPointCloudAttribute( name, QgsPointCloudAttribute::Short ) );
    }
    else if ( size == 4 )
    {
      attributes.push_back( QgsPointCloudAttribute( name, QgsPointCloudAttribute::Int32 ) );
    }
    else
    {
      // unknown attribute type
      return false;
    }

    float scale = 1.f;
    if ( schemaObj.contains( QLatin1String( "scale" ) ) )
      scale = schemaObj.value( QLatin1String( "scale" ) ).toDouble();

    float offset = 0.f;
    if ( schemaObj.contains( QLatin1String( "offset" ) ) )
      offset = schemaObj.value( QLatin1String( "offset" ) ).toDouble();

    if ( name == QLatin1String( "X" ) )
    {
      mOffset.set( offset, mOffset.y(), mOffset.z() );
      mScale.set( scale, mScale.y(), mScale.z() );
    }
    else if ( name == QLatin1String( "Y" ) )
    {
      mOffset.set( mOffset.x(), offset, mOffset.z() );
      mScale.set( mScale.x(), scale, mScale.z() );
    }
    else if ( name == QLatin1String( "Z" ) )
    {
      mOffset.set( mOffset.x(), mOffset.y(), offset );
      mScale.set( mScale.x(), mScale.y(), scale );
    }

    // store any metadata stats which are present for the attribute
    AttributeStatistics stats;
    if ( schemaObj.contains( QLatin1String( "count" ) ) )
      stats.count = schemaObj.value( QLatin1String( "count" ) ).toInt();
    if ( schemaObj.contains( QLatin1String( "minimum" ) ) )
      stats.minimum = schemaObj.value( QLatin1String( "minimum" ) ).toDouble();
    if ( schemaObj.contains( QLatin1String( "maximum" ) ) )
      stats.maximum = schemaObj.value( QLatin1String( "maximum" ) ).toDouble();
    if ( schemaObj.contains( QLatin1String( "count" ) ) )
      stats.mean = schemaObj.value( QLatin1String( "mean" ) ).toDouble();
    if ( schemaObj.contains( QLatin1String( "stddev" ) ) )
      stats.stDev = schemaObj.value( QLatin1String( "stddev" ) ).toDouble();
    if ( schemaObj.contains( QLatin1String( "variance" ) ) )
      stats.variance = schemaObj.value( QLatin1String( "variance" ) ).toDouble();
    mMetadataStats.insert( name, stats );

    if ( schemaObj.contains( QLatin1String( "counts" ) ) )
    {
      QMap< int, int >  classCounts;
      const QJsonArray counts = schemaObj.value( QLatin1String( "counts" ) ).toArray();
      for ( const QJsonValue &count : counts )
      {
        const QJsonObject countObj = count.toObject();
        classCounts.insert( countObj.value( QLatin1String( "value" ) ).toInt(), countObj.value( QLatin1String( "count" ) ).toInt() );
      }
      mAttributeClasses.insert( name, classCounts );
    }
  }
  setAttributes( attributes );

  // save mRootBounds

  // bounds (cube - octree volume)
  double xmin = bounds[0].toDouble();
  double ymin = bounds[1].toDouble();
  double zmin = bounds[2].toDouble();
  double xmax = bounds[3].toDouble();
  double ymax = bounds[4].toDouble();
  double zmax = bounds[5].toDouble();

  mRootBounds = QgsPointCloudDataBounds(
                  ( xmin - mOffset.x() ) / mScale.x(),
                  ( ymin - mOffset.y() ) / mScale.y(),
                  ( zmin - mOffset.z() ) / mScale.z(),
                  ( xmax - mOffset.x() ) / mScale.x(),
                  ( ymax - mOffset.y() ) / mScale.y(),
                  ( zmax - mOffset.z() ) / mScale.z()
                );


#ifdef QGIS_DEBUG
  double dx = xmax - xmin, dy = ymax - ymin, dz = zmax - zmin;
  QgsDebugMsgLevel( QStringLiteral( "lvl0 node size in CRS units: %1 %2 %3" ).arg( dx ).arg( dy ).arg( dz ), 2 );    // all dims should be the same
  QgsDebugMsgLevel( QStringLiteral( "res at lvl0 %1" ).arg( dx / mSpan ), 2 );
  QgsDebugMsgLevel( QStringLiteral( "res at lvl1 %1" ).arg( dx / mSpan / 2 ), 2 );
  QgsDebugMsgLevel( QStringLiteral( "res at lvl2 %1 with node size %2" ).arg( dx / mSpan / 4 ).arg( dx / 4 ), 2 );
#endif

  // load hierarchy
  return loadHierarchy();
}

QgsPointCloudBlock *QgsEptPointCloudIndex::nodeData( const IndexedPointCloudNode &n, const QgsPointCloudRequest &request )
{
  if ( !mHierarchy.contains( n ) )
    return nullptr;

  if ( mDataType == "binary" )
  {
    QString filename = QString( "%1/ept-data/%2.bin" ).arg( mDirectory ).arg( n.toString() );
    return QgsEptDecoder::decompressBinary( filename, attributes(), request.attributes() );
  }
  else if ( mDataType == "zstandard" )
  {
    QString filename = QString( "%1/ept-data/%2.zst" ).arg( mDirectory ).arg( n.toString() );
    return QgsEptDecoder::decompressZStandard( filename, attributes(), request.attributes() );
  }
  else if ( mDataType == "laszip" )
  {
    QString filename = QString( "%1/ept-data/%2.laz" ).arg( mDirectory ).arg( n.toString() );
    return QgsEptDecoder::decompressLaz( filename, attributes(), request.attributes() );
  }
  else
  {
    return nullptr;  // unsupported
  }
}

QgsCoordinateReferenceSystem QgsEptPointCloudIndex::crs() const
{
  return QgsCoordinateReferenceSystem::fromWkt( mWkt );
}

int QgsEptPointCloudIndex::pointCount() const
{
  return mPointCount;
}

QVariant QgsEptPointCloudIndex::metadataStatistic( const QString &attribute, QgsStatisticalSummary::Statistic statistic ) const
{
  if ( !mMetadataStats.contains( attribute ) )
    return QVariant();

  const AttributeStatistics &stats = mMetadataStats[ attribute ];
  switch ( statistic )
  {
    case QgsStatisticalSummary::Count:
      return stats.count >= 0 ? QVariant( stats.count ) : QVariant();

    case QgsStatisticalSummary::Mean:
      return std::isnan( stats.mean ) ? QVariant() : QVariant( stats.mean );

    case QgsStatisticalSummary::StDev:
      return std::isnan( stats.stDev ) ? QVariant() : QVariant( stats.stDev );

    case QgsStatisticalSummary::Min:
      return stats.minimum;

    case QgsStatisticalSummary::Max:
      return stats.maximum;

    case QgsStatisticalSummary::Range:
      return stats.minimum.isValid() && stats.maximum.isValid() ? QVariant( stats.maximum.toDouble() - stats.minimum.toDouble() ) : QVariant();

    case QgsStatisticalSummary::CountMissing:
    case QgsStatisticalSummary::Sum:
    case QgsStatisticalSummary::Median:
    case QgsStatisticalSummary::StDevSample:
    case QgsStatisticalSummary::Minority:
    case QgsStatisticalSummary::Majority:
    case QgsStatisticalSummary::Variety:
    case QgsStatisticalSummary::FirstQuartile:
    case QgsStatisticalSummary::ThirdQuartile:
    case QgsStatisticalSummary::InterQuartileRange:
    case QgsStatisticalSummary::First:
    case QgsStatisticalSummary::Last:
    case QgsStatisticalSummary::All:
      return QVariant();
  }
  return QVariant();
}

QVariantList QgsEptPointCloudIndex::metadataClasses( const QString &attribute ) const
{
  QVariantList classes;
  const QMap< int, int > values =  mAttributeClasses.value( attribute );
  for ( auto it = values.constBegin(); it != values.constEnd(); ++it )
  {
    classes << it.key();
  }
  return classes;
}

QVariant QgsEptPointCloudIndex::metadataClassStatistic( const QString &attribute, const QVariant &value, QgsStatisticalSummary::Statistic statistic ) const
{
  if ( statistic != QgsStatisticalSummary::Count )
    return QVariant();

  const QMap< int, int > values =  mAttributeClasses.value( attribute );
  if ( !values.contains( value.toInt() ) )
    return QVariant();
  return values.value( value.toInt() );
}

bool QgsEptPointCloudIndex::loadHierarchy()
{
  QQueue<QString> queue;
  queue.enqueue( QStringLiteral( "0-0-0-0" ) );
  while ( !queue.isEmpty() )
  {
    const QString filename = QStringLiteral( "%1/ept-hierarchy/%2.json" ).arg( mDirectory ).arg( queue.dequeue() );
    QFile fH( filename );
    if ( !fH.open( QIODevice::ReadOnly ) )
    {
      QgsDebugMsgLevel( QStringLiteral( "unable to read hierarchy from file %1" ).arg( filename ), 2 );
      return false;
    }

    QByteArray dataJsonH = fH.readAll();
    QJsonParseError errH;
    QJsonDocument docH = QJsonDocument::fromJson( dataJsonH, &errH );
    if ( errH.error != QJsonParseError::NoError )
    {
      QgsDebugMsgLevel( QStringLiteral( "QJsonParseError when reading hierarchy from file %1" ).arg( filename ), 2 );
      return false;
    }

    QJsonObject rootHObj = docH.object();
    for ( auto it = rootHObj.constBegin(); it != rootHObj.constEnd(); ++it )
    {
      QString nodeIdStr = it.key();
      int nodePointCount = it.value().toInt();
      if ( nodePointCount < 0 )
      {
        queue.enqueue( nodeIdStr );
      }
      else
      {
        IndexedPointCloudNode nodeId = IndexedPointCloudNode::fromString( nodeIdStr );
        mHierarchy[nodeId] = nodePointCount;
      }
    }
  }
  return true;
}

///@endcond
