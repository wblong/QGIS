/***************************************************************************
  qgs3dmapscene.cpp
  --------------------------------------
  Date                 : July 2017
  Copyright            : (C) 2017 by Martin Dobias
  Email                : wonder dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgs3dmapscene.h"

#include <Qt3DRender/QCamera>
#include <Qt3DRender/QMesh>
#include <Qt3DRender/QObjectPicker>
#include <Qt3DRender/QPickEvent>
#include <Qt3DRender/QPickingSettings>
#include <Qt3DRender/QPickTriangleEvent>
#include <Qt3DRender/QPointLight>
#include <Qt3DRender/QDirectionalLight>
#include <Qt3DRender/QRenderSettings>
#include <Qt3DRender/QSceneLoader>
#include <Qt3DExtras/QForwardRenderer>
#include <Qt3DExtras/QPhongMaterial>
#include <Qt3DExtras/QSphereMesh>
#include <Qt3DLogic/QFrameAction>
#include <Qt3DRender/QEffect>
#include <Qt3DRender/QTechnique>
#include <Qt3DRender/QRenderPass>
#include <Qt3DRender/QRenderState>
#include <Qt3DRender/QCullFace>
#include <Qt3DRender/QDepthTest>
#include <QSurface>

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QTimer>

#include "qgsapplication.h"
#include "qgsaabb.h"
#include "qgsabstract3dengine.h"
#include "qgs3dmapscenepickhandler.h"
#include "qgs3dmapsettings.h"
#include "qgs3dutils.h"
#include "qgsabstract3drenderer.h"
#include "qgscameracontroller.h"
#include "qgschunkedentity_p.h"
#include "qgschunknode_p.h"
#include "qgseventtracing.h"
#include "qgsmeshlayer.h"
#include "qgsmeshlayer3drenderer.h"
#include "qgspoint3dsymbol.h"
#include "qgsrulebased3drenderer.h"
#include "qgspointcloudlayer.h"
#include "qgspointcloudlayer3drenderer.h"
#include "qgssourcecache.h"
#include "qgsterrainentity_p.h"
#include "qgsterraingenerator.h"
#include "qgstessellatedpolygongeometry.h"
#include "qgsvectorlayer.h"
#include "qgsvectorlayer3drenderer.h"
#include "qgspoint3dbillboardmaterial.h"
#include "qgsmaplayertemporalproperties.h"

#include "qgslinematerial_p.h"
#include "qgs3dsceneexporter.h"
#include "qgsabstract3drenderer.h"
#include "qgs3dmapexportsettings.h"
#include "qgsmessageoutput.h"

#include "qgsskyboxentity.h"
#include "qgsskyboxsettings.h"

#include "qgswindow3dengine.h"

Qgs3DMapScene::Qgs3DMapScene( const Qgs3DMapSettings &map, QgsAbstract3DEngine *engine )
  : mMap( map )
  , mEngine( engine )
{

  connect( &map, &Qgs3DMapSettings::backgroundColorChanged, this, &Qgs3DMapScene::onBackgroundColorChanged );
  onBackgroundColorChanged();

  // TODO: strange - setting OnDemand render policy still keeps QGIS busy (Qt 5.9.0)
  // actually it is more busy than with the default "Always" policy although there are no changes in the scene.
  //mRenderer->renderSettings()->setRenderPolicy( Qt3DRender::QRenderSettings::OnDemand );

#if QT_VERSION >= 0x050900
  // we want precise picking of terrain (also bounding volume picking does not seem to work - not sure why)
  mEngine->renderSettings()->pickingSettings()->setPickMethod( Qt3DRender::QPickingSettings::TrianglePicking );
#endif

  QRect viewportRect( QPoint( 0, 0 ), mEngine->size() );

  // Camera
  float aspectRatio = ( float )viewportRect.width() / viewportRect.height();
  mEngine->camera()->lens()->setPerspectiveProjection( mMap.fieldOfView(), aspectRatio, 10.f, 10000.0f );

  mFrameAction = new Qt3DLogic::QFrameAction();
  connect( mFrameAction, &Qt3DLogic::QFrameAction::triggered,
           this, &Qgs3DMapScene::onFrameTriggered );
  addComponent( mFrameAction ); // takes ownership

  // Camera controlling
  mCameraController = new QgsCameraController( this ); // attaches to the scene
  mCameraController->setViewport( viewportRect );
  mCameraController->setCamera( mEngine->camera() );
  mCameraController->resetView( 1000 );

  addCameraViewCenterEntity( mEngine->camera() );
  updateLights();

  // create terrain entity

  createTerrainDeferred();
  connect( &map, &Qgs3DMapSettings::terrainGeneratorChanged, this, &Qgs3DMapScene::createTerrain );
  connect( &map, &Qgs3DMapSettings::terrainVerticalScaleChanged, this, &Qgs3DMapScene::createTerrain );
  connect( &map, &Qgs3DMapSettings::mapTileResolutionChanged, this, &Qgs3DMapScene::createTerrain );
  connect( &map, &Qgs3DMapSettings::maxTerrainScreenErrorChanged, this, &Qgs3DMapScene::createTerrain );
  connect( &map, &Qgs3DMapSettings::maxTerrainGroundErrorChanged, this, &Qgs3DMapScene::createTerrain );
  connect( &map, &Qgs3DMapSettings::terrainShadingChanged, this, &Qgs3DMapScene::createTerrain );
  connect( &map, &Qgs3DMapSettings::pointLightsChanged, this, &Qgs3DMapScene::updateLights );
  connect( &map, &Qgs3DMapSettings::directionalLightsChanged, this, &Qgs3DMapScene::updateLights );
  connect( &map, &Qgs3DMapSettings::showLightSourceOriginsChanged, this, &Qgs3DMapScene::updateLights );
  connect( &map, &Qgs3DMapSettings::fieldOfViewChanged, this, &Qgs3DMapScene::updateCameraLens );
  connect( &map, &Qgs3DMapSettings::projectionTypeChanged, this, &Qgs3DMapScene::updateCameraLens );
  connect( &map, &Qgs3DMapSettings::renderersChanged, this, &Qgs3DMapScene::onRenderersChanged );
  connect( &map, &Qgs3DMapSettings::skyboxSettingsChanged, this, &Qgs3DMapScene::onSkyboxSettingsChanged );
  connect( &map, &Qgs3DMapSettings::shadowSettingsChanged, this, &Qgs3DMapScene::onShadowSettingsChanged );
  connect( &map, &Qgs3DMapSettings::eyeDomeLightingEnabledChanged, this, &Qgs3DMapScene::onEyeDomeShadingSettingsChanged );
  connect( &map, &Qgs3DMapSettings::eyeDomeLightingStrengthChanged, this, &Qgs3DMapScene::onEyeDomeShadingSettingsChanged );
  connect( &map, &Qgs3DMapSettings::eyeDomeLightingDistanceChanged, this, &Qgs3DMapScene::onEyeDomeShadingSettingsChanged );
  connect( &map, &Qgs3DMapSettings::debugShadowMapSettingsChanged, this, &Qgs3DMapScene::onDebugShadowMapSettingsChanged );
  connect( &map, &Qgs3DMapSettings::debugDepthMapSettingsChanged, this, &Qgs3DMapScene::onDebugDepthMapSettingsChanged );

  connect( QgsApplication::instance()->sourceCache(), &QgsSourceCache::remoteSourceFetched, this, [ = ]( const QString & url )
  {
    const QList<QgsMapLayer *> modelVectorLayers = mModelVectorLayers;
    for ( QgsMapLayer *layer : modelVectorLayers )
    {
      QgsAbstract3DRenderer *renderer = layer->renderer3D();
      if ( renderer )
      {
        if ( renderer->type() == QLatin1String( "vector" ) )
        {
          const QgsPoint3DSymbol *pointSymbol = static_cast< const QgsPoint3DSymbol * >( static_cast< QgsVectorLayer3DRenderer *>( renderer )->symbol() );
          if ( pointSymbol->shapeProperties()[QStringLiteral( "model" )].toString() == url )
          {
            removeLayerEntity( layer );
            addLayerEntity( layer );
          }
        }
        else if ( renderer->type() == QLatin1String( "rulebased" ) )
        {
          const QgsRuleBased3DRenderer::RuleList rules = static_cast< QgsRuleBased3DRenderer *>( renderer )->rootRule()->descendants();
          for ( auto rule : rules )
          {
            const QgsPoint3DSymbol *pointSymbol = dynamic_cast< const QgsPoint3DSymbol * >( rule->symbol() );
            if ( pointSymbol->shapeProperties()[QStringLiteral( "model" )].toString() == url )
            {
              removeLayerEntity( layer );
              addLayerEntity( layer );
              break;
            }
          }
        }
      }
    }
  } );

  // create entities of renderers

  onRenderersChanged();

  // listen to changes of layers in order to add/remove 3D renderer entities
  connect( &map, &Qgs3DMapSettings::layersChanged, this, &Qgs3DMapScene::onLayersChanged );


#if 0
  ChunkedEntity *testChunkEntity = new ChunkedEntity( AABB( -500, 0, -500, 500, 100, 500 ), 2.f, 3.f, 7, new TestChunkLoaderFactory );
  testChunkEntity->setEnabled( false );
  testChunkEntity->setParent( this );
  chunkEntities << testChunkEntity;
#endif

  connect( mCameraController, &QgsCameraController::cameraChanged, this, &Qgs3DMapScene::onCameraChanged );
  connect( mCameraController, &QgsCameraController::viewportChanged, this, &Qgs3DMapScene::onCameraChanged );

#if 0
  // experiments with loading of existing 3D models.

  // scene loader only gets loaded only when added to a scene...
  // it loads everything: geometries, materials, transforms, lights, cameras (if any)
  Qt3DCore::QEntity *loaderEntity = new Qt3DCore::QEntity;
  Qt3DRender::QSceneLoader *loader = new Qt3DRender::QSceneLoader;
  loader->setSource( QUrl( "file:///home/martin/Downloads/LowPolyModels/tree.dae" ) );
  loaderEntity->addComponent( loader );
  loaderEntity->setParent( this );

  // mesh loads just geometry as one geometry...
  // so if there are different materials (e.g. colors) used in the model, this information is lost
  Qt3DCore::QEntity *meshEntity = new Qt3DCore::QEntity;
  Qt3DRender::QMesh *mesh = new Qt3DRender::QMesh;
  mesh->setSource( QUrl( "file:///home/martin/Downloads/LowPolyModels/tree.obj" ) );
  meshEntity->addComponent( mesh );
  Qt3DExtras::QPhongMaterial *material = new Qt3DExtras::QPhongMaterial;
  material->setAmbient( Qt::red );
  meshEntity->addComponent( material );
  Qt3DCore::QTransform *meshTransform = new Qt3DCore::QTransform;
  meshTransform->setScale( 1 );
  meshEntity->addComponent( meshTransform );
  meshEntity->setParent( this );
#endif
  onSkyboxSettingsChanged();

  // force initial update of chunked entities
  onCameraChanged();
  // force initial update of eye dome shading
  onEyeDomeShadingSettingsChanged();
  // force initial update of debugging setting of preview quads
  onDebugShadowMapSettingsChanged();
  onDebugDepthMapSettingsChanged();
}

void Qgs3DMapScene::viewZoomFull()
{
  QgsRectangle extent = mMap.terrainGenerator()->extent();
  float side = std::max( extent.width(), extent.height() );
  mCameraController->resetView( side );  // assuming FOV being 45 degrees
}

int Qgs3DMapScene::terrainPendingJobsCount() const
{
  return mTerrain ? mTerrain->pendingJobsCount() : 0;
}

int Qgs3DMapScene::totalPendingJobsCount() const
{
  int count = 0;
  for ( QgsChunkedEntity *entity : qgis::as_const( mChunkEntities ) )
    count += entity->pendingJobsCount();
  return count;
}

void Qgs3DMapScene::registerPickHandler( Qgs3DMapScenePickHandler *pickHandler )
{
  if ( mPickHandlers.isEmpty() )
  {
    // we need to add object pickers
    for ( Qt3DCore::QEntity *entity : mLayerEntities.values() )
    {
      if ( QgsChunkedEntity *chunkedEntity = qobject_cast<QgsChunkedEntity *>( entity ) )
        chunkedEntity->setPickingEnabled( true );
    }
  }

  mPickHandlers.append( pickHandler );
}

void Qgs3DMapScene::unregisterPickHandler( Qgs3DMapScenePickHandler *pickHandler )
{
  mPickHandlers.removeOne( pickHandler );

  if ( mPickHandlers.isEmpty() )
  {
    // we need to remove pickers
    for ( Qt3DCore::QEntity *entity : mLayerEntities.values() )
    {
      if ( QgsChunkedEntity *chunkedEntity = qobject_cast<QgsChunkedEntity *>( entity ) )
        chunkedEntity->setPickingEnabled( false );
    }
  }
}

void Qgs3DMapScene::onLayerEntityPickedObject( Qt3DRender::QPickEvent *pickEvent, QgsFeatureId fid )
{
  QgsMapLayer *layer = mLayerEntities.key( qobject_cast<QgsChunkedEntity *>( sender() ) );
  if ( !layer )
    return;

  QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( layer );
  if ( !vlayer )
    return;

  for ( Qgs3DMapScenePickHandler *pickHandler : qgis::as_const( mPickHandlers ) )
  {
    pickHandler->handlePickOnVectorLayer( vlayer, fid, pickEvent->worldIntersection(), pickEvent );
  }
}

float Qgs3DMapScene::worldSpaceError( float epsilon, float distance )
{
  Qt3DRender::QCamera *camera = mCameraController->camera();
  float fov = camera->fieldOfView();
  QRect rect = mCameraController->viewport();
  float screenSizePx = std::max( rect.width(), rect.height() ); // TODO: is this correct?

  // in qgschunkedentity_p.cpp there is inverse calculation (world space error to screen space error)
  // with explanation of the math.
  float frustumWidthAtDistance = 2 * distance * tan( fov / 2 );
  float err = frustumWidthAtDistance * epsilon / screenSizePx;
  return err;
}

QgsChunkedEntity::SceneState _sceneState( QgsCameraController *cameraController )
{
  Qt3DRender::QCamera *camera = cameraController->camera();
  QgsChunkedEntity::SceneState state;
  state.cameraFov = camera->fieldOfView();
  state.cameraPos = camera->position();
  QRect rect = cameraController->viewport();
  state.screenSizePx = std::max( rect.width(), rect.height() ); // TODO: is this correct?
  state.viewProjectionMatrix = camera->projectionMatrix() * camera->viewMatrix();
  return state;
}

void Qgs3DMapScene::onCameraChanged()
{
  if ( mMap.projectionType() == Qt3DRender::QCameraLens::OrthographicProjection )
  {
    QRect viewportRect( QPoint( 0, 0 ), mEngine->size() );
    const float viewWidthFromCenter = mCameraController->distance();
    const float viewHeightFromCenter =  viewportRect.height() * viewWidthFromCenter / viewportRect.width();
    mEngine->camera()->lens()->setOrthographicProjection( -viewWidthFromCenter, viewWidthFromCenter, -viewHeightFromCenter, viewHeightFromCenter, mEngine->camera()->nearPlane(), mEngine->camera()->farPlane() );
  }

  updateScene();
  bool changedCameraPlanes = updateCameraNearFarPlanes();

  if ( changedCameraPlanes )
  {
    // repeat update of entities - because we have updated camera's near/far planes,
    // the active nodes may have changed as well
    updateScene();
    updateCameraNearFarPlanes();
  }

  onShadowSettingsChanged();
}

void removeQLayerComponentsFromHierarchy( Qt3DCore::QEntity *entity )
{
  QVector<Qt3DCore::QComponent *> toBeRemovedComponents;
  for ( Qt3DCore::QComponent *component : entity->components() )
  {
    Qt3DRender::QLayer *layer = qobject_cast<Qt3DRender::QLayer *>( component );
    if ( layer != nullptr )
      toBeRemovedComponents.push_back( layer );
  }
  for ( Qt3DCore::QComponent *component : toBeRemovedComponents )
    entity->removeComponent( component );
  for ( Qt3DCore::QEntity *obj : entity->findChildren<Qt3DCore::QEntity *>() )
  {
    if ( obj != nullptr )
      removeQLayerComponentsFromHierarchy( obj );
  }
}

void addQLayerComponentsToHierarchy( Qt3DCore::QEntity *entity, const QVector<Qt3DRender::QLayer *> layers )
{
  for ( Qt3DRender::QLayer *layer : layers )
    entity->addComponent( layer );
  for ( Qt3DCore::QEntity *child : entity->findChildren<Qt3DCore::QEntity *>() )
  {
    if ( child != nullptr )
      addQLayerComponentsToHierarchy( child, layers );
  }
}

void Qgs3DMapScene::updateScene()
{
  QgsEventTracing::addEvent( QgsEventTracing::Instant, QStringLiteral( "3D" ), QStringLiteral( "Update Scene" ) );
  for ( QgsChunkedEntity *entity : qgis::as_const( mChunkEntities ) )
  {
    if ( entity->isEnabled() )
      entity->update( _sceneState( mCameraController ) );
  }
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
  QgsWindow3DEngine *windowEngine = qobject_cast<QgsWindow3DEngine *>( mEngine );
  if ( windowEngine != nullptr )
  {
    QVector<Qt3DRender::QLayer *> layers;
    layers.push_back( windowEngine->shadowRenderingFrameGraph()->castShadowsLayer() );
    layers.push_back( windowEngine->shadowRenderingFrameGraph()->forwardRenderLayer() );
    removeQLayerComponentsFromHierarchy( this );
    addQLayerComponentsToHierarchy( this, layers );
  }
#endif
  updateSceneState();
}

static void _updateNearFarPlane( const QList<QgsChunkNode *> &activeNodes, const QMatrix4x4 &viewMatrix, float &fnear, float &ffar )
{
  for ( QgsChunkNode *node : activeNodes )
  {
    // project each corner of bbox to camera coordinates
    // and determine closest and farthest point.
    QgsAABB bbox = node->bbox();
    for ( int i = 0; i < 8; ++i )
    {
      QVector4D p( ( ( i >> 0 ) & 1 ) ? bbox.xMin : bbox.xMax,
                   ( ( i >> 1 ) & 1 ) ? bbox.yMin : bbox.yMax,
                   ( ( i >> 2 ) & 1 ) ? bbox.zMin : bbox.zMax, 1 );
      QVector4D pc = viewMatrix * p;

      float dst = -pc.z();  // in camera coordinates, x grows right, y grows down, z grows to the back
      if ( dst < fnear )
        fnear = dst;
      if ( dst > ffar )
        ffar = dst;
    }
  }
}

bool Qgs3DMapScene::updateCameraNearFarPlanes()
{
  // Update near and far plane from the terrain.
  // this needs to be done with great care as we have kind of circular dependency here:
  // active nodes are culled based on the current frustum (which involves near + far plane)
  // and then based on active nodes we set near and far plane.
  //
  // All of this is just heuristics assuming that all other stuff is being rendered somewhere
  // around the area where the terrain is.
  //
  // Near/far plane is setup in order to make best use of the depth buffer to avoid:
  // 1. precision errors - if the range is too great
  // 2. unwanted clipping of scene - if the range is too small

  if ( mTerrain )
  {
    Qt3DRender::QCamera *camera = cameraController()->camera();
    QMatrix4x4 viewMatrix = camera->viewMatrix();
    float fnear = 1e9;
    float ffar = 0;

    QList<QgsChunkNode *> activeNodes = mTerrain->activeNodes();

    // it could be that there are no active nodes - they could be all culled or because root node
    // is not yet loaded - we still need at least something to understand bounds of our scene
    // so lets use the root node
    if ( activeNodes.isEmpty() )
      activeNodes << mTerrain->rootNode();

    _updateNearFarPlane( activeNodes, viewMatrix, fnear, ffar );

    // Also involve all the other chunked entities to make sure that they will not get
    // clipped by the near or far plane
    for ( QgsChunkedEntity *e : qgis::as_const( mChunkEntities ) )
    {
      if ( e != mTerrain )
        _updateNearFarPlane( e->activeNodes(), viewMatrix, fnear, ffar );
    }

    if ( fnear < 1 )
      fnear = 1;  // does not really make sense to use negative far plane (behind camera)

    if ( fnear == 1e9 && ffar == 0 )
    {
      // the update didn't work out... this should not happen
      // well at least temporarily use some conservative starting values
      qWarning() << "oops... this should not happen! couldn't determine near/far plane. defaulting to 1...1e9";
      fnear = 1;
      ffar = 1e9;
    }

    // set near/far plane - with some tolerance in front/behind expected near/far planes
    float newFar = ffar * 2;
    float newNear = fnear / 2;
    if ( !qgsFloatNear( newFar, camera->farPlane() ) || !qgsFloatNear( newNear, camera->nearPlane() ) )
    {
      camera->setFarPlane( newFar );
      camera->setNearPlane( newNear );
      return true;
    }
  }
  else
    qWarning() << "no terrain - not setting near/far plane";

  return false;
}

void Qgs3DMapScene::onFrameTriggered( float dt )
{
  mCameraController->frameTriggered( dt );

  for ( QgsChunkedEntity *entity : qgis::as_const( mChunkEntities ) )
  {
    if ( entity->isEnabled() && entity->needsUpdate() )
    {
      QgsDebugMsgLevel( QStringLiteral( "need for update" ), 2 );
      entity->update( _sceneState( mCameraController ) );
    }
  }

  updateSceneState();
}

void Qgs3DMapScene::createTerrain()
{
  if ( mTerrain )
  {
    mChunkEntities.removeOne( mTerrain );

    mTerrain->deleteLater();
    mTerrain = nullptr;

    emit terrainEntityChanged();
  }

  if ( !mTerrainUpdateScheduled )
  {
    // defer re-creation of terrain: there may be multiple invocations of this slot, so create the new entity just once
    QTimer::singleShot( 0, this, &Qgs3DMapScene::createTerrainDeferred );
    mTerrainUpdateScheduled = true;
    setSceneState( Updating );
  }
}

void Qgs3DMapScene::createTerrainDeferred()
{
  double tile0width = mMap.terrainGenerator()->extent().width();
  int maxZoomLevel = Qgs3DUtils::maxZoomLevel( tile0width, mMap.mapTileResolution(), mMap.maxTerrainGroundError() );
  QgsAABB rootBbox = mMap.terrainGenerator()->rootChunkBbox( mMap );
  float rootError = mMap.terrainGenerator()->rootChunkError( mMap );
  mMap.terrainGenerator()->setupQuadtree( rootBbox, rootError, maxZoomLevel );

  mTerrain = new QgsTerrainEntity( mMap );
  //mTerrain->setEnabled(false);
  mTerrain->setParent( this );

  if ( mMap.showTerrainBoundingBoxes() )
    mTerrain->setShowBoundingBoxes( true );

  mCameraController->setTerrainEntity( mTerrain );

  mChunkEntities << mTerrain;

  onCameraChanged();  // force update of the new terrain

  // make sure that renderers for layers are re-created as well
  Q_FOREACH ( QgsMapLayer *layer, mMap.layers() )
  {
    // remove old entity - if any
    removeLayerEntity( layer );

    // add new entity - if any 3D renderer
    addLayerEntity( layer );
  }

  mTerrainUpdateScheduled = false;

  connect( mTerrain, &QgsChunkedEntity::pendingJobsCountChanged, this, &Qgs3DMapScene::totalPendingJobsCountChanged );
  connect( mTerrain, &QgsTerrainEntity::pendingJobsCountChanged, this, &Qgs3DMapScene::terrainPendingJobsCountChanged );

  emit terrainEntityChanged();
}

void Qgs3DMapScene::onBackgroundColorChanged()
{
  mEngine->setClearColor( mMap.backgroundColor() );
}

void Qgs3DMapScene::updateLights()
{
  for ( Qt3DCore::QEntity *entity : qgis::as_const( mLightEntities ) )
    entity->deleteLater();
  mLightEntities.clear();
  for ( Qt3DCore::QEntity *entity : qgis::as_const( mLightOriginEntities ) )
    entity->deleteLater();
  mLightOriginEntities.clear();

  auto createLightOriginEntity = [ = ]( QVector3D translation, const QColor & color )->Qt3DCore::QEntity *
  {
    Qt3DCore::QEntity *originEntity = new Qt3DCore::QEntity;

    Qt3DCore::QTransform *trLightOriginCenter = new Qt3DCore::QTransform;
    trLightOriginCenter->setTranslation( translation );
    originEntity->addComponent( trLightOriginCenter );

    Qt3DExtras::QPhongMaterial *materialLightOriginCenter = new Qt3DExtras::QPhongMaterial;
    materialLightOriginCenter->setAmbient( color );
    originEntity->addComponent( materialLightOriginCenter );

    Qt3DExtras::QSphereMesh *rendererLightOriginCenter = new Qt3DExtras::QSphereMesh;
    rendererLightOriginCenter->setRadius( 20 );
    originEntity->addComponent( rendererLightOriginCenter );

    originEntity->setEnabled( true );
    originEntity->setParent( this );

    return originEntity;
  };

  const auto newPointLights = mMap.pointLights();
  for ( const QgsPointLightSettings &pointLightSettings : newPointLights )
  {
    Qt3DCore::QEntity *lightEntity = new Qt3DCore::QEntity;
    Qt3DCore::QTransform *lightTransform = new Qt3DCore::QTransform;
    lightTransform->setTranslation( QVector3D( pointLightSettings.position().x(),
                                    pointLightSettings.position().y(),
                                    pointLightSettings.position().z() ) );

    Qt3DRender::QPointLight *light = new Qt3DRender::QPointLight;
    light->setColor( pointLightSettings.color() );
    light->setIntensity( pointLightSettings.intensity() );

    light->setConstantAttenuation( pointLightSettings.constantAttenuation() );
    light->setLinearAttenuation( pointLightSettings.linearAttenuation() );
    light->setQuadraticAttenuation( pointLightSettings.quadraticAttenuation() );

    lightEntity->addComponent( light );
    lightEntity->addComponent( lightTransform );
    lightEntity->setParent( this );
    mLightEntities << lightEntity;

    if ( mMap.showLightSourceOrigins() )
      mLightOriginEntities << createLightOriginEntity( lightTransform->translation(), pointLightSettings.color() );
  }

  const auto newDirectionalLights = mMap.directionalLights();
  for ( const QgsDirectionalLightSettings &directionalLightSettings : newDirectionalLights )
  {
    Qt3DCore::QEntity *lightEntity = new Qt3DCore::QEntity;
    Qt3DCore::QTransform *lightTransform = new Qt3DCore::QTransform;

    Qt3DRender::QDirectionalLight *light = new Qt3DRender::QDirectionalLight;
    light->setColor( directionalLightSettings.color() );
    light->setIntensity( directionalLightSettings.intensity() );
    QgsVector3D direction = directionalLightSettings.direction();
    light->setWorldDirection( QVector3D( direction.x(), direction.y(), direction.z() ) );

    lightEntity->addComponent( light );
    lightEntity->addComponent( lightTransform );
    lightEntity->setParent( this );
    mLightEntities << lightEntity;
  }

  onShadowSettingsChanged();
}

void Qgs3DMapScene::updateCameraLens()
{
  mEngine->camera()->lens()->setFieldOfView( mMap.fieldOfView() );
  mEngine->camera()->lens()->setProjectionType( mMap.projectionType() );
  onCameraChanged();
}

void Qgs3DMapScene::onRenderersChanged()
{
  // remove entities (if any)
  qDeleteAll( mRenderersEntities );
  mRenderersEntities.clear();

  // re-add entities from new set of renderers
  const QList<QgsAbstract3DRenderer *> renderers = mMap.renderers();
  for ( const QgsAbstract3DRenderer *renderer : renderers )
  {
    Qt3DCore::QEntity *newEntity = renderer->createEntity( mMap );
    if ( newEntity )
    {
      newEntity->setParent( this );
      finalizeNewEntity( newEntity );
      mRenderersEntities[renderer] = newEntity;
    }
  }
}

void Qgs3DMapScene::onLayerRenderer3DChanged()
{
  QgsMapLayer *layer = qobject_cast<QgsMapLayer *>( sender() );
  Q_ASSERT( layer );

  // remove old entity - if any
  removeLayerEntity( layer );

  // add new entity - if any 3D renderer
  addLayerEntity( layer );
}

void Qgs3DMapScene::onLayersChanged()
{
  QSet<QgsMapLayer *> layersBefore = qgis::listToSet( mLayerEntities.keys() );
  QList<QgsMapLayer *> layersAdded;
  Q_FOREACH ( QgsMapLayer *layer, mMap.layers() )
  {
    if ( !layersBefore.contains( layer ) )
    {
      layersAdded << layer;
    }
    else
    {
      layersBefore.remove( layer );
    }
  }

  // what is left in layersBefore are layers that have been removed
  Q_FOREACH ( QgsMapLayer *layer, layersBefore )
  {
    removeLayerEntity( layer );
  }

  Q_FOREACH ( QgsMapLayer *layer, layersAdded )
  {
    addLayerEntity( layer );
  }
}

void Qgs3DMapScene::updateTemporal()
{
  for ( auto layer : mLayerEntities.keys() )
  {
    if ( layer->temporalProperties()->isActive() )
    {
      removeLayerEntity( layer );
      addLayerEntity( layer );
    }
  }
}

void Qgs3DMapScene::addLayerEntity( QgsMapLayer *layer )
{
  bool needsSceneUpdate = false;
  QgsAbstract3DRenderer *renderer = layer->renderer3D();
  if ( renderer )
  {
    // Fix vector layer's renderer to make sure the renderer is pointing to its layer.
    // It has happened before that renderer pointed to a different layer (probably after copying a style).
    // This is a bit of a hack and it should be handled in QgsMapLayer::setRenderer3D() but in qgis_core
    // the vector layer 3D renderer classes are not available.
    if ( layer->type() == QgsMapLayerType::VectorLayer &&
         ( renderer->type() == QLatin1String( "vector" ) || renderer->type() == QLatin1String( "rulebased" ) ) )
    {
      static_cast<QgsAbstractVectorLayer3DRenderer *>( renderer )->setLayer( static_cast<QgsVectorLayer *>( layer ) );
      if ( renderer->type() == QLatin1String( "vector" ) )
      {
        QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( layer );
        if ( vlayer->geometryType() == QgsWkbTypes::PointGeometry )
        {
          const QgsPoint3DSymbol *pointSymbol = static_cast< const QgsPoint3DSymbol * >( static_cast< QgsVectorLayer3DRenderer *>( renderer )->symbol() );
          if ( pointSymbol->shape() == QgsPoint3DSymbol::Model )
          {
            mModelVectorLayers.append( layer );
          }
        }
      }
      else if ( renderer->type() == QLatin1String( "rulebased" ) )
      {
        const QgsRuleBased3DRenderer::RuleList rules = static_cast< QgsRuleBased3DRenderer *>( renderer )->rootRule()->descendants();
        for ( auto rule : rules )
        {
          const QgsPoint3DSymbol *pointSymbol = dynamic_cast< const QgsPoint3DSymbol * >( rule->symbol() );
          if ( pointSymbol && pointSymbol->shape() == QgsPoint3DSymbol::Model )
          {
            mModelVectorLayers.append( layer );
            break;
          }
        }
      }
    }
    else if ( layer->type() == QgsMapLayerType::MeshLayer && renderer->type() == QLatin1String( "mesh" ) )
    {
      QgsMeshLayer3DRenderer *meshRenderer = static_cast<QgsMeshLayer3DRenderer *>( renderer );
      meshRenderer->setLayer( static_cast<QgsMeshLayer *>( layer ) );

      // Before entity creation, set the maximum texture size
      // Not very clean, but for now, only place found in the workflow to do that simple
      QgsMesh3DSymbol *sym = meshRenderer->symbol()->clone();
      sym->setMaximumTextureSize( maximumTextureSize() );
      meshRenderer->setSymbol( sym );
    }
    else if ( layer->type() == QgsMapLayerType::PointCloudLayer && renderer->type() == QLatin1String( "pointcloud" ) )
    {
      QgsPointCloudLayer3DRenderer *pointCloudRenderer = static_cast<QgsPointCloudLayer3DRenderer *>( renderer );
      pointCloudRenderer->setLayer( static_cast<QgsPointCloudLayer *>( layer ) );
    }

    Qt3DCore::QEntity *newEntity = renderer->createEntity( mMap );
    if ( newEntity )
    {
      newEntity->setParent( this );
      mLayerEntities.insert( layer, newEntity );

      finalizeNewEntity( newEntity );

      if ( QgsChunkedEntity *chunkedNewEntity = qobject_cast<QgsChunkedEntity *>( newEntity ) )
      {
        mChunkEntities.append( chunkedNewEntity );
        needsSceneUpdate = true;

        chunkedNewEntity->setPickingEnabled( !mPickHandlers.isEmpty() );
        connect( chunkedNewEntity, &QgsChunkedEntity::pickedObject, this, &Qgs3DMapScene::onLayerEntityPickedObject );

        connect( chunkedNewEntity, &QgsChunkedEntity::newEntityCreated, this, [this]( Qt3DCore::QEntity * entity )
        {
          finalizeNewEntity( entity );
        } );

        connect( chunkedNewEntity, &QgsChunkedEntity::pendingJobsCountChanged, this, &Qgs3DMapScene::totalPendingJobsCountChanged );
      }
    }
  }

  if ( needsSceneUpdate )
    onCameraChanged();   // needed for chunked entities

  connect( layer, &QgsMapLayer::renderer3DChanged, this, &Qgs3DMapScene::onLayerRenderer3DChanged );

  if ( layer->type() == QgsMapLayerType::VectorLayer )
  {
    QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( layer );
    connect( vlayer, &QgsVectorLayer::selectionChanged, this, &Qgs3DMapScene::onLayerRenderer3DChanged );
    connect( vlayer, &QgsVectorLayer::layerModified, this, &Qgs3DMapScene::onLayerRenderer3DChanged );
  }

  if ( layer->type() == QgsMapLayerType::MeshLayer )
  {
    connect( layer, &QgsMapLayer::rendererChanged, this, &Qgs3DMapScene::onLayerRenderer3DChanged );
  }

}

void Qgs3DMapScene::removeLayerEntity( QgsMapLayer *layer )
{
  Qt3DCore::QEntity *entity = mLayerEntities.take( layer );

  if ( QgsChunkedEntity *chunkedEntity = qobject_cast<QgsChunkedEntity *>( entity ) )
  {
    mChunkEntities.removeOne( chunkedEntity );
  }

  if ( entity )
    entity->deleteLater();

  disconnect( layer, &QgsMapLayer::renderer3DChanged, this, &Qgs3DMapScene::onLayerRenderer3DChanged );

  if ( layer->type() == QgsMapLayerType::VectorLayer )
  {
    QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( layer );
    disconnect( vlayer, &QgsVectorLayer::selectionChanged, this, &Qgs3DMapScene::onLayerRenderer3DChanged );
    disconnect( vlayer, &QgsVectorLayer::layerModified, this, &Qgs3DMapScene::onLayerRenderer3DChanged );
    mModelVectorLayers.removeAll( layer );
  }

  if ( layer->type() == QgsMapLayerType::MeshLayer )
  {
    disconnect( layer, &QgsMapLayer::rendererChanged, this, &Qgs3DMapScene::onLayerRenderer3DChanged );
  }
}

void Qgs3DMapScene::finalizeNewEntity( Qt3DCore::QEntity *newEntity )
{
  // this is probably not the best place for material-specific configuration,
  // maybe this could be more generalized when other materials need some specific treatment
  for ( QgsLineMaterial *lm : newEntity->findChildren<QgsLineMaterial *>() )
  {
    connect( mCameraController, &QgsCameraController::viewportChanged, lm, [lm, this]
    {
      lm->setViewportSize( mCameraController->viewport().size() );
    } );

    lm->setViewportSize( cameraController()->viewport().size() );
  }
  // configure billboard's viewport when the viewport is changed.
  for ( QgsPoint3DBillboardMaterial *bm : newEntity->findChildren<QgsPoint3DBillboardMaterial *>() )
  {
    connect( mCameraController, &QgsCameraController::viewportChanged, bm, [bm, this]
    {
      bm->setViewportSize( mCameraController->viewport().size() );
    } );

    bm->setViewportSize( mCameraController->viewport().size() );
  }
}

int Qgs3DMapScene::maximumTextureSize() const
{
  QSurface *surface = mEngine->surface();
  QOpenGLContext context;
  context.create();
  context.makeCurrent( surface );
  QOpenGLFunctions openglFunctions( &context );
  GLint size;
  openglFunctions.glGetIntegerv( GL_MAX_TEXTURE_SIZE, &size );
  return int( size );
}

void Qgs3DMapScene::addCameraViewCenterEntity( Qt3DRender::QCamera *camera )
{
  mEntityCameraViewCenter = new Qt3DCore::QEntity;

  Qt3DCore::QTransform *trCameraViewCenter = new Qt3DCore::QTransform;
  mEntityCameraViewCenter->addComponent( trCameraViewCenter );
  connect( camera, &Qt3DRender::QCamera::viewCenterChanged, this, [trCameraViewCenter, camera]
  {
    trCameraViewCenter->setTranslation( camera->viewCenter() );
  } );

  Qt3DExtras::QPhongMaterial *materialCameraViewCenter = new Qt3DExtras::QPhongMaterial;
  materialCameraViewCenter->setAmbient( Qt::red );
  mEntityCameraViewCenter->addComponent( materialCameraViewCenter );

  Qt3DExtras::QSphereMesh *rendererCameraViewCenter = new Qt3DExtras::QSphereMesh;
  rendererCameraViewCenter->setRadius( 10 );
  mEntityCameraViewCenter->addComponent( rendererCameraViewCenter );

  mEntityCameraViewCenter->setEnabled( mMap.showCameraViewCenter() );
  mEntityCameraViewCenter->setParent( this );

  connect( &mMap, &Qgs3DMapSettings::showCameraViewCenterChanged, this, [this]
  {
    mEntityCameraViewCenter->setEnabled( mMap.showCameraViewCenter() );
  } );
}

void Qgs3DMapScene::setSceneState( Qgs3DMapScene::SceneState state )
{
  if ( mSceneState == state )
    return;
  mSceneState = state;
  emit sceneStateChanged();
}

void Qgs3DMapScene::updateSceneState()
{
  if ( mTerrainUpdateScheduled )
  {
    setSceneState( Updating );
    return;
  }

  for ( QgsChunkedEntity *entity : qgis::as_const( mChunkEntities ) )
  {
    if ( entity->isEnabled() && entity->pendingJobsCount() > 0 )
    {
      setSceneState( Updating );
      return;
    }
  }

  setSceneState( Ready );
}

void Qgs3DMapScene::onSkyboxSettingsChanged()
{
  QgsSkyboxSettings skyboxSettings = mMap.skyboxSettings();
  if ( mSkybox != nullptr )
  {
    mSkybox->deleteLater();
    mSkybox = nullptr;
  }

  mEngine->setFrustumCullingEnabled( !mMap.isSkyboxEnabled() );

  if ( mMap.isSkyboxEnabled() )
  {
    QMap<QString, QString> faces;
    switch ( skyboxSettings.skyboxType() )
    {
      case QgsSkyboxEntity::DistinctTexturesSkybox:
        faces = skyboxSettings.cubeMapFacesPaths();
        mSkybox = new QgsCubeFacesSkyboxEntity(
          faces[QStringLiteral( "posX" )], faces[QStringLiteral( "posY" )], faces[QStringLiteral( "posZ" )],
          faces[QStringLiteral( "negX" )], faces[QStringLiteral( "negY" )], faces[QStringLiteral( "negZ" )],
          this
        );
        break;
      case QgsSkyboxEntity::PanoramicSkybox:
        mSkybox = new QgsPanoramicSkyboxEntity( skyboxSettings.panoramicTexturePath(), this );
        break;
    }
  }
}

void Qgs3DMapScene::onShadowSettingsChanged()
{
  QgsWindow3DEngine *windowEngine = dynamic_cast<QgsWindow3DEngine *>( mEngine );
  if ( windowEngine == nullptr )
    return;
  QgsShadowRenderingFrameGraph *shadowRenderingFrameGraph = windowEngine->shadowRenderingFrameGraph();

  QList<QgsDirectionalLightSettings> directionalLights = mMap.directionalLights();
  QgsShadowSettings shadowSettings = mMap.shadowSettings();
  int selectedLight = shadowSettings.selectedDirectionalLight();
  if ( shadowSettings.renderShadows() && selectedLight >= 0 && selectedLight < directionalLights.count() )
  {
    shadowRenderingFrameGraph->setShadowRenderingEnabled( true );
    shadowRenderingFrameGraph->setShadowBias( shadowSettings.shadowBias() );
    shadowRenderingFrameGraph->setShadowMapResolution( shadowSettings.shadowMapResolution() );
    QgsDirectionalLightSettings light = directionalLights[selectedLight];
    shadowRenderingFrameGraph->setupDirectionalLight( light, shadowSettings.maximumShadowRenderingDistance() );
  }
  else
    shadowRenderingFrameGraph->setShadowRenderingEnabled( false );
}

void Qgs3DMapScene::onDebugShadowMapSettingsChanged()
{
  QgsWindow3DEngine *windowEngine = dynamic_cast<QgsWindow3DEngine *>( mEngine );
  if ( windowEngine == nullptr )
    return;
  QgsShadowRenderingFrameGraph *shadowRenderingFrameGraph = windowEngine->shadowRenderingFrameGraph();
  shadowRenderingFrameGraph->setupShadowMapDebugging( mMap.debugShadowMapEnabled(), mMap.debugShadowMapCorner(), mMap.debugShadowMapSize() );
}

void Qgs3DMapScene::onDebugDepthMapSettingsChanged()
{
  QgsWindow3DEngine *windowEngine = dynamic_cast<QgsWindow3DEngine *>( mEngine );
  if ( windowEngine == nullptr )
    return;
  QgsShadowRenderingFrameGraph *shadowRenderingFrameGraph = windowEngine->shadowRenderingFrameGraph();
  shadowRenderingFrameGraph->setupDepthMapDebugging( mMap.debugDepthMapEnabled(), mMap.debugDepthMapCorner(), mMap.debugDepthMapSize() );
}

void Qgs3DMapScene::onEyeDomeShadingSettingsChanged()
{
  QgsWindow3DEngine *windowEngine = dynamic_cast<QgsWindow3DEngine *>( mEngine );
  if ( windowEngine == nullptr )
    return;
  QgsShadowRenderingFrameGraph *shadowRenderingFrameGraph = windowEngine->shadowRenderingFrameGraph();

  bool edlEnabled = mMap.eyeDomeLightingEnabled();
  double edlStrength = mMap.eyeDomeLightingStrength();
  double edlDistance = mMap.eyeDomeLightingDistance();
  shadowRenderingFrameGraph->setupEyeDomeLighting( edlEnabled, edlStrength, edlDistance );
}

void Qgs3DMapScene::exportScene( const Qgs3DMapExportSettings &exportSettings )
{
  QVector<QString> notParsedLayers;
  Qgs3DSceneExporter exporter;

  exporter.setTerrainResolution( exportSettings.terrrainResolution() );
  exporter.setSmoothEdges( exportSettings.smoothEdges() );
  exporter.setExportNormals( exportSettings.exportNormals() );
  exporter.setExportTextures( exportSettings.exportTextures() );
  exporter.setTerrainTextureResolution( exportSettings.terrainTextureResolution() );
  exporter.setScale( exportSettings.scale() );

  for ( auto it = mLayerEntities.constBegin(); it != mLayerEntities.constEnd(); ++it )
  {
    QgsMapLayer *layer = it.key();
    Qt3DCore::QEntity *rootEntity = it.value();
    QgsMapLayerType layerType =  layer->type();
    switch ( layerType )
    {
      case QgsMapLayerType::VectorLayer:
        if ( !exporter.parseVectorLayerEntity( rootEntity, qobject_cast<QgsVectorLayer *>( layer ) ) )
          notParsedLayers.push_back( layer->name() );
        break;
      case QgsMapLayerType::RasterLayer:
      case QgsMapLayerType::PluginLayer:
      case QgsMapLayerType::MeshLayer:
      case QgsMapLayerType::VectorTileLayer:
      case QgsMapLayerType::AnnotationLayer:
      case QgsMapLayerType::PointCloudLayer:
        notParsedLayers.push_back( layer->name() );
        break;
    }
  }

  if ( mTerrain )
    exporter.parseTerrain( mTerrain, "Terrain" );

  exporter.save( exportSettings.sceneName(), exportSettings.sceneFolderPath() );

  if ( !notParsedLayers.empty() )
  {
    QString message = tr( "The following layers were not exported:" ) + "\n";
    for ( const QString &layerName : notParsedLayers )
      message += layerName + "\n";
    QgsMessageOutput::showMessage( tr( "3D exporter warning" ), message, QgsMessageOutput::MessageText );
  }
}
