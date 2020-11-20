/***************************************************************************
 qgssymbollayerwidget.cpp - symbol layer widgets

 ---------------------
 begin                : November 2009
 copyright            : (C) 2009 by Martin Dobias
 email                : wonder dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgssymbollayerwidget.h"

#include "qgsdataitem.h"
#include "qgslinesymbollayer.h"
#include "qgsmarkersymbollayer.h"
#include "qgsfillsymbollayer.h"
#include "qgsgeometrygeneratorsymbollayer.h"
#include "qgssymbolslistwidget.h"
#include "qgsexpressioncontextutils.h"

#include "characterwidget.h"
#include "qgsdashspacedialog.h"
#include "qgssymbolselectordialog.h"
#include "qgssvgcache.h"
#include "qgssymbollayerutils.h"
#include "qgscolorramp.h"
#include "qgscolorrampbutton.h"
#include "qgsfontutils.h"
#include "qgsgradientcolorrampdialog.h"
#include "qgsproperty.h"
#include "qgsstyle.h" //for symbol selector dialog
#include "qgsmapcanvas.h"
#include "qgsapplication.h"
#include "qgsvectorlayer.h"
#include "qgssvgselectorwidget.h"
#include "qgslogger.h"
#include "qgssettings.h"
#include "qgsnewauxiliarylayerdialog.h"
#include "qgsnewauxiliaryfielddialog.h"
#include "qgsauxiliarystorage.h"
#include "qgsimagecache.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QColorDialog>
#include <QCursor>
#include <QDir>
#include <QFileDialog>
#include <QPainter>
#include <QStandardItemModel>
#include <QSvgRenderer>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QInputDialog>

QgsExpressionContext QgsSymbolLayerWidget::createExpressionContext() const
{
  if ( auto *lExpressionContext = mContext.expressionContext() )
    return *lExpressionContext;

  QgsExpressionContext expContext( mContext.globalProjectAtlasMapLayerScopes( vectorLayer() ) );

  QgsExpressionContextScope *symbolScope = QgsExpressionContextUtils::updateSymbolScope( nullptr, new QgsExpressionContextScope() );
  if ( const QgsSymbolLayer *symbolLayer = const_cast< QgsSymbolLayerWidget * >( this )->symbolLayer() )
  {
    //cheat a bit - set the symbol color variable to match the symbol layer's color (when we should really be using the *symbols*
    //color, but that's not accessible here). 99% of the time these will be the same anyway
    symbolScope->addVariable( QgsExpressionContextScope::StaticVariable( QgsExpressionContext::EXPR_SYMBOL_COLOR, symbolLayer->color(), true ) );
  }
  expContext << symbolScope;
  expContext.lastScope()->addVariable( QgsExpressionContextScope::StaticVariable( QgsExpressionContext::EXPR_GEOMETRY_PART_COUNT, 1, true ) );
  expContext.lastScope()->addVariable( QgsExpressionContextScope::StaticVariable( QgsExpressionContext::EXPR_GEOMETRY_PART_NUM, 1, true ) );
  expContext.lastScope()->addVariable( QgsExpressionContextScope::StaticVariable( QgsExpressionContext::EXPR_GEOMETRY_POINT_COUNT, 1, true ) );
  expContext.lastScope()->addVariable( QgsExpressionContextScope::StaticVariable( QgsExpressionContext::EXPR_GEOMETRY_POINT_NUM, 1, true ) );
  expContext.lastScope()->addVariable( QgsExpressionContextScope::StaticVariable( QStringLiteral( "symbol_layer_count" ), 1, true ) );
  expContext.lastScope()->addVariable( QgsExpressionContextScope::StaticVariable( QStringLiteral( "symbol_layer_index" ), 1, true ) );
  expContext.lastScope()->addVariable( QgsExpressionContextScope::StaticVariable( QStringLiteral( "symbol_marker_row" ), 1, true ) );
  expContext.lastScope()->addVariable( QgsExpressionContextScope::StaticVariable( QStringLiteral( "symbol_marker_column" ), 1, true ) );

  // additional scopes
  const auto constAdditionalExpressionContextScopes = mContext.additionalExpressionContextScopes();
  for ( const QgsExpressionContextScope &scope : constAdditionalExpressionContextScopes )
  {
    expContext.appendScope( new QgsExpressionContextScope( scope ) );
  }

  //TODO - show actual value
  expContext.setOriginalValueVariable( QVariant() );

  QStringList highlights;
  highlights << QgsExpressionContext::EXPR_ORIGINAL_VALUE << QgsExpressionContext::EXPR_SYMBOL_COLOR
             << QgsExpressionContext::EXPR_GEOMETRY_PART_COUNT << QgsExpressionContext::EXPR_GEOMETRY_PART_NUM
             << QgsExpressionContext::EXPR_GEOMETRY_POINT_COUNT << QgsExpressionContext::EXPR_GEOMETRY_POINT_NUM
             << QgsExpressionContext::EXPR_CLUSTER_COLOR << QgsExpressionContext::EXPR_CLUSTER_SIZE
             << QStringLiteral( "symbol_layer_count" ) << QStringLiteral( "symbol_layer_index" );


  if ( expContext.hasVariable( QStringLiteral( "zoom_level" ) ) )
  {
    highlights << QStringLiteral( "zoom_level" );
  }
  if ( expContext.hasVariable( QStringLiteral( "vector_tile_zoom" ) ) )
  {
    highlights << QStringLiteral( "vector_tile_zoom" );
  }

  expContext.setHighlightedVariables( highlights );

  return expContext;
}

void QgsSymbolLayerWidget::setContext( const QgsSymbolWidgetContext &context )
{
  mContext = context;
  const auto unitSelectionWidgets = findChildren<QgsUnitSelectionWidget *>();
  for ( QgsUnitSelectionWidget *unitWidget : unitSelectionWidgets )
  {
    unitWidget->setMapCanvas( mContext.mapCanvas() );
  }
}

QgsSymbolWidgetContext QgsSymbolLayerWidget::context() const
{
  return mContext;
}

void QgsSymbolLayerWidget::registerDataDefinedButton( QgsPropertyOverrideButton *button, QgsSymbolLayer::Property key )
{
  button->init( key, symbolLayer()->dataDefinedProperties(), QgsSymbolLayer::propertyDefinitions(), mVectorLayer, true );
  connect( button, &QgsPropertyOverrideButton::changed, this, &QgsSymbolLayerWidget::updateDataDefinedProperty );
  connect( button, &QgsPropertyOverrideButton::createAuxiliaryField, this, &QgsSymbolLayerWidget::createAuxiliaryField );

  button->registerExpressionContextGenerator( this );
}

void QgsSymbolLayerWidget::createAuxiliaryField()
{
  // try to create an auxiliary layer if not yet created
  if ( !mVectorLayer->auxiliaryLayer() )
  {
    QgsNewAuxiliaryLayerDialog dlg( mVectorLayer, this );
    dlg.exec();
  }

  // return if still not exists
  if ( !mVectorLayer->auxiliaryLayer() )
    return;

  QgsPropertyOverrideButton *button = qobject_cast<QgsPropertyOverrideButton *>( sender() );
  QgsSymbolLayer::Property key = static_cast<  QgsSymbolLayer::Property >( button->propertyKey() );
  QgsPropertyDefinition def = QgsSymbolLayer::propertyDefinitions()[key];

  // create property in auxiliary storage if necessary
  if ( !mVectorLayer->auxiliaryLayer()->exists( def ) )
  {
    QgsNewAuxiliaryFieldDialog dlg( def, mVectorLayer, true, this );
    if ( dlg.exec() == QDialog::Accepted )
      def = dlg.propertyDefinition();
  }

  // return if still not exist
  if ( !mVectorLayer->auxiliaryLayer()->exists( def ) )
    return;

  // update property with join field name from auxiliary storage
  QgsProperty property = button->toProperty();
  property.setField( QgsAuxiliaryLayer::nameFromProperty( def, true ) );
  property.setActive( true );
  button->updateFieldLists();
  button->setToProperty( property );
  symbolLayer()->setDataDefinedProperty( key, button->toProperty() );

  emit changed();
}

void QgsSymbolLayerWidget::updateDataDefinedProperty()
{
  QgsPropertyOverrideButton *button = qobject_cast<QgsPropertyOverrideButton *>( sender() );
  QgsSymbolLayer::Property key = static_cast<  QgsSymbolLayer::Property >( button->propertyKey() );
  symbolLayer()->setDataDefinedProperty( key, button->toProperty() );
  emit changed();
}

QgsSimpleLineSymbolLayerWidget::QgsSimpleLineSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;

  setupUi( this );
  connect( mCustomCheckBox, &QCheckBox::stateChanged, this, &QgsSimpleLineSymbolLayerWidget::mCustomCheckBox_stateChanged );
  connect( mChangePatternButton, &QPushButton::clicked, this, &QgsSimpleLineSymbolLayerWidget::mChangePatternButton_clicked );
  connect( mPenWidthUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSimpleLineSymbolLayerWidget::mPenWidthUnitWidget_changed );
  connect( mOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSimpleLineSymbolLayerWidget::mOffsetUnitWidget_changed );
  connect( mDashPatternUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSimpleLineSymbolLayerWidget::mDashPatternUnitWidget_changed );
  connect( mDrawInsideCheckBox, &QCheckBox::stateChanged, this, &QgsSimpleLineSymbolLayerWidget::mDrawInsideCheckBox_stateChanged );
  connect( mPatternOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSimpleLineSymbolLayerWidget::patternOffsetUnitChanged );
  connect( mCheckAlignDash, &QCheckBox::toggled, this, [ = ]
  {
    mCheckDashCorners->setEnabled( mCheckAlignDash->isChecked() );
    if ( !mCheckAlignDash->isChecked() )
      mCheckDashCorners->setChecked( false );

    if ( mLayer )
    {
      mLayer->setAlignDashPattern( mCheckAlignDash->isChecked() );
      emit changed();
    }
  } );
  connect( mCheckDashCorners, &QCheckBox::toggled, this, [ = ]
  {
    if ( mLayer )
    {
      mLayer->setTweakDashPatternOnCorners( mCheckDashCorners->isChecked() );
      emit changed();
    }
  } );

  mPenWidthUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                 << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mDashPatternUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                    << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mPatternOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                      << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );

  btnChangeColor->setAllowOpacity( true );
  btnChangeColor->setColorDialogTitle( tr( "Select Line Color" ) );
  btnChangeColor->setContext( QStringLiteral( "symbology" ) );

  mColorDDBtn->registerLinkedWidget( btnChangeColor );

  mRingFilterComboBox->addItem( QgsApplication::getThemeIcon( QStringLiteral( "mIconAllRings.svg" ) ), tr( "All Rings" ), QgsLineSymbolLayer::AllRings );
  mRingFilterComboBox->addItem( QgsApplication::getThemeIcon( QStringLiteral( "mIconExteriorRing.svg" ) ), tr( "Exterior Ring Only" ), QgsLineSymbolLayer::ExteriorRingOnly );
  mRingFilterComboBox->addItem( QgsApplication::getThemeIcon( QStringLiteral( "mIconInteriorRings.svg" ) ), tr( "Interior Rings Only" ), QgsLineSymbolLayer::InteriorRingsOnly );
  connect( mRingFilterComboBox, qgis::overload< int >::of( &QComboBox::currentIndexChanged ), this, [ = ]( int )
  {
    if ( mLayer )
    {
      mLayer->setRingFilter( static_cast< QgsLineSymbolLayer::RenderRingFilter >( mRingFilterComboBox->currentData().toInt() ) );
      emit changed();
    }
  } );

  spinOffset->setClearValue( 0.0 );
  spinPatternOffset->setClearValue( 0.0 );

  if ( vl && vl->geometryType() != QgsWkbTypes::PolygonGeometry )
  {
    //draw inside polygon checkbox only makes sense for polygon layers
    mDrawInsideCheckBox->hide();
    mRingFilterComboBox->hide();
    mRingsLabel->hide();
  }

  //make a temporary symbol for the size assistant preview
  mAssistantPreviewSymbol.reset( new QgsLineSymbol() );

  if ( vectorLayer() )
    mPenWidthDDBtn->setSymbol( mAssistantPreviewSymbol );

  connect( spinWidth, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSimpleLineSymbolLayerWidget::penWidthChanged );
  connect( btnChangeColor, &QgsColorButton::colorChanged, this, &QgsSimpleLineSymbolLayerWidget::colorChanged );
  connect( cboPenStyle, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsSimpleLineSymbolLayerWidget::penStyleChanged );
  connect( spinOffset, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSimpleLineSymbolLayerWidget::offsetChanged );
  connect( cboCapStyle, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsSimpleLineSymbolLayerWidget::penStyleChanged );
  connect( cboJoinStyle, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsSimpleLineSymbolLayerWidget::penStyleChanged );
  connect( spinPatternOffset, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSimpleLineSymbolLayerWidget::patternOffsetChanged );

  updatePatternIcon();

  connect( this, &QgsSymbolLayerWidget::changed, this, &QgsSimpleLineSymbolLayerWidget::updateAssistantSymbol );
}

void QgsSimpleLineSymbolLayerWidget::updateAssistantSymbol()
{
  for ( int i = mAssistantPreviewSymbol->symbolLayerCount() - 1 ; i >= 0; --i )
  {
    mAssistantPreviewSymbol->deleteSymbolLayer( i );
  }
  mAssistantPreviewSymbol->appendSymbolLayer( mLayer->clone() );
  QgsProperty ddWidth = mLayer->dataDefinedProperties().property( QgsSymbolLayer::PropertyStrokeWidth );
  if ( ddWidth )
    mAssistantPreviewSymbol->setDataDefinedWidth( ddWidth );
}


void QgsSimpleLineSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( !layer || layer->layerType() != QLatin1String( "SimpleLine" ) )
    return;

  // layer type is correct, we can do the cast
  mLayer = static_cast<QgsSimpleLineSymbolLayer *>( layer );

  // set units
  mPenWidthUnitWidget->blockSignals( true );
  mPenWidthUnitWidget->setUnit( mLayer->widthUnit() );
  mPenWidthUnitWidget->setMapUnitScale( mLayer->widthMapUnitScale() );
  mPenWidthUnitWidget->blockSignals( false );
  mOffsetUnitWidget->blockSignals( true );
  mOffsetUnitWidget->setUnit( mLayer->offsetUnit() );
  mOffsetUnitWidget->setMapUnitScale( mLayer->offsetMapUnitScale() );
  mOffsetUnitWidget->blockSignals( false );
  mDashPatternUnitWidget->blockSignals( true );
  mDashPatternUnitWidget->setUnit( mLayer->customDashPatternUnit() );
  mDashPatternUnitWidget->setMapUnitScale( mLayer->customDashPatternMapUnitScale() );
  mDashPatternUnitWidget->blockSignals( false );

  whileBlocking( mPatternOffsetUnitWidget )->setUnit( mLayer->dashPatternOffsetUnit() );
  whileBlocking( mPatternOffsetUnitWidget )->setMapUnitScale( mLayer->dashPatternOffsetMapUnitScale() );

  // set values
  spinWidth->blockSignals( true );
  spinWidth->setValue( mLayer->width() );
  spinWidth->blockSignals( false );
  btnChangeColor->blockSignals( true );
  btnChangeColor->setColor( mLayer->color() );
  btnChangeColor->blockSignals( false );
  spinOffset->blockSignals( true );
  spinOffset->setValue( mLayer->offset() );
  spinOffset->blockSignals( false );
  cboPenStyle->blockSignals( true );
  cboJoinStyle->blockSignals( true );
  cboCapStyle->blockSignals( true );
  cboPenStyle->setPenStyle( mLayer->penStyle() );
  cboJoinStyle->setPenJoinStyle( mLayer->penJoinStyle() );
  cboCapStyle->setPenCapStyle( mLayer->penCapStyle() );
  cboPenStyle->blockSignals( false );
  cboJoinStyle->blockSignals( false );
  cboCapStyle->blockSignals( false );
  whileBlocking( spinPatternOffset )->setValue( mLayer->dashPatternOffset() );

  //use a custom dash pattern?
  bool useCustomDashPattern = mLayer->useCustomDashPattern();
  mChangePatternButton->setEnabled( useCustomDashPattern );
  label_3->setEnabled( !useCustomDashPattern );
  cboPenStyle->setEnabled( !useCustomDashPattern );
  mCustomCheckBox->blockSignals( true );
  mCustomCheckBox->setCheckState( useCustomDashPattern ? Qt::Checked : Qt::Unchecked );
  mCustomCheckBox->blockSignals( false );

  //make sure height of custom dash button looks good under different platforms
  QSize size = mChangePatternButton->minimumSizeHint();
  int fontHeight = static_cast< int >( Qgis::UI_SCALE_FACTOR * fontMetrics().height() * 1.4 );
  mChangePatternButton->setMinimumSize( QSize( size.width(), std::max( size.height(), fontHeight ) ) );

  //draw inside polygon?
  const bool drawInsidePolygon = mLayer->drawInsidePolygon();
  whileBlocking( mDrawInsideCheckBox )->setCheckState( drawInsidePolygon ? Qt::Checked : Qt::Unchecked );

  whileBlocking( mRingFilterComboBox )->setCurrentIndex( mRingFilterComboBox->findData( mLayer->ringFilter() ) );

  whileBlocking( mCheckAlignDash )->setChecked( mLayer->alignDashPattern() );
  mCheckDashCorners->setEnabled( mLayer->alignDashPattern() );
  whileBlocking( mCheckDashCorners )->setChecked( mLayer->tweakDashPatternOnCorners() && mLayer->alignDashPattern() );

  updatePatternIcon();

  registerDataDefinedButton( mColorDDBtn, QgsSymbolLayer::PropertyStrokeColor );
  registerDataDefinedButton( mPenWidthDDBtn, QgsSymbolLayer::PropertyStrokeWidth );
  registerDataDefinedButton( mOffsetDDBtn, QgsSymbolLayer::PropertyOffset );
  registerDataDefinedButton( mDashPatternDDBtn, QgsSymbolLayer::PropertyCustomDash );
  registerDataDefinedButton( mPenStyleDDBtn, QgsSymbolLayer::PropertyStrokeStyle );
  registerDataDefinedButton( mJoinStyleDDBtn, QgsSymbolLayer::PropertyJoinStyle );
  registerDataDefinedButton( mCapStyleDDBtn, QgsSymbolLayer::PropertyCapStyle );
  registerDataDefinedButton( mPatternOffsetDDBtn, QgsSymbolLayer::PropertyDashPatternOffset );

  updateAssistantSymbol();
}

QgsSymbolLayer *QgsSimpleLineSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsSimpleLineSymbolLayerWidget::penWidthChanged()
{
  mLayer->setWidth( spinWidth->value() );
  updatePatternIcon();
  emit changed();
}

void QgsSimpleLineSymbolLayerWidget::colorChanged( const QColor &color )
{
  mLayer->setColor( color );
  emit changed();
}

void QgsSimpleLineSymbolLayerWidget::penStyleChanged()
{
  mLayer->setPenStyle( cboPenStyle->penStyle() );
  mLayer->setPenJoinStyle( cboJoinStyle->penJoinStyle() );
  mLayer->setPenCapStyle( cboCapStyle->penCapStyle() );
  updatePatternIcon();
  emit changed();
}

void QgsSimpleLineSymbolLayerWidget::offsetChanged()
{
  mLayer->setOffset( spinOffset->value() );
  updatePatternIcon();
  emit changed();
}

void QgsSimpleLineSymbolLayerWidget::patternOffsetChanged()
{
  mLayer->setDashPatternOffset( spinPatternOffset->value() );
  updatePatternIcon();
  emit changed();
}

void QgsSimpleLineSymbolLayerWidget::mCustomCheckBox_stateChanged( int state )
{
  bool checked = ( state == Qt::Checked );
  mChangePatternButton->setEnabled( checked );
  label_3->setEnabled( !checked );
  cboPenStyle->setEnabled( !checked );

  mLayer->setUseCustomDashPattern( checked );
  emit changed();
}

void QgsSimpleLineSymbolLayerWidget::mChangePatternButton_clicked()
{
  QgsPanelWidget *panel = QgsPanelWidget::findParentPanel( this );
  if ( panel && panel->dockMode() )
  {
    QgsDashSpaceWidget *widget = new QgsDashSpaceWidget( mLayer->customDashVector(), panel );
    widget->setPanelTitle( tr( "Custom Dash Pattern" ) );
    widget->setUnit( mDashPatternUnitWidget->unit() );
    connect( widget, &QgsPanelWidget::widgetChanged, this, [ this, widget ]()
    {
      mLayer->setCustomDashVector( widget->dashDotVector() );
      updatePatternIcon();
    } );
    connect( widget, &QgsPanelWidget::widgetChanged, this, &QgsSymbolLayerWidget::changed );
    panel->openPanel( widget );
    return;
  }

  QgsDashSpaceDialog d( mLayer->customDashVector() );
  d.setUnit( mDashPatternUnitWidget->unit() );
  if ( d.exec() == QDialog::Accepted )
  {
    mLayer->setCustomDashVector( d.dashDotVector() );
    updatePatternIcon();
    emit changed();
  }
}

void QgsSimpleLineSymbolLayerWidget::mPenWidthUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setWidthUnit( mPenWidthUnitWidget->unit() );
    mLayer->setWidthMapUnitScale( mPenWidthUnitWidget->getMapUnitScale() );
    updatePatternIcon();
    emit changed();
  }
}

void QgsSimpleLineSymbolLayerWidget::mOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetUnit( mOffsetUnitWidget->unit() );
    mLayer->setOffsetMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsSimpleLineSymbolLayerWidget::mDashPatternUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setCustomDashPatternUnit( mDashPatternUnitWidget->unit() );
    mLayer->setCustomDashPatternMapUnitScale( mDashPatternUnitWidget->getMapUnitScale() );
    updatePatternIcon();
    emit changed();
  }
}

void QgsSimpleLineSymbolLayerWidget::mDrawInsideCheckBox_stateChanged( int state )
{
  bool checked = ( state == Qt::Checked );
  mLayer->setDrawInsidePolygon( checked );
  emit changed();
}

void QgsSimpleLineSymbolLayerWidget::patternOffsetUnitChanged()
{
  if ( mLayer )
  {
    mLayer->setDashPatternOffsetUnit( mPatternOffsetUnitWidget->unit() );
    mLayer->setDashPatternOffsetMapUnitScale( mPatternOffsetUnitWidget->getMapUnitScale() );
    updatePatternIcon();
    emit changed();
  }
}

void QgsSimpleLineSymbolLayerWidget::updatePatternIcon()
{
  if ( !mLayer )
  {
    return;
  }
  std::unique_ptr< QgsSimpleLineSymbolLayer > layerCopy( mLayer->clone() );
  if ( !layerCopy )
  {
    return;
  }
  QColor color = qApp->palette().color( QPalette::WindowText );
  layerCopy->setColor( color );
  // reset offset, we don't want to show that in the preview
  layerCopy->setOffset( 0 );
  layerCopy->setUseCustomDashPattern( true );

  QSize currentIconSize;
  //icon size is button size with a small margin
#ifdef Q_OS_WIN
  currentIconSize = QSize( mChangePatternButton->width() - 10, mChangePatternButton->height() - 6 );
#else
  currentIconSize = QSize( mChangePatternButton->width() - 10, mChangePatternButton->height() - 12 );
#endif

  if ( !currentIconSize.isValid() || currentIconSize.width() <= 0 || currentIconSize.height() <= 0 )
  {
    return;
  }

  //create an icon pixmap
  std::unique_ptr< QgsLineSymbol > previewSymbol = qgis::make_unique< QgsLineSymbol >( QgsSymbolLayerList() << layerCopy.release() );
  const QIcon icon = QgsSymbolLayerUtils::symbolPreviewIcon( previewSymbol.get(), currentIconSize );
  mChangePatternButton->setIconSize( currentIconSize );
  mChangePatternButton->setIcon( icon );

  // set tooltip
  // create very large preview image
#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
  int width = static_cast< int >( Qgis::UI_SCALE_FACTOR * fontMetrics().width( 'X' ) * 23 );
#else
  int width = static_cast< int >( Qgis::UI_SCALE_FACTOR * fontMetrics().horizontalAdvance( 'X' ) * 23 );
#endif
  int height = static_cast< int >( width / 1.61803398875 ); // golden ratio

  QPixmap pm = QgsSymbolLayerUtils::symbolPreviewPixmap( previewSymbol.get(), QSize( width, height ), height / 20 );
  QByteArray data;
  QBuffer buffer( &data );
  pm.save( &buffer, "PNG", 100 );
  mChangePatternButton->setToolTip( QStringLiteral( "<img src='data:image/png;base64, %3'>" ).arg( QString( data.toBase64() ) ) );
}

void QgsSimpleLineSymbolLayerWidget::resizeEvent( QResizeEvent *event )
{
  QgsSymbolLayerWidget::resizeEvent( event );
  // redraw custom dash pattern icon -- the button size has changed
  updatePatternIcon();
}


///////////


QgsSimpleMarkerSymbolLayerWidget::QgsSimpleMarkerSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;

  setupUi( this );
  connect( mSizeUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSimpleMarkerSymbolLayerWidget::mSizeUnitWidget_changed );
  connect( mOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSimpleMarkerSymbolLayerWidget::mOffsetUnitWidget_changed );
  connect( mStrokeWidthUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSimpleMarkerSymbolLayerWidget::mStrokeWidthUnitWidget_changed );
  connect( mStrokeStyleComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsSimpleMarkerSymbolLayerWidget::mStrokeStyleComboBox_currentIndexChanged );
  connect( mStrokeWidthSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSimpleMarkerSymbolLayerWidget::mStrokeWidthSpinBox_valueChanged );
  connect( mHorizontalAnchorComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsSimpleMarkerSymbolLayerWidget::mHorizontalAnchorComboBox_currentIndexChanged );
  connect( mVerticalAnchorComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsSimpleMarkerSymbolLayerWidget::mVerticalAnchorComboBox_currentIndexChanged );
  mSizeUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                             << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mStrokeWidthUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                    << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );

  btnChangeColorFill->setAllowOpacity( true );
  btnChangeColorFill->setColorDialogTitle( tr( "Select Fill Color" ) );
  btnChangeColorFill->setContext( QStringLiteral( "symbology" ) );
  btnChangeColorFill->setShowNoColor( true );
  btnChangeColorFill->setNoColorString( tr( "Transparent Fill" ) );
  btnChangeColorStroke->setAllowOpacity( true );
  btnChangeColorStroke->setColorDialogTitle( tr( "Select Stroke Color" ) );
  btnChangeColorStroke->setContext( QStringLiteral( "symbology" ) );
  btnChangeColorStroke->setShowNoColor( true );
  btnChangeColorStroke->setNoColorString( tr( "Transparent Stroke" ) );

  mFillColorDDBtn->registerLinkedWidget( btnChangeColorFill );
  mStrokeColorDDBtn->registerLinkedWidget( btnChangeColorStroke );

  spinOffsetX->setClearValue( 0.0 );
  spinOffsetY->setClearValue( 0.0 );
  spinAngle->setClearValue( 0.0 );

  //make a temporary symbol for the size assistant preview
  mAssistantPreviewSymbol.reset( new QgsMarkerSymbol() );

  if ( vectorLayer() )
    mSizeDDBtn->setSymbol( mAssistantPreviewSymbol );

  int size = lstNames->iconSize().width();

#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
  size = std::max( 30, static_cast< int >( std::round( Qgis::UI_SCALE_FACTOR * fontMetrics().width( 'X' ) * 3 ) ) );
#else
  size = std::max( 30, static_cast< int >( std::round( Qgis::UI_SCALE_FACTOR * fontMetrics().horizontalAdvance( 'X' ) * 3 ) ) );
#endif

  lstNames->setGridSize( QSize( size * 1.2, size * 1.2 ) );
  lstNames->setIconSize( QSize( size, size ) );

  double markerSize = size * 0.8;
  const auto shapes = QgsSimpleMarkerSymbolLayerBase::availableShapes();
  for ( QgsSimpleMarkerSymbolLayerBase::Shape shape : shapes )
  {
    QgsSimpleMarkerSymbolLayer *lyr = new QgsSimpleMarkerSymbolLayer( shape, markerSize );
    lyr->setSizeUnit( QgsUnitTypes::RenderPixels );
    lyr->setColor( QColor( 200, 200, 200 ) );
    lyr->setStrokeColor( QColor( 0, 0, 0 ) );
    QIcon icon = QgsSymbolLayerUtils::symbolLayerPreviewIcon( lyr, QgsUnitTypes::RenderPixels, QSize( size, size ) );
    QListWidgetItem *item = new QListWidgetItem( icon, QString(), lstNames );
    item->setData( Qt::UserRole, static_cast< int >( shape ) );
    item->setToolTip( QgsSimpleMarkerSymbolLayerBase::encodeShape( shape ) );
    delete lyr;
  }
  // show at least 3 rows
  lstNames->setMinimumHeight( lstNames->gridSize().height() * 3.1 );

  connect( lstNames, &QListWidget::currentRowChanged, this, &QgsSimpleMarkerSymbolLayerWidget::setShape );
  connect( btnChangeColorStroke, &QgsColorButton::colorChanged, this, &QgsSimpleMarkerSymbolLayerWidget::setColorStroke );
  connect( btnChangeColorFill, &QgsColorButton::colorChanged, this, &QgsSimpleMarkerSymbolLayerWidget::setColorFill );
  connect( cboJoinStyle, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsSimpleMarkerSymbolLayerWidget::penJoinStyleChanged );
  connect( spinSize, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSimpleMarkerSymbolLayerWidget::setSize );
  connect( spinAngle, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSimpleMarkerSymbolLayerWidget::setAngle );
  connect( spinOffsetX, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSimpleMarkerSymbolLayerWidget::setOffset );
  connect( spinOffsetY, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSimpleMarkerSymbolLayerWidget::setOffset );
  connect( this, &QgsSymbolLayerWidget::changed, this, &QgsSimpleMarkerSymbolLayerWidget::updateAssistantSymbol );
}

void QgsSimpleMarkerSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( layer->layerType() != QLatin1String( "SimpleMarker" ) )
    return;

  // layer type is correct, we can do the cast
  mLayer = static_cast<QgsSimpleMarkerSymbolLayer *>( layer );

  // set values
  QgsSimpleMarkerSymbolLayerBase::Shape shape = mLayer->shape();
  for ( int i = 0; i < lstNames->count(); ++i )
  {
    if ( static_cast< QgsSimpleMarkerSymbolLayerBase::Shape >( lstNames->item( i )->data( Qt::UserRole ).toInt() ) == shape )
    {
      lstNames->setCurrentRow( i );
      break;
    }
  }
  btnChangeColorStroke->blockSignals( true );
  btnChangeColorStroke->setColor( mLayer->strokeColor() );
  btnChangeColorStroke->blockSignals( false );
  btnChangeColorFill->blockSignals( true );
  btnChangeColorFill->setColor( mLayer->fillColor() );
  btnChangeColorFill->setEnabled( QgsSimpleMarkerSymbolLayerBase::shapeIsFilled( mLayer->shape() ) );
  btnChangeColorFill->blockSignals( false );
  spinSize->blockSignals( true );
  spinSize->setValue( mLayer->size() );
  spinSize->blockSignals( false );
  spinAngle->blockSignals( true );
  spinAngle->setValue( mLayer->angle() );
  spinAngle->blockSignals( false );
  mStrokeStyleComboBox->blockSignals( true );
  mStrokeStyleComboBox->setPenStyle( mLayer->strokeStyle() );
  mStrokeStyleComboBox->blockSignals( false );
  mStrokeWidthSpinBox->blockSignals( true );
  mStrokeWidthSpinBox->setValue( mLayer->strokeWidth() );
  mStrokeWidthSpinBox->blockSignals( false );
  cboJoinStyle->blockSignals( true );
  cboJoinStyle->setPenJoinStyle( mLayer->penJoinStyle() );
  cboJoinStyle->blockSignals( false );

  // without blocking signals the value gets changed because of slot setOffset()
  spinOffsetX->blockSignals( true );
  spinOffsetX->setValue( mLayer->offset().x() );
  spinOffsetX->blockSignals( false );
  spinOffsetY->blockSignals( true );
  spinOffsetY->setValue( mLayer->offset().y() );
  spinOffsetY->blockSignals( false );

  mSizeUnitWidget->blockSignals( true );
  mSizeUnitWidget->setUnit( mLayer->sizeUnit() );
  mSizeUnitWidget->setMapUnitScale( mLayer->sizeMapUnitScale() );
  mSizeUnitWidget->blockSignals( false );
  mOffsetUnitWidget->blockSignals( true );
  mOffsetUnitWidget->setUnit( mLayer->offsetUnit() );
  mOffsetUnitWidget->setMapUnitScale( mLayer->offsetMapUnitScale() );
  mOffsetUnitWidget->blockSignals( false );
  mStrokeWidthUnitWidget->blockSignals( true );
  mStrokeWidthUnitWidget->setUnit( mLayer->strokeWidthUnit() );
  mStrokeWidthUnitWidget->setMapUnitScale( mLayer->strokeWidthMapUnitScale() );
  mStrokeWidthUnitWidget->blockSignals( false );

  //anchor points
  mHorizontalAnchorComboBox->blockSignals( true );
  mVerticalAnchorComboBox->blockSignals( true );
  mHorizontalAnchorComboBox->setCurrentIndex( mLayer->horizontalAnchorPoint() );
  mVerticalAnchorComboBox->setCurrentIndex( mLayer->verticalAnchorPoint() );
  mHorizontalAnchorComboBox->blockSignals( false );
  mVerticalAnchorComboBox->blockSignals( false );

  registerDataDefinedButton( mNameDDBtn, QgsSymbolLayer::PropertyName );
  registerDataDefinedButton( mFillColorDDBtn, QgsSymbolLayer::PropertyFillColor );
  registerDataDefinedButton( mStrokeColorDDBtn, QgsSymbolLayer::PropertyStrokeColor );
  registerDataDefinedButton( mStrokeWidthDDBtn, QgsSymbolLayer::PropertyStrokeWidth );
  registerDataDefinedButton( mStrokeStyleDDBtn, QgsSymbolLayer::PropertyStrokeStyle );
  registerDataDefinedButton( mJoinStyleDDBtn, QgsSymbolLayer::PropertyJoinStyle );
  registerDataDefinedButton( mSizeDDBtn, QgsSymbolLayer::PropertySize );
  registerDataDefinedButton( mAngleDDBtn, QgsSymbolLayer::PropertyAngle );
  registerDataDefinedButton( mOffsetDDBtn, QgsSymbolLayer::PropertyOffset );
  registerDataDefinedButton( mHorizontalAnchorDDBtn, QgsSymbolLayer::PropertyHorizontalAnchor );
  registerDataDefinedButton( mVerticalAnchorDDBtn, QgsSymbolLayer::PropertyVerticalAnchor );

  updateAssistantSymbol();
}

QgsSymbolLayer *QgsSimpleMarkerSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsSimpleMarkerSymbolLayerWidget::setShape()
{
  mLayer->setShape( static_cast< QgsSimpleMarkerSymbolLayerBase::Shape>( lstNames->currentItem()->data( Qt::UserRole ).toInt() ) );
  btnChangeColorFill->setEnabled( QgsSimpleMarkerSymbolLayerBase::shapeIsFilled( mLayer->shape() ) );
  emit changed();
}

void QgsSimpleMarkerSymbolLayerWidget::setColorStroke( const QColor &color )
{
  mLayer->setStrokeColor( color );
  emit changed();
}

void QgsSimpleMarkerSymbolLayerWidget::setColorFill( const QColor &color )
{
  mLayer->setColor( color );
  emit changed();
}

void QgsSimpleMarkerSymbolLayerWidget::penJoinStyleChanged()
{
  mLayer->setPenJoinStyle( cboJoinStyle->penJoinStyle() );
  emit changed();
}

void QgsSimpleMarkerSymbolLayerWidget::setSize()
{
  mLayer->setSize( spinSize->value() );
  emit changed();
}

void QgsSimpleMarkerSymbolLayerWidget::setAngle()
{
  mLayer->setAngle( spinAngle->value() );
  emit changed();
}

void QgsSimpleMarkerSymbolLayerWidget::setOffset()
{
  mLayer->setOffset( QPointF( spinOffsetX->value(), spinOffsetY->value() ) );
  emit changed();
}

void QgsSimpleMarkerSymbolLayerWidget::mStrokeStyleComboBox_currentIndexChanged( int index )
{
  Q_UNUSED( index )

  if ( mLayer )
  {
    mLayer->setStrokeStyle( mStrokeStyleComboBox->penStyle() );
    emit changed();
  }
}

void QgsSimpleMarkerSymbolLayerWidget::mStrokeWidthSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setStrokeWidth( d );
    emit changed();
  }
}

void QgsSimpleMarkerSymbolLayerWidget::mSizeUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setSizeUnit( mSizeUnitWidget->unit() );
    mLayer->setSizeMapUnitScale( mSizeUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsSimpleMarkerSymbolLayerWidget::mOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetUnit( mOffsetUnitWidget->unit() );
    mLayer->setOffsetMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsSimpleMarkerSymbolLayerWidget::mStrokeWidthUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setStrokeWidthUnit( mStrokeWidthUnitWidget->unit() );
    mLayer->setStrokeWidthMapUnitScale( mStrokeWidthUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsSimpleMarkerSymbolLayerWidget::mHorizontalAnchorComboBox_currentIndexChanged( int index )
{
  if ( mLayer )
  {
    mLayer->setHorizontalAnchorPoint( ( QgsMarkerSymbolLayer::HorizontalAnchorPoint ) index );
    emit changed();
  }
}

void QgsSimpleMarkerSymbolLayerWidget::mVerticalAnchorComboBox_currentIndexChanged( int index )
{
  if ( mLayer )
  {
    mLayer->setVerticalAnchorPoint( ( QgsMarkerSymbolLayer::VerticalAnchorPoint ) index );
    emit changed();
  }
}

void QgsSimpleMarkerSymbolLayerWidget::updateAssistantSymbol()
{
  for ( int i = mAssistantPreviewSymbol->symbolLayerCount() - 1 ; i >= 0; --i )
  {
    mAssistantPreviewSymbol->deleteSymbolLayer( i );
  }
  mAssistantPreviewSymbol->appendSymbolLayer( mLayer->clone() );
  QgsProperty ddSize = mLayer->dataDefinedProperties().property( QgsSymbolLayer::PropertySize );
  if ( ddSize )
    mAssistantPreviewSymbol->setDataDefinedSize( ddSize );
}


///////////

QgsSimpleFillSymbolLayerWidget::QgsSimpleFillSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;

  setupUi( this );
  connect( mStrokeWidthUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSimpleFillSymbolLayerWidget::mStrokeWidthUnitWidget_changed );
  connect( mOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSimpleFillSymbolLayerWidget::mOffsetUnitWidget_changed );
  mStrokeWidthUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                    << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );

  btnChangeColor->setAllowOpacity( true );
  btnChangeColor->setColorDialogTitle( tr( "Select Fill Color" ) );
  btnChangeColor->setContext( QStringLiteral( "symbology" ) );
  btnChangeColor->setShowNoColor( true );
  btnChangeColor->setNoColorString( tr( "Transparent Fill" ) );
  btnChangeStrokeColor->setAllowOpacity( true );
  btnChangeStrokeColor->setColorDialogTitle( tr( "Select Stroke Color" ) );
  btnChangeStrokeColor->setContext( QStringLiteral( "symbology" ) );
  btnChangeStrokeColor->setShowNoColor( true );
  btnChangeStrokeColor->setNoColorString( tr( "Transparent Stroke" ) );

  spinOffsetX->setClearValue( 0.0 );
  spinOffsetY->setClearValue( 0.0 );

  connect( btnChangeColor, &QgsColorButton::colorChanged, this, &QgsSimpleFillSymbolLayerWidget::setColor );
  connect( cboFillStyle, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsSimpleFillSymbolLayerWidget::setBrushStyle );
  connect( btnChangeStrokeColor, &QgsColorButton::colorChanged, this, &QgsSimpleFillSymbolLayerWidget::setStrokeColor );
  connect( spinStrokeWidth, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSimpleFillSymbolLayerWidget::strokeWidthChanged );
  connect( cboStrokeStyle, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsSimpleFillSymbolLayerWidget::strokeStyleChanged );
  connect( cboJoinStyle, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsSimpleFillSymbolLayerWidget::strokeStyleChanged );
  connect( spinOffsetX, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSimpleFillSymbolLayerWidget::offsetChanged );
  connect( spinOffsetY, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSimpleFillSymbolLayerWidget::offsetChanged );

  mFillColorDDBtn->registerLinkedWidget( btnChangeColor );
  mStrokeColorDDBtn->registerLinkedWidget( btnChangeStrokeColor );
}

void QgsSimpleFillSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( layer->layerType() != QLatin1String( "SimpleFill" ) )
    return;

  // layer type is correct, we can do the cast
  mLayer = static_cast<QgsSimpleFillSymbolLayer *>( layer );

  // set values
  btnChangeColor->blockSignals( true );
  btnChangeColor->setColor( mLayer->color() );
  btnChangeColor->blockSignals( false );
  cboFillStyle->blockSignals( true );
  cboFillStyle->setBrushStyle( mLayer->brushStyle() );
  cboFillStyle->blockSignals( false );
  btnChangeStrokeColor->blockSignals( true );
  btnChangeStrokeColor->setColor( mLayer->strokeColor() );
  btnChangeStrokeColor->blockSignals( false );
  cboStrokeStyle->blockSignals( true );
  cboStrokeStyle->setPenStyle( mLayer->strokeStyle() );
  cboStrokeStyle->blockSignals( false );
  spinStrokeWidth->blockSignals( true );
  spinStrokeWidth->setValue( mLayer->strokeWidth() );
  spinStrokeWidth->blockSignals( false );
  cboJoinStyle->blockSignals( true );
  cboJoinStyle->setPenJoinStyle( mLayer->penJoinStyle() );
  cboJoinStyle->blockSignals( false );
  spinOffsetX->blockSignals( true );
  spinOffsetX->setValue( mLayer->offset().x() );
  spinOffsetX->blockSignals( false );
  spinOffsetY->blockSignals( true );
  spinOffsetY->setValue( mLayer->offset().y() );
  spinOffsetY->blockSignals( false );

  mStrokeWidthUnitWidget->blockSignals( true );
  mStrokeWidthUnitWidget->setUnit( mLayer->strokeWidthUnit() );
  mStrokeWidthUnitWidget->setMapUnitScale( mLayer->strokeWidthMapUnitScale() );
  mStrokeWidthUnitWidget->blockSignals( false );
  mOffsetUnitWidget->blockSignals( true );
  mOffsetUnitWidget->setUnit( mLayer->offsetUnit() );
  mOffsetUnitWidget->setMapUnitScale( mLayer->offsetMapUnitScale() );
  mOffsetUnitWidget->blockSignals( false );

  registerDataDefinedButton( mFillColorDDBtn, QgsSymbolLayer::PropertyFillColor );
  registerDataDefinedButton( mStrokeColorDDBtn, QgsSymbolLayer::PropertyStrokeColor );
  registerDataDefinedButton( mStrokeWidthDDBtn, QgsSymbolLayer::PropertyStrokeWidth );
  registerDataDefinedButton( mFillStyleDDBtn, QgsSymbolLayer::PropertyFillStyle );
  registerDataDefinedButton( mStrokeStyleDDBtn, QgsSymbolLayer::PropertyStrokeStyle );
  registerDataDefinedButton( mJoinStyleDDBtn, QgsSymbolLayer::PropertyJoinStyle );
  registerDataDefinedButton( mOffsetDDBtn, QgsSymbolLayer::PropertyOffset );
}

QgsSymbolLayer *QgsSimpleFillSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsSimpleFillSymbolLayerWidget::setColor( const QColor &color )
{
  mLayer->setColor( color );
  emit changed();
}

void QgsSimpleFillSymbolLayerWidget::setStrokeColor( const QColor &color )
{
  mLayer->setStrokeColor( color );
  emit changed();
}

void QgsSimpleFillSymbolLayerWidget::setBrushStyle()
{
  mLayer->setBrushStyle( cboFillStyle->brushStyle() );
  emit changed();
}

void QgsSimpleFillSymbolLayerWidget::strokeWidthChanged()
{
  mLayer->setStrokeWidth( spinStrokeWidth->value() );
  emit changed();
}

void QgsSimpleFillSymbolLayerWidget::strokeStyleChanged()
{
  mLayer->setStrokeStyle( cboStrokeStyle->penStyle() );
  mLayer->setPenJoinStyle( cboJoinStyle->penJoinStyle() );
  emit changed();
}

void QgsSimpleFillSymbolLayerWidget::offsetChanged()
{
  mLayer->setOffset( QPointF( spinOffsetX->value(), spinOffsetY->value() ) );
  emit changed();
}

void QgsSimpleFillSymbolLayerWidget::mStrokeWidthUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setStrokeWidthUnit( mStrokeWidthUnitWidget->unit() );
    mLayer->setStrokeWidthMapUnitScale( mStrokeWidthUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsSimpleFillSymbolLayerWidget::mOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetUnit( mOffsetUnitWidget->unit() );
    mLayer->setOffsetMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

///////////

QgsFilledMarkerSymbolLayerWidget::QgsFilledMarkerSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;

  setupUi( this );
  connect( mSizeUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsFilledMarkerSymbolLayerWidget::mSizeUnitWidget_changed );
  connect( mOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsFilledMarkerSymbolLayerWidget::mOffsetUnitWidget_changed );
  connect( mHorizontalAnchorComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsFilledMarkerSymbolLayerWidget::mHorizontalAnchorComboBox_currentIndexChanged );
  connect( mVerticalAnchorComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsFilledMarkerSymbolLayerWidget::mVerticalAnchorComboBox_currentIndexChanged );
  mSizeUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                             << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );

  spinOffsetX->setClearValue( 0.0 );
  spinOffsetY->setClearValue( 0.0 );
  spinAngle->setClearValue( 0.0 );

  //make a temporary symbol for the size assistant preview
  mAssistantPreviewSymbol.reset( new QgsMarkerSymbol() );

  if ( vectorLayer() )
    mSizeDDBtn->setSymbol( mAssistantPreviewSymbol );

  int size = lstNames->iconSize().width();
#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
  size = std::max( 30, static_cast< int >( std::round( Qgis::UI_SCALE_FACTOR * fontMetrics().width( 'X' ) * 3 ) ) );
#else
  size = std::max( 30, static_cast< int >( std::round( Qgis::UI_SCALE_FACTOR * fontMetrics().horizontalAdvance( 'X' ) * 3 ) ) );
#endif
  lstNames->setGridSize( QSize( size * 1.2, size * 1.2 ) );
  lstNames->setIconSize( QSize( size, size ) );

  double markerSize = size * 0.8;
  const auto shapes = QgsSimpleMarkerSymbolLayerBase::availableShapes();
  for ( QgsSimpleMarkerSymbolLayerBase::Shape shape : shapes )
  {
    QgsSimpleMarkerSymbolLayer *lyr = new QgsSimpleMarkerSymbolLayer( shape, markerSize );
    lyr->setSizeUnit( QgsUnitTypes::RenderPixels );
    lyr->setColor( QColor( 200, 200, 200 ) );
    lyr->setStrokeColor( QColor( 0, 0, 0 ) );
    QIcon icon = QgsSymbolLayerUtils::symbolLayerPreviewIcon( lyr, QgsUnitTypes::RenderPixels, QSize( size, size ) );
    QListWidgetItem *item = new QListWidgetItem( icon, QString(), lstNames );
    item->setData( Qt::UserRole, static_cast< int >( shape ) );
    item->setToolTip( QgsSimpleMarkerSymbolLayerBase::encodeShape( shape ) );
    delete lyr;
  }
  // show at least 3 rows
  lstNames->setMinimumHeight( lstNames->gridSize().height() * 3.1 );

  connect( lstNames, &QListWidget::currentRowChanged, this, &QgsFilledMarkerSymbolLayerWidget::setShape );
  connect( spinSize, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsFilledMarkerSymbolLayerWidget::setSize );
  connect( spinAngle, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsFilledMarkerSymbolLayerWidget::setAngle );
  connect( spinOffsetX, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsFilledMarkerSymbolLayerWidget::setOffset );
  connect( spinOffsetY, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsFilledMarkerSymbolLayerWidget::setOffset );
  connect( this, &QgsSymbolLayerWidget::changed, this, &QgsFilledMarkerSymbolLayerWidget::updateAssistantSymbol );
}

void QgsFilledMarkerSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( layer->layerType() != QLatin1String( "FilledMarker" ) )
    return;

  // layer type is correct, we can do the cast
  mLayer = static_cast<QgsFilledMarkerSymbolLayer *>( layer );

  // set values
  QgsSimpleMarkerSymbolLayerBase::Shape shape = mLayer->shape();
  for ( int i = 0; i < lstNames->count(); ++i )
  {
    if ( static_cast< QgsSimpleMarkerSymbolLayerBase::Shape >( lstNames->item( i )->data( Qt::UserRole ).toInt() ) == shape )
    {
      lstNames->setCurrentRow( i );
      break;
    }
  }
  whileBlocking( spinSize )->setValue( mLayer->size() );
  whileBlocking( spinAngle )->setValue( mLayer->angle() );
  whileBlocking( spinOffsetX )->setValue( mLayer->offset().x() );
  whileBlocking( spinOffsetY )->setValue( mLayer->offset().y() );

  mSizeUnitWidget->blockSignals( true );
  mSizeUnitWidget->setUnit( mLayer->sizeUnit() );
  mSizeUnitWidget->setMapUnitScale( mLayer->sizeMapUnitScale() );
  mSizeUnitWidget->blockSignals( false );
  mOffsetUnitWidget->blockSignals( true );
  mOffsetUnitWidget->setUnit( mLayer->offsetUnit() );
  mOffsetUnitWidget->setMapUnitScale( mLayer->offsetMapUnitScale() );
  mOffsetUnitWidget->blockSignals( false );

  //anchor points
  whileBlocking( mHorizontalAnchorComboBox )->setCurrentIndex( mLayer->horizontalAnchorPoint() );
  whileBlocking( mVerticalAnchorComboBox )->setCurrentIndex( mLayer->verticalAnchorPoint() );

  registerDataDefinedButton( mNameDDBtn, QgsSymbolLayer::PropertyName );
  registerDataDefinedButton( mSizeDDBtn, QgsSymbolLayer::PropertySize );
  registerDataDefinedButton( mAngleDDBtn, QgsSymbolLayer::PropertyAngle );
  registerDataDefinedButton( mOffsetDDBtn, QgsSymbolLayer::PropertyOffset );
  registerDataDefinedButton( mHorizontalAnchorDDBtn, QgsSymbolLayer::PropertyHorizontalAnchor );
  registerDataDefinedButton( mVerticalAnchorDDBtn, QgsSymbolLayer::PropertyVerticalAnchor );

  updateAssistantSymbol();
}

QgsSymbolLayer *QgsFilledMarkerSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsFilledMarkerSymbolLayerWidget::setShape()
{
  mLayer->setShape( static_cast< QgsSimpleMarkerSymbolLayerBase::Shape>( lstNames->currentItem()->data( Qt::UserRole ).toInt() ) );
  emit changed();
}

void QgsFilledMarkerSymbolLayerWidget::setSize()
{
  mLayer->setSize( spinSize->value() );
  emit changed();
}

void QgsFilledMarkerSymbolLayerWidget::setAngle()
{
  mLayer->setAngle( spinAngle->value() );
  emit changed();
}

void QgsFilledMarkerSymbolLayerWidget::setOffset()
{
  mLayer->setOffset( QPointF( spinOffsetX->value(), spinOffsetY->value() ) );
  emit changed();
}

void QgsFilledMarkerSymbolLayerWidget::mSizeUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setSizeUnit( mSizeUnitWidget->unit() );
    mLayer->setSizeMapUnitScale( mSizeUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsFilledMarkerSymbolLayerWidget::mOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetUnit( mOffsetUnitWidget->unit() );
    mLayer->setOffsetMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsFilledMarkerSymbolLayerWidget::mHorizontalAnchorComboBox_currentIndexChanged( int index )
{
  if ( mLayer )
  {
    mLayer->setHorizontalAnchorPoint( ( QgsMarkerSymbolLayer::HorizontalAnchorPoint ) index );
    emit changed();
  }
}

void QgsFilledMarkerSymbolLayerWidget::mVerticalAnchorComboBox_currentIndexChanged( int index )
{
  if ( mLayer )
  {
    mLayer->setVerticalAnchorPoint( ( QgsMarkerSymbolLayer::VerticalAnchorPoint ) index );
    emit changed();
  }
}

void QgsFilledMarkerSymbolLayerWidget::updateAssistantSymbol()
{
  for ( int i = mAssistantPreviewSymbol->symbolLayerCount() - 1 ; i >= 0; --i )
  {
    mAssistantPreviewSymbol->deleteSymbolLayer( i );
  }
  mAssistantPreviewSymbol->appendSymbolLayer( mLayer->clone() );
  QgsProperty ddSize = mLayer->dataDefinedProperties().property( QgsSymbolLayer::PropertySize );
  if ( ddSize )
    mAssistantPreviewSymbol->setDataDefinedSize( ddSize );
}


///////////

QgsGradientFillSymbolLayerWidget::QgsGradientFillSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;

  setupUi( this );
  connect( mOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsGradientFillSymbolLayerWidget::mOffsetUnitWidget_changed );
  connect( mSpinAngle, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsGradientFillSymbolLayerWidget::mSpinAngle_valueChanged );
  mOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );

  btnColorRamp->setShowGradientOnly( true );

  btnChangeColor->setAllowOpacity( true );
  btnChangeColor->setColorDialogTitle( tr( "Select Gradient Color" ) );
  btnChangeColor->setContext( QStringLiteral( "symbology" ) );
  btnChangeColor->setShowNoColor( true );
  btnChangeColor->setNoColorString( tr( "Transparent" ) );
  btnChangeColor2->setAllowOpacity( true );
  btnChangeColor2->setColorDialogTitle( tr( "Select Gradient Color" ) );
  btnChangeColor2->setContext( QStringLiteral( "symbology" ) );
  btnChangeColor2->setShowNoColor( true );
  btnChangeColor2->setNoColorString( tr( "Transparent" ) );

  mStartColorDDBtn->registerLinkedWidget( btnChangeColor );
  mEndColorDDBtn->registerLinkedWidget( btnChangeColor2 );

  spinOffsetX->setClearValue( 0.0 );
  spinOffsetY->setClearValue( 0.0 );
  mSpinAngle->setClearValue( 0.0 );

  connect( btnChangeColor, &QgsColorButton::colorChanged, this, &QgsGradientFillSymbolLayerWidget::setColor );
  connect( btnChangeColor2, &QgsColorButton::colorChanged, this, &QgsGradientFillSymbolLayerWidget::setColor2 );
  connect( btnColorRamp, &QgsColorRampButton::colorRampChanged, this, &QgsGradientFillSymbolLayerWidget::applyColorRamp );
  connect( cboGradientType, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsGradientFillSymbolLayerWidget::setGradientType );
  connect( cboCoordinateMode, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsGradientFillSymbolLayerWidget::setCoordinateMode );
  connect( cboGradientSpread, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsGradientFillSymbolLayerWidget::setGradientSpread );
  connect( radioTwoColor, &QAbstractButton::toggled, this, &QgsGradientFillSymbolLayerWidget::colorModeChanged );
  connect( spinOffsetX, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsGradientFillSymbolLayerWidget::offsetChanged );
  connect( spinOffsetY, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsGradientFillSymbolLayerWidget::offsetChanged );
  connect( spinRefPoint1X, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsGradientFillSymbolLayerWidget::referencePointChanged );
  connect( spinRefPoint1Y, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsGradientFillSymbolLayerWidget::referencePointChanged );
  connect( checkRefPoint1Centroid, &QAbstractButton::toggled, this, &QgsGradientFillSymbolLayerWidget::referencePointChanged );
  connect( spinRefPoint2X, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsGradientFillSymbolLayerWidget::referencePointChanged );
  connect( spinRefPoint2Y, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsGradientFillSymbolLayerWidget::referencePointChanged );
  connect( checkRefPoint2Centroid, &QAbstractButton::toggled, this, &QgsGradientFillSymbolLayerWidget::referencePointChanged );
}

void QgsGradientFillSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( layer->layerType() != QLatin1String( "GradientFill" ) )
    return;

  // layer type is correct, we can do the cast
  mLayer = static_cast<QgsGradientFillSymbolLayer *>( layer );

  // set values
  btnChangeColor->blockSignals( true );
  btnChangeColor->setColor( mLayer->color() );
  btnChangeColor->blockSignals( false );
  btnChangeColor2->blockSignals( true );
  btnChangeColor2->setColor( mLayer->color2() );
  btnChangeColor2->blockSignals( false );

  if ( mLayer->gradientColorType() == QgsGradientFillSymbolLayer::SimpleTwoColor )
  {
    radioTwoColor->setChecked( true );
    btnColorRamp->setEnabled( false );
  }
  else
  {
    radioColorRamp->setChecked( true );
    btnChangeColor->setEnabled( false );
    btnChangeColor2->setEnabled( false );
  }

  // set source color ramp
  if ( mLayer->colorRamp() )
  {
    btnColorRamp->blockSignals( true );
    btnColorRamp->setColorRamp( mLayer->colorRamp() );
    btnColorRamp->blockSignals( false );
  }

  cboGradientType->blockSignals( true );
  switch ( mLayer->gradientType() )
  {
    case QgsGradientFillSymbolLayer::Linear:
      cboGradientType->setCurrentIndex( 0 );
      break;
    case QgsGradientFillSymbolLayer::Radial:
      cboGradientType->setCurrentIndex( 1 );
      break;
    case QgsGradientFillSymbolLayer::Conical:
      cboGradientType->setCurrentIndex( 2 );
      break;
  }
  cboGradientType->blockSignals( false );

  cboCoordinateMode->blockSignals( true );
  switch ( mLayer->coordinateMode() )
  {
    case QgsGradientFillSymbolLayer::Viewport:
      cboCoordinateMode->setCurrentIndex( 1 );
      checkRefPoint1Centroid->setEnabled( false );
      checkRefPoint2Centroid->setEnabled( false );
      break;
    case QgsGradientFillSymbolLayer::Feature:
    default:
      cboCoordinateMode->setCurrentIndex( 0 );
      break;
  }
  cboCoordinateMode->blockSignals( false );

  cboGradientSpread->blockSignals( true );
  switch ( mLayer->gradientSpread() )
  {
    case QgsGradientFillSymbolLayer::Pad:
      cboGradientSpread->setCurrentIndex( 0 );
      break;
    case QgsGradientFillSymbolLayer::Repeat:
      cboGradientSpread->setCurrentIndex( 1 );
      break;
    case QgsGradientFillSymbolLayer::Reflect:
      cboGradientSpread->setCurrentIndex( 2 );
      break;
  }
  cboGradientSpread->blockSignals( false );

  spinRefPoint1X->blockSignals( true );
  spinRefPoint1X->setValue( mLayer->referencePoint1().x() );
  spinRefPoint1X->blockSignals( false );
  spinRefPoint1Y->blockSignals( true );
  spinRefPoint1Y->setValue( mLayer->referencePoint1().y() );
  spinRefPoint1Y->blockSignals( false );
  checkRefPoint1Centroid->blockSignals( true );
  checkRefPoint1Centroid->setChecked( mLayer->referencePoint1IsCentroid() );
  if ( mLayer->referencePoint1IsCentroid() )
  {
    spinRefPoint1X->setEnabled( false );
    spinRefPoint1Y->setEnabled( false );
  }
  checkRefPoint1Centroid->blockSignals( false );
  spinRefPoint2X->blockSignals( true );
  spinRefPoint2X->setValue( mLayer->referencePoint2().x() );
  spinRefPoint2X->blockSignals( false );
  spinRefPoint2Y->blockSignals( true );
  spinRefPoint2Y->setValue( mLayer->referencePoint2().y() );
  spinRefPoint2Y->blockSignals( false );
  checkRefPoint2Centroid->blockSignals( true );
  checkRefPoint2Centroid->setChecked( mLayer->referencePoint2IsCentroid() );
  if ( mLayer->referencePoint2IsCentroid() )
  {
    spinRefPoint2X->setEnabled( false );
    spinRefPoint2Y->setEnabled( false );
  }
  checkRefPoint2Centroid->blockSignals( false );

  spinOffsetX->blockSignals( true );
  spinOffsetX->setValue( mLayer->offset().x() );
  spinOffsetX->blockSignals( false );
  spinOffsetY->blockSignals( true );
  spinOffsetY->setValue( mLayer->offset().y() );
  spinOffsetY->blockSignals( false );
  mSpinAngle->blockSignals( true );
  mSpinAngle->setValue( mLayer->angle() );
  mSpinAngle->blockSignals( false );

  mOffsetUnitWidget->blockSignals( true );
  mOffsetUnitWidget->setUnit( mLayer->offsetUnit() );
  mOffsetUnitWidget->setMapUnitScale( mLayer->offsetMapUnitScale() );
  mOffsetUnitWidget->blockSignals( false );

  registerDataDefinedButton( mStartColorDDBtn, QgsSymbolLayer::PropertyFillColor );
  registerDataDefinedButton( mEndColorDDBtn, QgsSymbolLayer::PropertySecondaryColor );
  registerDataDefinedButton( mAngleDDBtn, QgsSymbolLayer::PropertyAngle );
  registerDataDefinedButton( mGradientTypeDDBtn, QgsSymbolLayer::PropertyGradientType );
  registerDataDefinedButton( mCoordinateModeDDBtn, QgsSymbolLayer::PropertyCoordinateMode );
  registerDataDefinedButton( mSpreadDDBtn, QgsSymbolLayer::PropertyGradientSpread );
  registerDataDefinedButton( mRefPoint1XDDBtn, QgsSymbolLayer::PropertyGradientReference1X );
  registerDataDefinedButton( mRefPoint1YDDBtn, QgsSymbolLayer::PropertyGradientReference1Y );
  registerDataDefinedButton( mRefPoint1CentroidDDBtn, QgsSymbolLayer::PropertyGradientReference1IsCentroid );
  registerDataDefinedButton( mRefPoint2XDDBtn, QgsSymbolLayer::PropertyGradientReference2X );
  registerDataDefinedButton( mRefPoint2YDDBtn, QgsSymbolLayer::PropertyGradientReference2Y );
  registerDataDefinedButton( mRefPoint2CentroidDDBtn, QgsSymbolLayer::PropertyGradientReference2IsCentroid );
  registerDataDefinedButton( mOffsetDDBtn, QgsSymbolLayer::PropertyOffset );
}

QgsSymbolLayer *QgsGradientFillSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsGradientFillSymbolLayerWidget::setColor( const QColor &color )
{
  mLayer->setColor( color );
  emit changed();
}

void QgsGradientFillSymbolLayerWidget::setColor2( const QColor &color )
{
  mLayer->setColor2( color );
  emit changed();
}

void QgsGradientFillSymbolLayerWidget::colorModeChanged()
{
  if ( radioTwoColor->isChecked() )
  {
    mLayer->setGradientColorType( QgsGradientFillSymbolLayer::SimpleTwoColor );
  }
  else
  {
    mLayer->setGradientColorType( QgsGradientFillSymbolLayer::ColorRamp );
  }
  emit changed();
}

void QgsGradientFillSymbolLayerWidget::applyColorRamp()
{
  if ( btnColorRamp->isNull() )
    return;

  mLayer->setColorRamp( btnColorRamp->colorRamp()->clone() );
  emit changed();
}

void QgsGradientFillSymbolLayerWidget::setGradientType( int index )
{
  switch ( index )
  {
    case 0:
      mLayer->setGradientType( QgsGradientFillSymbolLayer::Linear );
      //set sensible default reference points
      spinRefPoint1X->setValue( 0.5 );
      spinRefPoint1Y->setValue( 0 );
      spinRefPoint2X->setValue( 0.5 );
      spinRefPoint2Y->setValue( 1 );
      break;
    case 1:
      mLayer->setGradientType( QgsGradientFillSymbolLayer::Radial );
      //set sensible default reference points
      spinRefPoint1X->setValue( 0 );
      spinRefPoint1Y->setValue( 0 );
      spinRefPoint2X->setValue( 1 );
      spinRefPoint2Y->setValue( 1 );
      break;
    case 2:
      mLayer->setGradientType( QgsGradientFillSymbolLayer::Conical );
      spinRefPoint1X->setValue( 0.5 );
      spinRefPoint1Y->setValue( 0.5 );
      spinRefPoint2X->setValue( 1 );
      spinRefPoint2Y->setValue( 1 );
      break;
  }
  emit changed();
}

void QgsGradientFillSymbolLayerWidget::setCoordinateMode( int index )
{

  switch ( index )
  {
    case 0:
      //feature coordinate mode
      mLayer->setCoordinateMode( QgsGradientFillSymbolLayer::Feature );
      //allow choice of centroid reference positions
      checkRefPoint1Centroid->setEnabled( true );
      checkRefPoint2Centroid->setEnabled( true );
      break;
    case 1:
      //viewport coordinate mode
      mLayer->setCoordinateMode( QgsGradientFillSymbolLayer::Viewport );
      //disable choice of centroid reference positions
      checkRefPoint1Centroid->setChecked( Qt::Unchecked );
      checkRefPoint1Centroid->setEnabled( false );
      checkRefPoint2Centroid->setChecked( Qt::Unchecked );
      checkRefPoint2Centroid->setEnabled( false );
      break;
  }

  emit changed();
}

void QgsGradientFillSymbolLayerWidget::setGradientSpread( int index )
{
  switch ( index )
  {
    case 0:
      mLayer->setGradientSpread( QgsGradientFillSymbolLayer::Pad );
      break;
    case 1:
      mLayer->setGradientSpread( QgsGradientFillSymbolLayer::Repeat );
      break;
    case 2:
      mLayer->setGradientSpread( QgsGradientFillSymbolLayer::Reflect );
      break;
  }

  emit changed();
}

void QgsGradientFillSymbolLayerWidget::offsetChanged()
{
  mLayer->setOffset( QPointF( spinOffsetX->value(), spinOffsetY->value() ) );
  emit changed();
}

void QgsGradientFillSymbolLayerWidget::referencePointChanged()
{
  mLayer->setReferencePoint1( QPointF( spinRefPoint1X->value(), spinRefPoint1Y->value() ) );
  mLayer->setReferencePoint1IsCentroid( checkRefPoint1Centroid->isChecked() );
  mLayer->setReferencePoint2( QPointF( spinRefPoint2X->value(), spinRefPoint2Y->value() ) );
  mLayer->setReferencePoint2IsCentroid( checkRefPoint2Centroid->isChecked() );
  emit changed();
}

void QgsGradientFillSymbolLayerWidget::mSpinAngle_valueChanged( double value )
{
  mLayer->setAngle( value );
  emit changed();
}

void QgsGradientFillSymbolLayerWidget::mOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetUnit( mOffsetUnitWidget->unit() );
    mLayer->setOffsetMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

///////////

QgsShapeburstFillSymbolLayerWidget::QgsShapeburstFillSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;

  setupUi( this );
  connect( mSpinBlurRadius, qgis::overload< int >::of( &QSpinBox::valueChanged ), this, &QgsShapeburstFillSymbolLayerWidget::mSpinBlurRadius_valueChanged );
  connect( mSpinMaxDistance, qgis::overload< double >::of( &QDoubleSpinBox::valueChanged ), this, &QgsShapeburstFillSymbolLayerWidget::mSpinMaxDistance_valueChanged );
  connect( mDistanceUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsShapeburstFillSymbolLayerWidget::mDistanceUnitWidget_changed );
  connect( mRadioUseWholeShape, &QRadioButton::toggled, this, &QgsShapeburstFillSymbolLayerWidget::mRadioUseWholeShape_toggled );
  connect( mOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsShapeburstFillSymbolLayerWidget::mOffsetUnitWidget_changed );
  connect( mIgnoreRingsCheckBox, &QCheckBox::stateChanged, this, &QgsShapeburstFillSymbolLayerWidget::mIgnoreRingsCheckBox_stateChanged );
  mDistanceUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                 << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );

  QButtonGroup *group1 = new QButtonGroup( this );
  group1->addButton( radioColorRamp );
  group1->addButton( radioTwoColor );
  QButtonGroup *group2 = new QButtonGroup( this );
  group2->addButton( mRadioUseMaxDistance );
  group2->addButton( mRadioUseWholeShape );
  btnChangeColor->setAllowOpacity( true );
  btnChangeColor->setColorDialogTitle( tr( "Select Gradient Color" ) );
  btnChangeColor->setContext( QStringLiteral( "symbology" ) );
  btnChangeColor->setShowNoColor( true );
  btnChangeColor->setNoColorString( tr( "Transparent" ) );
  btnChangeColor2->setAllowOpacity( true );
  btnChangeColor2->setColorDialogTitle( tr( "Select Gradient Color" ) );
  btnChangeColor2->setContext( QStringLiteral( "symbology" ) );
  btnChangeColor2->setShowNoColor( true );
  btnChangeColor2->setNoColorString( tr( "Transparent" ) );

  mStartColorDDBtn->registerLinkedWidget( btnChangeColor );
  mEndColorDDBtn->registerLinkedWidget( btnChangeColor2 );

  spinOffsetX->setClearValue( 0.0 );
  spinOffsetY->setClearValue( 0.0 );
  mSpinMaxDistance->setClearValue( 5.0 );

  btnColorRamp->setShowGradientOnly( true );

  connect( btnColorRamp, &QgsColorRampButton::colorRampChanged, this, &QgsShapeburstFillSymbolLayerWidget::applyColorRamp );

  connect( btnChangeColor, &QgsColorButton::colorChanged, this, &QgsShapeburstFillSymbolLayerWidget::setColor );
  connect( btnChangeColor2, &QgsColorButton::colorChanged, this, &QgsShapeburstFillSymbolLayerWidget::setColor2 );
  connect( radioTwoColor, &QAbstractButton::toggled, this, &QgsShapeburstFillSymbolLayerWidget::colorModeChanged );
  connect( spinOffsetX, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsShapeburstFillSymbolLayerWidget::offsetChanged );
  connect( spinOffsetY, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsShapeburstFillSymbolLayerWidget::offsetChanged );

  connect( mBlurSlider, &QAbstractSlider::valueChanged, mSpinBlurRadius, &QSpinBox::setValue );
  connect( mSpinBlurRadius, static_cast < void ( QSpinBox::* )( int ) > ( &QSpinBox::valueChanged ), mBlurSlider, &QAbstractSlider::setValue );
}

void QgsShapeburstFillSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( layer->layerType() != QLatin1String( "ShapeburstFill" ) )
    return;

  // layer type is correct, we can do the cast
  mLayer = static_cast<QgsShapeburstFillSymbolLayer *>( layer );

  // set values
  btnChangeColor->blockSignals( true );
  btnChangeColor->setColor( mLayer->color() );
  btnChangeColor->blockSignals( false );
  btnChangeColor2->blockSignals( true );
  btnChangeColor2->setColor( mLayer->color2() );
  btnChangeColor2->blockSignals( false );

  if ( mLayer->colorType() == QgsShapeburstFillSymbolLayer::SimpleTwoColor )
  {
    radioTwoColor->setChecked( true );
    btnColorRamp->setEnabled( false );
  }
  else
  {
    radioColorRamp->setChecked( true );
    btnChangeColor->setEnabled( false );
    btnChangeColor2->setEnabled( false );
  }

  mSpinBlurRadius->blockSignals( true );
  mBlurSlider->blockSignals( true );
  mSpinBlurRadius->setValue( mLayer->blurRadius() );
  mBlurSlider->setValue( mLayer->blurRadius() );
  mSpinBlurRadius->blockSignals( false );
  mBlurSlider->blockSignals( false );

  mSpinMaxDistance->blockSignals( true );
  mSpinMaxDistance->setValue( mLayer->maxDistance() );
  mSpinMaxDistance->blockSignals( false );

  mRadioUseWholeShape->blockSignals( true );
  mRadioUseMaxDistance->blockSignals( true );
  if ( mLayer->useWholeShape() )
  {
    mRadioUseWholeShape->setChecked( true );
    mSpinMaxDistance->setEnabled( false );
    mDistanceUnitWidget->setEnabled( false );
  }
  else
  {
    mRadioUseMaxDistance->setChecked( true );
    mSpinMaxDistance->setEnabled( true );
    mDistanceUnitWidget->setEnabled( true );
  }
  mRadioUseWholeShape->blockSignals( false );
  mRadioUseMaxDistance->blockSignals( false );

  mDistanceUnitWidget->blockSignals( true );
  mDistanceUnitWidget->setUnit( mLayer->distanceUnit() );
  mDistanceUnitWidget->setMapUnitScale( mLayer->distanceMapUnitScale() );
  mDistanceUnitWidget->blockSignals( false );

  mIgnoreRingsCheckBox->blockSignals( true );
  mIgnoreRingsCheckBox->setCheckState( mLayer->ignoreRings() ? Qt::Checked : Qt::Unchecked );
  mIgnoreRingsCheckBox->blockSignals( false );

  // set source color ramp
  if ( mLayer->colorRamp() )
  {
    btnColorRamp->blockSignals( true );
    btnColorRamp->setColorRamp( mLayer->colorRamp() );
    btnColorRamp->blockSignals( false );
  }

  spinOffsetX->blockSignals( true );
  spinOffsetX->setValue( mLayer->offset().x() );
  spinOffsetX->blockSignals( false );
  spinOffsetY->blockSignals( true );
  spinOffsetY->setValue( mLayer->offset().y() );
  spinOffsetY->blockSignals( false );
  mOffsetUnitWidget->blockSignals( true );
  mOffsetUnitWidget->setUnit( mLayer->offsetUnit() );
  mOffsetUnitWidget->setMapUnitScale( mLayer->offsetMapUnitScale() );
  mOffsetUnitWidget->blockSignals( false );

  registerDataDefinedButton( mStartColorDDBtn, QgsSymbolLayer::PropertyFillColor );
  registerDataDefinedButton( mEndColorDDBtn, QgsSymbolLayer::PropertySecondaryColor );
  registerDataDefinedButton( mBlurRadiusDDBtn, QgsSymbolLayer::PropertyBlurRadius );
  registerDataDefinedButton( mShadeWholeShapeDDBtn, QgsSymbolLayer::PropertyShapeburstUseWholeShape );
  registerDataDefinedButton( mShadeDistanceDDBtn, QgsSymbolLayer::PropertyShapeburstMaxDistance );
  registerDataDefinedButton( mIgnoreRingsDDBtn, QgsSymbolLayer::PropertyShapeburstIgnoreRings );
  registerDataDefinedButton( mOffsetDDBtn, QgsSymbolLayer::PropertyOffset );
}

QgsSymbolLayer *QgsShapeburstFillSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsShapeburstFillSymbolLayerWidget::setColor( const QColor &color )
{
  if ( mLayer )
  {
    mLayer->setColor( color );
    emit changed();
  }
}

void QgsShapeburstFillSymbolLayerWidget::setColor2( const QColor &color )
{
  if ( mLayer )
  {
    mLayer->setColor2( color );
    emit changed();
  }
}

void QgsShapeburstFillSymbolLayerWidget::colorModeChanged()
{
  if ( !mLayer )
  {
    return;
  }

  if ( radioTwoColor->isChecked() )
  {
    mLayer->setColorType( QgsShapeburstFillSymbolLayer::SimpleTwoColor );
  }
  else
  {
    mLayer->setColorType( QgsShapeburstFillSymbolLayer::ColorRamp );
  }
  emit changed();
}

void QgsShapeburstFillSymbolLayerWidget::mSpinBlurRadius_valueChanged( int value )
{
  if ( mLayer )
  {
    mLayer->setBlurRadius( value );
    emit changed();
  }
}

void QgsShapeburstFillSymbolLayerWidget::mSpinMaxDistance_valueChanged( double value )
{
  if ( mLayer )
  {
    mLayer->setMaxDistance( value );
    emit changed();
  }
}

void QgsShapeburstFillSymbolLayerWidget::mDistanceUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setDistanceUnit( mDistanceUnitWidget->unit() );
    mLayer->setDistanceMapUnitScale( mDistanceUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsShapeburstFillSymbolLayerWidget::mRadioUseWholeShape_toggled( bool value )
{
  if ( mLayer )
  {
    mLayer->setUseWholeShape( value );
    mDistanceUnitWidget->setEnabled( !value );
    emit changed();
  }
}

void QgsShapeburstFillSymbolLayerWidget::applyColorRamp()
{
  QgsColorRamp *ramp = btnColorRamp->colorRamp();
  if ( !ramp )
    return;

  mLayer->setColorRamp( ramp );
  emit changed();
}

void QgsShapeburstFillSymbolLayerWidget::offsetChanged()
{
  if ( mLayer )
  {
    mLayer->setOffset( QPointF( spinOffsetX->value(), spinOffsetY->value() ) );
    emit changed();
  }
}

void QgsShapeburstFillSymbolLayerWidget::mOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetUnit( mOffsetUnitWidget->unit() );
    mLayer->setOffsetMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}


void QgsShapeburstFillSymbolLayerWidget::mIgnoreRingsCheckBox_stateChanged( int state )
{
  bool checked = ( state == Qt::Checked );
  mLayer->setIgnoreRings( checked );
  emit changed();
}

///////////

QgsMarkerLineSymbolLayerWidget::QgsMarkerLineSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;

  setupUi( this );
  connect( mIntervalUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsMarkerLineSymbolLayerWidget::mIntervalUnitWidget_changed );
  connect( mOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsMarkerLineSymbolLayerWidget::mOffsetUnitWidget_changed );
  connect( mOffsetAlongLineUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsMarkerLineSymbolLayerWidget::mOffsetAlongLineUnitWidget_changed );
  connect( mAverageAngleUnit, &QgsUnitSelectionWidget::changed, this, &QgsMarkerLineSymbolLayerWidget::averageAngleUnitChanged );
  mIntervalUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                 << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mOffsetAlongLineUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                        << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mAverageAngleUnit->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );

  mRingFilterComboBox->addItem( QgsApplication::getThemeIcon( QStringLiteral( "mIconAllRings.svg" ) ), tr( "All Rings" ), QgsLineSymbolLayer::AllRings );
  mRingFilterComboBox->addItem( QgsApplication::getThemeIcon( QStringLiteral( "mIconExteriorRing.svg" ) ), tr( "Exterior Ring Only" ), QgsLineSymbolLayer::ExteriorRingOnly );
  mRingFilterComboBox->addItem( QgsApplication::getThemeIcon( QStringLiteral( "mIconInteriorRings.svg" ) ), tr( "Interior Rings Only" ), QgsLineSymbolLayer::InteriorRingsOnly );
  connect( mRingFilterComboBox, qgis::overload< int >::of( &QComboBox::currentIndexChanged ), this, [ = ]( int )
  {
    if ( mLayer )
    {
      mLayer->setRingFilter( static_cast< QgsLineSymbolLayer::RenderRingFilter >( mRingFilterComboBox->currentData().toInt() ) );
      emit changed();
    }
  } );

  spinOffset->setClearValue( 0.0 );


  if ( vl && vl->geometryType() != QgsWkbTypes::PolygonGeometry )
  {
    mRingFilterComboBox->hide();
    mRingsLabel->hide();
  }

  mSpinAverageAngleLength->setClearValue( 4.0 );

  connect( spinInterval, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsMarkerLineSymbolLayerWidget::setInterval );
  connect( mSpinOffsetAlongLine, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsMarkerLineSymbolLayerWidget::setOffsetAlongLine );
  connect( chkRotateMarker, &QAbstractButton::clicked, this, &QgsMarkerLineSymbolLayerWidget::setRotate );
  connect( spinOffset, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsMarkerLineSymbolLayerWidget::setOffset );
  connect( mSpinAverageAngleLength, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsMarkerLineSymbolLayerWidget::setAverageAngle );
  connect( radInterval, &QAbstractButton::clicked, this, &QgsMarkerLineSymbolLayerWidget::setPlacement );
  connect( radVertex, &QAbstractButton::clicked, this, &QgsMarkerLineSymbolLayerWidget::setPlacement );
  connect( radVertexLast, &QAbstractButton::clicked, this, &QgsMarkerLineSymbolLayerWidget::setPlacement );
  connect( radVertexFirst, &QAbstractButton::clicked, this, &QgsMarkerLineSymbolLayerWidget::setPlacement );
  connect( radCentralPoint, &QAbstractButton::clicked, this, &QgsMarkerLineSymbolLayerWidget::setPlacement );
  connect( radCurvePoint, &QAbstractButton::clicked, this, &QgsMarkerLineSymbolLayerWidget::setPlacement );
  connect( radSegmentCentralPoint, &QAbstractButton::clicked, this, &QgsMarkerLineSymbolLayerWidget::setPlacement );
}

void QgsMarkerLineSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( layer->layerType() != QLatin1String( "MarkerLine" ) )
    return;

  // layer type is correct, we can do the cast
  mLayer = static_cast<QgsMarkerLineSymbolLayer *>( layer );

  // set values
  spinInterval->blockSignals( true );
  spinInterval->setValue( mLayer->interval() );
  spinInterval->blockSignals( false );
  mSpinOffsetAlongLine->blockSignals( true );
  mSpinOffsetAlongLine->setValue( mLayer->offsetAlongLine() );
  mSpinOffsetAlongLine->blockSignals( false );
  chkRotateMarker->blockSignals( true );
  chkRotateMarker->setChecked( mLayer->rotateSymbols() );
  chkRotateMarker->blockSignals( false );
  spinOffset->blockSignals( true );
  spinOffset->setValue( mLayer->offset() );
  spinOffset->blockSignals( false );
  if ( mLayer->placement() == QgsTemplatedLineSymbolLayerBase::Interval )
    radInterval->setChecked( true );
  else if ( mLayer->placement() == QgsTemplatedLineSymbolLayerBase::Vertex )
    radVertex->setChecked( true );
  else if ( mLayer->placement() == QgsTemplatedLineSymbolLayerBase::LastVertex )
    radVertexLast->setChecked( true );
  else if ( mLayer->placement() == QgsTemplatedLineSymbolLayerBase::CentralPoint )
    radCentralPoint->setChecked( true );
  else if ( mLayer->placement() == QgsTemplatedLineSymbolLayerBase::CurvePoint )
    radCurvePoint->setChecked( true );
  else if ( mLayer->placement() == QgsTemplatedLineSymbolLayerBase::SegmentCenter )
    radSegmentCentralPoint->setChecked( true );
  else
    radVertexFirst->setChecked( true );

  // set units
  mIntervalUnitWidget->blockSignals( true );
  mIntervalUnitWidget->setUnit( mLayer->intervalUnit() );
  mIntervalUnitWidget->setMapUnitScale( mLayer->intervalMapUnitScale() );
  mIntervalUnitWidget->blockSignals( false );
  mOffsetUnitWidget->blockSignals( true );
  mOffsetUnitWidget->setUnit( mLayer->offsetUnit() );
  mOffsetUnitWidget->setMapUnitScale( mLayer->offsetMapUnitScale() );
  mOffsetUnitWidget->blockSignals( false );
  mOffsetAlongLineUnitWidget->blockSignals( true );
  mOffsetAlongLineUnitWidget->setUnit( mLayer->offsetAlongLineUnit() );
  mOffsetAlongLineUnitWidget->setMapUnitScale( mLayer->offsetAlongLineMapUnitScale() );
  mOffsetAlongLineUnitWidget->blockSignals( false );

  whileBlocking( mAverageAngleUnit )->setUnit( mLayer->averageAngleUnit() );
  whileBlocking( mAverageAngleUnit )->setMapUnitScale( mLayer->averageAngleMapUnitScale() );
  whileBlocking( mSpinAverageAngleLength )->setValue( mLayer->averageAngleLength() );

  whileBlocking( mRingFilterComboBox )->setCurrentIndex( mRingFilterComboBox->findData( mLayer->ringFilter() ) );

  setPlacement(); // update gui

  registerDataDefinedButton( mIntervalDDBtn, QgsSymbolLayer::PropertyInterval );
  registerDataDefinedButton( mLineOffsetDDBtn, QgsSymbolLayer::PropertyOffset );
  registerDataDefinedButton( mPlacementDDBtn, QgsSymbolLayer::PropertyPlacement );
  registerDataDefinedButton( mOffsetAlongLineDDBtn, QgsSymbolLayer::PropertyOffsetAlongLine );
  registerDataDefinedButton( mAverageAngleDDBtn, QgsSymbolLayer::PropertyAverageAngleLength );
}

QgsSymbolLayer *QgsMarkerLineSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsMarkerLineSymbolLayerWidget::setInterval( double val )
{
  mLayer->setInterval( val );
  emit changed();
}

void QgsMarkerLineSymbolLayerWidget::setOffsetAlongLine( double val )
{
  mLayer->setOffsetAlongLine( val );
  emit changed();
}

void QgsMarkerLineSymbolLayerWidget::setRotate()
{
  mSpinAverageAngleLength->setEnabled( chkRotateMarker->isChecked() && ( radInterval->isChecked() || radCentralPoint->isChecked() ) );
  mAverageAngleUnit->setEnabled( mSpinAverageAngleLength->isEnabled() );

  mLayer->setRotateSymbols( chkRotateMarker->isChecked() );
  emit changed();
}

void QgsMarkerLineSymbolLayerWidget::setOffset()
{
  mLayer->setOffset( spinOffset->value() );
  emit changed();
}

void QgsMarkerLineSymbolLayerWidget::setPlacement()
{
  bool interval = radInterval->isChecked();
  spinInterval->setEnabled( interval );
  mSpinOffsetAlongLine->setEnabled( radInterval->isChecked() || radVertexLast->isChecked() || radVertexFirst->isChecked() );
  mOffsetAlongLineUnitWidget->setEnabled( mSpinOffsetAlongLine->isEnabled() );
  mSpinAverageAngleLength->setEnabled( chkRotateMarker->isChecked() && ( radInterval->isChecked() || radCentralPoint->isChecked() ) );
  mAverageAngleUnit->setEnabled( mSpinAverageAngleLength->isEnabled() );
  //mLayer->setPlacement( interval ? QgsMarkerLineSymbolLayer::Interval : QgsMarkerLineSymbolLayer::Vertex );
  if ( radInterval->isChecked() )
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::Interval );
  else if ( radVertex->isChecked() )
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::Vertex );
  else if ( radVertexLast->isChecked() )
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::LastVertex );
  else if ( radVertexFirst->isChecked() )
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::FirstVertex );
  else if ( radCurvePoint->isChecked() )
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::CurvePoint );
  else if ( radSegmentCentralPoint->isChecked() )
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::SegmentCenter );
  else
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::CentralPoint );

  emit changed();
}

void QgsMarkerLineSymbolLayerWidget::mIntervalUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setIntervalUnit( mIntervalUnitWidget->unit() );
    mLayer->setIntervalMapUnitScale( mIntervalUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsMarkerLineSymbolLayerWidget::mOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetUnit( mOffsetUnitWidget->unit() );
    mLayer->setOffsetMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsMarkerLineSymbolLayerWidget::mOffsetAlongLineUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetAlongLineUnit( mOffsetAlongLineUnitWidget->unit() );
    mLayer->setOffsetAlongLineMapUnitScale( mOffsetAlongLineUnitWidget->getMapUnitScale() );
  }
  emit changed();
}

void QgsMarkerLineSymbolLayerWidget::averageAngleUnitChanged()
{
  if ( mLayer )
  {
    mLayer->setAverageAngleUnit( mAverageAngleUnit->unit() );
    mLayer->setAverageAngleMapUnitScale( mAverageAngleUnit->getMapUnitScale() );
  }
  emit changed();
}

void QgsMarkerLineSymbolLayerWidget::setAverageAngle( double val )
{
  if ( mLayer )
  {
    mLayer->setAverageAngleLength( val );
    emit changed();
  }
}


///////////

QgsHashedLineSymbolLayerWidget::QgsHashedLineSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;

  setupUi( this );
  connect( mIntervalUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsHashedLineSymbolLayerWidget::mIntervalUnitWidget_changed );
  connect( mOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsHashedLineSymbolLayerWidget::mOffsetUnitWidget_changed );
  connect( mOffsetAlongLineUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsHashedLineSymbolLayerWidget::mOffsetAlongLineUnitWidget_changed );
  connect( mAverageAngleUnit, &QgsUnitSelectionWidget::changed, this, &QgsHashedLineSymbolLayerWidget::averageAngleUnitChanged );
  connect( mHashLengthUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsHashedLineSymbolLayerWidget::hashLengthUnitWidgetChanged );
  mIntervalUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                 << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mOffsetAlongLineUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                        << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mAverageAngleUnit->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mHashLengthUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                   << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );

  mRingFilterComboBox->addItem( QgsApplication::getThemeIcon( QStringLiteral( "mIconAllRings.svg" ) ), tr( "All Rings" ), QgsLineSymbolLayer::AllRings );
  mRingFilterComboBox->addItem( QgsApplication::getThemeIcon( QStringLiteral( "mIconExteriorRing.svg" ) ), tr( "Exterior Ring Only" ), QgsLineSymbolLayer::ExteriorRingOnly );
  mRingFilterComboBox->addItem( QgsApplication::getThemeIcon( QStringLiteral( "mIconInteriorRings.svg" ) ), tr( "Interior Rings Only" ), QgsLineSymbolLayer::InteriorRingsOnly );
  connect( mRingFilterComboBox, qgis::overload< int >::of( &QComboBox::currentIndexChanged ), this, [ = ]( int )
  {
    if ( mLayer )
    {
      mLayer->setRingFilter( static_cast< QgsLineSymbolLayer::RenderRingFilter >( mRingFilterComboBox->currentData().toInt() ) );
      emit changed();
    }
  } );

  spinOffset->setClearValue( 0.0 );


  if ( vl && vl->geometryType() != QgsWkbTypes::PolygonGeometry )
  {
    mRingFilterComboBox->hide();
    mRingsLabel->hide();
  }

  mHashRotationSpinBox->setClearValue( 0 );
  mSpinAverageAngleLength->setClearValue( 4.0 );

  connect( spinInterval, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsHashedLineSymbolLayerWidget::setInterval );
  connect( mSpinOffsetAlongLine, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsHashedLineSymbolLayerWidget::setOffsetAlongLine );
  connect( mSpinHashLength, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsHashedLineSymbolLayerWidget::setHashLength );
  connect( mHashRotationSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsHashedLineSymbolLayerWidget::setHashAngle );
  connect( chkRotateMarker, &QAbstractButton::clicked, this, &QgsHashedLineSymbolLayerWidget::setRotate );
  connect( spinOffset, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsHashedLineSymbolLayerWidget::setOffset );
  connect( mSpinAverageAngleLength, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsHashedLineSymbolLayerWidget::setAverageAngle );
  connect( radInterval, &QAbstractButton::clicked, this, &QgsHashedLineSymbolLayerWidget::setPlacement );
  connect( radVertex, &QAbstractButton::clicked, this, &QgsHashedLineSymbolLayerWidget::setPlacement );
  connect( radVertexLast, &QAbstractButton::clicked, this, &QgsHashedLineSymbolLayerWidget::setPlacement );
  connect( radVertexFirst, &QAbstractButton::clicked, this, &QgsHashedLineSymbolLayerWidget::setPlacement );
  connect( radCentralPoint, &QAbstractButton::clicked, this, &QgsHashedLineSymbolLayerWidget::setPlacement );
  connect( radCurvePoint, &QAbstractButton::clicked, this, &QgsHashedLineSymbolLayerWidget::setPlacement );
  connect( radSegmentCentralPoint, &QAbstractButton::clicked, this, &QgsHashedLineSymbolLayerWidget::setPlacement );
}

void QgsHashedLineSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( layer->layerType() != QLatin1String( "HashLine" ) )
    return;

  // layer type is correct, we can do the cast
  mLayer = static_cast<QgsHashedLineSymbolLayer *>( layer );

  // set values
  spinInterval->blockSignals( true );
  spinInterval->setValue( mLayer->interval() );
  spinInterval->blockSignals( false );
  mSpinOffsetAlongLine->blockSignals( true );
  mSpinOffsetAlongLine->setValue( mLayer->offsetAlongLine() );
  mSpinOffsetAlongLine->blockSignals( false );
  whileBlocking( mSpinHashLength )->setValue( mLayer->hashLength() );
  whileBlocking( mHashRotationSpinBox )->setValue( mLayer->hashAngle() );
  chkRotateMarker->blockSignals( true );
  chkRotateMarker->setChecked( mLayer->rotateSymbols() );
  chkRotateMarker->blockSignals( false );
  spinOffset->blockSignals( true );
  spinOffset->setValue( mLayer->offset() );
  spinOffset->blockSignals( false );
  if ( mLayer->placement() == QgsTemplatedLineSymbolLayerBase::Interval )
    radInterval->setChecked( true );
  else if ( mLayer->placement() == QgsTemplatedLineSymbolLayerBase::Vertex )
    radVertex->setChecked( true );
  else if ( mLayer->placement() == QgsTemplatedLineSymbolLayerBase::LastVertex )
    radVertexLast->setChecked( true );
  else if ( mLayer->placement() == QgsTemplatedLineSymbolLayerBase::CentralPoint )
    radCentralPoint->setChecked( true );
  else if ( mLayer->placement() == QgsTemplatedLineSymbolLayerBase::CurvePoint )
    radCurvePoint->setChecked( true );
  else if ( mLayer->placement() == QgsTemplatedLineSymbolLayerBase::SegmentCenter )
    radSegmentCentralPoint->setChecked( true );
  else
    radVertexFirst->setChecked( true );

  // set units
  mIntervalUnitWidget->blockSignals( true );
  mIntervalUnitWidget->setUnit( mLayer->intervalUnit() );
  mIntervalUnitWidget->setMapUnitScale( mLayer->intervalMapUnitScale() );
  mIntervalUnitWidget->blockSignals( false );
  mOffsetUnitWidget->blockSignals( true );
  mOffsetUnitWidget->setUnit( mLayer->offsetUnit() );
  mOffsetUnitWidget->setMapUnitScale( mLayer->offsetMapUnitScale() );
  mOffsetUnitWidget->blockSignals( false );
  mOffsetAlongLineUnitWidget->blockSignals( true );
  mOffsetAlongLineUnitWidget->setUnit( mLayer->offsetAlongLineUnit() );
  mOffsetAlongLineUnitWidget->setMapUnitScale( mLayer->offsetAlongLineMapUnitScale() );
  mOffsetAlongLineUnitWidget->blockSignals( false );
  whileBlocking( mAverageAngleUnit )->setUnit( mLayer->averageAngleUnit() );
  whileBlocking( mAverageAngleUnit )->setMapUnitScale( mLayer->averageAngleMapUnitScale() );
  whileBlocking( mSpinAverageAngleLength )->setValue( mLayer->averageAngleLength() );
  whileBlocking( mHashLengthUnitWidget )->setUnit( mLayer->hashLengthUnit() );
  whileBlocking( mHashLengthUnitWidget )->setMapUnitScale( mLayer->hashLengthMapUnitScale() );

  whileBlocking( mRingFilterComboBox )->setCurrentIndex( mRingFilterComboBox->findData( mLayer->ringFilter() ) );

  setPlacement(); // update gui

  registerDataDefinedButton( mIntervalDDBtn, QgsSymbolLayer::PropertyInterval );
  registerDataDefinedButton( mLineOffsetDDBtn, QgsSymbolLayer::PropertyOffset );
  registerDataDefinedButton( mPlacementDDBtn, QgsSymbolLayer::PropertyPlacement );
  registerDataDefinedButton( mOffsetAlongLineDDBtn, QgsSymbolLayer::PropertyOffsetAlongLine );
  registerDataDefinedButton( mHashLengthDDBtn, QgsSymbolLayer::PropertyLineDistance );
  registerDataDefinedButton( mHashRotationDDBtn, QgsSymbolLayer::PropertyLineAngle );
  registerDataDefinedButton( mAverageAngleDDBtn, QgsSymbolLayer::PropertyAverageAngleLength );
}

QgsSymbolLayer *QgsHashedLineSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsHashedLineSymbolLayerWidget::setInterval( double val )
{
  mLayer->setInterval( val );
  emit changed();
}

void QgsHashedLineSymbolLayerWidget::setOffsetAlongLine( double val )
{
  mLayer->setOffsetAlongLine( val );
  emit changed();
}

void QgsHashedLineSymbolLayerWidget::setHashLength( double val )
{
  mLayer->setHashLength( val );
  emit changed();
}

void QgsHashedLineSymbolLayerWidget::setHashAngle( double val )
{
  mLayer->setHashAngle( val );
  emit changed();
}

void QgsHashedLineSymbolLayerWidget::setRotate()
{
  mSpinAverageAngleLength->setEnabled( chkRotateMarker->isChecked() && ( radInterval->isChecked() || radCentralPoint->isChecked() ) );
  mAverageAngleUnit->setEnabled( mSpinAverageAngleLength->isEnabled() );

  mLayer->setRotateSymbols( chkRotateMarker->isChecked() );
  emit changed();
}

void QgsHashedLineSymbolLayerWidget::setOffset()
{
  mLayer->setOffset( spinOffset->value() );
  emit changed();
}

void QgsHashedLineSymbolLayerWidget::setPlacement()
{
  bool interval = radInterval->isChecked();
  spinInterval->setEnabled( interval );
  mSpinOffsetAlongLine->setEnabled( radInterval->isChecked() || radVertexLast->isChecked() || radVertexFirst->isChecked() );
  mOffsetAlongLineUnitWidget->setEnabled( mSpinOffsetAlongLine->isEnabled() );
  mSpinAverageAngleLength->setEnabled( chkRotateMarker->isChecked() && ( radInterval->isChecked() || radCentralPoint->isChecked() ) );
  mAverageAngleUnit->setEnabled( mSpinAverageAngleLength->isEnabled() );
  //mLayer->setPlacement( interval ? QgsMarkerLineSymbolLayer::Interval : QgsMarkerLineSymbolLayer::Vertex );
  if ( radInterval->isChecked() )
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::Interval );
  else if ( radVertex->isChecked() )
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::Vertex );
  else if ( radVertexLast->isChecked() )
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::LastVertex );
  else if ( radVertexFirst->isChecked() )
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::FirstVertex );
  else if ( radCurvePoint->isChecked() )
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::CurvePoint );
  else if ( radSegmentCentralPoint->isChecked() )
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::SegmentCenter );
  else
    mLayer->setPlacement( QgsTemplatedLineSymbolLayerBase::CentralPoint );

  emit changed();
}

void QgsHashedLineSymbolLayerWidget::mIntervalUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setIntervalUnit( mIntervalUnitWidget->unit() );
    mLayer->setIntervalMapUnitScale( mIntervalUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsHashedLineSymbolLayerWidget::mOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetUnit( mOffsetUnitWidget->unit() );
    mLayer->setOffsetMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsHashedLineSymbolLayerWidget::mOffsetAlongLineUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetAlongLineUnit( mOffsetAlongLineUnitWidget->unit() );
    mLayer->setOffsetAlongLineMapUnitScale( mOffsetAlongLineUnitWidget->getMapUnitScale() );
  }
  emit changed();
}

void QgsHashedLineSymbolLayerWidget::hashLengthUnitWidgetChanged()
{
  if ( mLayer )
  {
    mLayer->setHashLengthUnit( mHashLengthUnitWidget->unit() );
    mLayer->setHashLengthMapUnitScale( mHashLengthUnitWidget->getMapUnitScale() );
  }
  emit changed();
}

void QgsHashedLineSymbolLayerWidget::averageAngleUnitChanged()
{
  if ( mLayer )
  {
    mLayer->setAverageAngleUnit( mAverageAngleUnit->unit() );
    mLayer->setAverageAngleMapUnitScale( mAverageAngleUnit->getMapUnitScale() );
  }
  emit changed();
}

void QgsHashedLineSymbolLayerWidget::setAverageAngle( double val )
{
  if ( mLayer )
  {
    mLayer->setAverageAngleLength( val );
    emit changed();
  }
}

///////////


QgsSvgMarkerSymbolLayerWidget::QgsSvgMarkerSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;

  setupUi( this );

  mSvgSelectorWidget->sourceLineEdit()->setPropertyOverrideToolButtonVisible( true );
  mSvgSelectorWidget->sourceLineEdit()->setLastPathSettingsKey( QStringLiteral( "/UI/lastSVGMarkerDir" ) );

  connect( mSvgSelectorWidget->sourceLineEdit(), &QgsSvgSourceLineEdit::sourceChanged, this, &QgsSvgMarkerSymbolLayerWidget::svgSourceChanged );
  connect( mChangeColorButton, &QgsColorButton::colorChanged, this, &QgsSvgMarkerSymbolLayerWidget::mChangeColorButton_colorChanged );
  connect( mChangeStrokeColorButton, &QgsColorButton::colorChanged, this, &QgsSvgMarkerSymbolLayerWidget::mChangeStrokeColorButton_colorChanged );
  connect( mStrokeWidthSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSvgMarkerSymbolLayerWidget::mStrokeWidthSpinBox_valueChanged );
  connect( mSizeUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSvgMarkerSymbolLayerWidget::mSizeUnitWidget_changed );
  connect( mStrokeWidthUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSvgMarkerSymbolLayerWidget::mStrokeWidthUnitWidget_changed );
  connect( mOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSvgMarkerSymbolLayerWidget::mOffsetUnitWidget_changed );
  connect( mHorizontalAnchorComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsSvgMarkerSymbolLayerWidget::mHorizontalAnchorComboBox_currentIndexChanged );
  connect( mVerticalAnchorComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsSvgMarkerSymbolLayerWidget::mVerticalAnchorComboBox_currentIndexChanged );
  mSizeUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                             << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mStrokeWidthUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                    << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mChangeColorButton->setAllowOpacity( true );
  mChangeColorButton->setColorDialogTitle( tr( "Select Fill color" ) );
  mChangeColorButton->setContext( QStringLiteral( "symbology" ) );
  mChangeStrokeColorButton->setAllowOpacity( true );
  mChangeStrokeColorButton->setColorDialogTitle( tr( "Select Stroke Color" ) );
  mChangeStrokeColorButton->setContext( QStringLiteral( "symbology" ) );

  mFillColorDDBtn->registerLinkedWidget( mChangeColorButton );
  mStrokeColorDDBtn->registerLinkedWidget( mChangeStrokeColorButton );

  spinOffsetX->setClearValue( 0.0 );
  spinOffsetY->setClearValue( 0.0 );
  spinAngle->setClearValue( 0.0 );

  connect( spinWidth, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSvgMarkerSymbolLayerWidget::setWidth );
  connect( spinHeight, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSvgMarkerSymbolLayerWidget::setHeight );
  connect( mLockAspectRatio, static_cast < void ( QgsRatioLockButton::* )( bool ) > ( &QgsRatioLockButton::lockChanged ), this, &QgsSvgMarkerSymbolLayerWidget::lockAspectRatioChanged );
  connect( spinAngle, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSvgMarkerSymbolLayerWidget::setAngle );
  connect( spinOffsetX, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSvgMarkerSymbolLayerWidget::setOffset );
  connect( spinOffsetY, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSvgMarkerSymbolLayerWidget::setOffset );
  connect( this, &QgsSymbolLayerWidget::changed, this, &QgsSvgMarkerSymbolLayerWidget::updateAssistantSymbol );

  connect( mSvgSelectorWidget, &QgsSvgSelectorWidget::svgSelected, this, &QgsSvgMarkerSymbolLayerWidget::setSvgPath );


  //make a temporary symbol for the size assistant preview
  mAssistantPreviewSymbol.reset( new QgsMarkerSymbol() );

  if ( vectorLayer() )
  {
    mWidthDDBtn->setSymbol( mAssistantPreviewSymbol );
    mHeightDDBtn->setSymbol( mAssistantPreviewSymbol );
  }
}

#include <QTime>
#include <QAbstractListModel>
#include <QPixmapCache>
#include <QStyle>




void QgsSvgMarkerSymbolLayerWidget::setGuiForSvg( const QgsSvgMarkerSymbolLayer *layer, bool skipDefaultColors )
{
  if ( !layer )
  {
    return;
  }

  //activate gui for svg parameters only if supported by the svg file
  bool hasFillParam, hasFillOpacityParam, hasStrokeParam, hasStrokeWidthParam, hasStrokeOpacityParam;
  QColor defaultFill, defaultStroke;
  double defaultStrokeWidth, defaultFillOpacity, defaultStrokeOpacity;
  bool hasDefaultFillColor, hasDefaultFillOpacity, hasDefaultStrokeColor, hasDefaultStrokeWidth, hasDefaultStrokeOpacity;
  QgsApplication::svgCache()->containsParams( layer->path(), hasFillParam, hasDefaultFillColor, defaultFill,
      hasFillOpacityParam, hasDefaultFillOpacity, defaultFillOpacity,
      hasStrokeParam, hasDefaultStrokeColor, defaultStroke,
      hasStrokeWidthParam, hasDefaultStrokeWidth, defaultStrokeWidth,
      hasStrokeOpacityParam, hasDefaultStrokeOpacity, defaultStrokeOpacity );
  mChangeColorButton->setEnabled( hasFillParam );
  mChangeColorButton->setAllowOpacity( hasFillOpacityParam );
  mChangeStrokeColorButton->setEnabled( hasStrokeParam );
  mChangeStrokeColorButton->setAllowOpacity( hasStrokeOpacityParam );
  mStrokeWidthSpinBox->setEnabled( hasStrokeWidthParam );

  if ( hasFillParam )
  {
    QColor fill = layer->fillColor();
    double existingOpacity = hasFillOpacityParam ? fill.alphaF() : 1.0;
    if ( hasDefaultFillColor && !skipDefaultColors )
    {
      fill = defaultFill;
    }
    fill.setAlphaF( hasDefaultFillOpacity ? defaultFillOpacity : existingOpacity );
    mChangeColorButton->setColor( fill );
  }
  if ( hasStrokeParam )
  {
    QColor stroke = layer->strokeColor();
    double existingOpacity = hasStrokeOpacityParam ? stroke.alphaF() : 1.0;
    if ( hasDefaultStrokeColor && !skipDefaultColors )
    {
      stroke = defaultStroke;
    }
    stroke.setAlphaF( hasDefaultStrokeOpacity ? defaultStrokeOpacity : existingOpacity );
    mChangeStrokeColorButton->setColor( stroke );
  }

  whileBlocking( mSvgSelectorWidget->sourceLineEdit() )->setSource( layer->path() );

  mStrokeWidthSpinBox->blockSignals( true );
  mStrokeWidthSpinBox->setValue( hasDefaultStrokeWidth ? defaultStrokeWidth : layer->strokeWidth() );
  mStrokeWidthSpinBox->blockSignals( false );

  bool preservedAspectRatio = layer->preservedAspectRatio();
  spinHeight->blockSignals( true );
  if ( preservedAspectRatio )
  {
    spinHeight->setValue( layer->size() * layer->defaultAspectRatio() );
  }
  else
  {
    spinHeight->setValue( layer->size() * layer->fixedAspectRatio() );
  }
  spinHeight->setEnabled( layer->defaultAspectRatio() > 0.0 );
  spinHeight->blockSignals( false );
  whileBlocking( mLockAspectRatio )->setLocked( preservedAspectRatio );
}

void QgsSvgMarkerSymbolLayerWidget::updateAssistantSymbol()
{
  for ( int i = mAssistantPreviewSymbol->symbolLayerCount() - 1 ; i >= 0; --i )
  {
    mAssistantPreviewSymbol->deleteSymbolLayer( i );
  }
  mAssistantPreviewSymbol->appendSymbolLayer( mLayer->clone() );
  QgsProperty ddSize = mLayer->dataDefinedProperties().property( QgsSymbolLayer::PropertySize );
  if ( ddSize )
    mAssistantPreviewSymbol->setDataDefinedSize( ddSize );
}


void QgsSvgMarkerSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( !layer )
  {
    return;
  }

  if ( layer->layerType() != QLatin1String( "SvgMarker" ) )
    return;

  // layer type is correct, we can do the cast
  mLayer = static_cast<QgsSvgMarkerSymbolLayer *>( layer );

  // set values
  mSvgSelectorWidget->setSvgPath( mLayer->path() );

  spinWidth->blockSignals( true );
  spinWidth->setValue( mLayer->size() );
  spinWidth->blockSignals( false );
  spinAngle->blockSignals( true );
  spinAngle->setValue( mLayer->angle() );
  spinAngle->blockSignals( false );

  // without blocking signals the value gets changed because of slot setOffset()
  spinOffsetX->blockSignals( true );
  spinOffsetX->setValue( mLayer->offset().x() );
  spinOffsetX->blockSignals( false );
  spinOffsetY->blockSignals( true );
  spinOffsetY->setValue( mLayer->offset().y() );
  spinOffsetY->blockSignals( false );

  mSizeUnitWidget->blockSignals( true );
  mSizeUnitWidget->setUnit( mLayer->sizeUnit() );
  mSizeUnitWidget->setMapUnitScale( mLayer->sizeMapUnitScale() );
  mSizeUnitWidget->blockSignals( false );
  mStrokeWidthUnitWidget->blockSignals( true );
  mStrokeWidthUnitWidget->setUnit( mLayer->strokeWidthUnit() );
  mStrokeWidthUnitWidget->setMapUnitScale( mLayer->strokeWidthMapUnitScale() );
  mStrokeWidthUnitWidget->blockSignals( false );
  mOffsetUnitWidget->blockSignals( true );
  mOffsetUnitWidget->setUnit( mLayer->offsetUnit() );
  mOffsetUnitWidget->setMapUnitScale( mLayer->offsetMapUnitScale() );
  mOffsetUnitWidget->blockSignals( false );

  //anchor points
  mHorizontalAnchorComboBox->blockSignals( true );
  mVerticalAnchorComboBox->blockSignals( true );
  mHorizontalAnchorComboBox->setCurrentIndex( mLayer->horizontalAnchorPoint() );
  mVerticalAnchorComboBox->setCurrentIndex( mLayer->verticalAnchorPoint() );
  mHorizontalAnchorComboBox->blockSignals( false );
  mVerticalAnchorComboBox->blockSignals( false );

  setGuiForSvg( mLayer, true );

  registerDataDefinedButton( mWidthDDBtn, QgsSymbolLayer::PropertyWidth );
  registerDataDefinedButton( mHeightDDBtn, QgsSymbolLayer::PropertyHeight );
  registerDataDefinedButton( mStrokeWidthDDBtn, QgsSymbolLayer::PropertyStrokeWidth );
  registerDataDefinedButton( mAngleDDBtn, QgsSymbolLayer::PropertyAngle );
  registerDataDefinedButton( mOffsetDDBtn, QgsSymbolLayer::PropertyOffset );
  registerDataDefinedButton( mFillColorDDBtn, QgsSymbolLayer::PropertyFillColor );
  registerDataDefinedButton( mStrokeColorDDBtn, QgsSymbolLayer::PropertyStrokeColor );
  registerDataDefinedButton( mHorizontalAnchorDDBtn, QgsSymbolLayer::PropertyHorizontalAnchor );
  registerDataDefinedButton( mVerticalAnchorDDBtn, QgsSymbolLayer::PropertyVerticalAnchor );

  registerDataDefinedButton( mSvgSelectorWidget->sourceLineEdit()->propertyOverrideToolButton(), QgsSymbolLayer::PropertyName );

  updateAssistantSymbol();
}

QgsSymbolLayer *QgsSvgMarkerSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsSvgMarkerSymbolLayerWidget::setContext( const QgsSymbolWidgetContext &context )
{
  QgsSymbolLayerWidget::setContext( context );
  mSvgSelectorWidget->sourceLineEdit()->setMessageBar( context.messageBar() );
}

void QgsSvgMarkerSymbolLayerWidget::setSvgPath( const QString &name )
{
  mLayer->setPath( name );
  whileBlocking( mSvgSelectorWidget->sourceLineEdit() )->setSource( name );

  setGuiForSvg( mLayer );
  emit changed();
}

void QgsSvgMarkerSymbolLayerWidget::setWidth()
{
  double defaultAspectRatio = mLayer->defaultAspectRatio();
  double fixedAspectRatio = 0.0;
  spinHeight->blockSignals( true );
  if ( defaultAspectRatio <= 0.0 )
  {
    spinHeight->setValue( spinWidth->value() );
  }
  else if ( mLockAspectRatio->locked() )
  {
    spinHeight->setValue( spinWidth->value() * defaultAspectRatio );
  }
  else
  {
    fixedAspectRatio = spinHeight->value() / spinWidth->value();
  }
  spinHeight->blockSignals( false );
  mLayer->setSize( spinWidth->value() );
  mLayer->setFixedAspectRatio( fixedAspectRatio );
  emit changed();
}

void QgsSvgMarkerSymbolLayerWidget::setHeight()
{
  double defaultAspectRatio = mLayer->defaultAspectRatio();
  double fixedAspectRatio = 0.0;
  spinWidth->blockSignals( true );
  if ( defaultAspectRatio <= 0.0 )
  {
    spinWidth->setValue( spinHeight->value() );
  }
  else if ( mLockAspectRatio->locked() )
  {
    spinWidth->setValue( spinHeight->value() / defaultAspectRatio );
  }
  else
  {
    fixedAspectRatio = spinHeight->value() / spinWidth->value();
  }
  spinWidth->blockSignals( false );
  mLayer->setSize( spinWidth->value() );
  mLayer->setFixedAspectRatio( fixedAspectRatio );
  emit changed();
}

void QgsSvgMarkerSymbolLayerWidget::lockAspectRatioChanged( const bool locked )
{
  //spinHeight->setEnabled( !locked );
  double defaultAspectRatio = mLayer->defaultAspectRatio();
  if ( defaultAspectRatio <= 0.0 )
  {
    whileBlocking( mLockAspectRatio )->setLocked( true );
  }
  else if ( locked )
  {
    mLayer->setFixedAspectRatio( 0.0 );
    setWidth();
  }
  else
  {
    mLayer->setFixedAspectRatio( spinHeight->value() / spinWidth->value() );
  }
  //emit changed();
}

void QgsSvgMarkerSymbolLayerWidget::setAngle()
{
  mLayer->setAngle( spinAngle->value() );
  emit changed();
}

void QgsSvgMarkerSymbolLayerWidget::setOffset()
{
  mLayer->setOffset( QPointF( spinOffsetX->value(), spinOffsetY->value() ) );
  emit changed();
}

void QgsSvgMarkerSymbolLayerWidget::svgSourceChanged( const QString &text )
{
  mLayer->setPath( text );
  setGuiForSvg( mLayer );
  emit changed();
}

void QgsSvgMarkerSymbolLayerWidget::mChangeColorButton_colorChanged( const QColor &color )
{
  if ( !mLayer )
  {
    return;
  }

  mLayer->setFillColor( color );
  emit changed();
}

void QgsSvgMarkerSymbolLayerWidget::mChangeStrokeColorButton_colorChanged( const QColor &color )
{
  if ( !mLayer )
  {
    return;
  }

  mLayer->setStrokeColor( color );
  emit changed();
}

void QgsSvgMarkerSymbolLayerWidget::mStrokeWidthSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setStrokeWidth( d );
    emit changed();
  }
}

void QgsSvgMarkerSymbolLayerWidget::mSizeUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setSizeUnit( mSizeUnitWidget->unit() );
    mLayer->setSizeMapUnitScale( mSizeUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsSvgMarkerSymbolLayerWidget::mStrokeWidthUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setStrokeWidthUnit( mStrokeWidthUnitWidget->unit() );
    mLayer->setStrokeWidthMapUnitScale( mStrokeWidthUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsSvgMarkerSymbolLayerWidget::mOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetUnit( mOffsetUnitWidget->unit() );
    mLayer->setOffsetMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsSvgMarkerSymbolLayerWidget::mHorizontalAnchorComboBox_currentIndexChanged( int index )
{
  if ( mLayer )
  {
    mLayer->setHorizontalAnchorPoint( QgsMarkerSymbolLayer::HorizontalAnchorPoint( index ) );
    emit changed();
  }
}

void QgsSvgMarkerSymbolLayerWidget::mVerticalAnchorComboBox_currentIndexChanged( int index )
{
  if ( mLayer )
  {
    mLayer->setVerticalAnchorPoint( QgsMarkerSymbolLayer::VerticalAnchorPoint( index ) );
    emit changed();
  }
}

/////////////

QgsSVGFillSymbolLayerWidget::QgsSVGFillSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent ): QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;
  setupUi( this );

  mSvgSelectorWidget->sourceLineEdit()->setPropertyOverrideToolButtonVisible( true );

  connect( mTextureWidthSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSVGFillSymbolLayerWidget::mTextureWidthSpinBox_valueChanged );
  connect( mSvgSelectorWidget->sourceLineEdit(), &QgsSvgSourceLineEdit::sourceChanged, this, &QgsSVGFillSymbolLayerWidget::svgSourceChanged );
  connect( mRotationSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSVGFillSymbolLayerWidget::mRotationSpinBox_valueChanged );
  connect( mChangeColorButton, &QgsColorButton::colorChanged, this, &QgsSVGFillSymbolLayerWidget::mChangeColorButton_colorChanged );
  connect( mChangeStrokeColorButton, &QgsColorButton::colorChanged, this, &QgsSVGFillSymbolLayerWidget::mChangeStrokeColorButton_colorChanged );
  connect( mStrokeWidthSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsSVGFillSymbolLayerWidget::mStrokeWidthSpinBox_valueChanged );
  connect( mTextureWidthUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSVGFillSymbolLayerWidget::mTextureWidthUnitWidget_changed );
  connect( mSvgStrokeWidthUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsSVGFillSymbolLayerWidget::mSvgStrokeWidthUnitWidget_changed );
  mTextureWidthUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                     << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mSvgStrokeWidthUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                       << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );

  mRotationSpinBox->setClearValue( 0.0 );

  mChangeColorButton->setColorDialogTitle( tr( "Select Fill Color" ) );
  mChangeColorButton->setContext( QStringLiteral( "symbology" ) );
  mChangeStrokeColorButton->setColorDialogTitle( tr( "Select Stroke Color" ) );
  mChangeStrokeColorButton->setContext( QStringLiteral( "symbology" ) );

  mFilColorDDBtn->registerLinkedWidget( mChangeColorButton );
  mStrokeColorDDBtn->registerLinkedWidget( mChangeStrokeColorButton );

  connect( mSvgSelectorWidget, &QgsSvgSelectorWidget::svgSelected, this, &QgsSVGFillSymbolLayerWidget::setFile );

}

void QgsSVGFillSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( !layer )
  {
    return;
  }

  if ( layer->layerType() != QLatin1String( "SVGFill" ) )
  {
    return;
  }

  mLayer = dynamic_cast<QgsSVGFillSymbolLayer *>( layer );
  if ( mLayer )
  {
    double width = mLayer->patternWidth();
    mTextureWidthSpinBox->blockSignals( true );
    mTextureWidthSpinBox->setValue( width );
    mTextureWidthSpinBox->blockSignals( false );
    whileBlocking( mSvgSelectorWidget->sourceLineEdit() )->setSource( mLayer->svgFilePath() );
    mRotationSpinBox->blockSignals( true );
    mRotationSpinBox->setValue( mLayer->angle() );
    mRotationSpinBox->blockSignals( false );
    mTextureWidthUnitWidget->blockSignals( true );
    mTextureWidthUnitWidget->setUnit( mLayer->patternWidthUnit() );
    mTextureWidthUnitWidget->setMapUnitScale( mLayer->patternWidthMapUnitScale() );
    mTextureWidthUnitWidget->blockSignals( false );
    mSvgStrokeWidthUnitWidget->blockSignals( true );
    mSvgStrokeWidthUnitWidget->setUnit( mLayer->svgStrokeWidthUnit() );
    mSvgStrokeWidthUnitWidget->setMapUnitScale( mLayer->svgStrokeWidthMapUnitScale() );
    mSvgStrokeWidthUnitWidget->blockSignals( false );
    mChangeColorButton->blockSignals( true );
    mChangeColorButton->setColor( mLayer->svgFillColor() );
    mChangeColorButton->blockSignals( false );
    mChangeStrokeColorButton->blockSignals( true );
    mChangeStrokeColorButton->setColor( mLayer->svgStrokeColor() );
    mChangeStrokeColorButton->blockSignals( false );
    mStrokeWidthSpinBox->blockSignals( true );
    mStrokeWidthSpinBox->setValue( mLayer->svgStrokeWidth() );
    mStrokeWidthSpinBox->blockSignals( false );
  }
  updateParamGui( false );

  registerDataDefinedButton( mTextureWidthDDBtn, QgsSymbolLayer::PropertyWidth );
  registerDataDefinedButton( mRotationDDBtn, QgsSymbolLayer::PropertyAngle );
  registerDataDefinedButton( mFilColorDDBtn, QgsSymbolLayer::PropertyFillColor );
  registerDataDefinedButton( mStrokeColorDDBtn, QgsSymbolLayer::PropertyStrokeColor );
  registerDataDefinedButton( mStrokeWidthDDBtn, QgsSymbolLayer::PropertyStrokeWidth );

  registerDataDefinedButton( mSvgSelectorWidget->sourceLineEdit()->propertyOverrideToolButton(), QgsSymbolLayer::PropertyFile );
}

QgsSymbolLayer *QgsSVGFillSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsSVGFillSymbolLayerWidget::setContext( const QgsSymbolWidgetContext &context )
{
  QgsSymbolLayerWidget::setContext( context );
  mSvgSelectorWidget->sourceLineEdit()->setMessageBar( context.messageBar() );
}

void QgsSVGFillSymbolLayerWidget::mTextureWidthSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setPatternWidth( d );
    emit changed();
  }
}

void QgsSVGFillSymbolLayerWidget::svgSourceChanged( const QString &text )
{
  if ( !mLayer )
  {
    return;
  }

  mLayer->setSvgFilePath( text );
  updateParamGui();
  emit changed();
}

void QgsSVGFillSymbolLayerWidget::setFile( const QString &name )
{
  mLayer->setSvgFilePath( name );
  whileBlocking( mSvgSelectorWidget->sourceLineEdit() )->setSource( name );

  updateParamGui();
  emit changed();
}


void QgsSVGFillSymbolLayerWidget::mRotationSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setAngle( d );
    emit changed();
  }
}

void QgsSVGFillSymbolLayerWidget::updateParamGui( bool resetValues )
{
  //activate gui for svg parameters only if supported by the svg file
  bool hasFillParam, hasFillOpacityParam, hasStrokeParam, hasStrokeWidthParam, hasStrokeOpacityParam;
  QColor defaultFill, defaultStroke;
  double defaultStrokeWidth, defaultFillOpacity, defaultStrokeOpacity;
  bool hasDefaultFillColor, hasDefaultFillOpacity, hasDefaultStrokeColor, hasDefaultStrokeWidth, hasDefaultStrokeOpacity;
  QgsApplication::svgCache()->containsParams( mSvgSelectorWidget->sourceLineEdit()->source(), hasFillParam, hasDefaultFillColor, defaultFill,
      hasFillOpacityParam, hasDefaultFillOpacity, defaultFillOpacity,
      hasStrokeParam, hasDefaultStrokeColor, defaultStroke,
      hasStrokeWidthParam, hasDefaultStrokeWidth, defaultStrokeWidth,
      hasStrokeOpacityParam, hasDefaultStrokeOpacity, defaultStrokeOpacity );
  if ( resetValues )
  {
    QColor fill = mChangeColorButton->color();
    double newOpacity = hasFillOpacityParam ? fill.alphaF() : 1.0;
    if ( hasDefaultFillColor )
    {
      fill = defaultFill;
    }
    fill.setAlphaF( hasDefaultFillOpacity ? defaultFillOpacity : newOpacity );
    mChangeColorButton->setColor( fill );
  }
  mChangeColorButton->setEnabled( hasFillParam );
  mChangeColorButton->setAllowOpacity( hasFillOpacityParam );
  if ( resetValues )
  {
    QColor stroke = mChangeStrokeColorButton->color();
    double newOpacity = hasStrokeOpacityParam ? stroke.alphaF() : 1.0;
    if ( hasDefaultStrokeColor )
    {
      stroke = defaultStroke;
    }
    stroke.setAlphaF( hasDefaultStrokeOpacity ? defaultStrokeOpacity : newOpacity );
    mChangeStrokeColorButton->setColor( stroke );
  }
  mChangeStrokeColorButton->setEnabled( hasStrokeParam );
  mChangeStrokeColorButton->setAllowOpacity( hasStrokeOpacityParam );
  if ( hasDefaultStrokeWidth && resetValues )
  {
    mStrokeWidthSpinBox->setValue( defaultStrokeWidth );
  }
  mStrokeWidthSpinBox->setEnabled( hasStrokeWidthParam );
}

void QgsSVGFillSymbolLayerWidget::mChangeColorButton_colorChanged( const QColor &color )
{
  if ( !mLayer )
  {
    return;
  }

  mLayer->setSvgFillColor( color );
  emit changed();
}

void QgsSVGFillSymbolLayerWidget::mChangeStrokeColorButton_colorChanged( const QColor &color )
{
  if ( !mLayer )
  {
    return;
  }

  mLayer->setSvgStrokeColor( color );
  emit changed();
}

void QgsSVGFillSymbolLayerWidget::mStrokeWidthSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setSvgStrokeWidth( d );
    emit changed();
  }
}

void QgsSVGFillSymbolLayerWidget::mTextureWidthUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setPatternWidthUnit( mTextureWidthUnitWidget->unit() );
    mLayer->setPatternWidthMapUnitScale( mTextureWidthUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsSVGFillSymbolLayerWidget::mSvgStrokeWidthUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setSvgStrokeWidthUnit( mSvgStrokeWidthUnitWidget->unit() );
    mLayer->setSvgStrokeWidthMapUnitScale( mSvgStrokeWidthUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

/////////////

QgsLinePatternFillSymbolLayerWidget::QgsLinePatternFillSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent ):
  QgsSymbolLayerWidget( parent, vl )
{
  setupUi( this );
  connect( mAngleSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsLinePatternFillSymbolLayerWidget::mAngleSpinBox_valueChanged );
  connect( mDistanceSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsLinePatternFillSymbolLayerWidget::mDistanceSpinBox_valueChanged );
  connect( mOffsetSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsLinePatternFillSymbolLayerWidget::mOffsetSpinBox_valueChanged );
  connect( mDistanceUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsLinePatternFillSymbolLayerWidget::mDistanceUnitWidget_changed );
  connect( mOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsLinePatternFillSymbolLayerWidget::mOffsetUnitWidget_changed );
  mDistanceUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                 << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mOffsetSpinBox->setClearValue( 0 );
  mAngleSpinBox->setClearValue( 0 );
}

void QgsLinePatternFillSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( layer->layerType() != QLatin1String( "LinePatternFill" ) )
  {
    return;
  }

  QgsLinePatternFillSymbolLayer *patternLayer = static_cast<QgsLinePatternFillSymbolLayer *>( layer );
  if ( patternLayer )
  {
    mLayer = patternLayer;
    mAngleSpinBox->blockSignals( true );
    mAngleSpinBox->setValue( mLayer->lineAngle() );
    mAngleSpinBox->blockSignals( false );
    mDistanceSpinBox->blockSignals( true );
    mDistanceSpinBox->setValue( mLayer->distance() );
    mDistanceSpinBox->blockSignals( false );
    mOffsetSpinBox->blockSignals( true );
    mOffsetSpinBox->setValue( mLayer->offset() );
    mOffsetSpinBox->blockSignals( false );

    //units
    mDistanceUnitWidget->blockSignals( true );
    mDistanceUnitWidget->setUnit( mLayer->distanceUnit() );
    mDistanceUnitWidget->setMapUnitScale( mLayer->distanceMapUnitScale() );
    mDistanceUnitWidget->blockSignals( false );
    mOffsetUnitWidget->blockSignals( true );
    mOffsetUnitWidget->setUnit( mLayer->offsetUnit() );
    mOffsetUnitWidget->setMapUnitScale( mLayer->offsetMapUnitScale() );
    mOffsetUnitWidget->blockSignals( false );
  }

  registerDataDefinedButton( mAngleDDBtn, QgsSymbolLayer::PropertyLineAngle );
  registerDataDefinedButton( mDistanceDDBtn, QgsSymbolLayer::PropertyLineDistance );
}

QgsSymbolLayer *QgsLinePatternFillSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsLinePatternFillSymbolLayerWidget::mAngleSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setLineAngle( d );
    emit changed();
  }
}

void QgsLinePatternFillSymbolLayerWidget::mDistanceSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setDistance( d );
    emit changed();
  }
}

void QgsLinePatternFillSymbolLayerWidget::mOffsetSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setOffset( d );
    emit changed();
  }
}

void QgsLinePatternFillSymbolLayerWidget::mDistanceUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setDistanceUnit( mDistanceUnitWidget->unit() );
    mLayer->setDistanceMapUnitScale( mDistanceUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsLinePatternFillSymbolLayerWidget::mOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetUnit( mOffsetUnitWidget->unit() );
    mLayer->setOffsetMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

/////////////

QgsPointPatternFillSymbolLayerWidget::QgsPointPatternFillSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent ):
  QgsSymbolLayerWidget( parent, vl )
{
  setupUi( this );
  connect( mHorizontalDistanceSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsPointPatternFillSymbolLayerWidget::mHorizontalDistanceSpinBox_valueChanged );
  connect( mVerticalDistanceSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsPointPatternFillSymbolLayerWidget::mVerticalDistanceSpinBox_valueChanged );
  connect( mHorizontalDisplacementSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsPointPatternFillSymbolLayerWidget::mHorizontalDisplacementSpinBox_valueChanged );
  connect( mVerticalDisplacementSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsPointPatternFillSymbolLayerWidget::mVerticalDisplacementSpinBox_valueChanged );
  connect( mHorizontalOffsetSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsPointPatternFillSymbolLayerWidget::mHorizontalOffsetSpinBox_valueChanged );
  connect( mVerticalOffsetSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsPointPatternFillSymbolLayerWidget::mVerticalOffsetSpinBox_valueChanged );
  connect( mHorizontalDistanceUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsPointPatternFillSymbolLayerWidget::mHorizontalDistanceUnitWidget_changed );
  connect( mVerticalDistanceUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsPointPatternFillSymbolLayerWidget::mVerticalDistanceUnitWidget_changed );
  connect( mHorizontalDisplacementUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsPointPatternFillSymbolLayerWidget::mHorizontalDisplacementUnitWidget_changed );
  connect( mVerticalDisplacementUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsPointPatternFillSymbolLayerWidget::mVerticalDisplacementUnitWidget_changed );
  connect( mHorizontalOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsPointPatternFillSymbolLayerWidget::mHorizontalOffsetUnitWidget_changed );
  connect( mVerticalOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsPointPatternFillSymbolLayerWidget::mVerticalOffsetUnitWidget_changed );
  mHorizontalDistanceUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
      << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mVerticalDistanceUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                         << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mHorizontalDisplacementUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
      << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mVerticalDisplacementUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
      << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mHorizontalOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                         << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mVerticalOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                       << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
}


void QgsPointPatternFillSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( !layer || layer->layerType() != QLatin1String( "PointPatternFill" ) )
  {
    return;
  }

  mLayer = static_cast<QgsPointPatternFillSymbolLayer *>( layer );
  whileBlocking( mHorizontalDistanceSpinBox )->setValue( mLayer->distanceX() );
  whileBlocking( mVerticalDistanceSpinBox )->setValue( mLayer->distanceY() );
  whileBlocking( mHorizontalDisplacementSpinBox )->setValue( mLayer->displacementX() );
  whileBlocking( mVerticalDisplacementSpinBox )->setValue( mLayer->displacementY() );
  whileBlocking( mHorizontalOffsetSpinBox )->setValue( mLayer->offsetX() );
  whileBlocking( mVerticalOffsetSpinBox )->setValue( mLayer->offsetY() );

  mHorizontalDistanceUnitWidget->blockSignals( true );
  mHorizontalDistanceUnitWidget->setUnit( mLayer->distanceXUnit() );
  mHorizontalDistanceUnitWidget->setMapUnitScale( mLayer->distanceXMapUnitScale() );
  mHorizontalDistanceUnitWidget->blockSignals( false );
  mVerticalDistanceUnitWidget->blockSignals( true );
  mVerticalDistanceUnitWidget->setUnit( mLayer->distanceYUnit() );
  mVerticalDistanceUnitWidget->setMapUnitScale( mLayer->distanceYMapUnitScale() );
  mVerticalDistanceUnitWidget->blockSignals( false );
  mHorizontalDisplacementUnitWidget->blockSignals( true );
  mHorizontalDisplacementUnitWidget->setUnit( mLayer->displacementXUnit() );
  mHorizontalDisplacementUnitWidget->setMapUnitScale( mLayer->displacementXMapUnitScale() );
  mHorizontalDisplacementUnitWidget->blockSignals( false );
  mVerticalDisplacementUnitWidget->blockSignals( true );
  mVerticalDisplacementUnitWidget->setUnit( mLayer->displacementYUnit() );
  mVerticalDisplacementUnitWidget->setMapUnitScale( mLayer->displacementYMapUnitScale() );
  mVerticalDisplacementUnitWidget->blockSignals( false );
  mHorizontalOffsetUnitWidget->blockSignals( true );
  mHorizontalOffsetUnitWidget->setUnit( mLayer->offsetXUnit() );
  mHorizontalOffsetUnitWidget->setMapUnitScale( mLayer->offsetXMapUnitScale() );
  mHorizontalOffsetUnitWidget->blockSignals( false );
  mVerticalOffsetUnitWidget->blockSignals( true );
  mVerticalOffsetUnitWidget->setUnit( mLayer->offsetYUnit() );
  mVerticalOffsetUnitWidget->setMapUnitScale( mLayer->offsetYMapUnitScale() );
  mVerticalOffsetUnitWidget->blockSignals( false );

  registerDataDefinedButton( mHorizontalDistanceDDBtn, QgsSymbolLayer::PropertyDistanceX );
  registerDataDefinedButton( mVerticalDistanceDDBtn, QgsSymbolLayer::PropertyDistanceY );
  registerDataDefinedButton( mHorizontalDisplacementDDBtn, QgsSymbolLayer::PropertyDisplacementX );
  registerDataDefinedButton( mVerticalDisplacementDDBtn, QgsSymbolLayer::PropertyDisplacementY );
  registerDataDefinedButton( mHorizontalOffsetDDBtn, QgsSymbolLayer::PropertyOffsetX );
  registerDataDefinedButton( mVerticalOffsetDDBtn, QgsSymbolLayer::PropertyOffsetY );
}

QgsSymbolLayer *QgsPointPatternFillSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsPointPatternFillSymbolLayerWidget::mHorizontalDistanceSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setDistanceX( d );
    emit changed();
  }
}

void QgsPointPatternFillSymbolLayerWidget::mVerticalDistanceSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setDistanceY( d );
    emit changed();
  }
}

void QgsPointPatternFillSymbolLayerWidget::mHorizontalDisplacementSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setDisplacementX( d );
    emit changed();
  }
}

void QgsPointPatternFillSymbolLayerWidget::mVerticalDisplacementSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setDisplacementY( d );
    emit changed();
  }
}

void QgsPointPatternFillSymbolLayerWidget::mHorizontalOffsetSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setOffsetX( d );
    emit changed();
  }
}

void QgsPointPatternFillSymbolLayerWidget::mVerticalOffsetSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setOffsetY( d );
    emit changed();
  }
}

void QgsPointPatternFillSymbolLayerWidget::mHorizontalDistanceUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setDistanceXUnit( mHorizontalDistanceUnitWidget->unit() );
    mLayer->setDistanceXMapUnitScale( mHorizontalDistanceUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsPointPatternFillSymbolLayerWidget::mVerticalDistanceUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setDistanceYUnit( mVerticalDistanceUnitWidget->unit() );
    mLayer->setDistanceYMapUnitScale( mVerticalDistanceUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsPointPatternFillSymbolLayerWidget::mHorizontalDisplacementUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setDisplacementXUnit( mHorizontalDisplacementUnitWidget->unit() );
    mLayer->setDisplacementXMapUnitScale( mHorizontalDisplacementUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsPointPatternFillSymbolLayerWidget::mVerticalDisplacementUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setDisplacementYUnit( mVerticalDisplacementUnitWidget->unit() );
    mLayer->setDisplacementYMapUnitScale( mVerticalDisplacementUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsPointPatternFillSymbolLayerWidget::mHorizontalOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetXUnit( mHorizontalOffsetUnitWidget->unit() );
    mLayer->setOffsetXMapUnitScale( mHorizontalOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsPointPatternFillSymbolLayerWidget::mVerticalOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetYUnit( mVerticalOffsetUnitWidget->unit() );
    mLayer->setOffsetYMapUnitScale( mVerticalOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

/////////////

QgsFontMarkerSymbolLayerWidget::QgsFontMarkerSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;

  setupUi( this );
  connect( mSizeUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsFontMarkerSymbolLayerWidget::mSizeUnitWidget_changed );
  connect( mOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsFontMarkerSymbolLayerWidget::mOffsetUnitWidget_changed );
  connect( mStrokeWidthUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsFontMarkerSymbolLayerWidget::mStrokeWidthUnitWidget_changed );
  connect( mStrokeWidthSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsFontMarkerSymbolLayerWidget::mStrokeWidthSpinBox_valueChanged );
  connect( mHorizontalAnchorComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsFontMarkerSymbolLayerWidget::mHorizontalAnchorComboBox_currentIndexChanged );
  connect( mVerticalAnchorComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsFontMarkerSymbolLayerWidget::mVerticalAnchorComboBox_currentIndexChanged );
  mSizeUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                             << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mStrokeWidthUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                    << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );
  mOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );

  widgetChar = new CharacterWidget();
  scrollArea->setWidget( widgetChar );
  scrollArea->setVerticalOnly( true );

  btnColor->setAllowOpacity( true );
  btnColor->setColorDialogTitle( tr( "Select Symbol Fill Color" ) );
  btnColor->setContext( QStringLiteral( "symbology" ) );
  btnStrokeColor->setAllowOpacity( true );
  btnStrokeColor->setColorDialogTitle( tr( "Select Symbol Stroke Color" ) );
  btnStrokeColor->setContext( QStringLiteral( "symbology" ) );

  mColorDDBtn->registerLinkedWidget( btnColor );
  mStrokeColorDDBtn->registerLinkedWidget( btnStrokeColor );

  spinOffsetX->setClearValue( 0.0 );
  spinOffsetY->setClearValue( 0.0 );
  spinAngle->setClearValue( 0.0 );

  //make a temporary symbol for the size assistant preview
  mAssistantPreviewSymbol.reset( new QgsMarkerSymbol() );

  if ( vectorLayer() )
    mSizeDDBtn->setSymbol( mAssistantPreviewSymbol );

  connect( cboFont, &QFontComboBox::currentFontChanged, this, &QgsFontMarkerSymbolLayerWidget::setFontFamily );
  connect( mFontStyleComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsFontMarkerSymbolLayerWidget::mFontStyleComboBox_currentIndexChanged );
  connect( spinSize, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsFontMarkerSymbolLayerWidget::setSize );
  connect( cboJoinStyle, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsFontMarkerSymbolLayerWidget::penJoinStyleChanged );
  connect( btnColor, &QgsColorButton::colorChanged, this, &QgsFontMarkerSymbolLayerWidget::setColor );
  connect( btnStrokeColor, &QgsColorButton::colorChanged, this, &QgsFontMarkerSymbolLayerWidget::setColorStroke );
  connect( cboJoinStyle, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsFontMarkerSymbolLayerWidget::penJoinStyleChanged );
  connect( spinAngle, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsFontMarkerSymbolLayerWidget::setAngle );
  connect( spinOffsetX, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsFontMarkerSymbolLayerWidget::setOffset );
  connect( spinOffsetY, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsFontMarkerSymbolLayerWidget::setOffset );
  connect( widgetChar, &CharacterWidget::characterSelected, this, &QgsFontMarkerSymbolLayerWidget::setCharacter );
  connect( mCharLineEdit, &QLineEdit::textChanged, this, &QgsFontMarkerSymbolLayerWidget::setCharacterFromText );

  connect( this, &QgsSymbolLayerWidget::changed, this, &QgsFontMarkerSymbolLayerWidget::updateAssistantSymbol );
}

void QgsFontMarkerSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( layer->layerType() != QLatin1String( "FontMarker" ) )
    return;

  // layer type is correct, we can do the cast
  mLayer = static_cast<QgsFontMarkerSymbolLayer *>( layer );

  mRefFont.setFamily( mLayer->fontFamily() );
  mRefFont.setStyleName( QgsFontUtils::translateNamedStyle( mLayer->fontStyle() ) );

  mFontStyleComboBox->blockSignals( true );
  populateFontStyleComboBox();
  mFontStyleComboBox->blockSignals( false );

  // set values
  whileBlocking( cboFont )->setCurrentFont( mRefFont );
  whileBlocking( spinSize )->setValue( mLayer->size() );
  whileBlocking( btnColor )->setColor( mLayer->color() );
  whileBlocking( btnStrokeColor )->setColor( mLayer->strokeColor() );
  whileBlocking( mStrokeWidthSpinBox )->setValue( mLayer->strokeWidth() );
  whileBlocking( spinAngle )->setValue( mLayer->angle() );

  widgetChar->blockSignals( true );
  widgetChar->setFont( mRefFont );
  if ( mLayer->character().length() == 1 )
  {
    widgetChar->setCharacter( mLayer->character().at( 0 ) );
  }
  widgetChar->blockSignals( false );
  whileBlocking( mCharLineEdit )->setText( mLayer->character() );
  mCharPreview->setFont( mRefFont );

  //block
  whileBlocking( spinOffsetX )->setValue( mLayer->offset().x() );
  whileBlocking( spinOffsetY )->setValue( mLayer->offset().y() );

  mSizeUnitWidget->blockSignals( true );
  mSizeUnitWidget->setUnit( mLayer->sizeUnit() );
  mSizeUnitWidget->setMapUnitScale( mLayer->sizeMapUnitScale() );
  mSizeUnitWidget->blockSignals( false );

  mStrokeWidthUnitWidget->blockSignals( true );
  mStrokeWidthUnitWidget->setUnit( mLayer->strokeWidthUnit() );
  mStrokeWidthUnitWidget->setMapUnitScale( mLayer->strokeWidthMapUnitScale() );
  mStrokeWidthUnitWidget->blockSignals( false );

  mOffsetUnitWidget->blockSignals( true );
  mOffsetUnitWidget->setUnit( mLayer->offsetUnit() );
  mOffsetUnitWidget->setMapUnitScale( mLayer->offsetMapUnitScale() );
  mOffsetUnitWidget->blockSignals( false );

  whileBlocking( cboJoinStyle )->setPenJoinStyle( mLayer->penJoinStyle() );

  //anchor points
  whileBlocking( mHorizontalAnchorComboBox )->setCurrentIndex( mLayer->horizontalAnchorPoint() );
  whileBlocking( mVerticalAnchorComboBox )->setCurrentIndex( mLayer->verticalAnchorPoint() );

  registerDataDefinedButton( mFontFamilyDDBtn, QgsSymbolLayer::PropertyFontFamily );
  registerDataDefinedButton( mFontStyleDDBtn, QgsSymbolLayer::PropertyFontStyle );
  registerDataDefinedButton( mSizeDDBtn, QgsSymbolLayer::PropertySize );
  registerDataDefinedButton( mRotationDDBtn, QgsSymbolLayer::PropertyAngle );
  registerDataDefinedButton( mColorDDBtn, QgsSymbolLayer::PropertyFillColor );
  registerDataDefinedButton( mStrokeColorDDBtn, QgsSymbolLayer::PropertyStrokeColor );
  registerDataDefinedButton( mStrokeWidthDDBtn, QgsSymbolLayer::PropertyStrokeWidth );
  registerDataDefinedButton( mJoinStyleDDBtn, QgsSymbolLayer::PropertyJoinStyle );
  registerDataDefinedButton( mOffsetDDBtn, QgsSymbolLayer::PropertyOffset );
  registerDataDefinedButton( mHorizontalAnchorDDBtn, QgsSymbolLayer::PropertyHorizontalAnchor );
  registerDataDefinedButton( mVerticalAnchorDDBtn, QgsSymbolLayer::PropertyVerticalAnchor );
  registerDataDefinedButton( mCharDDBtn, QgsSymbolLayer::PropertyCharacter );

  updateAssistantSymbol();
}

QgsSymbolLayer *QgsFontMarkerSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsFontMarkerSymbolLayerWidget::setFontFamily( const QFont &font )
{
  if ( mLayer )
  {
    mLayer->setFontFamily( font.family() );
    mRefFont.setFamily( font.family() );
    widgetChar->setFont( mRefFont );
    mCharPreview->setFont( mRefFont );
    populateFontStyleComboBox();
    emit changed();
  }
}

void QgsFontMarkerSymbolLayerWidget::setFontStyle( const QString &style )
{
  if ( mLayer )
  {
    QgsFontUtils::updateFontViaStyle( mRefFont, style );
    mLayer->setFontStyle( QgsFontUtils::untranslateNamedStyle( style ) );
    widgetChar->setFont( mRefFont );
    mCharPreview->setFont( mRefFont );
    emit changed();
  }
}

void QgsFontMarkerSymbolLayerWidget::setColor( const QColor &color )
{
  mLayer->setColor( color );
  emit changed();
}

void QgsFontMarkerSymbolLayerWidget::setColorStroke( const QColor &color )
{
  mLayer->setStrokeColor( color );
  emit changed();
}

void QgsFontMarkerSymbolLayerWidget::setSize( double size )
{
  mLayer->setSize( size );
  //widgetChar->updateSize(size);
  emit changed();
}

void QgsFontMarkerSymbolLayerWidget::setAngle( double angle )
{
  mLayer->setAngle( angle );
  emit changed();
}

void QgsFontMarkerSymbolLayerWidget::setCharacterFromText( const QString &text )
{
  mCharPreview->setText( text );

  if ( text.isEmpty() )
    return;

  // take the last character of a string for a better experience when users cycle through several characters on their keyboard
  QString character = text;
  if ( text.contains( QRegularExpression( QStringLiteral( "^0x[0-9a-fA-F]{1,4}$" ) ) ) )
  {
    bool ok = false;
    unsigned int value = text.toUInt( &ok, 0 );
    if ( ok )
    {
      character = QChar( value );
      mCharPreview->setText( character );
    }
  }

  if ( character != mLayer->character() )
  {
    mLayer->setCharacter( character );
    if ( mLayer->character().length() == 1 )
    {
      whileBlocking( widgetChar )->setCharacter( mLayer->character().at( 0 ) );
    }
    else
    {
      widgetChar->clearCharacter();
    }
    emit changed();
  }
}

void QgsFontMarkerSymbolLayerWidget::setCharacter( QChar chr )
{
  if ( mLayer->character().length() > 1 || QGuiApplication::keyboardModifiers() & Qt::ControlModifier )
  {
    mCharLineEdit->insert( chr );
    return;
  }

  mLayer->setCharacter( chr );
  whileBlocking( mCharLineEdit )->setText( chr );
  mCharPreview->setText( chr );
  emit changed();
}

void QgsFontMarkerSymbolLayerWidget::setOffset()
{
  mLayer->setOffset( QPointF( spinOffsetX->value(), spinOffsetY->value() ) );
  emit changed();
}

void QgsFontMarkerSymbolLayerWidget::penJoinStyleChanged()
{
  mLayer->setPenJoinStyle( cboJoinStyle->penJoinStyle() );
  emit changed();
}

void QgsFontMarkerSymbolLayerWidget::mSizeUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setSizeUnit( mSizeUnitWidget->unit() );
    mLayer->setSizeMapUnitScale( mSizeUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsFontMarkerSymbolLayerWidget::mOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetUnit( mOffsetUnitWidget->unit() );
    mLayer->setOffsetMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsFontMarkerSymbolLayerWidget::mStrokeWidthUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setStrokeWidthUnit( mSizeUnitWidget->unit() );
    mLayer->setStrokeWidthMapUnitScale( mSizeUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsFontMarkerSymbolLayerWidget::populateFontStyleComboBox()
{
  mFontStyleComboBox->clear();
  QStringList styles = mFontDB.styles( mRefFont.family() );
  const auto constStyles = styles;
  for ( const QString &style : constStyles )
  {
    mFontStyleComboBox->addItem( style );
  }

  QString targetStyle = mFontDB.styleString( mRefFont );
  if ( !styles.contains( targetStyle ) )
  {
    QFont f = QFont( mRefFont.family() );
    targetStyle = QFontInfo( f ).styleName();
    mRefFont.setStyleName( targetStyle );
  }
  int curIndx = 0;
  int stylIndx = mFontStyleComboBox->findText( targetStyle );
  if ( stylIndx > -1 )
  {
    curIndx = stylIndx;
  }

  mFontStyleComboBox->setCurrentIndex( curIndx );
}

void QgsFontMarkerSymbolLayerWidget::mFontStyleComboBox_currentIndexChanged( int index )
{
  Q_UNUSED( index );
  setFontStyle( mFontStyleComboBox->currentText() );
}

void QgsFontMarkerSymbolLayerWidget::mHorizontalAnchorComboBox_currentIndexChanged( int index )
{
  if ( mLayer )
  {
    mLayer->setHorizontalAnchorPoint( QgsMarkerSymbolLayer::HorizontalAnchorPoint( index ) );
    emit changed();
  }
}

void QgsFontMarkerSymbolLayerWidget::mVerticalAnchorComboBox_currentIndexChanged( int index )
{
  if ( mLayer )
  {
    mLayer->setVerticalAnchorPoint( QgsMarkerSymbolLayer::VerticalAnchorPoint( index ) );
    emit changed();
  }
}

void QgsFontMarkerSymbolLayerWidget::mStrokeWidthSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setStrokeWidth( d );
    emit changed();
  }
}

void QgsFontMarkerSymbolLayerWidget::updateAssistantSymbol()
{
  for ( int i = mAssistantPreviewSymbol->symbolLayerCount() - 1 ; i >= 0; --i )
  {
    mAssistantPreviewSymbol->deleteSymbolLayer( i );
  }
  mAssistantPreviewSymbol->appendSymbolLayer( mLayer->clone() );
  QgsProperty ddSize = mLayer->dataDefinedProperties().property( QgsSymbolLayer::PropertySize );
  if ( ddSize )
    mAssistantPreviewSymbol->setDataDefinedSize( ddSize );
}

///////////////


QgsCentroidFillSymbolLayerWidget::QgsCentroidFillSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;

  setupUi( this );
  connect( mDrawInsideCheckBox, &QCheckBox::stateChanged, this, &QgsCentroidFillSymbolLayerWidget::mDrawInsideCheckBox_stateChanged );
  connect( mDrawAllPartsCheckBox, &QCheckBox::stateChanged, this, &QgsCentroidFillSymbolLayerWidget::mDrawAllPartsCheckBox_stateChanged );
  connect( mClipPointsCheckBox, &QCheckBox::stateChanged, this, &QgsCentroidFillSymbolLayerWidget::mClipPointsCheckBox_stateChanged );
  connect( mClipOnCurrentPartOnlyCheckBox, &QCheckBox::stateChanged, this, &QgsCentroidFillSymbolLayerWidget::mClipOnCurrentPartOnlyCheckBox_stateChanged );
}

void QgsCentroidFillSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( layer->layerType() != QLatin1String( "CentroidFill" ) )
    return;

  // layer type is correct, we can do the cast
  mLayer = static_cast<QgsCentroidFillSymbolLayer *>( layer );

  // set values
  whileBlocking( mDrawInsideCheckBox )->setChecked( mLayer->pointOnSurface() );
  whileBlocking( mDrawAllPartsCheckBox )->setChecked( mLayer->pointOnAllParts() );
  whileBlocking( mClipPointsCheckBox )->setChecked( mLayer->clipPoints() );
  whileBlocking( mClipOnCurrentPartOnlyCheckBox )->setChecked( mLayer->clipOnCurrentPartOnly() );
}

QgsSymbolLayer *QgsCentroidFillSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsCentroidFillSymbolLayerWidget::mDrawInsideCheckBox_stateChanged( int state )
{
  mLayer->setPointOnSurface( state == Qt::Checked );
  emit changed();
}

void QgsCentroidFillSymbolLayerWidget::mDrawAllPartsCheckBox_stateChanged( int state )
{
  mLayer->setPointOnAllParts( state == Qt::Checked );
  emit changed();
}

void QgsCentroidFillSymbolLayerWidget::mClipPointsCheckBox_stateChanged( int state )
{
  mLayer->setClipPoints( state == Qt::Checked );
  emit changed();
}

void QgsCentroidFillSymbolLayerWidget::mClipOnCurrentPartOnlyCheckBox_stateChanged( int state )
{
  mLayer->setClipOnCurrentPartOnly( state == Qt::Checked );
  emit changed();
}

///////////

QgsRasterMarkerSymbolLayerWidget::QgsRasterMarkerSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;

  setupUi( this );

  mImageSourceLineEdit->setLastPathSettingsKey( QStringLiteral( "/UI/lastRasterMarkerImageDir" ) );

  connect( mImageSourceLineEdit, &QgsImageSourceLineEdit::sourceChanged, this, &QgsRasterMarkerSymbolLayerWidget::imageSourceChanged );
  connect( mOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsRasterMarkerSymbolLayerWidget::mOffsetUnitWidget_changed );
  connect( mRotationSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsRasterMarkerSymbolLayerWidget::setAngle );
  connect( mSizeUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsRasterMarkerSymbolLayerWidget::mSizeUnitWidget_changed );
  connect( mWidthSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsRasterMarkerSymbolLayerWidget::setWidth );
  connect( mHeightSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsRasterMarkerSymbolLayerWidget::setHeight );
  connect( mLockAspectRatio, static_cast < void ( QgsRatioLockButton::* )( bool ) > ( &QgsRatioLockButton::lockChanged ), this, &QgsRasterMarkerSymbolLayerWidget::setLockAspectRatio );

  mSizeUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderPixels << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits
                             << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches << QgsUnitTypes::RenderPercentage );
  mOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );

  mSpinOffsetX->setClearValue( 0.0 );
  mSpinOffsetY->setClearValue( 0.0 );
  mRotationSpinBox->setClearValue( 0.0 );

  connect( mSpinOffsetX, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsRasterMarkerSymbolLayerWidget::setOffset );
  connect( mSpinOffsetY, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsRasterMarkerSymbolLayerWidget::setOffset );
  connect( mOpacityWidget, &QgsOpacityWidget::opacityChanged, this, &QgsRasterMarkerSymbolLayerWidget::setOpacity );

  connect( mHorizontalAnchorComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsRasterMarkerSymbolLayerWidget::mHorizontalAnchorComboBox_currentIndexChanged );
  connect( mVerticalAnchorComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsRasterMarkerSymbolLayerWidget::mVerticalAnchorComboBox_currentIndexChanged );
}

void QgsRasterMarkerSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( !layer )
  {
    return;
  }

  if ( layer->layerType() != QLatin1String( "RasterMarker" ) )
    return;

  // layer type is correct, we can do the cast
  mLayer = static_cast<QgsRasterMarkerSymbolLayer *>( layer );

  // set values
  whileBlocking( mImageSourceLineEdit )->setSource( mLayer->path() );

  whileBlocking( mWidthSpinBox )->setValue( mLayer->size() );
  bool preservedAspectRatio = mLayer->preservedAspectRatio();
  mHeightSpinBox->blockSignals( true );
  if ( preservedAspectRatio )
  {
    mHeightSpinBox->setValue( mLayer->size() );
  }
  else
  {
    mHeightSpinBox->setValue( mLayer->size() * mLayer->fixedAspectRatio() );
  }
  mHeightSpinBox->setEnabled( mLayer->defaultAspectRatio() > 0.0 );
  mHeightSpinBox->blockSignals( false );
  whileBlocking( mLockAspectRatio )->setLocked( preservedAspectRatio );

  whileBlocking( mRotationSpinBox )->setValue( mLayer->angle() );
  whileBlocking( mOpacityWidget )->setOpacity( mLayer->opacity() );

  whileBlocking( mSpinOffsetX )->setValue( mLayer->offset().x() );
  whileBlocking( mSpinOffsetY )->setValue( mLayer->offset().y() );

  mSizeUnitWidget->blockSignals( true );
  mSizeUnitWidget->setUnit( mLayer->sizeUnit() );
  mSizeUnitWidget->setMapUnitScale( mLayer->sizeMapUnitScale() );
  mSizeUnitWidget->blockSignals( false );
  mOffsetUnitWidget->blockSignals( true );
  mOffsetUnitWidget->setUnit( mLayer->offsetUnit() );
  mOffsetUnitWidget->setMapUnitScale( mLayer->offsetMapUnitScale() );
  mOffsetUnitWidget->blockSignals( false );

  //anchor points
  whileBlocking( mHorizontalAnchorComboBox )->setCurrentIndex( mLayer->horizontalAnchorPoint() );
  whileBlocking( mVerticalAnchorComboBox )->setCurrentIndex( mLayer->verticalAnchorPoint() );

  registerDataDefinedButton( mWidthDDBtn, QgsSymbolLayer::PropertyWidth );
  registerDataDefinedButton( mHeightDDBtn, QgsSymbolLayer::PropertyHeight );
  registerDataDefinedButton( mRotationDDBtn, QgsSymbolLayer::PropertyAngle );
  registerDataDefinedButton( mOpacityDDBtn, QgsSymbolLayer::PropertyOpacity );
  registerDataDefinedButton( mOffsetDDBtn, QgsSymbolLayer::PropertyOffset );
  registerDataDefinedButton( mFilenameDDBtn, QgsSymbolLayer::PropertyName );
  registerDataDefinedButton( mHorizontalAnchorDDBtn, QgsSymbolLayer::PropertyHorizontalAnchor );
  registerDataDefinedButton( mVerticalAnchorDDBtn, QgsSymbolLayer::PropertyVerticalAnchor );

  updatePreviewImage();
}

QgsSymbolLayer *QgsRasterMarkerSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsRasterMarkerSymbolLayerWidget::setContext( const QgsSymbolWidgetContext &context )
{
  QgsSymbolLayerWidget::setContext( context );
  mImageSourceLineEdit->setMessageBar( context.messageBar() );
}

void QgsRasterMarkerSymbolLayerWidget::imageSourceChanged( const QString &text )
{
  mLayer->setPath( text );
  updatePreviewImage();
  emit changed();
}

void QgsRasterMarkerSymbolLayerWidget::updatePreviewImage()
{
  bool fitsInCache = false;
  QImage image = QgsApplication::imageCache()->pathAsImage( mLayer->path(), QSize( 150, 150 ), true, 1.0, fitsInCache );
  if ( image.isNull() )
  {
    mLabelImagePreview->setPixmap( QPixmap() );
    return;
  }

  QImage previewImage( 150, 150, QImage::Format_ARGB32 );
  previewImage.fill( Qt::transparent );
  QRect imageRect( ( 150 - image.width() ) / 2.0, ( 150 - image.height() ) / 2.0, image.width(), image.height() );
  QPainter p;
  p.begin( &previewImage );
  //draw a checkerboard background
  uchar pixDataRGB[] = { 150, 150, 150, 150,
                         100, 100, 100, 150,
                         100, 100, 100, 150,
                         150, 150, 150, 150
                       };
  QImage img( pixDataRGB, 2, 2, 8, QImage::Format_ARGB32 );
  QPixmap pix = QPixmap::fromImage( img.scaled( 8, 8 ) );
  QBrush checkerBrush;
  checkerBrush.setTexture( pix );
  p.fillRect( imageRect, checkerBrush );

  if ( mLayer->opacity() < 1.0 )
  {
    p.setOpacity( mLayer->opacity() );
  }

  p.drawImage( imageRect.left(), imageRect.top(), image );
  p.end();
  mLabelImagePreview->setPixmap( QPixmap::fromImage( previewImage ) );
}

void QgsRasterMarkerSymbolLayerWidget::setWidth()
{
  double defaultAspectRatio = mLayer->defaultAspectRatio();
  double fixedAspectRatio = 0.0;
  mHeightSpinBox->blockSignals( true );
  if ( defaultAspectRatio <= 0.0 )
  {
    mHeightSpinBox->setValue( mWidthSpinBox->value() );
  }
  else if ( mLockAspectRatio->locked() )
  {
    mHeightSpinBox->setValue( mWidthSpinBox->value() * defaultAspectRatio );
  }
  else
  {
    fixedAspectRatio = mHeightSpinBox->value() / mWidthSpinBox->value();
  }
  mHeightSpinBox->blockSignals( false );
  mLayer->setSize( mWidthSpinBox->value() );
  mLayer->setFixedAspectRatio( fixedAspectRatio );
  emit changed();
}

void QgsRasterMarkerSymbolLayerWidget::setHeight()
{
  double defaultAspectRatio = mLayer->defaultAspectRatio();
  double fixedAspectRatio = 0.0;
  mWidthSpinBox->blockSignals( true );
  if ( defaultAspectRatio <= 0.0 )
  {
    mWidthSpinBox->setValue( mHeightSpinBox->value() );
  }
  else if ( mLockAspectRatio->locked() )
  {
    mWidthSpinBox->setValue( mHeightSpinBox->value() / defaultAspectRatio );
  }
  else
  {
    fixedAspectRatio = mHeightSpinBox->value() / mWidthSpinBox->value();
  }
  mWidthSpinBox->blockSignals( false );
  mLayer->setSize( mWidthSpinBox->value() );
  mLayer->setFixedAspectRatio( fixedAspectRatio );
  emit changed();
}

void QgsRasterMarkerSymbolLayerWidget::setLockAspectRatio( const bool locked )
{
  double defaultAspectRatio = mLayer->defaultAspectRatio();
  if ( defaultAspectRatio <= 0.0 )
  {
    whileBlocking( mLockAspectRatio )->setLocked( true );
  }
  else if ( locked )
  {
    mLayer->setFixedAspectRatio( 0.0 );
    setWidth();
  }
  else
  {
    mLayer->setFixedAspectRatio( mHeightSpinBox->value() / mWidthSpinBox->value() );
  }
}

void QgsRasterMarkerSymbolLayerWidget::setAngle()
{
  mLayer->setAngle( mRotationSpinBox->value() );
  emit changed();
}

void QgsRasterMarkerSymbolLayerWidget::setOpacity( double value )
{
  mLayer->setOpacity( value );
  emit changed();
  updatePreviewImage();
}

void QgsRasterMarkerSymbolLayerWidget::setOffset()
{
  mLayer->setOffset( QPointF( mSpinOffsetX->value(), mSpinOffsetY->value() ) );
  emit changed();
}

void QgsRasterMarkerSymbolLayerWidget::mSizeUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setSizeUnit( mSizeUnitWidget->unit() );
    mLayer->setSizeMapUnitScale( mSizeUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsRasterMarkerSymbolLayerWidget::mOffsetUnitWidget_changed()
{
  if ( mLayer )
  {
    mLayer->setOffsetUnit( mOffsetUnitWidget->unit() );
    mLayer->setOffsetMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsRasterMarkerSymbolLayerWidget::mHorizontalAnchorComboBox_currentIndexChanged( int index )
{
  if ( mLayer )
  {
    mLayer->setHorizontalAnchorPoint( QgsMarkerSymbolLayer::HorizontalAnchorPoint( index ) );
    emit changed();
  }
}

void QgsRasterMarkerSymbolLayerWidget::mVerticalAnchorComboBox_currentIndexChanged( int index )
{
  if ( mLayer )
  {
    mLayer->setVerticalAnchorPoint( QgsMarkerSymbolLayer::VerticalAnchorPoint( index ) );
    emit changed();
  }
}

///////////////

QgsRasterFillSymbolLayerWidget::QgsRasterFillSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )
{
  mLayer = nullptr;
  setupUi( this );

  mImageSourceLineEdit->setLastPathSettingsKey( QStringLiteral( "/UI/lastRasterMarkerImageDir" ) );
  connect( mImageSourceLineEdit, &QgsImageSourceLineEdit::sourceChanged, this, &QgsRasterFillSymbolLayerWidget::imageSourceChanged );

  connect( mOffsetUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsRasterFillSymbolLayerWidget::mOffsetUnitWidget_changed );
  connect( mRotationSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsRasterFillSymbolLayerWidget::mRotationSpinBox_valueChanged );
  connect( mWidthUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsRasterFillSymbolLayerWidget::mWidthUnitWidget_changed );
  connect( mWidthSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsRasterFillSymbolLayerWidget::mWidthSpinBox_valueChanged );

  mWidthUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderPixels << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits
                              << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches << QgsUnitTypes::RenderPercentage );
  mOffsetUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMetersInMapUnits << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                               << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );

  mSpinOffsetX->setClearValue( 0.0 );
  mSpinOffsetY->setClearValue( 0.0 );
  mRotationSpinBox->setClearValue( 0.0 );

  connect( cboCoordinateMode, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsRasterFillSymbolLayerWidget::setCoordinateMode );
  connect( mSpinOffsetX, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsRasterFillSymbolLayerWidget::offsetChanged );
  connect( mSpinOffsetY, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsRasterFillSymbolLayerWidget::offsetChanged );
  connect( mOpacityWidget, &QgsOpacityWidget::opacityChanged, this, &QgsRasterFillSymbolLayerWidget::opacityChanged );
}


void QgsRasterFillSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( !layer )
  {
    return;
  }

  if ( layer->layerType() != QLatin1String( "RasterFill" ) )
  {
    return;
  }

  mLayer = dynamic_cast<QgsRasterFillSymbolLayer *>( layer );
  if ( !mLayer )
  {
    return;
  }

  whileBlocking( mImageSourceLineEdit )->setSource( mLayer->imageFilePath() );

  cboCoordinateMode->blockSignals( true );
  switch ( mLayer->coordinateMode() )
  {
    case QgsRasterFillSymbolLayer::Viewport:
      cboCoordinateMode->setCurrentIndex( 1 );
      break;
    case QgsRasterFillSymbolLayer::Feature:
    default:
      cboCoordinateMode->setCurrentIndex( 0 );
      break;
  }
  cboCoordinateMode->blockSignals( false );
  whileBlocking( mOpacityWidget )->setOpacity( mLayer->opacity() );
  whileBlocking( mRotationSpinBox )->setValue( mLayer->angle() );

  whileBlocking( mSpinOffsetX )->setValue( mLayer->offset().x() );
  whileBlocking( mSpinOffsetY )->setValue( mLayer->offset().y() );
  mOffsetUnitWidget->blockSignals( true );
  mOffsetUnitWidget->setUnit( mLayer->offsetUnit() );
  mOffsetUnitWidget->setMapUnitScale( mLayer->offsetMapUnitScale() );
  mOffsetUnitWidget->blockSignals( false );

  whileBlocking( mWidthSpinBox )->setValue( mLayer->width() );
  mWidthUnitWidget->blockSignals( true );
  mWidthUnitWidget->setUnit( mLayer->widthUnit() );
  mWidthUnitWidget->setMapUnitScale( mLayer->widthMapUnitScale() );
  mWidthUnitWidget->blockSignals( false );

  updatePreviewImage();

  registerDataDefinedButton( mFilenameDDBtn, QgsSymbolLayer::PropertyFile );
  registerDataDefinedButton( mOpacityDDBtn, QgsSymbolLayer::PropertyOpacity );
  registerDataDefinedButton( mRotationDDBtn, QgsSymbolLayer::PropertyAngle );
  registerDataDefinedButton( mWidthDDBtn, QgsSymbolLayer::PropertyWidth );
  registerDataDefinedButton( mOffsetDDBtn, QgsSymbolLayer::PropertyOffset );
}

QgsSymbolLayer *QgsRasterFillSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsRasterFillSymbolLayerWidget::imageSourceChanged( const QString &text )
{
  mLayer->setImageFilePath( text );
  updatePreviewImage();
  emit changed();
}

void QgsRasterFillSymbolLayerWidget::setCoordinateMode( int index )
{
  switch ( index )
  {
    case 0:
      //feature coordinate mode
      mLayer->setCoordinateMode( QgsRasterFillSymbolLayer::Feature );
      break;
    case 1:
      //viewport coordinate mode
      mLayer->setCoordinateMode( QgsRasterFillSymbolLayer::Viewport );
      break;
  }

  emit changed();
}

void QgsRasterFillSymbolLayerWidget::opacityChanged( double value )
{
  if ( !mLayer )
  {
    return;
  }

  mLayer->setOpacity( value );
  emit changed();
  updatePreviewImage();
}

void QgsRasterFillSymbolLayerWidget::offsetChanged()
{
  mLayer->setOffset( QPointF( mSpinOffsetX->value(), mSpinOffsetY->value() ) );
  emit changed();
}

void QgsRasterFillSymbolLayerWidget::mOffsetUnitWidget_changed()
{
  if ( !mLayer )
  {
    return;
  }
  mLayer->setOffsetUnit( mOffsetUnitWidget->unit() );
  mLayer->setOffsetMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
  emit changed();
}

void QgsRasterFillSymbolLayerWidget::mRotationSpinBox_valueChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setAngle( d );
    emit changed();
  }
}

void QgsRasterFillSymbolLayerWidget::mWidthUnitWidget_changed()
{
  if ( !mLayer )
  {
    return;
  }
  mLayer->setWidthUnit( mWidthUnitWidget->unit() );
  mLayer->setWidthMapUnitScale( mOffsetUnitWidget->getMapUnitScale() );
  emit changed();
}

void QgsRasterFillSymbolLayerWidget::mWidthSpinBox_valueChanged( double d )
{
  if ( !mLayer )
  {
    return;
  }
  mLayer->setWidth( d );
  emit changed();
}

void QgsRasterFillSymbolLayerWidget::updatePreviewImage()
{
  bool fitsInCache = false;
  QImage image = QgsApplication::imageCache()->pathAsImage( mLayer->imageFilePath(), QSize( 150, 150 ), true, 1.0, fitsInCache );
  if ( image.isNull() )
  {
    mLabelImagePreview->setPixmap( QPixmap() );
    return;
  }

  QImage previewImage( 150, 150, QImage::Format_ARGB32 );
  previewImage.fill( Qt::transparent );
  QRect imageRect( ( 150 - image.width() ) / 2.0, ( 150 - image.height() ) / 2.0, image.width(), image.height() );
  QPainter p;
  p.begin( &previewImage );
  //draw a checkerboard background
  uchar pixDataRGB[] = { 150, 150, 150, 150,
                         100, 100, 100, 150,
                         100, 100, 100, 150,
                         150, 150, 150, 150
                       };
  QImage img( pixDataRGB, 2, 2, 8, QImage::Format_ARGB32 );
  QPixmap pix = QPixmap::fromImage( img.scaled( 8, 8 ) );
  QBrush checkerBrush;
  checkerBrush.setTexture( pix );
  p.fillRect( imageRect, checkerBrush );

  if ( mLayer->opacity() < 1.0 )
  {
    p.setOpacity( mLayer->opacity() );
  }

  p.drawImage( imageRect.left(), imageRect.top(), image );
  p.end();
  mLabelImagePreview->setPixmap( QPixmap::fromImage( previewImage ) );
}


QgsGeometryGeneratorSymbolLayerWidget::QgsGeometryGeneratorSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent )
  : QgsSymbolLayerWidget( parent, vl )

{
  setupUi( this );
  modificationExpressionSelector->setMultiLine( true );
  modificationExpressionSelector->setLayer( const_cast<QgsVectorLayer *>( vl ) );
  modificationExpressionSelector->registerExpressionContextGenerator( this );
  cbxGeometryType->addItem( QgsLayerItem::iconPolygon(), tr( "Polygon / MultiPolygon" ), QgsSymbol::Fill );
  cbxGeometryType->addItem( QgsLayerItem::iconLine(), tr( "LineString / MultiLineString" ), QgsSymbol::Line );
  cbxGeometryType->addItem( QgsLayerItem::iconPoint(), tr( "Point / MultiPoint" ), QgsSymbol::Marker );
  connect( modificationExpressionSelector, &QgsExpressionLineEdit::expressionChanged, this, &QgsGeometryGeneratorSymbolLayerWidget::updateExpression );
  connect( cbxGeometryType, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsGeometryGeneratorSymbolLayerWidget::updateSymbolType );
}

void QgsGeometryGeneratorSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *l )
{
  mLayer = static_cast<QgsGeometryGeneratorSymbolLayer *>( l );
  modificationExpressionSelector->setExpression( mLayer->geometryExpression() );
  cbxGeometryType->setCurrentIndex( cbxGeometryType->findData( mLayer->symbolType() ) );
}

QgsSymbolLayer *QgsGeometryGeneratorSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsGeometryGeneratorSymbolLayerWidget::updateExpression( const QString &string )
{
  mLayer->setGeometryExpression( string );

  emit changed();
}

void QgsGeometryGeneratorSymbolLayerWidget::updateSymbolType()
{
  mLayer->setSymbolType( static_cast<QgsSymbol::SymbolType>( cbxGeometryType->currentData().toInt() ) );

  emit symbolChanged();
}

//
// QgsRandomMarkerFillSymbolLayerWidget
//


QgsRandomMarkerFillSymbolLayerWidget::QgsRandomMarkerFillSymbolLayerWidget( QgsVectorLayer *vl, QWidget *parent ):
  QgsSymbolLayerWidget( parent, vl )
{
  setupUi( this );

  mCountMethodComboBox->addItem( tr( "Absolute Count" ), QgsRandomMarkerFillSymbolLayer::AbsoluteCount );
  mCountMethodComboBox->addItem( tr( "Density-based Count" ), QgsRandomMarkerFillSymbolLayer::DensityBasedCount );

  mPointCountSpinBox->setShowClearButton( true );
  mPointCountSpinBox->setClearValue( 100 );
  mSeedSpinBox->setShowClearButton( true );
  mSeedSpinBox->setClearValue( 0 );

  connect( mCountMethodComboBox, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsRandomMarkerFillSymbolLayerWidget::countMethodChanged );
  connect( mPointCountSpinBox, static_cast < void ( QSpinBox::* )( int ) > ( &QSpinBox::valueChanged ), this, &QgsRandomMarkerFillSymbolLayerWidget::countChanged );
  connect( mDensityAreaSpinBox, static_cast < void ( QDoubleSpinBox::* )( double ) > ( &QDoubleSpinBox::valueChanged ), this, &QgsRandomMarkerFillSymbolLayerWidget::densityAreaChanged );
  connect( mSeedSpinBox, static_cast < void ( QSpinBox::* )( int ) > ( &QSpinBox::valueChanged ), this, &QgsRandomMarkerFillSymbolLayerWidget::seedChanged );
  connect( mClipPointsCheckBox, &QCheckBox::toggled, this, [ = ]( bool checked )
  {
    if ( mLayer )
    {
      mLayer->setClipPoints( checked );
      emit changed();
    }
  } );

  mDensityAreaUnitWidget->setUnits( QgsUnitTypes::RenderUnitList() << QgsUnitTypes::RenderMillimeters << QgsUnitTypes::RenderMapUnits << QgsUnitTypes::RenderPixels
                                    << QgsUnitTypes::RenderPoints << QgsUnitTypes::RenderInches );

  connect( mDensityAreaUnitWidget, &QgsUnitSelectionWidget::changed, this, &QgsRandomMarkerFillSymbolLayerWidget::densityAreaUnitChanged );
}

void QgsRandomMarkerFillSymbolLayerWidget::setSymbolLayer( QgsSymbolLayer *layer )
{
  if ( !layer || layer->layerType() != QLatin1String( "RandomMarkerFill" ) )
  {
    return;
  }

  mLayer = static_cast<QgsRandomMarkerFillSymbolLayer *>( layer );
  whileBlocking( mPointCountSpinBox )->setValue( mLayer->pointCount() );
  whileBlocking( mSeedSpinBox )->setValue( mLayer->seed() );
  whileBlocking( mClipPointsCheckBox )->setChecked( mLayer->clipPoints() );

  bool showDensityBasedCountWidgets = false;
  switch ( mLayer->countMethod() )
  {
    case QgsRandomMarkerFillSymbolLayer::DensityBasedCount:
      showDensityBasedCountWidgets = true;
      break;
    case QgsRandomMarkerFillSymbolLayer::AbsoluteCount:
      break;
  }
  mDensityAreaLabel->setVisible( showDensityBasedCountWidgets );
  mDensityAreaSpinBox->setVisible( showDensityBasedCountWidgets );
  mDensityAreaUnitWidget->setVisible( showDensityBasedCountWidgets );
  mDensityAreaDdbtn->setVisible( showDensityBasedCountWidgets );

  whileBlocking( mCountMethodComboBox )->setCurrentIndex( mCountMethodComboBox->findData( mLayer->countMethod() ) );
  whileBlocking( mDensityAreaSpinBox )->setValue( mLayer->densityArea() );
  mDensityAreaUnitWidget->blockSignals( true );
  mDensityAreaUnitWidget->setUnit( mLayer->densityAreaUnit() );
  mDensityAreaUnitWidget->setMapUnitScale( mLayer->densityAreaUnitScale() );
  mDensityAreaUnitWidget->blockSignals( false );

  registerDataDefinedButton( mPointCountDdbtn, QgsSymbolLayer::PropertyPointCount );
  registerDataDefinedButton( mDensityAreaDdbtn, QgsSymbolLayer::PropertyDensityArea );
  registerDataDefinedButton( mSeedDdbtn, QgsSymbolLayer::PropertyRandomSeed );
  registerDataDefinedButton( mClipPointsDdbtn, QgsSymbolLayer::PropertyClipPoints );
}

QgsSymbolLayer *QgsRandomMarkerFillSymbolLayerWidget::symbolLayer()
{
  return mLayer;
}

void QgsRandomMarkerFillSymbolLayerWidget::countMethodChanged( int )
{

  bool showDensityBasedCountWidgets = false;
  switch ( static_cast< QgsRandomMarkerFillSymbolLayer::CountMethod >( mCountMethodComboBox->currentData().toInt() ) )
  {
    case QgsRandomMarkerFillSymbolLayer::DensityBasedCount:
      showDensityBasedCountWidgets = true;
      break;
    case QgsRandomMarkerFillSymbolLayer::AbsoluteCount:
      break;
  }
  mDensityAreaLabel->setVisible( showDensityBasedCountWidgets );
  mDensityAreaSpinBox->setVisible( showDensityBasedCountWidgets );
  mDensityAreaUnitWidget->setVisible( showDensityBasedCountWidgets );
  mDensityAreaDdbtn->setVisible( showDensityBasedCountWidgets );

  if ( mLayer )
  {
    mLayer->setCountMethod( static_cast< QgsRandomMarkerFillSymbolLayer::CountMethod >( mCountMethodComboBox->currentData().toInt() ) );
    emit changed();
  }
}

void QgsRandomMarkerFillSymbolLayerWidget::countChanged( int d )
{
  if ( mLayer )
  {
    mLayer->setPointCount( d );
    emit changed();
  }
}

void QgsRandomMarkerFillSymbolLayerWidget::densityAreaChanged( double d )
{
  if ( mLayer )
  {
    mLayer->setDensityArea( d );
    emit changed();
  }
}

void QgsRandomMarkerFillSymbolLayerWidget::densityAreaUnitChanged()
{
  if ( mLayer )
  {
    mLayer->setDensityAreaUnit( mDensityAreaUnitWidget->unit() );
    mLayer->setDensityAreaUnitScale( mDensityAreaUnitWidget->getMapUnitScale() );
    emit changed();
  }
}

void QgsRandomMarkerFillSymbolLayerWidget::seedChanged( int d )
{
  if ( mLayer )
  {
    mLayer->setSeed( d );
    emit changed();
  }
}
