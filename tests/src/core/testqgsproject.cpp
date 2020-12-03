/***************************************************************************
     testqgsproject.cpp
     --------------------------------------
    Date                 : June 2014
    Copyright            : (C) 2014 by Martin Dobias
    Email                : wonder.sk at gmail dot com
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

#include "qgsapplication.h"
#include "qgsmarkersymbollayer.h"
#include "qgspathresolver.h"
#include "qgsproject.h"
#include "qgssinglesymbolrenderer.h"
#include "qgslayertree.h"
#include "qgssettings.h"
#include "qgsunittypes.h"
#include "qgsvectorlayer.h"
#include "qgssymbollayerutils.h"
#include "qgslayoutmanager.h"

class TestQgsProject : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase();// will be called before the first testfunction is executed.
    void cleanupTestCase();// will be called after the last testfunction was executed.
    void init();// will be called before each testfunction is executed.
    void cleanup();// will be called after every testfunction.

    void testReadPath();
    void testPathResolver();
    void testPathResolverSvg();
    void testProjectUnits();
    void variablesChanged();
    void testLayerFlags();
    void testLocalFiles();
    void testLocalUrlFiles();
    void testReadFlags();
    void testSetGetCrs();
    void testEmbeddedLayerGroupFromQgz();
    void projectSaveUser();
    void testCrsExpressions();
};

void TestQgsProject::init()
{
}

void TestQgsProject::cleanup()
{
  // will be called after every testfunction.
}

void TestQgsProject::initTestCase()
{
  // Runs once before any tests are run

  // Set up the QgsSettings environment
  QCoreApplication::setOrganizationName( QStringLiteral( "QGIS" ) );
  QCoreApplication::setOrganizationDomain( QStringLiteral( "qgis.org" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "QGIS-TEST" ) );

  QgsApplication::init();
  QgsApplication::initQgis();
  QgsSettings().clear();
}


void TestQgsProject::cleanupTestCase()
{
  // Runs once after all tests are run
  QgsApplication::exitQgis();
}

void TestQgsProject::testReadPath()
{
  QgsProject *prj = new QgsProject;
  // this is a bit hacky as we do not really load such project
  QString prefix;
#if defined(Q_OS_WIN)
  prefix = "C:";
#endif
  prj->setFileName( prefix + "/home/qgis/a-project-file.qgs" ); // not expected to exist
  // make sure we work with relative paths!
  prj->writeEntry( QStringLiteral( "Paths" ), QStringLiteral( "Absolute" ), false );

  QCOMPARE( prj->readPath( "./x.shp" ), QString( prefix + "/home/qgis/x.shp" ) );
  QCOMPARE( prj->readPath( "../x.shp" ), QString( prefix + "/home/x.shp" ) );

  // TODO: old style (seems QGIS < 1.3) - needs existing project file and existing file
  // QCOMPARE( prj->readPath( "x.shp" ), QString( "/home/qgis/x.shp" ) );

  // VSI: /vsizip, /vsitar, /vsigzip, *.zip, *.gz, *.tgz, ...

  QCOMPARE( prj->readPath( "./x.gz" ), QString( prefix + "/home/qgis/x.gz" ) );
  QCOMPARE( prj->readPath( "/vsigzip/./x.gz" ), QString( "/vsigzip/%1/home/qgis/x.gz" ).arg( prefix ) ); // not sure how useful this really is...

  delete prj;
}

void TestQgsProject::testPathResolver()
{
  // Test resolver with a non existing file path
  QgsPathResolver resolverLegacy( QStringLiteral( "/home/qgis/test.qgs" ) );
  QCOMPARE( resolverLegacy.readPath( QString() ), QString() );
  QCOMPARE( resolverLegacy.writePath( QString() ), QString() );
  QCOMPARE( resolverLegacy.writePath( "/home/qgis/file1.txt" ), QString( "./file1.txt" ) );
  QCOMPARE( resolverLegacy.writePath( "/home/qgis/subdir/file1.txt" ), QString( "./subdir/file1.txt" ) );
  QCOMPARE( resolverLegacy.writePath( "/home/file1.txt" ), QString( "../file1.txt" ) );
  QCOMPARE( resolverLegacy.readPath( "./file1.txt" ), QString( "/home/qgis/file1.txt" ) );
  QCOMPARE( resolverLegacy.readPath( "./subdir/file1.txt" ), QString( "/home/qgis/subdir/file1.txt" ) );
  QCOMPARE( resolverLegacy.readPath( "../file1.txt" ), QString( "/home/file1.txt" ) );
  QCOMPARE( resolverLegacy.readPath( "/home/qgis/file1.txt" ), QString( "/home/qgis/file1.txt" ) );

  // Test resolver with existing file path
  QTemporaryDir tmpDir;
  QString tmpDirName = tmpDir.path();
  QDir dir( tmpDirName );
  dir.mkpath( tmpDirName + "/home/qgis/" );

  QgsPathResolver resolverRel( QString( tmpDirName + "/home/qgis/test.qgs" ) );
  QCOMPARE( resolverRel.readPath( QString() ), QString() );
  QCOMPARE( resolverRel.writePath( QString() ), QString() );
  QCOMPARE( resolverRel.writePath( tmpDirName + "/home/qgis/file1.txt" ), QString( "./file1.txt" ) );
  QCOMPARE( resolverRel.writePath( tmpDirName + "/home/qgis/subdir/file1.txt" ), QString( "./subdir/file1.txt" ) );
  QCOMPARE( resolverRel.writePath( tmpDirName + "/home/file1.txt" ), QString( "../file1.txt" ) );
  QCOMPARE( resolverRel.readPath( "./file1.txt" ), QString( tmpDirName + "/home/qgis/file1.txt" ) );
  QCOMPARE( resolverRel.readPath( "./subdir/file1.txt" ), QString( tmpDirName + "/home/qgis/subdir/file1.txt" ) );
  QCOMPARE( resolverRel.readPath( "../file1.txt" ), QString( tmpDirName + "/home/file1.txt" ) );
  QCOMPARE( resolverRel.readPath( tmpDirName + "/home/qgis/file1.txt" ), QString( tmpDirName + "/home/qgis/file1.txt" ) );

  // test older style relative path - file must exist for this to work
  QTemporaryFile tmpFile;
  tmpFile.open(); // fileName is not available until we open the file
  QString tmpName =  tmpFile.fileName();
  tmpFile.close();
  QgsPathResolver tempRel( tmpName );
  QFileInfo fi( tmpName );
  QFile testFile( fi.path() + QStringLiteral( "/file1.txt" ) );
  QVERIFY( testFile.open( QIODevice::WriteOnly | QIODevice::Text ) );
  testFile.close();
  QVERIFY( QFile::exists( fi.path() + QStringLiteral( "/file1.txt" ) ) );
  QCOMPARE( tempRel.readPath( "file1.txt" ), QString( fi.path() + QStringLiteral( "/file1.txt" ) ) );

  QgsPathResolver resolverAbs;
  QCOMPARE( resolverAbs.writePath( "/home/qgis/file1.txt" ), QString( "/home/qgis/file1.txt" ) );
  QCOMPARE( resolverAbs.readPath( "/home/qgis/file1.txt" ), QString( "/home/qgis/file1.txt" ) );
  QCOMPARE( resolverAbs.readPath( "./file1.txt" ), QString( "./file1.txt" ) );

  // TODO: test non-canonical paths - there are inconsistencies in the implementation
  // e.g. base filename "/home/qgis/../test.qgs" resolving "/home/qgis/../file1.txt" back and forth
}

static void _useRendererWithSvgSymbol( QgsVectorLayer *layer, const QString &path )
{
  QgsSvgMarkerSymbolLayer *sl = new QgsSvgMarkerSymbolLayer( path );
  QgsMarkerSymbol *markerSymbol = new QgsMarkerSymbol( QgsSymbolLayerList() << sl );
  QgsSingleSymbolRenderer *renderer = new QgsSingleSymbolRenderer( markerSymbol );
  layer->setRenderer( renderer );
}

static QString _getLayerSvgMarkerPath( const QgsProject &prj, const QString &layerName )
{
  QList<QgsMapLayer *> layers = prj.mapLayersByName( layerName );
  Q_ASSERT( layers.count() == 1 );
  Q_ASSERT( layers[0]->type() == QgsMapLayerType::VectorLayer );
  QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( layers[0] );
  Q_ASSERT( layer->renderer() );
  Q_ASSERT( layer->renderer()->type() == "singleSymbol" );
  QgsSingleSymbolRenderer *r = static_cast<QgsSingleSymbolRenderer *>( layer->renderer() );
  QgsSymbol *s = r->symbol();
  Q_ASSERT( s && s->symbolLayerCount() == 1 );
  Q_ASSERT( s->symbolLayer( 0 )->layerType() == "SvgMarker" );
  QgsSvgMarkerSymbolLayer *sl = static_cast<QgsSvgMarkerSymbolLayer *>( s->symbolLayer( 0 ) );
  return sl->path();
}

static QHash<QString, QString> _parseSvgPathsForLayers( const QString &projectFilename )
{
  QHash<QString, QString> projectFileSvgPaths;   // key = layer name, value = svg path

  QDomDocument doc;
  QFile projectFile( projectFilename );
  bool res = projectFile.open( QIODevice::ReadOnly );
  Q_ASSERT( res );
  res = doc.setContent( &projectFile );
  Q_ASSERT( res );
  projectFile.close();

  QDomElement docElem = doc.documentElement();
  QDomElement layersElem = docElem.firstChildElement( QStringLiteral( "projectlayers" ) );
  QDomElement layerElem = layersElem.firstChildElement();
  while ( !layerElem.isNull() )
  {
    QString layerName = layerElem.firstChildElement( QStringLiteral( "layername" ) ).text();
    QString svgPath;
    QDomElement symbolElem = layerElem.firstChildElement( QStringLiteral( "renderer-v2" ) ).firstChildElement( QStringLiteral( "symbols" ) ).firstChildElement( QStringLiteral( "symbol" ) ).firstChildElement( QStringLiteral( "layer" ) );
    QDomElement propElem = symbolElem.firstChildElement( QStringLiteral( "prop" ) );
    while ( !propElem.isNull() )
    {
      if ( propElem.attribute( QStringLiteral( "k" ) ) == QLatin1String( "name" ) )
      {
        svgPath = propElem.attribute( QStringLiteral( "v" ) );
        break;
      }
      propElem = propElem.nextSiblingElement( QStringLiteral( "prop" ) );
    }
    projectFileSvgPaths[layerName] = svgPath;
    layerElem = layerElem.nextSiblingElement();
  }
  return projectFileSvgPaths;
}

void TestQgsProject::testPathResolverSvg()
{
  QString dataDir( TEST_DATA_DIR ); //defined in CmakeLists.txt
  QString layerPath = dataDir + "/points.shp";

  QVERIFY( QgsSymbolLayerUtils::svgSymbolNameToPath( QString(), QgsPathResolver() ).isEmpty() );
  QVERIFY( QgsSymbolLayerUtils::svgSymbolPathToName( QString(), QgsPathResolver() ).isEmpty() );

  // build a project with 3 layers, each having a simple renderer with SVG marker
  // - existing SVG file in project dir
  // - existing SVG file in QGIS dir
  // - non-exsiting SVG file

  QTemporaryDir dir;
  QVERIFY( dir.isValid() );
  // on mac the returned path was not canonical and the resolver failed to convert paths properly
  QString dirPath = QFileInfo( dir.path() ).canonicalFilePath();

  QString projectFilename = dirPath + "/project.qgs";
  QString ourSvgPath = dirPath + "/valid.svg";
  QString invalidSvgPath = dirPath + "/invalid.svg";

  QFile svgFile( ourSvgPath );
  QVERIFY( svgFile.open( QIODevice::WriteOnly ) );
  svgFile.write( "<svg/>" );   // not a proper SVG, but good enough for this case
  svgFile.close();

  QVERIFY( QFileInfo::exists( ourSvgPath ) );  // should exist now

  QString librarySvgPath = QgsSymbolLayerUtils::svgSymbolNameToPath( QStringLiteral( "transport/transport_airport.svg" ), QgsPathResolver() );
  QCOMPARE( QgsSymbolLayerUtils::svgSymbolPathToName( librarySvgPath, QgsPathResolver() ), QStringLiteral( "transport/transport_airport.svg" ) );

  QgsVectorLayer *layer1 = new QgsVectorLayer( layerPath, QStringLiteral( "points 1" ), QStringLiteral( "ogr" ) );
  _useRendererWithSvgSymbol( layer1, ourSvgPath );

  QgsVectorLayer *layer2 = new QgsVectorLayer( layerPath, QStringLiteral( "points 2" ), QStringLiteral( "ogr" ) );
  _useRendererWithSvgSymbol( layer2, invalidSvgPath );

  QgsVectorLayer *layer3 = new QgsVectorLayer( layerPath, QStringLiteral( "points 3" ), QStringLiteral( "ogr" ) );
  _useRendererWithSvgSymbol( layer3, librarySvgPath );

  QVERIFY( layer1->isValid() );

  QgsProject project;
  project.addMapLayers( QList<QgsMapLayer *>() << layer1 << layer2 << layer3 );
  project.write( projectFilename );

  // make sure the path resolver works with relative paths (enabled by default)
  QCOMPARE( project.pathResolver().readPath( "./a.txt" ), QString( dirPath + "/a.txt" ) );
  QCOMPARE( project.pathResolver().writePath( dirPath + "/a.txt" ), QString( "./a.txt" ) );

  // check that the saved paths are relative

  // key = layer name, value = svg path
  QHash<QString, QString> projectFileSvgPaths = _parseSvgPathsForLayers( projectFilename );

  QCOMPARE( projectFileSvgPaths.count(), 3 );
  QCOMPARE( projectFileSvgPaths["points 1"], QString( "./valid.svg" ) ); // relative path to project
  QCOMPARE( projectFileSvgPaths["points 2"], invalidSvgPath );  // full path to non-existent file (not sure why - but that's how it works now)
  QCOMPARE( projectFileSvgPaths["points 3"], QString( "transport/transport_airport.svg" ) );  // relative path to library

  // load project again, check that the paths are absolute
  QgsProject projectLoaded;
  projectLoaded.read( projectFilename );
  QString svg1 = _getLayerSvgMarkerPath( projectLoaded, QStringLiteral( "points 1" ) );
  QString svg2 = _getLayerSvgMarkerPath( projectLoaded, QStringLiteral( "points 2" ) );
  QString svg3 = _getLayerSvgMarkerPath( projectLoaded, QStringLiteral( "points 3" ) );
  QCOMPARE( svg1, ourSvgPath );
  QCOMPARE( svg2, invalidSvgPath );
  QCOMPARE( svg3, librarySvgPath );

  //
  // now let's use these layers in embedded in another project...
  //

  QList<QDomNode> brokenNodes;
  QgsProject projectMaster;
  QVERIFY( projectMaster.createEmbeddedLayer( layer1->id(), projectFilename, brokenNodes ) );
  QVERIFY( projectMaster.createEmbeddedLayer( layer2->id(), projectFilename, brokenNodes ) );
  QVERIFY( projectMaster.createEmbeddedLayer( layer3->id(), projectFilename, brokenNodes ) );

  QString svg1x = _getLayerSvgMarkerPath( projectMaster, QStringLiteral( "points 1" ) );
  QString svg2x = _getLayerSvgMarkerPath( projectLoaded, QStringLiteral( "points 2" ) );
  QString svg3x = _getLayerSvgMarkerPath( projectLoaded, QStringLiteral( "points 3" ) );
  QCOMPARE( svg1x, ourSvgPath );
  QCOMPARE( svg2x, invalidSvgPath );
  QCOMPARE( svg3x, librarySvgPath );

}


void TestQgsProject::testProjectUnits()
{
  //test setting and retrieving project units

  // DISTANCE

  //first set a default QGIS distance unit
  QgsSettings s;
  s.setValue( QStringLiteral( "/qgis/measure/displayunits" ), QgsUnitTypes::encodeUnit( QgsUnitTypes::DistanceFeet ) );

  QgsProject *prj = new QgsProject;
  // new project should inherit QGIS default distance unit
  prj->clear();
  QCOMPARE( prj->distanceUnits(), QgsUnitTypes::DistanceFeet );

  //changing default QGIS unit should not affect existing project
  s.setValue( QStringLiteral( "/qgis/measure/displayunits" ), QgsUnitTypes::encodeUnit( QgsUnitTypes::DistanceNauticalMiles ) );
  QCOMPARE( prj->distanceUnits(), QgsUnitTypes::DistanceFeet );

  //test setting new units for project
  prj->setDistanceUnits( QgsUnitTypes::DistanceNauticalMiles );
  QCOMPARE( prj->distanceUnits(), QgsUnitTypes::DistanceNauticalMiles );

  // AREA

  //first set a default QGIS area unit
  s.setValue( QStringLiteral( "/qgis/measure/areaunits" ), QgsUnitTypes::encodeUnit( QgsUnitTypes::AreaSquareYards ) );

  // new project should inherit QGIS default area unit
  prj->clear();
  QCOMPARE( prj->areaUnits(), QgsUnitTypes::AreaSquareYards );

  //changing default QGIS unit should not affect existing project
  s.setValue( QStringLiteral( "/qgis/measure/areaunits" ), QgsUnitTypes::encodeUnit( QgsUnitTypes::AreaAcres ) );
  QCOMPARE( prj->areaUnits(), QgsUnitTypes::AreaSquareYards );

  //test setting new units for project
  prj->setAreaUnits( QgsUnitTypes::AreaAcres );
  QCOMPARE( prj->areaUnits(), QgsUnitTypes::AreaAcres );

  delete prj;
}

void TestQgsProject::variablesChanged()
{
  QgsProject *prj = new QgsProject;
  QSignalSpy spyVariablesChanged( prj, &QgsProject::customVariablesChanged );
  QVariantMap vars;
  vars.insert( QStringLiteral( "variable" ), QStringLiteral( "1" ) );
  prj->setCustomVariables( vars );
  QVERIFY( spyVariablesChanged.count() == 1 );
  delete prj;
}

void TestQgsProject::testLayerFlags()
{
  QString dataDir( TEST_DATA_DIR ); //defined in CmakeLists.txt
  QString layerPath = dataDir + "/points.shp";
  QgsVectorLayer *layer1 = new QgsVectorLayer( layerPath, QStringLiteral( "points 1" ), QStringLiteral( "ogr" ) );
  QgsVectorLayer *layer2 = new QgsVectorLayer( layerPath, QStringLiteral( "points 2" ), QStringLiteral( "ogr" ) );

  QgsProject prj;
  prj.addMapLayer( layer1 );
  prj.addMapLayer( layer2 );

  layer2->setFlags( layer2->flags() & ~QgsMapLayer::Removable );

  QString layer2id = layer2->id();

  QTemporaryFile f;
  QVERIFY( f.open() );
  f.close();
  prj.setFileName( f.fileName() );
  prj.write();

  // test reading required layers back
  QgsProject prj2;
  prj2.setFileName( f.fileName() );
  QVERIFY( prj2.read() );
  QgsMapLayer *layer = prj.mapLayer( layer2id );
  QVERIFY( layer );
  QVERIFY( !layer->flags().testFlag( QgsMapLayer::Removable ) );
}

void TestQgsProject::testLocalFiles()
{
  QTemporaryFile f;
  QVERIFY( f.open() );
  f.close();
  QgsProject prj;
  QFileInfo info( f.fileName() );
  prj.setFileName( f.fileName() );
  prj.write();
  QString shpPath = info.dir().path() + '/' + info.baseName() + ".shp";
  QString layerPath = "file://" + shpPath;
  QFile f2( shpPath );
  QVERIFY( f2.open( QFile::ReadWrite ) );
  f2.close();
  QgsPathResolver resolver( f.fileName( ) );
  QCOMPARE( resolver.writePath( layerPath ), QString( "./" + info.baseName() + ".shp" ) ) ;

}

void TestQgsProject::testLocalUrlFiles()
{
  QTemporaryFile f;
  QVERIFY( f.open() );
  f.close();
  QgsProject prj;
  QFileInfo info( f.fileName() );
  prj.setFileName( f.fileName() );
  prj.write();
  QString shpPath = info.dir().path() + '/' + info.baseName() + ".shp";
  QString extraStuff {"?someVar=someValue&someOtherVar=someOtherValue" };
  QString layerPath = "file://" + shpPath + extraStuff;
  QFile f2( shpPath );
  QVERIFY( f2.open( QFile::ReadWrite ) );
  f2.close();
  QgsPathResolver resolver( f.fileName( ) );
  QCOMPARE( resolver.writePath( layerPath ), QString( "./" + info.baseName() + ".shp" + extraStuff ) ) ;

}

void TestQgsProject::testReadFlags()
{
  QString project1Path = QString( TEST_DATA_DIR ) + QStringLiteral( "/embedded_groups/project1.qgs" );
  QgsProject p;
  QVERIFY( p.read( project1Path, QgsProject::ReadFlag::FlagDontResolveLayers ) );
  auto layers = p.mapLayers();
  QCOMPARE( layers.count(), 3 );
  // layers should be invalid - we skipped loading them!
  QVERIFY( !layers.value( QStringLiteral( "points20170310142652246" ) )->isValid() );
  QVERIFY( !layers.value( QStringLiteral( "lines20170310142652255" ) )->isValid() );
  QVERIFY( !layers.value( QStringLiteral( "polys20170310142652234" ) )->isValid() );

  // but they should have renderers (and other stuff!)
  QCOMPARE( qobject_cast< QgsVectorLayer * >( layers.value( QStringLiteral( "points20170310142652246" ) ) )->renderer()->type(), QStringLiteral( "categorizedSymbol" ) );
  QCOMPARE( qobject_cast< QgsVectorLayer * >( layers.value( QStringLiteral( "lines20170310142652255" ) ) )->renderer()->type(), QStringLiteral( "categorizedSymbol" ) );
  QCOMPARE( qobject_cast< QgsVectorLayer * >( layers.value( QStringLiteral( "polys20170310142652234" ) ) )->renderer()->type(), QStringLiteral( "categorizedSymbol" ) );
  QVERIFY( ! layers.value( QStringLiteral( "polys20170310142652234" ) )->originalXmlProperties().isEmpty() );

  // do not store styles
  QVERIFY( p.read( project1Path, QgsProject::ReadFlag::FlagDontStoreOriginalStyles ) );
  layers = p.mapLayers();
  QVERIFY( layers.value( QStringLiteral( "polys20170310142652234" ) )->originalXmlProperties().isEmpty() );

  // project with embedded groups
  QString project2Path = QString( TEST_DATA_DIR ) + QStringLiteral( "/embedded_groups/project2.qgs" );
  QgsProject p2;
  QVERIFY( p2.read( project2Path, QgsProject::ReadFlag::FlagDontResolveLayers ) );
  // layers should be invalid - we skipped loading them!
  layers = p2.mapLayers();
  QCOMPARE( layers.count(), 2 );
  QVERIFY( !layers.value( QStringLiteral( "lines20170310142652255" ) )->isValid() );
  QVERIFY( !layers.value( QStringLiteral( "polys20170310142652234" ) )->isValid() );
  QCOMPARE( qobject_cast< QgsVectorLayer * >( layers.value( QStringLiteral( "lines20170310142652255" ) ) )->renderer()->type(), QStringLiteral( "categorizedSymbol" ) );
  QCOMPARE( qobject_cast< QgsVectorLayer * >( layers.value( QStringLiteral( "polys20170310142652234" ) ) )->renderer()->type(), QStringLiteral( "categorizedSymbol" ) );


  QString project3Path = QString( TEST_DATA_DIR ) + QStringLiteral( "/layouts/layout_casting.qgs" );
  QgsProject p3;
  QVERIFY( p3.read( project3Path, QgsProject::ReadFlag::FlagDontLoadLayouts ) );
  QCOMPARE( p3.layoutManager()->layouts().count(), 0 );
}

void TestQgsProject::testEmbeddedLayerGroupFromQgz()
{
  QString path = QString( TEST_DATA_DIR ) + QStringLiteral( "/embedded_groups/project1.qgz" );
  QList<QDomNode> brokenNodes;

  QgsProject p0;
  p0.read( path );
  QgsMapLayer *points = p0.mapLayersByName( "points" )[0];
  QgsMapLayer *polys = p0.mapLayersByName( "polys" )[0];

  QgsProject p1;
  p1.createEmbeddedLayer( points->id(), p0.fileName(), brokenNodes );
  p1.createEmbeddedGroup( "group1", p0.fileName(), QStringList() );

  QCOMPARE( p1.layerIsEmbedded( points->id() ), path );
  QCOMPARE( p1.layerIsEmbedded( polys->id() ), path );

  // test embedded layers when origin project is something like ../XXX
  path = QString( TEST_DATA_DIR ) + QStringLiteral( "/embedded_layers/project.qgz" );
  QgsProject p2;
  p2.read( path );

  QgsMapLayer *points2 = p0.mapLayersByName( "points" )[0];
  bool saveFlag = p2.mEmbeddedLayers[points2->id()].second;
  QCOMPARE( saveFlag, true );

  bool valid = p2.loadEmbeddedNodes( p2.layerTreeRoot() );
  QCOMPARE( valid, true );
}

void TestQgsProject::projectSaveUser()
{
  QgsProject p;
  QVERIFY( p.saveUser().isEmpty() );
  QVERIFY( p.saveUserFullName().isEmpty() );
  QVERIFY( !p.lastSaveDateTime().isValid() );

  QTemporaryFile f;
  QVERIFY( f.open() );
  f.close();
  p.setFileName( f.fileName() );
  p.write();

  QCOMPARE( p.saveUser(), QgsApplication::userLoginName() );
  QCOMPARE( p.saveUserFullName(), QgsApplication::userFullName() );
  QCOMPARE( p.lastSaveDateTime().date(), QDateTime::currentDateTime().date() );
  QCOMPARE( p.lastSaveVersion().text(), QgsProjectVersion( Qgis::version() ).text() );

  QgsSettings s;
  s.setValue( QStringLiteral( "projects/anonymize_saved_projects" ), true, QgsSettings::Core );

  p.write();

  QVERIFY( p.saveUser().isEmpty() );
  QVERIFY( p.saveUserFullName().isEmpty() );
  QVERIFY( !p.lastSaveDateTime().isValid() );

  s.setValue( QStringLiteral( "projects/anonymize_saved_projects" ), false, QgsSettings::Core );

  p.write();
  QCOMPARE( p.saveUser(), QgsApplication::userLoginName() );
  QCOMPARE( p.saveUserFullName(), QgsApplication::userFullName() );
  QCOMPARE( p.lastSaveDateTime().date(), QDateTime::currentDateTime().date() );

  QgsProject p2;
  QVERIFY( p2.read( QString( TEST_DATA_DIR ) + QStringLiteral( "/embedded_groups/project1.qgs" ) ) );
  QCOMPARE( p2.lastSaveVersion().text(), QStringLiteral( "2.99.0-Master" ) );
  p2.clear();
  QVERIFY( p2.lastSaveVersion().isNull() );
}

void TestQgsProject::testSetGetCrs()
{
  QgsProject p;

  // Set 4326
  //  - CRS changes
  //  - ellipsoid stays as NONE
  QSignalSpy crsChangedSpy( &p, &QgsProject::crsChanged );
  QSignalSpy ellipsoidChangedSpy( &p, &QgsProject::ellipsoidChanged );

  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 4326 ) );

  QCOMPARE( crsChangedSpy.count(), 1 );
  QCOMPARE( ellipsoidChangedSpy.count(), 0 );

  QCOMPARE( p.crs(), QgsCoordinateReferenceSystem::fromEpsgId( 4326 ) );
  QCOMPARE( p.ellipsoid(), QStringLiteral( "NONE" ) );

  crsChangedSpy.clear();
  ellipsoidChangedSpy.clear();

  // Set 21781
  //  - CRS changes
  //  - ellipsoid stays as NONE

  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 21781 ) );

  QCOMPARE( crsChangedSpy.count(), 1 );
  QCOMPARE( ellipsoidChangedSpy.count(), 0 );

  QCOMPARE( p.crs(), QgsCoordinateReferenceSystem::fromEpsgId( 21781 ) );
  QCOMPARE( p.ellipsoid(), QStringLiteral( "NONE" ) );

  crsChangedSpy.clear();
  ellipsoidChangedSpy.clear();

  // Set 21781 again, including adjustEllipsoid flag
  //  - CRS changes
  //  - ellipsoid changes to Bessel

  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 21781 ), true );

  QCOMPARE( crsChangedSpy.count(), 0 );
  QCOMPARE( ellipsoidChangedSpy.count(), 1 );

  QCOMPARE( p.crs(), QgsCoordinateReferenceSystem::fromEpsgId( 21781 ) );
#if PROJ_VERSION_MAJOR>=6
  QCOMPARE( p.ellipsoid(), QStringLiteral( "EPSG:7004" ) );
#else
  QCOMPARE( p.ellipsoid(), QStringLiteral( "bessel" ) );
#endif

  crsChangedSpy.clear();
  ellipsoidChangedSpy.clear();

  // Set 2056, including adjustEllipsoid flag
  //  - CRS changes
  //  - ellipsoid stays

  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 2056 ), true );

  QCOMPARE( crsChangedSpy.count(), 1 );
  QCOMPARE( ellipsoidChangedSpy.count(), 0 );

  QCOMPARE( p.crs(), QgsCoordinateReferenceSystem::fromEpsgId( 2056 ) );
#if PROJ_VERSION_MAJOR>=6
  QCOMPARE( p.ellipsoid(), QStringLiteral( "EPSG:7004" ) );
#else
  QCOMPARE( p.ellipsoid(), QStringLiteral( "bessel" ) );
#endif

  crsChangedSpy.clear();
  ellipsoidChangedSpy.clear();
}

void TestQgsProject::testCrsExpressions()
{
  QgsProject p;
  QVariant r;

  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 4326 ) );

  QgsExpressionContext c = p.createExpressionContext();

  QgsExpression e2( QStringLiteral( "@project_crs" ) );
  r = e2.evaluate( &c );
  QCOMPARE( r.toString(), QString( "EPSG:4326" ) );

  QgsExpression e3( QStringLiteral( "@project_crs_definition" ) );
  r = e3.evaluate( &c );
  QCOMPARE( r.toString(), QString( "+proj=longlat +datum=WGS84 +no_defs" ) );

  QgsExpression e4( QStringLiteral( "@project_units" ) );
  r = e4.evaluate( &c );
  QCOMPARE( r.toString(), QString( "degrees" ) );

  QgsExpression e5( QStringLiteral( "@project_crs_description" ) );
  r = e5.evaluate( &c );
  QCOMPARE( r.toString(), QString( "WGS 84" ) );

  QgsExpression e6( QStringLiteral( "@project_crs_acronym" ) );
  r = e6.evaluate( &c );
  QCOMPARE( r.toString(), QString( "longlat" ) );

  QgsExpression e7( QStringLiteral( "@project_crs_proj4" ) );
  r = e7.evaluate( &c );
  QCOMPARE( r.toString(), QString( "+proj=longlat +datum=WGS84 +no_defs" ) );

  QgsExpression e8( QStringLiteral( "@project_crs_wkt" ) );
  r = e8.evaluate( &c );
  QVERIFY( r.toString().length() >= 15 );

  QgsExpression e9( QStringLiteral( "@project_crs_ellipsoid" ) );
  r = e9.evaluate( &c );
#if PROJ_VERSION_MAJOR>=6
  QCOMPARE( r.toString(), QString( "EPSG:7030" ) );
#else
  QCOMPARE( r.toString(), QString( "WGS84" ) );
#endif
}

QGSTEST_MAIN( TestQgsProject )
#include "testqgsproject.moc"
