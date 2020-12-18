/***************************************************************************
  qgs3dmapcanvas.cpp
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

#include "qgs3dmapcanvas.h"

#include <QBoxLayout>
#include <Qt3DExtras/Qt3DWindow>
#include <Qt3DRender/QRenderCapture>
#include <QMouseEvent>


#include "qgscameracontroller.h"
#include "qgs3dmapsettings.h"
#include "qgs3dmapscene.h"
#include "qgs3dmaptool.h"
#include "qgswindow3dengine.h"
#include "qgs3dnavigationwidget.h"
#include "qgsproject.h"
#include "qgsprojectviewsettings.h"
#include "qgssettings.h"
#include "qgstemporalcontroller.h"
#include "qgsflatterraingenerator.h"
#include "qgsonlineterraingenerator.h"

Qgs3DMapCanvas::Qgs3DMapCanvas( QWidget *parent )
  : QWidget( parent )
{
  QgsSettings setting;
  mEngine = new QgsWindow3DEngine( this );

  connect( mEngine, &QgsAbstract3DEngine::imageCaptured, this, [ = ]( const QImage & image )
  {
    image.save( mCaptureFileName, mCaptureFileFormat.toLocal8Bit().data() );
    emit savedAsImage( mCaptureFileName );
  } );

  mContainer = QWidget::createWindowContainer( mEngine->window() );
  mNavigationWidget = new Qgs3DNavigationWidget( this );

  QHBoxLayout *hLayout = new QHBoxLayout( this );
  hLayout->setContentsMargins( 0, 0, 0, 0 );
  hLayout->addWidget( mContainer, 1 );
  hLayout->addWidget( mNavigationWidget );
  this->setOnScreenNavigationVisibility(
    setting.value( QStringLiteral( "/3D/navigationWidget/visibility" ), true, QgsSettings::Gui ).toBool()
  );

  mEngine->window()->setCursor( Qt::OpenHandCursor );
}

Qgs3DMapCanvas::~Qgs3DMapCanvas()
{
  if ( mMapTool )
    mMapTool->deactivate();
  // make sure the scene is deleted while map settings object is still alive
  delete mScene;
  mScene = nullptr;
  delete mMap;
  mMap = nullptr;
}

void Qgs3DMapCanvas::resizeEvent( QResizeEvent *ev )
{
  QWidget::resizeEvent( ev );

  if ( !mScene )
    return;

  QRect viewportRect( QPoint( 0, 0 ), size() );
  mScene->cameraController()->setViewport( viewportRect );
}

void Qgs3DMapCanvas::setMap( Qgs3DMapSettings *map )
{
  // TODO: eventually we want to get rid of this
  Q_ASSERT( !mMap );
  Q_ASSERT( !mScene );

  //QRect viewportRect( QPoint( 0, 0 ), size() );
  Qgs3DMapScene *newScene = new Qgs3DMapScene( *map, mEngine );

  mEngine->setRootEntity( newScene );

  if ( mScene )
  {
    disconnect( mScene, &Qgs3DMapScene::fpsCountChanged, this, &Qgs3DMapCanvas::fpsCountChanged );
    disconnect( mScene, &Qgs3DMapScene::fpsCounterEnabledChanged, this, &Qgs3DMapCanvas::fpsCounterEnabledChanged );
    mScene->deleteLater();
  }
  mScene = newScene;
  connect( mScene, &Qgs3DMapScene::fpsCountChanged, this, &Qgs3DMapCanvas::fpsCountChanged );
  connect( mScene, &Qgs3DMapScene::fpsCounterEnabledChanged, this, &Qgs3DMapCanvas::fpsCounterEnabledChanged );

  delete mMap;
  mMap = map;

  resetView();

  // Connect the camera to the navigation widget.
  QObject::connect(
    this->cameraController(),
    &QgsCameraController::cameraChanged,
    mNavigationWidget,
    [ = ]
  {
    mNavigationWidget->updateFromCamera();
  }
  );

  emit mapSettingsChanged();
}

QgsCameraController *Qgs3DMapCanvas::cameraController()
{
  return mScene ? mScene->cameraController() : nullptr;
}

void Qgs3DMapCanvas::resetView( bool resetExtent )
{
  if ( resetExtent )
  {
    if ( map()->terrainGenerator()->type() == QgsTerrainGenerator::Flat ||
         map()->terrainGenerator()->type() == QgsTerrainGenerator::Online )
    {
      const QgsReferencedRectangle extent = QgsProject::instance()->viewSettings()->fullExtent();
      QgsCoordinateTransform ct( extent.crs(), map()->crs(), QgsProject::instance()->transformContext() );
      ct.setBallparkTransformsAreAppropriate( true );
      QgsRectangle rect;
      try
      {
        rect = ct.transformBoundingBox( extent );
      }
      catch ( QgsCsException & )
      {
        rect = extent;
      }
      map()->terrainGenerator()->setExtent( rect );

      // reproject terrain's extent to map CRS
      QgsRectangle te = map()->terrainGenerator()->extent();
      QgsCoordinateTransform terrainToMapTransform( map()->terrainGenerator()->crs(), map()->crs(), QgsProject::instance() );
      te = terrainToMapTransform.transformBoundingBox( te );

      QgsPointXY center = te.center();
      map()->setOrigin( QgsVector3D( center.x(), center.y(), 0 ) );
    }
  }

  mScene->viewZoomFull();
}

void Qgs3DMapCanvas::setViewFromTop( const QgsPointXY &center, float distance, float rotation )
{
  float worldX = center.x() - mMap->origin().x();
  float worldY = center.y() - mMap->origin().y();
  mScene->cameraController()->setViewFromTop( worldX, -worldY, distance, rotation );
}

void Qgs3DMapCanvas::saveAsImage( const QString fileName, const QString fileFormat )
{
  if ( !fileName.isEmpty() )
  {
    mCaptureFileName = fileName;
    mCaptureFileFormat = fileFormat;
    mEngine->requestCaptureImage();
  }
}

void Qgs3DMapCanvas::setMapTool( Qgs3DMapTool *tool )
{
  if ( tool == mMapTool )
    return;

  // For Camera Control tool
  if ( mMapTool && !tool )
  {
    mEngine->window()->removeEventFilter( this );
    mScene->cameraController()->setEnabled( true );
    mEngine->window()->setCursor( Qt::OpenHandCursor );
  }
  else if ( !mMapTool && tool )
  {
    mEngine->window()->installEventFilter( this );
    mScene->cameraController()->setEnabled( tool->allowsCameraControls() );
  }

  if ( mMapTool )
    mMapTool->deactivate();

  mMapTool = tool;

  if ( mMapTool )
  {
    mMapTool->activate();
    mEngine->window()->setCursor( mMapTool->cursor() );
  }

}

bool Qgs3DMapCanvas::eventFilter( QObject *watched, QEvent *event )
{
  if ( !mMapTool )
    return false;

  Q_UNUSED( watched )
  switch ( event->type() )
  {
    case QEvent::MouseButtonPress:
      mMapTool->mousePressEvent( static_cast<QMouseEvent *>( event ) );
      break;
    case QEvent::MouseButtonRelease:
      mMapTool->mouseReleaseEvent( static_cast<QMouseEvent *>( event ) );
      break;
    case QEvent::MouseMove:
      mMapTool->mouseMoveEvent( static_cast<QMouseEvent *>( event ) );
      break;
    default:
      break;
  }
  return false;
}

void Qgs3DMapCanvas::setOnScreenNavigationVisibility( bool visibility )
{
  mNavigationWidget->setVisible( visibility );
  QgsSettings setting;
  setting.setValue( QStringLiteral( "/3D/navigationWidget/visibility" ), visibility, QgsSettings::Gui );
}

void Qgs3DMapCanvas::setTemporalController( QgsTemporalController *temporalController )
{
  if ( mTemporalController )
    disconnect( mTemporalController, &QgsTemporalController::updateTemporalRange, this, &Qgs3DMapCanvas::updateTemporalRange );

  mTemporalController = temporalController;
  connect( mTemporalController, &QgsTemporalController::updateTemporalRange, this, &Qgs3DMapCanvas::updateTemporalRange );
}

void Qgs3DMapCanvas::updateTemporalRange( const QgsDateTimeRange &temporalrange )
{
  mMap->setTemporalRange( temporalrange );
  mScene->updateTemporal();
}
