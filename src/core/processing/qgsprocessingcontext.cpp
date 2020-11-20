/***************************************************************************
                         qgsprocessingcontext.cpp
                         ----------------------
    begin                : April 2017
    copyright            : (C) 2017 by Nyall Dawson
    email                : nyall dot dawson at gmail dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsprocessingcontext.h"
#include "qgsprocessingutils.h"
#include "qgsproviderregistry.h"
#include "qgssettings.h"

QgsProcessingContext::QgsProcessingContext()
  : mPreferredVectorFormat( QgsProcessingUtils::defaultVectorExtension() )
  , mPreferredRasterFormat( QgsProcessingUtils::defaultRasterExtension() )
{
  auto callback = [ = ]( const QgsFeature & feature )
  {
    if ( mFeedback )
      mFeedback->reportError( QObject::tr( "Encountered a transform error when reprojecting feature with id %1." ).arg( feature.id() ) );
  };
  mTransformErrorCallback = callback;
}

QgsProcessingContext::~QgsProcessingContext()
{
  for ( auto it = mLayersToLoadOnCompletion.constBegin(); it != mLayersToLoadOnCompletion.constEnd(); ++it )
  {
    delete it.value().postProcessor();
  }
}

void QgsProcessingContext::setLayersToLoadOnCompletion( const QMap<QString, QgsProcessingContext::LayerDetails> &layers )
{
  for ( auto it = mLayersToLoadOnCompletion.constBegin(); it != mLayersToLoadOnCompletion.constEnd(); ++it )
  {
    if ( !layers.contains( it.key() ) || layers.value( it.key() ).postProcessor() != it.value().postProcessor() )
      delete it.value().postProcessor();
  }
  mLayersToLoadOnCompletion = layers;
}

void QgsProcessingContext::addLayerToLoadOnCompletion( const QString &layer, const QgsProcessingContext::LayerDetails &details )
{
  if ( mLayersToLoadOnCompletion.contains( layer ) && mLayersToLoadOnCompletion.value( layer ).postProcessor() != details.postProcessor() )
    delete mLayersToLoadOnCompletion.value( layer ).postProcessor();

  mLayersToLoadOnCompletion.insert( layer, details );
}

void QgsProcessingContext::setInvalidGeometryCheck( QgsFeatureRequest::InvalidGeometryCheck check )
{
  mInvalidGeometryCheck = check;
  mUseDefaultInvalidGeometryCallback = true;
  mInvalidGeometryCallback = defaultInvalidGeometryCallbackForCheck( check );
}

std::function<void ( const QgsFeature & )> QgsProcessingContext::invalidGeometryCallback( QgsFeatureSource *source ) const
{
  if ( mUseDefaultInvalidGeometryCallback )
    return defaultInvalidGeometryCallbackForCheck( mInvalidGeometryCheck, source );
  else
    return mInvalidGeometryCallback;
}

std::function<void ( const QgsFeature & )> QgsProcessingContext::defaultInvalidGeometryCallbackForCheck( QgsFeatureRequest::InvalidGeometryCheck check, QgsFeatureSource *source ) const
{
  const QString sourceName = source ? source->sourceName() : QString();
  switch ( check )
  {
    case  QgsFeatureRequest::GeometryAbortOnInvalid:
    {
      auto callback = [sourceName]( const QgsFeature & feature )
      {
        if ( !sourceName.isEmpty() )
          throw QgsProcessingException( QObject::tr( "Feature (%1) from “%2” has invalid geometry. Please fix the geometry or change the Processing setting to the “Ignore invalid input features” option." ).arg( feature.id() ).arg( sourceName ) );
        else
          throw QgsProcessingException( QObject::tr( "Feature (%1) has invalid geometry. Please fix the geometry or change the Processing setting to the “Ignore invalid input features” option." ).arg( feature.id() ) );
      };
      return callback;
    }

    case QgsFeatureRequest::GeometrySkipInvalid:
    {
      auto callback = [ = ]( const QgsFeature & feature )
      {
        if ( mFeedback )
        {
          if ( !sourceName.isEmpty() )
            mFeedback->reportError( QObject::tr( "Feature (%1) from “%2” has invalid geometry and has been skipped. Please fix the geometry or change the Processing setting to the “Ignore invalid input features” option." ).arg( feature.id() ).arg( sourceName ) );
          else
            mFeedback->reportError( QObject::tr( "Feature (%1) has invalid geometry and has been skipped. Please fix the geometry or change the Processing setting to the “Ignore invalid input features” option." ).arg( feature.id() ) );
        }
      };
      return callback;
    }

    case QgsFeatureRequest::GeometryNoCheck:
      return nullptr;
  }
  return nullptr;
}

void QgsProcessingContext::takeResultsFrom( QgsProcessingContext &context )
{
  setLayersToLoadOnCompletion( context.mLayersToLoadOnCompletion );
  context.mLayersToLoadOnCompletion.clear();
  tempLayerStore.transferLayersFromStore( context.temporaryLayerStore() );
}

QgsMapLayer *QgsProcessingContext::getMapLayer( const QString &identifier )
{
  return QgsProcessingUtils::mapLayerFromString( identifier, *this, false );
}

QgsMapLayer *QgsProcessingContext::takeResultLayer( const QString &id )
{
  return tempLayerStore.takeMapLayer( tempLayerStore.mapLayer( id ) );
}

QgsDateTimeRange QgsProcessingContext::currentTimeRange() const
{
  return mCurrentTimeRange;
}

void QgsProcessingContext::setCurrentTimeRange( const QgsDateTimeRange &currentTimeRange )
{
  mCurrentTimeRange = currentTimeRange;
}

QString QgsProcessingContext::ellipsoid() const
{
  return mEllipsoid;
}

void QgsProcessingContext::setEllipsoid( const QString &ellipsoid )
{
  mEllipsoid = ellipsoid;
}

QgsUnitTypes::DistanceUnit QgsProcessingContext::distanceUnit() const
{
  return mDistanceUnit;
}

void QgsProcessingContext::setDistanceUnit( QgsUnitTypes::DistanceUnit unit )
{
  mDistanceUnit = unit;
}

QgsUnitTypes::AreaUnit QgsProcessingContext::areaUnit() const
{
  return mAreaUnit;
}

void QgsProcessingContext::setAreaUnit( QgsUnitTypes::AreaUnit areaUnit )
{
  mAreaUnit = areaUnit;
}

QgsProcessingLayerPostProcessorInterface *QgsProcessingContext::LayerDetails::postProcessor() const
{
  return mPostProcessor;
}

void QgsProcessingContext::LayerDetails::setPostProcessor( QgsProcessingLayerPostProcessorInterface *processor )
{
  if ( mPostProcessor && mPostProcessor != processor )
    delete mPostProcessor;

  mPostProcessor = processor;
}

void QgsProcessingContext::LayerDetails::setOutputLayerName( QgsMapLayer *layer ) const
{
  if ( !layer )
    return;

  const bool preferFilenameAsLayerName = QgsSettings().value( QStringLiteral( "Processing/Configuration/PREFER_FILENAME_AS_LAYER_NAME" ), true ).toBool();

  // note - for temporary layers, we don't use the filename, regardless of user setting (it will be meaningless!)
  if ( ( !forceName && preferFilenameAsLayerName && !layer->isTemporary() ) || name.isEmpty() )
  {
    const QVariantMap sourceParts = QgsProviderRegistry::instance()->decodeUri( layer->providerType(), layer->source() );
    const QString layerName = sourceParts.value( QStringLiteral( "layerName" ) ).toString();
    // if output layer name exists, use that!
    if ( !layerName.isEmpty() )
      layer->setName( layerName );
    else
    {
      const QString path = sourceParts.value( QStringLiteral( "path" ) ).toString();
      if ( !path.isEmpty() )
      {
        const QFileInfo fi( path );
        layer->setName( fi.baseName() );
      }
      else if ( !name.isEmpty() )
      {
        // fallback to parameter's name -- shouldn't happen!
        layer->setName( name );
      }
    }
  }
  else
  {
    layer->setName( name );
  }
}
