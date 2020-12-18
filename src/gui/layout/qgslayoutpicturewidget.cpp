/***************************************************************************
                         qgslayoutpicturewidget.cpp
                         --------------------------
    begin                : October 2017
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

#include "qgslayoutpicturewidget.h"
#include "qgsapplication.h"
#include "qgslayoutitemmap.h"
#include "qgslayoutitempicture.h"
#include "qgslayout.h"
#include "qgsexpressionbuilderdialog.h"
#include "qgssvgcache.h"
#include "qgssettings.h"
#include "qgssvgselectorwidget.h"

#include <QDoubleValidator>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QPainter>
#include <QProgressDialog>
#include <QSvgRenderer>

QgsLayoutPictureWidget::QgsLayoutPictureWidget( QgsLayoutItemPicture *picture )
  : QgsLayoutItemBaseWidget( nullptr, picture )
  , mPicture( picture )
{
  setupUi( this );

  mResizeModeComboBox->addItem( tr( "Zoom" ), QgsLayoutItemPicture::Zoom );
  mResizeModeComboBox->addItem( tr( "Stretch" ), QgsLayoutItemPicture::Stretch );
  mResizeModeComboBox->addItem( tr( "Clip" ), QgsLayoutItemPicture::Clip );
  mResizeModeComboBox->addItem( tr( "Zoom and Resize Frame" ), QgsLayoutItemPicture::ZoomResizeFrame );
  mResizeModeComboBox->addItem( tr( "Resize Frame to Image Size" ), QgsLayoutItemPicture::FrameToImageSize );

  mAnchorPointComboBox->addItem( tr( "Top Left" ), QgsLayoutItem::UpperLeft );
  mAnchorPointComboBox->addItem( tr( "Top Center" ), QgsLayoutItem::UpperMiddle );
  mAnchorPointComboBox->addItem( tr( "Top Right" ), QgsLayoutItem::UpperRight );
  mAnchorPointComboBox->addItem( tr( "Middle Left" ), QgsLayoutItem::MiddleLeft );
  mAnchorPointComboBox->addItem( tr( "Middle" ), QgsLayoutItem::Middle );
  mAnchorPointComboBox->addItem( tr( "Middle Right" ), QgsLayoutItem::MiddleRight );
  mAnchorPointComboBox->addItem( tr( "Bottom Left" ), QgsLayoutItem::LowerLeft );
  mAnchorPointComboBox->addItem( tr( "Bottom Center" ), QgsLayoutItem::LowerMiddle );
  mAnchorPointComboBox->addItem( tr( "Bottom Right" ), QgsLayoutItem::LowerRight );

  connect( mPictureRotationSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsLayoutPictureWidget::mPictureRotationSpinBox_valueChanged );
  connect( mRotationFromComposerMapCheckBox, &QCheckBox::stateChanged, this, &QgsLayoutPictureWidget::mRotationFromComposerMapCheckBox_stateChanged );
  connect( mResizeModeComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsLayoutPictureWidget::mResizeModeComboBox_currentIndexChanged );
  connect( mAnchorPointComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsLayoutPictureWidget::mAnchorPointComboBox_currentIndexChanged );
  connect( mFillColorButton, &QgsColorButton::colorChanged, this, &QgsLayoutPictureWidget::mFillColorButton_colorChanged );
  connect( mStrokeColorButton, &QgsColorButton::colorChanged, this, &QgsLayoutPictureWidget::mStrokeColorButton_colorChanged );
  connect( mStrokeWidthSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsLayoutPictureWidget::mStrokeWidthSpinBox_valueChanged );
  connect( mPictureRotationOffsetSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsLayoutPictureWidget::mPictureRotationOffsetSpinBox_valueChanged );
  connect( mNorthTypeComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsLayoutPictureWidget::mNorthTypeComboBox_currentIndexChanged );
  connect( mRadioSVG, &QRadioButton::toggled, this, &QgsLayoutPictureWidget::modeChanged );
  connect( mRadioRaster, &QRadioButton::toggled, this, &QgsLayoutPictureWidget::modeChanged );
  connect( mSvgSourceLineEdit, &QgsSvgSourceLineEdit::sourceChanged, this, &QgsLayoutPictureWidget::svgSourceChanged );
  connect( mImageSourceLineEdit, &QgsImageSourceLineEdit::sourceChanged, this, &QgsLayoutPictureWidget::rasterSourceChanged );

  mSvgSourceLineEdit->setLastPathSettingsKey( QStringLiteral( "/UI/lastComposerPictureDir" ) );

  setPanelTitle( tr( "Picture Properties" ) );

  mFillColorButton->setAllowOpacity( true );
  mFillColorButton->setColorDialogTitle( tr( "Select Fill Color" ) );
  mFillColorButton->setContext( QStringLiteral( "composer" ) );
  mStrokeColorButton->setAllowOpacity( true );
  mStrokeColorButton->setColorDialogTitle( tr( "Select Stroke Color" ) );
  mStrokeColorButton->setContext( QStringLiteral( "composer" ) );

  mFillColorDDBtn->registerLinkedWidget( mFillColorButton );
  mStrokeColorDDBtn->registerLinkedWidget( mStrokeColorButton );

  mNorthTypeComboBox->blockSignals( true );
  mNorthTypeComboBox->addItem( tr( "Grid North" ), QgsLayoutItemPicture::GridNorth );
  mNorthTypeComboBox->addItem( tr( "True North" ), QgsLayoutItemPicture::TrueNorth );
  mNorthTypeComboBox->blockSignals( false );
  mPictureRotationOffsetSpinBox->setClearValue( 0.0 );
  mPictureRotationSpinBox->setClearValue( 0.0 );

  viewGroups->setHeaderHidden( true );
#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
  mIconSize = std::max( 30, static_cast< int >( std::round( Qgis::UI_SCALE_FACTOR * fontMetrics().width( 'X' ) * 4 ) ) );
#else
  mIconSize = std::max( 30, static_cast< int >( std::round( Qgis::UI_SCALE_FACTOR * fontMetrics().horizontalAdvance( 'X' ) * 4 ) ) );
#endif
  viewImages->setGridSize( QSize( mIconSize * 1.2, mIconSize * 1.2 ) );
  viewImages->setUniformItemSizes( false );
  populateList();

  connect( viewImages->selectionModel(), &QItemSelectionModel::currentChanged, this, &QgsLayoutPictureWidget::setSvgName );
  connect( viewGroups->selectionModel(), &QItemSelectionModel::currentChanged, this, &QgsLayoutPictureWidget::populateIcons );


  //add widget for general composer item properties
  mItemPropertiesWidget = new QgsLayoutItemPropertiesWidget( this, picture );
  mainLayout->addWidget( mItemPropertiesWidget );

  if ( mPicture->layout() )
  {
    mComposerMapComboBox->setCurrentLayout( mPicture->layout() );
    mComposerMapComboBox->setItemType( QgsLayoutItemRegistry::LayoutMap );
    connect( mComposerMapComboBox, &QgsLayoutItemComboBox::itemChanged, this, &QgsLayoutPictureWidget::mapChanged );
  }

  setGuiElementValues();

  connect( mPicture, &QgsLayoutObject::changed, this, &QgsLayoutPictureWidget::setGuiElementValues );
  connect( mPicture, &QgsLayoutItemPicture::pictureRotationChanged, this, &QgsLayoutPictureWidget::setPicRotationSpinValue );

  //connections for data defined buttons
  mSourceDDBtn->registerEnabledWidget( mImageSourceLineEdit, false );
  mSourceDDBtn->registerEnabledWidget( mSvgSourceLineEdit, false );

  registerDataDefinedButton( mSourceDDBtn, QgsLayoutObject::PictureSource );
  registerDataDefinedButton( mFillColorDDBtn, QgsLayoutObject::PictureSvgBackgroundColor );
  registerDataDefinedButton( mStrokeColorDDBtn, QgsLayoutObject::PictureSvgStrokeColor );
  registerDataDefinedButton( mStrokeWidthDDBtn, QgsLayoutObject::PictureSvgStrokeWidth );

  updatePictureTypeWidgets();
}

void QgsLayoutPictureWidget::setMasterLayout( QgsMasterLayoutInterface *masterLayout )
{
  if ( mItemPropertiesWidget )
    mItemPropertiesWidget->setMasterLayout( masterLayout );
}

void QgsLayoutPictureWidget::mPictureRotationSpinBox_valueChanged( double d )
{
  if ( mPicture )
  {
    mPicture->beginCommand( tr( "Change Picture Rotation" ), QgsLayoutItem::UndoPictureRotation );
    mPicture->setPictureRotation( d );
    mPicture->endCommand();
  }
}

void QgsLayoutPictureWidget::mResizeModeComboBox_currentIndexChanged( int )
{
  if ( !mPicture )
  {
    return;
  }

  mPicture->beginCommand( tr( "Change Resize Mode" ) );
  mPicture->setResizeMode( static_cast< QgsLayoutItemPicture::ResizeMode >( mResizeModeComboBox->currentData().toInt() ) );
  mPicture->endCommand();

  //disable picture rotation for non-zoom modes
  mRotationGroupBox->setEnabled( mPicture->resizeMode() == QgsLayoutItemPicture::Zoom ||
                                 mPicture->resizeMode() == QgsLayoutItemPicture::ZoomResizeFrame );

  //disable anchor point control for certain zoom modes
  if ( mPicture->resizeMode() == QgsLayoutItemPicture::Zoom ||
       mPicture->resizeMode() == QgsLayoutItemPicture::Clip )
  {
    mAnchorPointComboBox->setEnabled( true );
  }
  else
  {
    mAnchorPointComboBox->setEnabled( false );
  }
}

void QgsLayoutPictureWidget::mAnchorPointComboBox_currentIndexChanged( int )
{
  if ( !mPicture )
  {
    return;
  }

  mPicture->beginCommand( tr( "Change Placement" ) );
  mPicture->setPictureAnchor( static_cast< QgsLayoutItem::ReferencePoint >( mAnchorPointComboBox->currentData().toInt() ) );
  mPicture->endCommand();
}

bool QgsLayoutPictureWidget::setNewItem( QgsLayoutItem *item )
{
  if ( item->type() != QgsLayoutItemRegistry::LayoutPicture )
    return false;

  if ( mPicture )
  {
    disconnect( mPicture, &QgsLayoutObject::changed, this, &QgsLayoutPictureWidget::setGuiElementValues );
    disconnect( mPicture, &QgsLayoutItemPicture::pictureRotationChanged, this, &QgsLayoutPictureWidget::setPicRotationSpinValue );
  }

  mPicture = qobject_cast< QgsLayoutItemPicture * >( item );
  mItemPropertiesWidget->setItem( mPicture );

  if ( mPicture )
  {
    connect( mPicture, &QgsLayoutObject::changed, this, &QgsLayoutPictureWidget::setGuiElementValues );
    connect( mPicture, &QgsLayoutItemPicture::pictureRotationChanged, this, &QgsLayoutPictureWidget::setPicRotationSpinValue );
  }

  setGuiElementValues();

  return true;
}

void QgsLayoutPictureWidget::mRotationFromComposerMapCheckBox_stateChanged( int state )
{
  if ( !mPicture )
  {
    return;
  }

  mPicture->beginCommand( tr( "Toggle Rotation Sync" ) );
  if ( state == Qt::Unchecked )
  {
    mPicture->setLinkedMap( nullptr );
    mPictureRotationSpinBox->setEnabled( true );
    mComposerMapComboBox->setEnabled( false );
    mNorthTypeComboBox->setEnabled( false );
    mPictureRotationOffsetSpinBox->setEnabled( false );
    mPicture->setPictureRotation( mPictureRotationSpinBox->value() );
  }
  else
  {
    QgsLayoutItemMap *map = qobject_cast< QgsLayoutItemMap * >( mComposerMapComboBox->currentItem() );
    mPicture->setLinkedMap( map );
    mPictureRotationSpinBox->setEnabled( false );
    mNorthTypeComboBox->setEnabled( true );
    mPictureRotationOffsetSpinBox->setEnabled( true );
    mComposerMapComboBox->setEnabled( true );
  }
  mPicture->endCommand();
}

void QgsLayoutPictureWidget::mapChanged( QgsLayoutItem *item )
{
  if ( !mPicture )
  {
    return;
  }

  //get composition
  const QgsLayout *layout = mPicture->layout();
  if ( !layout )
  {
    return;
  }

  QgsLayoutItemMap *map = qobject_cast< QgsLayoutItemMap *>( item );
  if ( !map )
  {
    return;
  }

  mPicture->beginCommand( tr( "Change Rotation Map" ) );
  mPicture->setLinkedMap( map );
  mPicture->update();
  mPicture->endCommand();
}

void QgsLayoutPictureWidget::setPicRotationSpinValue( double r )
{
  mPictureRotationSpinBox->blockSignals( true );
  mPictureRotationSpinBox->setValue( r );
  mPictureRotationSpinBox->blockSignals( false );
}

void QgsLayoutPictureWidget::setGuiElementValues()
{
  //set initial gui values
  if ( mPicture )
  {
    mPictureRotationSpinBox->blockSignals( true );
    mComposerMapComboBox->blockSignals( true );
    mRotationFromComposerMapCheckBox->blockSignals( true );
    mNorthTypeComboBox->blockSignals( true );
    mPictureRotationOffsetSpinBox->blockSignals( true );
    mResizeModeComboBox->blockSignals( true );
    mAnchorPointComboBox->blockSignals( true );
    mFillColorButton->blockSignals( true );
    mStrokeColorButton->blockSignals( true );
    mStrokeWidthSpinBox->blockSignals( true );

    mPictureRotationSpinBox->setValue( mPicture->pictureRotation() );

    mComposerMapComboBox->setItem( mPicture->linkedMap() );

    if ( mPicture->linkedMap() )
    {
      mRotationFromComposerMapCheckBox->setCheckState( Qt::Checked );
      mPictureRotationSpinBox->setEnabled( false );
      mComposerMapComboBox->setEnabled( true );
      mNorthTypeComboBox->setEnabled( true );
      mPictureRotationOffsetSpinBox->setEnabled( true );
    }
    else
    {
      mRotationFromComposerMapCheckBox->setCheckState( Qt::Unchecked );
      mPictureRotationSpinBox->setEnabled( true );
      mComposerMapComboBox->setEnabled( false );
      mNorthTypeComboBox->setEnabled( false );
      mPictureRotationOffsetSpinBox->setEnabled( false );
    }
    mNorthTypeComboBox->setCurrentIndex( mNorthTypeComboBox->findData( mPicture->northMode() ) );
    mPictureRotationOffsetSpinBox->setValue( mPicture->northOffset() );

    mResizeModeComboBox->setCurrentIndex( mResizeModeComboBox->findData( mPicture->resizeMode() ) );
    //disable picture rotation for non-zoom modes
    mRotationGroupBox->setEnabled( mPicture->resizeMode() == QgsLayoutItemPicture::Zoom ||
                                   mPicture->resizeMode() == QgsLayoutItemPicture::ZoomResizeFrame );

    mAnchorPointComboBox->setCurrentIndex( mAnchorPointComboBox->findData( mPicture->pictureAnchor() ) );
    //disable anchor point control for certain zoom modes
    if ( mPicture->resizeMode() == QgsLayoutItemPicture::Zoom ||
         mPicture->resizeMode() == QgsLayoutItemPicture::Clip )
    {
      mAnchorPointComboBox->setEnabled( true );
    }
    else
    {
      mAnchorPointComboBox->setEnabled( false );
    }

    whileBlocking( mRadioSVG )->setChecked( mPicture->mode() == QgsLayoutItemPicture::FormatSVG );
    whileBlocking( mRadioRaster )->setChecked( mPicture->mode() == QgsLayoutItemPicture::FormatRaster );
    updatePictureTypeWidgets();

    if ( mRadioSVG->isChecked() )
    {
      whileBlocking( mSvgSourceLineEdit )->setSource( mPicture->picturePath() );

      mBlockSvgModelChanges++;
      QAbstractItemModel *m = viewImages->model();
      QItemSelectionModel *selModel = viewImages->selectionModel();
      for ( int i = 0; i < m->rowCount(); i++ )
      {
        QModelIndex idx( m->index( i, 0 ) );
        if ( m->data( idx ).toString() == mPicture->picturePath() )
        {
          selModel->select( idx, QItemSelectionModel::SelectCurrent );
          selModel->setCurrentIndex( idx, QItemSelectionModel::SelectCurrent );
          break;
        }
      }
      mBlockSvgModelChanges--;
    }
    else if ( mRadioRaster->isChecked() )
    {
      whileBlocking( mImageSourceLineEdit )->setSource( mPicture->picturePath() );
    }

    updateSvgParamGui( false );
    mFillColorButton->setColor( mPicture->svgFillColor() );
    mStrokeColorButton->setColor( mPicture->svgStrokeColor() );
    mStrokeWidthSpinBox->setValue( mPicture->svgStrokeWidth() );

    mRotationFromComposerMapCheckBox->blockSignals( false );
    mPictureRotationSpinBox->blockSignals( false );
    mComposerMapComboBox->blockSignals( false );
    mNorthTypeComboBox->blockSignals( false );
    mPictureRotationOffsetSpinBox->blockSignals( false );
    mResizeModeComboBox->blockSignals( false );
    mAnchorPointComboBox->blockSignals( false );
    mFillColorButton->blockSignals( false );
    mStrokeColorButton->blockSignals( false );
    mStrokeWidthSpinBox->blockSignals( false );

    populateDataDefinedButtons();
  }
}

void QgsLayoutPictureWidget::updateSvgParamGui( bool resetValues )
{
  if ( !mPicture )
    return;

  QString picturePath = mPicture->picturePath();

  //activate gui for svg parameters only if supported by the svg file
  bool hasFillParam, hasFillOpacityParam, hasStrokeParam, hasStrokeWidthParam, hasStrokeOpacityParam;
  QColor defaultFill, defaultStroke;
  double defaultStrokeWidth, defaultFillOpacity, defaultStrokeOpacity;
  bool hasDefaultFillColor, hasDefaultFillOpacity, hasDefaultStrokeColor, hasDefaultStrokeWidth, hasDefaultStrokeOpacity;
  QgsApplication::svgCache()->containsParams( picturePath, hasFillParam, hasDefaultFillColor, defaultFill,
      hasFillOpacityParam, hasDefaultFillOpacity, defaultFillOpacity,
      hasStrokeParam, hasDefaultStrokeColor, defaultStroke,
      hasStrokeWidthParam, hasDefaultStrokeWidth, defaultStrokeWidth,
      hasStrokeOpacityParam, hasDefaultStrokeOpacity, defaultStrokeOpacity );

  if ( resetValues )
  {
    QColor fill = mFillColorButton->color();
    double newOpacity = hasFillOpacityParam ? fill.alphaF() : 1.0;
    if ( hasDefaultFillColor )
    {
      fill = defaultFill;
    }
    fill.setAlphaF( hasDefaultFillOpacity ? defaultFillOpacity : newOpacity );
    mFillColorButton->setColor( fill );
  }
  mFillColorButton->setEnabled( hasFillParam );
  mFillColorButton->setAllowOpacity( hasFillOpacityParam );
  if ( resetValues )
  {
    QColor stroke = mStrokeColorButton->color();
    double newOpacity = hasStrokeOpacityParam ? stroke.alphaF() : 1.0;
    if ( hasDefaultStrokeColor )
    {
      stroke = defaultStroke;
    }
    stroke.setAlphaF( hasDefaultStrokeOpacity ? defaultStrokeOpacity : newOpacity );
    mStrokeColorButton->setColor( stroke );
  }
  mStrokeColorButton->setEnabled( hasStrokeParam );
  mStrokeColorButton->setAllowOpacity( hasStrokeOpacityParam );
  if ( hasDefaultStrokeWidth && resetValues )
  {
    mStrokeWidthSpinBox->setValue( defaultStrokeWidth );
  }
  mStrokeWidthSpinBox->setEnabled( hasStrokeWidthParam );
}

void QgsLayoutPictureWidget::mFillColorButton_colorChanged( const QColor &color )
{
  mPicture->beginCommand( tr( "Change Picture Fill Color" ), QgsLayoutItem::UndoPictureFillColor );
  mPicture->setSvgFillColor( color );
  mPicture->endCommand();
  mPicture->update();
}

void QgsLayoutPictureWidget::mStrokeColorButton_colorChanged( const QColor &color )
{
  mPicture->beginCommand( tr( "Change Picture Stroke Color" ), QgsLayoutItem::UndoPictureStrokeColor );
  mPicture->setSvgStrokeColor( color );
  mPicture->endCommand();
  mPicture->update();
}

void QgsLayoutPictureWidget::mStrokeWidthSpinBox_valueChanged( double d )
{
  mPicture->beginCommand( tr( "Change Picture Stroke Width" ), QgsLayoutItem::UndoPictureStrokeWidth );
  mPicture->setSvgStrokeWidth( d );
  mPicture->endCommand();
  mPicture->update();
}

void QgsLayoutPictureWidget::mPictureRotationOffsetSpinBox_valueChanged( double d )
{
  mPicture->beginCommand( tr( "Change Picture North Offset" ), QgsLayoutItem::UndoPictureNorthOffset );
  mPicture->setNorthOffset( d );
  mPicture->endCommand();
  mPicture->update();
}

void QgsLayoutPictureWidget::mNorthTypeComboBox_currentIndexChanged( int index )
{
  mPicture->beginCommand( tr( "Change Picture North Mode" ) );
  mPicture->setNorthMode( static_cast< QgsLayoutItemPicture::NorthMode >( mNorthTypeComboBox->itemData( index ).toInt() ) );
  mPicture->endCommand();
  mPicture->update();
}

void QgsLayoutPictureWidget::modeChanged()
{
  const QgsLayoutItemPicture::Format newFormat = mRadioSVG->isChecked() ? QgsLayoutItemPicture::FormatSVG : QgsLayoutItemPicture::FormatRaster;
  if ( mPicture && mPicture->mode() != newFormat )
  {
    whileBlocking( mSvgSourceLineEdit )->setSource( QString() );
    whileBlocking( mImageSourceLineEdit )->setSource( QString() );
    mPicture->beginCommand( tr( "Change Picture Type" ) );
    mPicture->setPicturePath( QString(), newFormat );
    mPicture->endCommand();
  }
  updatePictureTypeWidgets();
}

void QgsLayoutPictureWidget::updatePictureTypeWidgets()
{
  mRasterFrame->setVisible( mRadioRaster->isChecked() );
  mSVGFrame->setVisible( mRadioSVG->isChecked() );
  mSVGParamsGroupBox->setVisible( mRadioSVG->isChecked() );

  // need to move the data defined button to the appropriate frame -- we can't have two buttons linked to the one property!
  if ( mRadioSVG->isChecked() )
    mSvgDDBtnFrame->layout()->addWidget( mSourceDDBtn );
  else
    mRasterDDBtnFrame->layout()->addWidget( mSourceDDBtn );
}

void QgsLayoutPictureWidget::populateList()
{
  QAbstractItemModel *oldModel = viewGroups->model();
  QgsSvgSelectorGroupsModel *g = new QgsSvgSelectorGroupsModel( viewGroups );
  viewGroups->setModel( g );
  delete oldModel;

  // Set the tree expanded at the first level
  int rows = g->rowCount( g->indexFromItem( g->invisibleRootItem() ) );
  for ( int i = 0; i < rows; i++ )
  {
    viewGroups->setExpanded( g->indexFromItem( g->item( i ) ), true );
  }

  // Initially load the icons in the List view without any grouping
  oldModel = viewImages->model();
  QgsSvgSelectorListModel *m = new QgsSvgSelectorListModel( viewImages, mIconSize );
  viewImages->setModel( m );

  delete oldModel;
}

void QgsLayoutPictureWidget::populateIcons( const QModelIndex &idx )
{
  QString path = idx.data( Qt::UserRole + 1 ).toString();

  QAbstractItemModel *oldModel = viewImages->model();
  QgsSvgSelectorListModel *m = new QgsSvgSelectorListModel( viewImages, path, mIconSize );
  viewImages->setModel( m );
  delete oldModel;

  connect( viewImages->selectionModel(), &QItemSelectionModel::currentChanged, this, &QgsLayoutPictureWidget::setSvgName );
}

void QgsLayoutPictureWidget::setSvgName( const QModelIndex &idx )
{
  if ( mBlockSvgModelChanges )
    return;

  QString name = idx.data( Qt::UserRole ).toString();
  whileBlocking( mSvgSourceLineEdit )->setSource( name );
  svgSourceChanged( name );
}

void QgsLayoutPictureWidget::svgSourceChanged( const QString &source )
{
  if ( mPicture )
  {
    mPicture->beginCommand( tr( "Change Picture" ) );
    mPicture->setPicturePath( source, QgsLayoutItemPicture::FormatSVG );
    mPicture->update();
    mPicture->endCommand();
    updateSvgParamGui();
  }
}

void QgsLayoutPictureWidget::rasterSourceChanged( const QString &source )
{
  if ( mPicture )
  {
    mPicture->beginCommand( tr( "Change Picture" ) );
    mPicture->setPicturePath( source, QgsLayoutItemPicture::FormatRaster );
    mPicture->update();
    mPicture->endCommand();
  }
}

void QgsLayoutPictureWidget::populateDataDefinedButtons()
{
  updateDataDefinedButton( mSourceDDBtn );
  updateDataDefinedButton( mFillColorDDBtn );
  updateDataDefinedButton( mStrokeColorDDBtn );
  updateDataDefinedButton( mStrokeWidthDDBtn );

  //initial state of controls - disable related controls when dd buttons are active
  mImageSourceLineEdit->setEnabled( !mSourceDDBtn->isActive() );
  mSvgSourceLineEdit->setEnabled( !mSourceDDBtn->isActive() );
}

