/***************************************************************************
     testqgssimplemarker.cpp
     -----------------------
    Date                 : Nov 2015
    Copyright            : (C) 2015 by Nyall Dawson
    Email                : nyall dot dawson at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "qgstest.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>

//qgis includes...
#include <qgsmaplayer.h>
#include <qgsvectorlayer.h>
#include <qgsapplication.h>
#include <qgsproviderregistry.h>
#include <qgsproject.h>
#include <qgssymbol.h>
#include <qgssinglesymbolrenderer.h>
#include "qgsmarkersymbollayer.h"
#include "qgsproperty.h"

//qgis test includes
#include "qgsrenderchecker.h"

static QString _fileNameForTest( const QString &testName )
{
  return QDir::tempPath() + '/' + testName + ".png";
}

static bool _verifyImage( const QString &testName, QString &report )
{
  QgsRenderChecker checker;
  checker.setControlPathPrefix( QStringLiteral( "qgssimplemarkertest" ) );
  checker.setControlName( "expected_" + testName );
  checker.setRenderedImage( _fileNameForTest( testName ) );
  checker.setSizeTolerance( 3, 3 );
  bool equal = checker.compareImages( testName, 500 );
  report += checker.report();
  return equal;
}

/**
 * \ingroup UnitTests
 * This is a unit test for simple marker symbol types.
 */
class TestQgsSimpleMarkerSymbol : public QObject
{
    Q_OBJECT

  public:
    TestQgsSimpleMarkerSymbol() = default;

  private slots:
    void initTestCase();// will be called before the first testfunction is executed.
    void cleanupTestCase();// will be called after the last testfunction was executed.
    void init() {} // will be called before each testfunction is executed.
    void cleanup() {} // will be called after every testfunction.

    void simpleMarkerSymbol();
    void simpleMarkerSymbolRotation();
    void simpleMarkerSymbolPreviewRotation();
    void simpleMarkerSymbolPreviewRotation_data();
    void simpleMarkerSymbolBevelJoin();
    void simpleMarkerSymbolMiterJoin();
    void simpleMarkerSymbolRoundJoin();
    void bounds();
    void boundsWithOffset();
    void boundsWithRotation();
    void boundsWithRotationAndOffset();
    void colors();
    void opacityWithDataDefinedColor();
    void dataDefinedOpacity();

  private:
    bool mTestHasError =  false ;

    bool imageCheck( const QString &type );
    QgsMapSettings mMapSettings;
    QgsVectorLayer *mpPointsLayer = nullptr;
    QgsSimpleMarkerSymbolLayer *mSimpleMarkerLayer = nullptr;
    QgsMarkerSymbol *mMarkerSymbol = nullptr;
    QgsSingleSymbolRenderer *mSymbolRenderer = nullptr;
    QString mTestDataDir;
    QString mReport;
};


void TestQgsSimpleMarkerSymbol::initTestCase()
{
  mTestHasError = false;
  // init QGIS's paths - true means that all path will be inited from prefix
  QgsApplication::init();
  QgsApplication::initQgis();
  QgsApplication::showSettings();

  //create some objects that will be used in all tests...
  QString myDataDir( TEST_DATA_DIR ); //defined in CmakeLists.txt
  mTestDataDir = myDataDir + '/';

  //
  //create a poly layer that will be used in all tests...
  //
  QString pointFileName = mTestDataDir + "points.shp";
  QFileInfo pointFileInfo( pointFileName );
  mpPointsLayer = new QgsVectorLayer( pointFileInfo.filePath(),
                                      pointFileInfo.completeBaseName(), QStringLiteral( "ogr" ) );

  // Register the layer with the registry
  QgsProject::instance()->addMapLayers(
    QList<QgsMapLayer *>() << mpPointsLayer );

  //setup symbol
  mSimpleMarkerLayer = new QgsSimpleMarkerSymbolLayer();
  mMarkerSymbol = new QgsMarkerSymbol();
  mMarkerSymbol->changeSymbolLayer( 0, mSimpleMarkerLayer );
  mSymbolRenderer = new QgsSingleSymbolRenderer( mMarkerSymbol );
  mpPointsLayer->setRenderer( mSymbolRenderer );

  // We only need maprender instead of mapcanvas
  // since maprender does not require a qui
  // and is more light weight
  //
  mMapSettings.setLayers( QList<QgsMapLayer *>() << mpPointsLayer );
  mReport += QLatin1String( "<h1>Simple Marker Tests</h1>\n" );

}
void TestQgsSimpleMarkerSymbol::cleanupTestCase()
{
  QString myReportFile = QDir::tempPath() + "/qgistest.html";
  QFile myFile( myReportFile );
  if ( myFile.open( QIODevice::WriteOnly | QIODevice::Append ) )
  {
    QTextStream myQTextStream( &myFile );
    myQTextStream << mReport;
    myFile.close();
  }

  QgsApplication::exitQgis();
}

