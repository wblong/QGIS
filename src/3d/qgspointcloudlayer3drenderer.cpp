/***************************************************************************
  qgspointcloudlayer3drenderer.cpp
  --------------------------------------
  Date                 : October 2020
  Copyright            : (C) 2020 by Peter Petrik
  Email                : zilolv dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgspointcloudlayer3drenderer.h"

#include "qgs3dutils.h"
#include "qgschunkedentity_p.h"
#include "qgspointcloudlayerchunkloader_p.h"

#include "qgspointcloudindex.h"
#include "qgspointcloudlayer.h"
#include "qgsxmlutils.h"
#include "qgsapplication.h"
#include "qgs3dsymbolregistry.h"
#include "qgspointcloud3dsymbol.h"
#include "qgspointcloudlayerelevationproperties.h"

#include "qgis.h"

QgsPointCloud3DRenderContext::QgsPointCloud3DRenderContext( const Qgs3DMapSettings &map, std::unique_ptr<QgsPointCloud3DSymbol> symbol, double zValueScale, double zValueFixedOffset )
  : Qgs3DRenderContext( map )
  , mSymbol( std::move( symbol ) )
  , mZValueScale( zValueScale )
  , mZValueFixedOffset( zValueFixedOffset )
{
  auto callback = []()->bool
  {
    return false;
  };
  mIsCanceledCallback = callback;
}

void QgsPointCloud3DRenderContext::setAttributes( const QgsPointCloudAttributeCollection &attributes )
{
  mAttributes = attributes;
}

void QgsPointCloud3DRenderContext::setSymbol( QgsPointCloud3DSymbol *symbol )
{
  mSymbol.reset( symbol );
}

void QgsPointCloud3DRenderContext::setFilteredOutCategories( const QgsPointCloudCategoryList &categories )
{
  mFilteredOutCategories = categories;
}

QSet<int> QgsPointCloud3DRenderContext::getFilteredOutValues() const
{
  QSet<int> filteredOut;
  for ( QgsPointCloudCategory category : mFilteredOutCategories )
    filteredOut.insert( category.value() );
  return filteredOut;
}

QgsPointCloudLayer3DRendererMetadata::QgsPointCloudLayer3DRendererMetadata()
  : Qgs3DRendererAbstractMetadata( QStringLiteral( "pointcloud" ) )
{
}

QgsAbstract3DRenderer *QgsPointCloudLayer3DRendererMetadata::createRenderer( QDomElement &elem, const QgsReadWriteContext &context )
{
  QgsPointCloudLayer3DRenderer *r = new QgsPointCloudLayer3DRenderer;
  r->readXml( elem, context );
  return r;
}


// ---------


QgsPointCloudLayer3DRenderer::QgsPointCloudLayer3DRenderer( )
{
}

void QgsPointCloudLayer3DRenderer::setLayer( QgsPointCloudLayer *layer )
{
  mLayerRef = QgsMapLayerRef( layer );
}

QgsPointCloudLayer *QgsPointCloudLayer3DRenderer::layer() const
{
  return qobject_cast<QgsPointCloudLayer *>( mLayerRef.layer );
}

QString QgsPointCloudLayer3DRenderer::type() const
{
  return QStringLiteral( "pointcloud" );
}

QgsPointCloudLayer3DRenderer *QgsPointCloudLayer3DRenderer::clone() const
{
  QgsPointCloudLayer3DRenderer *r = new QgsPointCloudLayer3DRenderer;
  if ( mSymbol )
  {
    QgsAbstract3DSymbol *symbolClone = mSymbol->clone();
    r->setSymbol( dynamic_cast<QgsPointCloud3DSymbol *>( symbolClone ) );
  }
  return r;
}

Qt3DCore::QEntity *QgsPointCloudLayer3DRenderer::createEntity( const Qgs3DMapSettings &map ) const
{
  QgsPointCloudLayer *pcl = layer();
  if ( !pcl || !pcl->dataProvider() || !pcl->dataProvider()->index() )
    return nullptr;
  if ( !mSymbol )
    return nullptr;

  return new QgsPointCloudLayerChunkedEntity( pcl->dataProvider()->index(), map, dynamic_cast<QgsPointCloud3DSymbol *>( mSymbol->clone() ), maximumScreenError(), showBoundingBoxes(),
         static_cast< const QgsPointCloudLayerElevationProperties * >( pcl->elevationProperties() )->zScale(),
         static_cast< const QgsPointCloudLayerElevationProperties * >( pcl->elevationProperties() )->zOffset() );
}

void QgsPointCloudLayer3DRenderer::setSymbol( QgsPointCloud3DSymbol *symbol )
{
  mSymbol.reset( symbol );
}

void QgsPointCloudLayer3DRenderer::writeXml( QDomElement &elem, const QgsReadWriteContext &context ) const
{
  Q_UNUSED( context )

  QDomDocument doc = elem.ownerDocument();

  elem.setAttribute( QStringLiteral( "layer" ), mLayerRef.layerId );
  elem.setAttribute( QStringLiteral( "max-screen-error" ), maximumScreenError() );
  elem.setAttribute( QStringLiteral( "show-bounding-boxes" ), showBoundingBoxes() ? QStringLiteral( "1" ) : QStringLiteral( "0" ) );

  QDomElement elemSymbol = doc.createElement( QStringLiteral( "symbol" ) );
  if ( mSymbol )
  {
    elemSymbol.setAttribute( QStringLiteral( "type" ), mSymbol->symbolType() );
    mSymbol->writeXml( elemSymbol, context );
  }
  elem.appendChild( elemSymbol );
}

void QgsPointCloudLayer3DRenderer::readXml( const QDomElement &elem, const QgsReadWriteContext &context )
{
  mLayerRef = QgsMapLayerRef( elem.attribute( QStringLiteral( "layer" ) ) );

  QDomElement elemSymbol = elem.firstChildElement( QStringLiteral( "symbol" ) );

  const QString symbolType = elemSymbol.attribute( QStringLiteral( "type" ) );
  mMaximumScreenError = elem.attribute( QStringLiteral( "max-screen-error" ), QStringLiteral( "1.0" ) ).toDouble();
  mShowBoundingBoxes = elem.attribute( QStringLiteral( "show-bounding-boxes" ), QStringLiteral( "0" ) ).toInt();

  if ( symbolType == QLatin1String( "single-color" ) )
    mSymbol.reset( new QgsSingleColorPointCloud3DSymbol );
  else if ( symbolType == QLatin1String( "color-ramp" ) )
    mSymbol.reset( new QgsColorRampPointCloud3DSymbol );
  else if ( symbolType == QLatin1String( "rgb" ) )
    mSymbol.reset( new QgsRgbPointCloud3DSymbol );
  else if ( symbolType == QLatin1String( "classification" ) )
    mSymbol.reset( new QgsClassificationPointCloud3DSymbol );
  else
    mSymbol.reset();

  if ( mSymbol )
    mSymbol->readXml( elemSymbol, context );
}

void QgsPointCloudLayer3DRenderer::resolveReferences( const QgsProject &project )
{
  mLayerRef.setLayer( project.mapLayer( mLayerRef.layerId ) );
}

double QgsPointCloudLayer3DRenderer::maximumScreenError() const
{
  return mMaximumScreenError;
}

void QgsPointCloudLayer3DRenderer::setMaximumScreenError( double error )
{
  mMaximumScreenError = error;
}

bool QgsPointCloudLayer3DRenderer::showBoundingBoxes() const
{
  return mShowBoundingBoxes;
}

void QgsPointCloudLayer3DRenderer::setShowBoundingBoxes( bool showBoundingBoxes )
{
  mShowBoundingBoxes = showBoundingBoxes;
}

