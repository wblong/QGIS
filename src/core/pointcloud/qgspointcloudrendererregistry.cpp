/***************************************************************************
    qgspointcloudrendererregistry.cpp
    ---------------------
    begin                : November 2020
    copyright            : (C) 2020 by Nyall Dawson
    email                : nyall dot dawson at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "qgspointcloudrendererregistry.h"
#include "qgspointcloudrenderer.h"

// default renderers
#include "qgspointcloudattributebyramprenderer.h"
#include "qgspointcloudrgbrenderer.h"
#include "qgspointcloudclassifiedrenderer.h"
#include "qgspointcloudlayer.h"

QgsPointCloudRendererRegistry::QgsPointCloudRendererRegistry()
{
  // add default renderers
  addRenderer( new QgsPointCloudRendererMetadata( QStringLiteral( "ramp" ),
               QObject::tr( "Attribute by Ramp" ),
               QgsPointCloudAttributeByRampRenderer::create ) );
  addRenderer( new QgsPointCloudRendererMetadata( QStringLiteral( "rgb" ),
               QObject::tr( "RGB" ),
               QgsPointCloudRgbRenderer::create ) );
  addRenderer( new QgsPointCloudRendererMetadata( QStringLiteral( "classified" ),
               QObject::tr( "Classification" ),
               QgsPointCloudClassifiedRenderer::create ) );
}

QgsPointCloudRendererRegistry::~QgsPointCloudRendererRegistry()
{
  qDeleteAll( mRenderers );
}

bool QgsPointCloudRendererRegistry::addRenderer( QgsPointCloudRendererAbstractMetadata *metadata )
{
  if ( !metadata || mRenderers.contains( metadata->name() ) )
    return false;

  mRenderers[metadata->name()] = metadata;
  mRenderersOrder << metadata->name();
  return true;
}

bool QgsPointCloudRendererRegistry::removeRenderer( const QString &rendererName )
{
  if ( !mRenderers.contains( rendererName ) )
    return false;

  delete mRenderers[rendererName];
  mRenderers.remove( rendererName );
  mRenderersOrder.removeAll( rendererName );
  return true;
}

QgsPointCloudRendererAbstractMetadata *QgsPointCloudRendererRegistry::rendererMetadata( const QString &rendererName )
{
  return mRenderers.value( rendererName );
}

QStringList QgsPointCloudRendererRegistry::renderersList() const
{
  QStringList renderers;
  for ( const QString &renderer : mRenderersOrder )
  {
    QgsPointCloudRendererAbstractMetadata *r = mRenderers.value( renderer );
    if ( r )
      renderers << renderer;
  }
  return renderers;
}

QgsPointCloudRenderer *QgsPointCloudRendererRegistry::defaultRenderer( const QgsPointCloudDataProvider *provider )
{
  if ( !provider )
    return new QgsPointCloudAttributeByRampRenderer();

  const QgsPointCloudAttributeCollection attributes = provider->attributes();

  //if red/green/blue attributes are present, then default to a RGB renderer
  if ( attributes.indexOf( QStringLiteral( "Red" ) ) >= 0 && attributes.indexOf( QStringLiteral( "Green" ) ) >= 0 && attributes.indexOf( QStringLiteral( "Blue" ) ) >= 0 )
  {
    std::unique_ptr< QgsPointCloudRgbRenderer > renderer = qgis::make_unique< QgsPointCloudRgbRenderer >();

    // set initial guess for rgb ranges
    const QVariant redMax = provider->metadataStatistic( QStringLiteral( "Red" ), QgsStatisticalSummary::Max );
    const QVariant greenMax = provider->metadataStatistic( QStringLiteral( "Red" ), QgsStatisticalSummary::Max );
    const QVariant blueMax = provider->metadataStatistic( QStringLiteral( "Red" ), QgsStatisticalSummary::Max );
    if ( redMax.isValid() && greenMax.isValid() && blueMax.isValid() )
    {
      const int maxValue = std::max( blueMax.toInt(), std::max( redMax.toInt(), greenMax.toInt() ) );
      // try and guess suitable range from input max values -- we don't just take the provider max value directly here, but rather see if it's
      // likely to be 8 bit or 16 bit color values
      const int rangeGuess = maxValue > 255 ? 65024 : 255;

      if ( rangeGuess > 255 )
      {
        // looks like 16 bit colors, so default to a stretch contrast enhancement
        QgsContrastEnhancement contrast( Qgis::UnknownDataType );
        contrast.setMinimumValue( 0 );
        contrast.setMaximumValue( rangeGuess );
        contrast.setContrastEnhancementAlgorithm( QgsContrastEnhancement::StretchToMinimumMaximum );
        renderer->setRedContrastEnhancement( new QgsContrastEnhancement( contrast ) );
        renderer->setGreenContrastEnhancement( new QgsContrastEnhancement( contrast ) );
        renderer->setBlueContrastEnhancement( new QgsContrastEnhancement( contrast ) );
      }
    }

    return renderer.release();
  }
  else
  {
    // default to shading by Z
    std::unique_ptr< QgsPointCloudAttributeByRampRenderer > renderer = qgis::make_unique< QgsPointCloudAttributeByRampRenderer >();
    renderer->setAttribute( QStringLiteral( "Z" ) );

    // set initial range for z values if possible
    const QVariant zMin = provider->metadataStatistic( QStringLiteral( "Z" ), QgsStatisticalSummary::Min );
    const QVariant zMax = provider->metadataStatistic( QStringLiteral( "Z" ), QgsStatisticalSummary::Max );
    if ( zMin.isValid() && zMax.isValid() )
    {
      renderer->setMinimum( zMin.toDouble() );
      renderer->setMaximum( zMax.toDouble() );

      QgsColorRampShader shader = renderer->colorRampShader();
      shader.setMinimumValue( zMin.toDouble() );
      shader.setMaximumValue( zMax.toDouble() );
      shader.classifyColorRamp( 5, -1, QgsRectangle(), nullptr );
      renderer->setColorRampShader( shader );
    }
    return renderer.release();
  }
}