void TestQgsSimpleMarkerSymbol::simpleMarkerSymbol()
{
  mReport += QLatin1String( "<h2>Simple marker symbol layer test</h2>\n" );

  mSimpleMarkerLayer->setColor( Qt::blue );
  mSimpleMarkerLayer->setStrokeColor( Qt::black );
  mSimpleMarkerLayer->setShape( QgsSimpleMarkerSymbolLayerBase::Circle );
  mSimpleMarkerLayer->setSize( 5 );
  mSimpleMarkerLayer->setStrokeWidth( 1 );
  QVERIFY( imageCheck( "simplemarker" ) );
}

void TestQgsSimpleMarkerSymbol::simpleMarkerSymbolRotation()
{
  mReport += QLatin1String( "<h2>Simple marker symbol layer test</h2>\n" );

  mSimpleMarkerLayer->setColor( Qt::blue );
  mSimpleMarkerLayer->setStrokeColor( Qt::black );
  mSimpleMarkerLayer->setShape( QgsSimpleMarkerSymbolLayerBase::Square );
  mSimpleMarkerLayer->setSize( 15 );
  mSimpleMarkerLayer->setAngle( 45 );
  mSimpleMarkerLayer->setStrokeWidth( 0.2 );
  mSimpleMarkerLayer->setPenJoinStyle( Qt::BevelJoin );
  QVERIFY( imageCheck( "simplemarker_rotation" ) );
}

void TestQgsSimpleMarkerSymbol::simpleMarkerSymbolPreviewRotation()
{
  QFETCH( QString, name );
  QFETCH( double, angle );
  QFETCH( QString, expression );
  QgsMarkerSymbol markerSymbol;
  QgsSimpleMarkerSymbolLayer *simpleMarkerLayer = new QgsSimpleMarkerSymbolLayer();
  markerSymbol.changeSymbolLayer( 0, simpleMarkerLayer );

  simpleMarkerLayer->setShape( QgsSimpleMarkerSymbolLayerBase::Shape::Arrow );
  simpleMarkerLayer->setAngle( angle );
  simpleMarkerLayer->setSize( 20 );
  simpleMarkerLayer->setColor( Qt::red );
  simpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyAngle, QgsProperty::fromExpression( expression ) );

  QgsExpressionContext ec;
  QImage image = markerSymbol.bigSymbolPreviewImage( &ec );
  image.save( _fileNameForTest( name ) );
  QVERIFY( _verifyImage( name, mReport ) );
}

void TestQgsSimpleMarkerSymbol::simpleMarkerSymbolPreviewRotation_data()
{
  QTest::addColumn<QString>( "name" );
  QTest::addColumn<double>( "angle" );
  QTest::addColumn<QString>( "expression" );

  QTest::newRow( "field_based" ) << QStringLiteral( "field_based" ) << 20. << QStringLiteral( "orientation" ); // Should fallback to 20 because orientation is not available
  QTest::newRow( "static_expression" ) << QStringLiteral( "static_expression" ) << 20. << QStringLiteral( "40" ); // Should use 40 because expression has precedence
}

void TestQgsSimpleMarkerSymbol::simpleMarkerSymbolBevelJoin()
{
  mReport += QLatin1String( "<h2>Simple marker symbol layer test</h2>\n" );

  mSimpleMarkerLayer->setColor( Qt::blue );
  mSimpleMarkerLayer->setStrokeColor( Qt::black );
  mSimpleMarkerLayer->setShape( QgsSimpleMarkerSymbolLayerBase::Triangle );
  mSimpleMarkerLayer->setSize( 25 );
  mSimpleMarkerLayer->setAngle( 0 );
  mSimpleMarkerLayer->setStrokeWidth( 3 );
  mSimpleMarkerLayer->setPenJoinStyle( Qt::BevelJoin );
  QVERIFY( imageCheck( "simplemarker_beveljoin" ) );
}

void TestQgsSimpleMarkerSymbol::simpleMarkerSymbolMiterJoin()
{
  mReport += QLatin1String( "<h2>Simple marker symbol layer test</h2>\n" );

  mSimpleMarkerLayer->setColor( Qt::blue );
  mSimpleMarkerLayer->setStrokeColor( Qt::black );
  mSimpleMarkerLayer->setShape( QgsSimpleMarkerSymbolLayerBase::Triangle );
  mSimpleMarkerLayer->setSize( 25 );
  mSimpleMarkerLayer->setStrokeWidth( 3 );
  mSimpleMarkerLayer->setPenJoinStyle( Qt::MiterJoin );
  QVERIFY( imageCheck( "simplemarker_miterjoin" ) );
}

void TestQgsSimpleMarkerSymbol::simpleMarkerSymbolRoundJoin()
{
  mReport += QLatin1String( "<h2>Simple marker symbol layer test</h2>\n" );

  mSimpleMarkerLayer->setColor( Qt::blue );
  mSimpleMarkerLayer->setStrokeColor( Qt::black );
  mSimpleMarkerLayer->setShape( QgsSimpleMarkerSymbolLayerBase::Triangle );
  mSimpleMarkerLayer->setSize( 25 );
  mSimpleMarkerLayer->setStrokeWidth( 3 );
  mSimpleMarkerLayer->setPenJoinStyle( Qt::RoundJoin );
  QVERIFY( imageCheck( "simplemarker_roundjoin" ) );
}

void TestQgsSimpleMarkerSymbol::bounds()
{
  mSimpleMarkerLayer->setColor( QColor( 200, 200, 200 ) );
  mSimpleMarkerLayer->setStrokeColor( QColor( 0, 0, 0 ) );
  mSimpleMarkerLayer->setShape( QgsSimpleMarkerSymbolLayerBase::Circle );
  mSimpleMarkerLayer->setSize( 5 );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertySize, QgsProperty::fromExpression( QStringLiteral( "min(\"importance\" * 2, 6)" ) ) );
  mSimpleMarkerLayer->setStrokeWidth( 0.5 );

  mMapSettings.setFlag( QgsMapSettings::DrawSymbolBounds, true );
  bool result = imageCheck( QStringLiteral( "simplemarker_bounds" ) );
  mMapSettings.setFlag( QgsMapSettings::DrawSymbolBounds, false );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertySize, QgsProperty() );
  QVERIFY( result );
}

void TestQgsSimpleMarkerSymbol::boundsWithOffset()
{
  mSimpleMarkerLayer->setColor( QColor( 200, 200, 200 ) );
  mSimpleMarkerLayer->setStrokeColor( QColor( 0, 0, 0 ) );
  mSimpleMarkerLayer->setShape( QgsSimpleMarkerSymbolLayerBase::Circle );
  mSimpleMarkerLayer->setSize( 5 );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyOffset, QgsProperty::fromExpression( QStringLiteral( "if(importance > 2, '5,10', '10, 5')" ) ) );
  mSimpleMarkerLayer->setStrokeWidth( 0.5 );

  mMapSettings.setFlag( QgsMapSettings::DrawSymbolBounds, true );
  bool result = imageCheck( QStringLiteral( "simplemarker_boundsoffset" ) );
  mMapSettings.setFlag( QgsMapSettings::DrawSymbolBounds, false );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyOffset, QgsProperty() );
  QVERIFY( result );
}

void TestQgsSimpleMarkerSymbol::boundsWithRotation()
{
  mSimpleMarkerLayer->setColor( QColor( 200, 200, 200 ) );
  mSimpleMarkerLayer->setStrokeColor( QColor( 0, 0, 0 ) );
  mSimpleMarkerLayer->setShape( QgsSimpleMarkerSymbolLayerBase::Square );
  mSimpleMarkerLayer->setSize( 5 );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyAngle, QgsProperty::fromExpression( QStringLiteral( "importance * 20" ) ) );
  mSimpleMarkerLayer->setStrokeWidth( 0.5 );

  mMapSettings.setFlag( QgsMapSettings::DrawSymbolBounds, true );
  bool result = imageCheck( QStringLiteral( "simplemarker_boundsrotation" ) );
  mMapSettings.setFlag( QgsMapSettings::DrawSymbolBounds, false );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyAngle, QgsProperty() );
  QVERIFY( result );
}

void TestQgsSimpleMarkerSymbol::boundsWithRotationAndOffset()
{
  mSimpleMarkerLayer->setColor( QColor( 200, 200, 200 ) );
  mSimpleMarkerLayer->setStrokeColor( QColor( 0, 0, 0 ) );
  mSimpleMarkerLayer->setShape( QgsSimpleMarkerSymbolLayerBase::Square );
  mSimpleMarkerLayer->setSize( 5 );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyOffset, QgsProperty::fromExpression( QStringLiteral( "if(importance > 2, '5,10', '10, 5')" ) ) );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyAngle, QgsProperty::fromExpression( QStringLiteral( "heading" ) ) );
  mSimpleMarkerLayer->setStrokeWidth( 0.5 );

  mMapSettings.setFlag( QgsMapSettings::DrawSymbolBounds, true );
  bool result = imageCheck( QStringLiteral( "simplemarker_boundsrotationoffset" ) );
  mMapSettings.setFlag( QgsMapSettings::DrawSymbolBounds, false );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyOffset, QgsProperty() );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyAngle, QgsProperty() );
  QVERIFY( result );
}

void TestQgsSimpleMarkerSymbol::colors()
{
  //test logic for setting/retrieving symbol color

  QgsSimpleMarkerSymbolLayer marker;
  marker.setStrokeColor( QColor( 200, 200, 200 ) );
  marker.setFillColor( QColor( 100, 100, 100 ) );

  //start with a filled shape - color should be fill color
  marker.setShape( QgsSimpleMarkerSymbolLayerBase::Circle );
  QCOMPARE( marker.color(), QColor( 100, 100, 100 ) );
  marker.setColor( QColor( 150, 150, 150 ) );
  QCOMPARE( marker.fillColor(), QColor( 150, 150, 150 ) );

  //now try with a non-filled (stroke only) shape - color should be stroke color
  marker.setShape( QgsSimpleMarkerSymbolLayerBase::Cross );
  QCOMPARE( marker.color(), QColor( 200, 200, 200 ) );
  marker.setColor( QColor( 250, 250, 250 ) );
  QCOMPARE( marker.strokeColor(), QColor( 250, 250, 250 ) );
}

void TestQgsSimpleMarkerSymbol::opacityWithDataDefinedColor()
{
  mSimpleMarkerLayer->setColor( QColor( 200, 200, 200 ) );
  mSimpleMarkerLayer->setStrokeColor( QColor( 0, 0, 0 ) );
  mSimpleMarkerLayer->setShape( QgsSimpleMarkerSymbolLayerBase::Square );
  mSimpleMarkerLayer->setSize( 5 );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyFillColor, QgsProperty::fromExpression( QStringLiteral( "if(importance > 2, 'red', 'green')" ) ) );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyStrokeColor, QgsProperty::fromExpression( QStringLiteral( "if(importance > 2, 'blue', 'magenta')" ) ) );
  mSimpleMarkerLayer->setStrokeWidth( 0.5 );
  mMarkerSymbol->setOpacity( 0.5 );

  bool result = imageCheck( QStringLiteral( "simplemarker_opacityddcolor" ) );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyFillColor, QgsProperty() );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyStrokeColor, QgsProperty() );
  mMarkerSymbol->setOpacity( 1.0 );
  QVERIFY( result );
}

void TestQgsSimpleMarkerSymbol::dataDefinedOpacity()
{
  mSimpleMarkerLayer->setColor( QColor( 200, 200, 200 ) );
  mSimpleMarkerLayer->setStrokeColor( QColor( 0, 0, 0 ) );
  mSimpleMarkerLayer->setShape( QgsSimpleMarkerSymbolLayerBase::Square );
  mSimpleMarkerLayer->setSize( 5 );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyFillColor, QgsProperty::fromExpression( QStringLiteral( "if(importance > 2, 'red', 'green')" ) ) );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyStrokeColor, QgsProperty::fromExpression( QStringLiteral( "if(importance > 2, 'blue', 'magenta')" ) ) );
  mSimpleMarkerLayer->setStrokeWidth( 0.5 );
  mMarkerSymbol->setDataDefinedProperty( QgsSymbol::PropertyOpacity, QgsProperty::fromExpression( QStringLiteral( "if(\"Heading\" > 100, 25, 50)" ) ) );

  bool result = imageCheck( QStringLiteral( "simplemarker_ddopacity" ) );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyFillColor, QgsProperty() );
  mSimpleMarkerLayer->setDataDefinedProperty( QgsSymbolLayer::PropertyStrokeColor, QgsProperty() );
  mMarkerSymbol->setDataDefinedProperty( QgsSymbol::PropertyOpacity, QgsProperty() );
  QVERIFY( result );
}

//
// Private helper functions not called directly by CTest
//


bool TestQgsSimpleMarkerSymbol::imageCheck( const QString &testType )
{
  //use the QgsRenderChecker test utility class to
  //ensure the rendered output matches our control image
  mMapSettings.setExtent( mpPointsLayer->extent() );
  mMapSettings.setOutputDpi( 96 );
  QgsRenderChecker myChecker;
  myChecker.setControlPathPrefix( QStringLiteral( "symbol_simplemarker" ) );
  myChecker.setControlName( "expected_" + testType );
  myChecker.setMapSettings( mMapSettings );
  bool myResultFlag = myChecker.runTest( testType );
  mReport += myChecker.report();
  return myResultFlag;
}

QGSTEST_MAIN( TestQgsSimpleMarkerSymbol )
#include "testqgssimplemarker.moc"
