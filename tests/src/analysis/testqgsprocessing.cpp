/***************************************************************************
                         testqgsprocessing.cpp
                         ---------------------
    begin                : January 2017
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

#include "qgsprocessingregistry.h"
#include "qgsprocessingprovider.h"
#include "qgsprocessingutils.h"
#include "qgsprocessingalgorithm.h"
#include "qgsprocessingcontext.h"
#include "qgsprocessingparametertype.h"
#include "qgsprocessingmodelalgorithm.h"
#include "qgsprocessingmodelgroupbox.h"
#include "qgsnativealgorithms.h"
#include <QObject>
#include <QtTest/QSignalSpy>
#include <QList>
#include <QFileInfo>
#include "qgis.h"
#include "qgstest.h"
#include "qgsrasterlayer.h"
#include "qgsmeshlayer.h"
#include "qgsproject.h"
#include "qgspoint.h"
#include "qgsgeometry.h"
#include "qgsvectorfilewriter.h"
#include "qgsexpressioncontext.h"
#include "qgsxmlutils.h"
#include "qgsreferencedgeometry.h"
#include "qgssettings.h"
#include "qgsmessagelog.h"
#include "qgsvectorlayer.h"
#include "qgsexpressioncontextutils.h"
#include "qgsprintlayout.h"
#include "qgslayoutmanager.h"
#include "qgslayoutitemlabel.h"
#include "qgscoordinatetransformcontext.h"
#include "qgsrasterfilewriter.h"
#include "qgsprocessingparameterfieldmap.h"
#include "qgsprocessingparameteraggregate.h"
#include "qgsprocessingparametertininputlayers.h"
#include "qgsprocessingparameterdxflayers.h"
#include "qgsprocessingparametermeshdataset.h"
#include "qgsdxfexport.h"

class DummyAlgorithm : public QgsProcessingAlgorithm
{
  public:

    DummyAlgorithm( const QString &name ) : mName( name ) { mFlags = QgsProcessingAlgorithm::flags(); }

    void initAlgorithm( const QVariantMap & = QVariantMap() ) override {}
    QString name() const override { return mName; }
    QString displayName() const override { return mName; }
    QVariantMap processAlgorithm( const QVariantMap &, QgsProcessingContext &, QgsProcessingFeedback * ) override { return QVariantMap(); }

    Flags flags() const override { return mFlags; }
    DummyAlgorithm *createInstance() const override { return new DummyAlgorithm( name() ); }

    QString mName;

    Flags mFlags;

    void checkParameterVals()
    {
      addParameter( new QgsProcessingParameterString( "p1" ) );
      QVariantMap params;
      QgsProcessingContext context;

      QVERIFY( !checkParameterValues( params, context ) );
      params.insert( "p1", "a" );
      QVERIFY( checkParameterValues( params, context ) );
      // optional param
      addParameter( new QgsProcessingParameterString( "p2", QString(), QVariant(), false, true ) );
      QVERIFY( checkParameterValues( params, context ) );
      params.insert( "p2", "a" );
      QVERIFY( checkParameterValues( params, context ) );
    }

    void runParameterChecks()
    {
      QVERIFY( parameterDefinitions().isEmpty() );
      QVERIFY( addParameter( new QgsProcessingParameterBoolean( "p1" ) ) );
      QCOMPARE( parameterDefinitions().count(), 1 );
      QCOMPARE( parameterDefinitions().at( 0 )->name(), QString( "p1" ) );
      QCOMPARE( parameterDefinitions().at( 0 )->algorithm(), this );

      QVERIFY( !addParameter( nullptr ) );
      QCOMPARE( parameterDefinitions().count(), 1 );
      // duplicate name!
      QgsProcessingParameterBoolean *p2 = new QgsProcessingParameterBoolean( "p1" );
      QVERIFY( !addParameter( p2 ) );
      QCOMPARE( parameterDefinitions().count(), 1 );

      QCOMPARE( parameterDefinition( "p1" ), parameterDefinitions().at( 0 ) );
      // parameterDefinition should be case insensitive
      QCOMPARE( parameterDefinition( "P1" ), parameterDefinitions().at( 0 ) );
      QVERIFY( !parameterDefinition( "invalid" ) );

      QCOMPARE( countVisibleParameters(), 1 );
      QgsProcessingParameterBoolean *p3 = new QgsProcessingParameterBoolean( "p3" );
      QVERIFY( addParameter( p3 ) );
      QCOMPARE( countVisibleParameters(), 2 );
      QgsProcessingParameterBoolean *p4 = new QgsProcessingParameterBoolean( "p4" );
      p4->setFlags( QgsProcessingParameterDefinition::FlagHidden );
      QVERIFY( addParameter( p4 ) );
      QCOMPARE( countVisibleParameters(), 2 );


      //destination styleparameters
      QVERIFY( destinationParameterDefinitions().isEmpty() );
      QgsProcessingParameterFeatureSink *p5 = new QgsProcessingParameterFeatureSink( "p5" );
      QVERIFY( addParameter( p5, false ) );
      QCOMPARE( destinationParameterDefinitions(), QgsProcessingParameterDefinitions() << p5 );
      QgsProcessingParameterFeatureSink *p6 = new QgsProcessingParameterFeatureSink( "p6" );
      QVERIFY( addParameter( p6, false ) );
      QCOMPARE( destinationParameterDefinitions(), QgsProcessingParameterDefinitions() << p5 << p6 );

      // remove parameter
      removeParameter( "non existent" );
      removeParameter( "p6" );
      QCOMPARE( destinationParameterDefinitions(), QgsProcessingParameterDefinitions() << p5 );
      removeParameter( "p5" );
      QVERIFY( destinationParameterDefinitions().isEmpty() );

      // try with auto output creation
      QgsProcessingParameterVectorDestination *p7 = new QgsProcessingParameterVectorDestination( "p7", "my output" );
      QVERIFY( addParameter( p7 ) );
      QCOMPARE( destinationParameterDefinitions(), QgsProcessingParameterDefinitions() << p7 );
      QVERIFY( outputDefinition( "p7" ) );
      QCOMPARE( outputDefinition( "p7" )->name(), QStringLiteral( "p7" ) );
      QCOMPARE( outputDefinition( "p7" )->type(), QStringLiteral( "outputVector" ) );
      QCOMPARE( outputDefinition( "p7" )->description(), QStringLiteral( "my output" ) );

      // duplicate output name
      QVERIFY( addOutput( new QgsProcessingOutputVectorLayer( "p8" ) ) );
      QgsProcessingParameterVectorDestination *p8 = new QgsProcessingParameterVectorDestination( "p8" );
      // this should fail - it would result in a duplicate output name
      QVERIFY( !addParameter( p8 ) );

      // default vector format extension
      QgsProcessingParameterFeatureSink *sinkParam = new QgsProcessingParameterFeatureSink( "sink" );
      QCOMPARE( sinkParam->defaultFileExtension(), QStringLiteral( "gpkg" ) ); // before alg is accessible
      QVERIFY( !sinkParam->algorithm() );
      QVERIFY( !sinkParam->provider() );
      QVERIFY( addParameter( sinkParam ) );
      QCOMPARE( sinkParam->defaultFileExtension(), QStringLiteral( "gpkg" ) );
      QCOMPARE( sinkParam->algorithm(), this );
      QVERIFY( !sinkParam->provider() );

      // default raster format extension
      QgsProcessingParameterRasterDestination *rasterParam = new QgsProcessingParameterRasterDestination( "raster" );
      QCOMPARE( rasterParam->defaultFileExtension(), QStringLiteral( "tif" ) ); // before alg is accessible
      QVERIFY( addParameter( rasterParam ) );
      QCOMPARE( rasterParam->defaultFileExtension(), QStringLiteral( "tif" ) );

      // should allow parameters with same name but different case (required for grass provider)
      QgsProcessingParameterBoolean *p1C = new QgsProcessingParameterBoolean( "P1" );
      QVERIFY( addParameter( p1C ) );
      QCOMPARE( parameterDefinitions().count(), 8 );

      // remove parameter and auto created output
      QgsProcessingParameterVectorDestination *p9 = new QgsProcessingParameterVectorDestination( "p9", "output" );
      QVERIFY( addParameter( p9 ) );
      QVERIFY( outputDefinition( "p9" ) );
      QCOMPARE( outputDefinition( "p9" )->name(), QStringLiteral( "p9" ) );
      QCOMPARE( outputDefinition( "p9" )->type(), QStringLiteral( "outputVector" ) );
      removeParameter( "p9" );
      QVERIFY( !outputDefinition( "p9" ) );

      // remove parameter and check manually added output isn't removed
      QVERIFY( addParameter( new QgsProcessingParameterVectorDestination( "p10", "output" ), false ) );
      QVERIFY( addOutput( new QgsProcessingOutputVectorLayer( "p10" ) ) );
      QCOMPARE( outputDefinition( "p10" )->name(), QStringLiteral( "p10" ) );
      QCOMPARE( outputDefinition( "p10" )->type(), QStringLiteral( "outputVector" ) );
      removeParameter( "p10" );
      QVERIFY( outputDefinition( "p10" ) );

      // parameterDefinition should be case insensitive, but prioritize correct case matches
      QCOMPARE( parameterDefinition( "p1" ), parameterDefinitions().at( 0 ) );
      QCOMPARE( parameterDefinition( "P1" ), parameterDefinitions().at( 7 ) );
    }

    void runParameterChecks2()
    {
      // default vector format extension, taken from provider
      QgsProcessingParameterFeatureSink *sinkParam = new QgsProcessingParameterFeatureSink( "sink2" );
      QCOMPARE( sinkParam->defaultFileExtension(), QStringLiteral( "gpkg" ) ); // before alg is accessible
      QVERIFY( !sinkParam->algorithm() );
      QVERIFY( !sinkParam->provider() );
      QVERIFY( addParameter( sinkParam ) );
      QCOMPARE( sinkParam->defaultFileExtension(), QStringLiteral( "xshp" ) );
      QCOMPARE( sinkParam->algorithm(), this );
      QCOMPARE( sinkParam->provider(), provider() );

      // default raster format extension
      QgsProcessingParameterRasterDestination *rasterParam = new QgsProcessingParameterRasterDestination( "raster2" );
      QCOMPARE( rasterParam->defaultFileExtension(), QStringLiteral( "tif" ) ); // before alg is accessible
      QVERIFY( addParameter( rasterParam ) );
      QCOMPARE( rasterParam->defaultFileExtension(), QStringLiteral( "pcx" ) );
    }

    void runOutputChecks()
    {
      QVERIFY( outputDefinitions().isEmpty() );
      QVERIFY( addOutput( new QgsProcessingOutputVectorLayer( "p1" ) ) );
      QCOMPARE( outputDefinitions().count(), 1 );
      QCOMPARE( outputDefinitions().at( 0 )->name(), QString( "p1" ) );

      // make sure manually added outputs are not deleted by calling removeParameter
      removeParameter( "p1" );
      QCOMPARE( outputDefinitions().count(), 1 );

      QVERIFY( !addOutput( nullptr ) );
      QCOMPARE( outputDefinitions().count(), 1 );
      // duplicate name!
      QgsProcessingOutputVectorLayer *p2 = new QgsProcessingOutputVectorLayer( "p1" );
      QVERIFY( !addOutput( p2 ) );
      QCOMPARE( outputDefinitions().count(), 1 );

      QCOMPARE( outputDefinition( "p1" ), outputDefinitions().at( 0 ) );
      // parameterDefinition should be case insensitive
      QCOMPARE( outputDefinition( "P1" ), outputDefinitions().at( 0 ) );
      QVERIFY( !outputDefinition( "invalid" ) );

      QVERIFY( !hasHtmlOutputs() );
      QgsProcessingOutputHtml *p3 = new QgsProcessingOutputHtml( "p3" );
      QVERIFY( addOutput( p3 ) );
      QVERIFY( hasHtmlOutputs() );
    }

    void runValidateInputCrsChecks()
    {
      addParameter( new QgsProcessingParameterMapLayer( "p1" ) );
      addParameter( new QgsProcessingParameterMapLayer( "p2" ) );
      QVariantMap parameters;

      QgsVectorLayer *layer3111 = new QgsVectorLayer( "Point?crs=epsg:3111", "v1", "memory" );
      QgsProject p;
      p.addMapLayer( layer3111 );

      QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
      QString raster1 = testDataDir + "landsat_4326.tif";
      QFileInfo fi1( raster1 );
      QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
      QVERIFY( r1->isValid() );
      p.addMapLayer( r1 );

      QgsVectorLayer *layer4326 = new QgsVectorLayer( "Point?crs=epsg:4326", "v1", "memory" );
      p.addMapLayer( layer4326 );

      QgsProcessingContext context;
      context.setProject( &p );

      // flag not set
      mFlags = QgsProcessingAlgorithm::Flags();
      parameters.insert( "p1", QVariant::fromValue( layer3111 ) );
      QVERIFY( validateInputCrs( parameters, context ) );
      mFlags = FlagRequiresMatchingCrs;
      QVERIFY( validateInputCrs( parameters, context ) );

      // two layers, different crs
      parameters.insert( "p2", QVariant::fromValue( layer4326 ) );
      // flag not set
      mFlags = QgsProcessingAlgorithm::Flags();
      QVERIFY( validateInputCrs( parameters, context ) );
      mFlags = FlagRequiresMatchingCrs;
      QVERIFY( !validateInputCrs( parameters, context ) );

      // raster layer
      parameters.remove( "p2" );
      addParameter( new QgsProcessingParameterRasterLayer( "p3" ) );
      parameters.insert( "p3", QVariant::fromValue( r1 ) );
      QVERIFY( !validateInputCrs( parameters, context ) );

      // feature source
      parameters.remove( "p3" );
      addParameter( new QgsProcessingParameterFeatureSource( "p4" ) );
      parameters.insert( "p4", layer4326->id() );
      QVERIFY( !validateInputCrs( parameters, context ) );

      parameters.remove( "p4" );
      addParameter( new QgsProcessingParameterMultipleLayers( "p5" ) );
      parameters.insert( "p5", QVariantList() << layer4326->id() << r1->id() );
      QVERIFY( !validateInputCrs( parameters, context ) );

      // extent
      parameters.clear();
      parameters.insert( "p1", QVariant::fromValue( layer3111 ) );
      addParameter( new QgsProcessingParameterExtent( "extent" ) );
      parameters.insert( "extent", QgsReferencedRectangle( QgsRectangle( 1, 2, 3, 4 ), QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) ) );
      QVERIFY( !validateInputCrs( parameters, context ) );
      parameters.insert( "extent", QgsReferencedRectangle( QgsRectangle( 1, 2, 3, 4 ), QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:3111" ) ) ) );
      QVERIFY( validateInputCrs( parameters, context ) );

      // point
      parameters.clear();
      parameters.insert( "p1", QVariant::fromValue( layer3111 ) );
      addParameter( new QgsProcessingParameterPoint( "point" ) );
      parameters.insert( "point", QgsReferencedPointXY( QgsPointXY( 1, 2 ), QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) ) );
      QVERIFY( !validateInputCrs( parameters, context ) );
      parameters.insert( "point", QgsReferencedPointXY( QgsPointXY( 1, 2 ), QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:3111" ) ) ) );
      QVERIFY( validateInputCrs( parameters, context ) );
    }

    void runAsPythonCommandChecks()
    {
      addParameter( new QgsProcessingParameterString( "p1" ) );
      addParameter( new QgsProcessingParameterString( "p2" ) );
      QgsProcessingParameterString *hidden = new QgsProcessingParameterString( "p3" );
      hidden->setFlags( QgsProcessingParameterDefinition::FlagHidden );
      addParameter( hidden );

      QVariantMap params;
      QgsProcessingContext context;

      QCOMPARE( asPythonCommand( params, context ), QStringLiteral( "processing.run(\"test\", {})" ) );
      params.insert( "p1", "a" );
      QCOMPARE( asPythonCommand( params, context ), QStringLiteral( "processing.run(\"test\", {'p1':'a'})" ) );
      params.insert( "p2", QVariant() );
      QCOMPARE( asPythonCommand( params, context ), QStringLiteral( "processing.run(\"test\", {'p1':'a','p2':None})" ) );
      params.insert( "p2", "b" );
      QCOMPARE( asPythonCommand( params, context ), QStringLiteral( "processing.run(\"test\", {'p1':'a','p2':'b'})" ) );

      // hidden, shouldn't be shown
      params.insert( "p3", "b" );
      QCOMPARE( asPythonCommand( params, context ), QStringLiteral( "processing.run(\"test\", {'p1':'a','p2':'b'})" ) );
    }

    void addDestParams()
    {
      QgsProcessingParameterFeatureSink *sinkParam1 = new QgsProcessingParameterFeatureSink( "supports" );
      sinkParam1->setSupportsNonFileBasedOutput( true );
      addParameter( sinkParam1 );
      QgsProcessingParameterFeatureSink *sinkParam2 = new QgsProcessingParameterFeatureSink( "non_supports" );
      sinkParam2->setSupportsNonFileBasedOutput( false );
      addParameter( sinkParam2 );
    }

};

//dummy provider for testing
class DummyProvider : public QgsProcessingProvider // clazy:exclude=missing-qobject-macro
{
  public:

    DummyProvider( const QString &id ) : mId( id ) {}

    QString id() const override { return mId; }

    QString name() const override { return "dummy"; }

    void unload() override { if ( unloaded ) { *unloaded = true; } }

    QString defaultVectorFileExtension( bool ) const override
    {
      return "xshp"; // shape-X. Just like shapefiles, but to the max!
    }

    QString defaultRasterFileExtension() const override
    {
      return "pcx"; // next-gen raster storage
    }

    bool supportsNonFileBasedOutput() const override
    {
      return supportsNonFileOutputs;
    }

    bool isActive() const override
    {
      return active;
    }

    bool active = true;
    bool *unloaded = nullptr;
    bool supportsNonFileOutputs = false;

  protected:

    void loadAlgorithms() override
    {
      QVERIFY( addAlgorithm( new DummyAlgorithm( "alg1" ) ) );
      QVERIFY( addAlgorithm( new DummyAlgorithm( "alg2" ) ) );

      //dupe name
      QgsProcessingAlgorithm *a = new DummyAlgorithm( "alg1" );
      QVERIFY( !addAlgorithm( a ) );
      delete a;

      QVERIFY( !addAlgorithm( nullptr ) );
    }

    QString mId;

    friend class TestQgsProcessing;
};

class DummyProviderNoLoad : public DummyProvider // clazy:exclude=missing-qobject-macro
{
  public:

    DummyProviderNoLoad( const QString &id ) : DummyProvider( id ) {}

    bool load() override
    {
      return false;
    }

};

class DummyAlgorithm2 : public QgsProcessingAlgorithm
{
  public:

    DummyAlgorithm2( const QString &name ) : mName( name ) { mFlags = QgsProcessingAlgorithm::flags(); }

    void initAlgorithm( const QVariantMap & = QVariantMap() ) override
    {
      addParameter( new QgsProcessingParameterVectorDestination( QStringLiteral( "vector_dest" ) ) );
      addParameter( new QgsProcessingParameterRasterDestination( QStringLiteral( "raster_dest" ) ) );
      addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "sink" ) ) );
    }
    QString name() const override { return mName; }
    QString displayName() const override { return mName; }
    QVariantMap processAlgorithm( const QVariantMap &, QgsProcessingContext &, QgsProcessingFeedback * ) override { return QVariantMap(); }

    Flags flags() const override { return mFlags; }
    DummyAlgorithm2 *createInstance() const override { return new DummyAlgorithm2( name() ); }

    QString mName;

    Flags mFlags;

};

class DummyProvider3 : public QgsProcessingProvider // clazy:exclude=missing-qobject-macro
{
  public:

    DummyProvider3()  = default;
    QString id() const override { return QStringLiteral( "dummy3" ); }
    QString name() const override { return QStringLiteral( "dummy3" ); }

    QStringList supportedOutputVectorLayerExtensions() const override
    {
      return QStringList() << QStringLiteral( "mif" ) << QStringLiteral( "tab" );
    }

    QStringList supportedOutputTableExtensions() const override
    {
      return QStringList() << QStringLiteral( "dbf" );
    }

    QStringList supportedOutputRasterLayerExtensions() const override
    {
      return QStringList() << QStringLiteral( "mig" ) << QStringLiteral( "sdat" );
    }

    void loadAlgorithms() override
    {
      QVERIFY( addAlgorithm( new DummyAlgorithm2( "alg1" ) ) );
    }

};

class DummyProvider4 : public QgsProcessingProvider // clazy:exclude=missing-qobject-macro
{
  public:

    DummyProvider4()  = default;
    QString id() const override { return QStringLiteral( "dummy4" ); }
    QString name() const override { return QStringLiteral( "dummy4" ); }

    bool supportsNonFileBasedOutput() const override
    {
      return false;
    }

    QStringList supportedOutputVectorLayerExtensions() const override
    {
      return QStringList() << QStringLiteral( "mif" );
    }

    QStringList supportedOutputRasterLayerExtensions() const override
    {
      return QStringList() << QStringLiteral( "mig" );
    }

    void loadAlgorithms() override
    {
      QVERIFY( addAlgorithm( new DummyAlgorithm2( "alg1" ) ) );
    }

};

class DummyParameterType : public QgsProcessingParameterType
{


    // QgsProcessingParameterType interface
  public:
    QgsProcessingParameterDefinition *create( const QString &name ) const override
    {
      return new QgsProcessingParameterString( name );
    }

    QString description() const override
    {
      return QStringLiteral( "Description" );
    }

    QString name() const override
    {
      return QStringLiteral( "ParamType" );
    }

    QString id() const override
    {
      return QStringLiteral( "paramType" );
    }
};

class TestQgsProcessing: public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();// will be called before the first testfunction is executed.
    void cleanupTestCase(); // will be called after the last testfunction was executed.
    void init() {} // will be called before each testfunction is executed.
    void cleanup() {} // will be called after every testfunction.
    void instance();
    void addProvider();
    void providerById();
    void removeProvider();
    void compatibleLayers();
    void encodeDecodeUriProvider();
    void normalizeLayerSource();
    void context();
    void feedback();
    void mapLayers();
    void mapLayerFromStore();
    void mapLayerFromString();
    void algorithm();
    void features();
    void uniqueValues();
    void createIndex();
    void generateTemporaryDestination();
    void parseDestinationString();
    void createFeatureSink();
    void source();
    void parameters();
    void algorithmParameters();
    void algorithmOutputs();
    void parameterGeneral();
    void parameterBoolean();
    void parameterCrs();
    void parameterMapLayer();
    void parameterExtent();
    void parameterPoint();
    void parameterGeometry();
    void parameterFile();
    void parameterMatrix();
    void parameterLayerList();
    void parameterNumber();
    void parameterDistance();
    void parameterScale();
    void parameterRange();
    void parameterRasterLayer();
    void parameterEnum();
    void parameterString();
    void parameterAuthConfig();
    void parameterExpression();
    void parameterField();
    void parameterVectorLayer();
    void parameterMeshLayer();
    void parameterFeatureSource();
    void parameterFeatureSink();
    void parameterVectorOut();
    void parameterRasterOut();
    void parameterFileOut();
    void parameterFolderOut();
    void parameterBand();
    void parameterLayout();
    void parameterLayoutItem();
    void parameterColor();
    void parameterCoordinateOperation();
    void parameterMapTheme();
    void parameterDateTime();
    void parameterProviderConnection();
    void parameterDatabaseSchema();
    void parameterDatabaseTable();
    void parameterFieldMapping();
    void parameterAggregate();
    void parameterTinInputLayers();
    void parameterMeshDatasetGroups();
    void parameterMeshDatasetTime();
    void parameterDxfLayers();
    void checkParamValues();
    void combineLayerExtent();
    void processingFeatureSource();
    void processingFeatureSink();
    void algorithmScope();
    void modelScope();
    void validateInputCrs();
    void generateIteratingDestination();
    void asPythonCommand();
    void modelerAlgorithm();
    void modelExecution();
    void modelBranchPruning();
    void modelBranchPruningConditional();
    void modelWithProviderWithLimitedTypes();
    void modelVectorOutputIsCompatibleType();
    void modelAcceptableValues();
    void modelValidate();
    void modelInputs();
    void modelDependencies();
    void tempUtils();
    void convertCompatible();
    void create();
    void combineFields();
    void fieldNamesToIndices();
    void indicesToFields();
    void variantToPythonLiteral();
    void stringToPythonLiteral();
    void defaultExtensionsForProvider();
    void supportedExtensions();
    void supportsNonFileBasedOutput();
    void addParameterType();
    void removeParameterType();
    void parameterTypes();
    void parameterType();
    void sourceTypeToString_data();
    void sourceTypeToString();
    void modelSource();

  private:

};

void TestQgsProcessing::initTestCase()
{
  QgsApplication::init();
  QgsApplication::initQgis();

  // Set up the QgsSettings environment
  QCoreApplication::setOrganizationName( QStringLiteral( "QGIS" ) );
  QCoreApplication::setOrganizationDomain( QStringLiteral( "qgis.org" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "QGIS-TEST" ) );

  QgsSettings settings;
  settings.clear();

  QgsApplication::processingRegistry()->addProvider( new QgsNativeAlgorithms( QgsApplication::processingRegistry() ) );
}

void TestQgsProcessing::cleanupTestCase()
{
  QFile::remove( QDir::tempPath() + "/create_feature_sink.tab" );
  QgsVectorFileWriter::deleteShapeFile( QDir::tempPath() + "/create_feature_sink2.gpkg" );

  QgsApplication::exitQgis();
}

void TestQgsProcessing::instance()
{
  // test that application has a registry instance
  QVERIFY( QgsApplication::processingRegistry() );
}

void TestQgsProcessing::addProvider()
{
  QgsProcessingRegistry r;
  QSignalSpy spyProviderAdded( &r, &QgsProcessingRegistry::providerAdded );

  QVERIFY( r.providers().isEmpty() );

  QVERIFY( !r.addProvider( nullptr ) );

  // add a provider
  DummyProvider *p = new DummyProvider( "p1" );
  QVERIFY( r.addProvider( p ) );
  QCOMPARE( r.providers(), QList< QgsProcessingProvider * >() << p );
  QCOMPARE( spyProviderAdded.count(), 1 );
  QCOMPARE( spyProviderAdded.last().at( 0 ).toString(), QString( "p1" ) );

  //try adding another provider
  DummyProvider *p2 = new DummyProvider( "p2" );
  QVERIFY( r.addProvider( p2 ) );
  QCOMPARE( qgis::listToSet( r.providers() ), QSet< QgsProcessingProvider * >() << p << p2 );
  QCOMPARE( spyProviderAdded.count(), 2 );
  QCOMPARE( spyProviderAdded.last().at( 0 ).toString(), QString( "p2" ) );

  //try adding a provider with duplicate id
  DummyProvider *p3 = new DummyProvider( "p2" );
  QVERIFY( !r.addProvider( p3 ) );
  QCOMPARE( qgis::listToSet( r.providers() ), QSet< QgsProcessingProvider * >() << p << p2 );
  QCOMPARE( spyProviderAdded.count(), 2 );

  // test that adding a provider which does not load means it is not added to registry
  DummyProviderNoLoad *p4 = new DummyProviderNoLoad( "p4" );
  QVERIFY( !r.addProvider( p4 ) );
  QCOMPARE( qgis::listToSet( r.providers() ), QSet< QgsProcessingProvider * >() << p << p2 );
  QCOMPARE( spyProviderAdded.count(), 2 );
}

void TestQgsProcessing::providerById()
{
  QgsProcessingRegistry r;

  // no providers
  QVERIFY( !r.providerById( "p1" ) );

  // add a provider
  DummyProvider *p = new DummyProvider( "p1" );
  QVERIFY( r.addProvider( p ) );
  QCOMPARE( r.providerById( "p1" ), p );
  QVERIFY( !r.providerById( "p2" ) );

  //try adding another provider
  DummyProvider *p2 = new DummyProvider( "p2" );
  QVERIFY( r.addProvider( p2 ) );
  QCOMPARE( r.providerById( "p1" ), p );
  QCOMPARE( r.providerById( "p2" ), p2 );
  QVERIFY( !r.providerById( "p3" ) );
}

void TestQgsProcessing::removeProvider()
{
  QgsProcessingRegistry r;
  QSignalSpy spyProviderRemoved( &r, &QgsProcessingRegistry::providerRemoved );

  QVERIFY( !r.removeProvider( nullptr ) );
  QVERIFY( !r.removeProvider( "p1" ) );
  // provider not in registry
  DummyProvider *p = new DummyProvider( "p1" );
  QVERIFY( !r.removeProvider( p ) );
  QCOMPARE( spyProviderRemoved.count(), 0 );

  // add some providers
  QVERIFY( r.addProvider( p ) );
  DummyProvider *p2 = new DummyProvider( "p2" );
  QVERIFY( r.addProvider( p2 ) );

  // remove one by pointer
  bool unloaded = false;
  p->unloaded = &unloaded;
  QVERIFY( r.removeProvider( p ) );
  QCOMPARE( spyProviderRemoved.count(), 1 );
  QCOMPARE( spyProviderRemoved.last().at( 0 ).toString(), QString( "p1" ) );
  QCOMPARE( r.providers(), QList< QgsProcessingProvider * >() << p2 );

  //test that provider was unloaded
  QVERIFY( unloaded );

  // should fail, already removed
  QVERIFY( !r.removeProvider( "p1" ) );

  // remove one by id
  QVERIFY( r.removeProvider( "p2" ) );
  QCOMPARE( spyProviderRemoved.count(), 2 );
  QCOMPARE( spyProviderRemoved.last().at( 0 ).toString(), QString( "p2" ) );
  QVERIFY( r.providers().isEmpty() );
}

void TestQgsProcessing::compatibleLayers()
{
  QgsProject p;

  // add a bunch of layers to a project
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QString raster3 = testDataDir + "/raster/band1_float32_noct_epsg4326.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QVERIFY( r1->isValid() );
  QFileInfo fi2( raster2 );
  QgsRasterLayer *r2 = new QgsRasterLayer( fi2.filePath(), "ar2" );
  QVERIFY( r2->isValid() );
  QFileInfo fi3( raster3 );
  QgsRasterLayer *r3 = new QgsRasterLayer( fi3.filePath(), "zz" );
  QVERIFY( r3->isValid() );

  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon", "V4", "memory" );
  QgsVectorLayer *v2 = new QgsVectorLayer( "Point", "v1", "memory" );
  QgsVectorLayer *v3 = new QgsVectorLayer( "LineString", "v3", "memory" );
  QgsVectorLayer *v4 = new QgsVectorLayer( "none", "vvvv4", "memory" );

  QFileInfo fm( testDataDir + "/mesh/quad_and_triangle.2dm" );
  QgsMeshLayer *m1 = new QgsMeshLayer( fm.filePath(), "MX", "mdal" );
  QVERIFY( m1->isValid() );
  QgsMeshLayer *m2 = new QgsMeshLayer( fm.filePath(), "mA", "mdal" );
  QVERIFY( m2->isValid() );
  p.addMapLayers( QList<QgsMapLayer *>() << r1 << r2 << r3 << v1 << v2 << v3 << v4 << m1 << m2 );

  // compatibleRasterLayers
  QVERIFY( QgsProcessingUtils::compatibleRasterLayers( nullptr ).isEmpty() );

  // sorted
  QStringList lIds;
  Q_FOREACH ( QgsRasterLayer *rl, QgsProcessingUtils::compatibleRasterLayers( &p ) )
    lIds << rl->name();
  QCOMPARE( lIds, QStringList() << "ar2" << "R1" << "zz" );

  // unsorted
  lIds.clear();
  Q_FOREACH ( QgsRasterLayer *rl, QgsProcessingUtils::compatibleRasterLayers( &p, false ) )
    lIds << rl->name();
  QCOMPARE( lIds, QStringList() << "R1" << "ar2" << "zz" );

  // compatibleMeshLayers
  QVERIFY( QgsProcessingUtils::compatibleMeshLayers( nullptr ).isEmpty() );

  // sorted
  lIds.clear();
  Q_FOREACH ( QgsMeshLayer *rl, QgsProcessingUtils::compatibleMeshLayers( &p ) )
    lIds << rl->name();
  QCOMPARE( lIds, QStringList() << "mA" << "MX" );

  // unsorted
  lIds.clear();
  Q_FOREACH ( QgsMeshLayer *rl, QgsProcessingUtils::compatibleMeshLayers( &p, false ) )
    lIds << rl->name();
  QCOMPARE( lIds, QStringList() << "MX" << "mA" );

  // compatibleVectorLayers
  QVERIFY( QgsProcessingUtils::compatibleVectorLayers( nullptr ).isEmpty() );

  // sorted
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "v1" << "v3" << "V4" << "vvvv4" );

  // unsorted
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p, QList<int>(), false ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "V4" << "v1" << "v3" << "vvvv4" );

  // point only
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p, QList<int>() << QgsProcessing::TypeVectorPoint ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "v1" );

  // polygon only
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p, QList<int>() << QgsProcessing::TypeVectorPolygon ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "V4" );

  // line only
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p, QList<int>() << QgsProcessing::TypeVectorLine ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "v3" );

  // point and line only
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p, QList<int>() << QgsProcessing::TypeVectorPoint << QgsProcessing::TypeVectorLine ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "v1" << "v3" );

  // any vector w geometry
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p, QList<int>() << QgsProcessing::TypeVectorAnyGeometry ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "v1" << "v3" << "V4" );

  // any vector
  lIds.clear();
  Q_FOREACH ( QgsVectorLayer *vl, QgsProcessingUtils::compatibleVectorLayers( &p, QList<int>() << QgsProcessing::TypeVector ) )
    lIds << vl->name();
  QCOMPARE( lIds, QStringList() << "v1" << "v3" << "V4" << "vvvv4" );

  // all layers
  QVERIFY( QgsProcessingUtils::compatibleLayers( nullptr ).isEmpty() );

  // sorted
  lIds.clear();
  Q_FOREACH ( QgsMapLayer *l, QgsProcessingUtils::compatibleLayers( &p ) )
    lIds << l->name();
  QCOMPARE( lIds, QStringList() << "ar2" << "mA" << "MX" << "R1" << "v1" << "v3" << "V4" << "vvvv4" <<  "zz" );

  // unsorted
  lIds.clear();
  Q_FOREACH ( QgsMapLayer *l, QgsProcessingUtils::compatibleLayers( &p, false ) )
    lIds << l->name();
  QCOMPARE( lIds, QStringList() << "R1" << "ar2" << "zz"  << "V4" << "v1" << "v3" << "vvvv4" << "MX" << "mA" );
}

void TestQgsProcessing::encodeDecodeUriProvider()
{
  QString provider;
  QString uri;
  QCOMPARE( QgsProcessingUtils::encodeProviderKeyAndUri( QStringLiteral( "ogr" ), QStringLiteral( "/home/me/test.shp" ) ), QStringLiteral( "ogr:///home/me/test.shp" ) );
  QVERIFY( QgsProcessingUtils::decodeProviderKeyAndUri( QStringLiteral( "ogr:///home/me/test.shp" ), provider, uri ) );
  QCOMPARE( provider, QStringLiteral( "ogr" ) );
  QCOMPARE( uri, QStringLiteral( "/home/me/test.shp" ) );
  QCOMPARE( QgsProcessingUtils::encodeProviderKeyAndUri( QStringLiteral( "ogr" ), QStringLiteral( "http://mysourcem/a.json" ) ), QStringLiteral( "ogr://http://mysourcem/a.json" ) );
  QVERIFY( QgsProcessingUtils::decodeProviderKeyAndUri( QStringLiteral( "ogr://http://mysourcem/a.json" ), provider, uri ) );
  QCOMPARE( provider, QStringLiteral( "ogr" ) );
  QCOMPARE( uri, QStringLiteral( "http://mysourcem/a.json" ) );
  QCOMPARE( QgsProcessingUtils::encodeProviderKeyAndUri( QStringLiteral( "postgres" ), QStringLiteral( "host=blah blah etc" ) ), QStringLiteral( "postgres://host=blah blah etc" ) );
  QVERIFY( QgsProcessingUtils::decodeProviderKeyAndUri( QStringLiteral( "postgres://host=blah blah etc" ), provider, uri ) );
  QCOMPARE( provider, QStringLiteral( "postgres" ) );
  QCOMPARE( uri, QStringLiteral( "host=blah blah etc" ) );

  // should reject non valid providers
  QVERIFY( !QgsProcessingUtils::decodeProviderKeyAndUri( QStringLiteral( "asdasda://host=blah blah etc" ), provider, uri ) );
  QVERIFY( !QgsProcessingUtils::decodeProviderKeyAndUri( QStringLiteral( "http://mysourcem/a.json" ), provider, uri ) );
}

void TestQgsProcessing::normalizeLayerSource()
{
  QCOMPARE( QgsProcessingUtils::normalizeLayerSource( "data\\layers\\test.shp" ), QString( "data/layers/test.shp" ) );
  QCOMPARE( QgsProcessingUtils::normalizeLayerSource( "data\\layers \"new\"\\test.shp" ), QString( "data/layers \"new\"/test.shp" ) );
}


class TestPostProcessor : public QgsProcessingLayerPostProcessorInterface
{
  public:

    TestPostProcessor( bool *deleted )
      : deleted( deleted )
    {}

    ~TestPostProcessor() override
    {
      *deleted = true;
    }

    bool *deleted = nullptr;

    void postProcessLayer( QgsMapLayer *, QgsProcessingContext &, QgsProcessingFeedback * ) override
    {
    }
};


void TestQgsProcessing::context()
{
  QgsProcessingContext context;

  // simple tests for getters/setters
  context.setDefaultEncoding( "my_enc" );
  QCOMPARE( context.defaultEncoding(), QStringLiteral( "my_enc" ) );

  context.setFlags( QgsProcessingContext::Flags() );
  QCOMPARE( context.flags(), QgsProcessingContext::Flags() );

  QCOMPARE( context.ellipsoid(), QString() );
  QCOMPARE( context.distanceUnit(), QgsUnitTypes::DistanceUnknownUnit );
  QCOMPARE( context.areaUnit(), QgsUnitTypes::AreaUnknownUnit );

  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem( "EPSG:4536" ) );
  p.setEllipsoid( QStringLiteral( "WGS84" ) );
  p.setDistanceUnits( QgsUnitTypes::DistanceFeet );
  p.setAreaUnits( QgsUnitTypes::AreaHectares );
  context.setProject( &p );
  QCOMPARE( context.project(), &p );
  QCOMPARE( context.ellipsoid(), QStringLiteral( "WGS84" ) );
  QCOMPARE( context.distanceUnit(), QgsUnitTypes::DistanceFeet );
  QCOMPARE( context.areaUnit(), QgsUnitTypes::AreaHectares );

  // if context ellipsoid/units are already set then setting the project shouldn't overwrite them
  p.setEllipsoid( QStringLiteral( "WGS84v2" ) );
  p.setDistanceUnits( QgsUnitTypes::DistanceMiles );
  p.setAreaUnits( QgsUnitTypes::AreaAcres );
  context.setProject( &p );
  QCOMPARE( context.ellipsoid(), QStringLiteral( "WGS84" ) );
  QCOMPARE( context.distanceUnit(), QgsUnitTypes::DistanceFeet );
  QCOMPARE( context.areaUnit(), QgsUnitTypes::AreaHectares );

  context.setInvalidGeometryCheck( QgsFeatureRequest::GeometrySkipInvalid );
  QCOMPARE( context.invalidGeometryCheck(), QgsFeatureRequest::GeometrySkipInvalid );

  QgsVectorLayer *vector = new QgsVectorLayer( "Polygon", "vector", "memory" );
  context.temporaryLayerStore()->addMapLayer( vector );
  QCOMPARE( context.temporaryLayerStore()->mapLayer( vector->id() ), vector );

  QgsProcessingContext context2;
  context2.copyThreadSafeSettings( context );
  QCOMPARE( context2.defaultEncoding(), context.defaultEncoding() );
  QCOMPARE( context2.invalidGeometryCheck(), context.invalidGeometryCheck() );
  QCOMPARE( context2.flags(), context.flags() );
  QCOMPARE( context2.project(), context.project() );
  // layers from temporaryLayerStore must not be copied by copyThreadSafeSettings
  QVERIFY( context2.temporaryLayerStore()->mapLayers().isEmpty() );

  // layers to load on completion
  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon", "V1", "memory" );
  QgsVectorLayer *v2 = new QgsVectorLayer( "Polygon", "V2", "memory" );
  QVERIFY( context.layersToLoadOnCompletion().isEmpty() );
  QVERIFY( !context.willLoadLayerOnCompletion( v1->id() ) );
  QVERIFY( !context.willLoadLayerOnCompletion( v2->id() ) );
  QMap< QString, QgsProcessingContext::LayerDetails > layers;
  QgsProcessingContext::LayerDetails details( QStringLiteral( "v1" ), &p );
  bool ppDeleted = false;
  TestPostProcessor *pp = new TestPostProcessor( &ppDeleted );
  details.setPostProcessor( pp );
  layers.insert( v1->id(), details );
  context.setLayersToLoadOnCompletion( layers );
  QCOMPARE( context.layersToLoadOnCompletion().count(), 1 );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 0 ), v1->id() );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "v1" ) );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).postProcessor(), pp );
  QVERIFY( context.willLoadLayerOnCompletion( v1->id() ) );
  QCOMPARE( context.layerToLoadOnCompletionDetails( v1->id() ).name, QStringLiteral( "v1" ) );
  QVERIFY( !context.willLoadLayerOnCompletion( v2->id() ) );
  context.addLayerToLoadOnCompletion( v2->id(), QgsProcessingContext::LayerDetails( QStringLiteral( "v2" ), &p ) );
  QCOMPARE( context.layersToLoadOnCompletion().count(), 2 );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 0 ), v1->id() );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "v1" ) );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).postProcessor(), pp );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 1 ), v2->id() );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 1 ).name, QStringLiteral( "v2" ) );
  QVERIFY( !context.layersToLoadOnCompletion().values().at( 1 ).postProcessor() );
  QCOMPARE( context.layerToLoadOnCompletionDetails( v1->id() ).name, QStringLiteral( "v1" ) );
  QCOMPARE( context.layerToLoadOnCompletionDetails( v2->id() ).name, QStringLiteral( "v2" ) );
  QVERIFY( context.willLoadLayerOnCompletion( v1->id() ) );
  QVERIFY( context.willLoadLayerOnCompletion( v2->id() ) );

  // ensure that copyThreadSafeSettings doesn't copy layersToLoadOnCompletion()
  context2.copyThreadSafeSettings( context );
  QVERIFY( context2.layersToLoadOnCompletion().isEmpty() );

  layers.clear();
  layers.insert( v2->id(), QgsProcessingContext::LayerDetails( QStringLiteral( "v2" ), &p ) );
  context.setLayersToLoadOnCompletion( layers );
  QVERIFY( ppDeleted );

  QCOMPARE( context.layersToLoadOnCompletion().count(), 1 );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 0 ), v2->id() );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "v2" ) );
  context.addLayerToLoadOnCompletion( v1->id(), QgsProcessingContext::LayerDetails( QString(), &p ) );
  QCOMPARE( context.layersToLoadOnCompletion().count(), 2 );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 0 ), v1->id() );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 1 ), v2->id() );

  context.temporaryLayerStore()->addMapLayer( v1 );
  context.temporaryLayerStore()->addMapLayer( v2 );

  // test takeResultsFrom
  context2.takeResultsFrom( context );
  QVERIFY( context.temporaryLayerStore()->mapLayers().isEmpty() );
  QVERIFY( context.layersToLoadOnCompletion().isEmpty() );
  // should now be in context2
  QCOMPARE( context2.temporaryLayerStore()->mapLayer( v1->id() ), v1 );
  QCOMPARE( context2.temporaryLayerStore()->mapLayer( v2->id() ), v2 );
  QCOMPARE( context2.layersToLoadOnCompletion().count(), 2 );
  QCOMPARE( context2.layersToLoadOnCompletion().keys().at( 0 ), v1->id() );
  QCOMPARE( context2.layersToLoadOnCompletion().keys().at( 1 ), v2->id() );

  // make sure postprocessor is correctly deleted
  ppDeleted = false;
  pp = new TestPostProcessor( &ppDeleted );
  details = QgsProcessingContext::LayerDetails( QStringLiteral( "v1" ), &p );
  details.setPostProcessor( pp );
  layers.insert( v1->id(), details );
  context.setLayersToLoadOnCompletion( layers );
  // overwrite with existing
  context.setLayersToLoadOnCompletion( layers );
  QVERIFY( !ppDeleted );
  QCOMPARE( context.layerToLoadOnCompletionDetails( v1->id() ).postProcessor(), pp );
  bool pp2Deleted = false;
  TestPostProcessor *pp2 = new TestPostProcessor( &pp2Deleted );
  details = QgsProcessingContext::LayerDetails( QStringLiteral( "v1" ), &p );
  details.setPostProcessor( pp2 );
  layers.insert( v1->id(), details );
  context.setLayersToLoadOnCompletion( layers );
  QVERIFY( ppDeleted );
  QVERIFY( !pp2Deleted );
  QCOMPARE( context.layerToLoadOnCompletionDetails( v1->id() ).postProcessor(), pp2 );
  ppDeleted = false;
  pp = new TestPostProcessor( &ppDeleted );
  details = QgsProcessingContext::LayerDetails( QStringLiteral( "v1" ), &p );
  details.setPostProcessor( pp );
  context.addLayerToLoadOnCompletion( v1->id(), details );
  QVERIFY( !ppDeleted );
  QVERIFY( pp2Deleted );
  QCOMPARE( context.layerToLoadOnCompletionDetails( v1->id() ).postProcessor(), pp );
  pp2Deleted = false;
  pp2 = new TestPostProcessor( &pp2Deleted );
  context.layerToLoadOnCompletionDetails( v1->id() ).setPostProcessor( pp2 );
  QVERIFY( ppDeleted );
  QVERIFY( !pp2Deleted );
  QCOMPARE( context.layerToLoadOnCompletionDetails( v1->id() ).postProcessor(), pp2 );

  // take result layer
  QgsMapLayer *result = context2.takeResultLayer( v1->id() );
  QCOMPARE( result, v1 );
  QString id = v1->id();
  delete v1;
  QVERIFY( !context2.temporaryLayerStore()->mapLayer( id ) );
  QVERIFY( !context2.takeResultLayer( id ) );
  result = context2.takeResultLayer( v2->id() );
  QCOMPARE( result, v2 );
  id = v2->id();
  delete v2;
  QVERIFY( !context2.temporaryLayerStore()->mapLayer( id ) );
}

void TestQgsProcessing::feedback()
{
  QgsProcessingFeedback f;
  f.pushInfo( QStringLiteral( "info" ) );
  f.reportError( QStringLiteral( "error" ) );
  f.pushDebugInfo( QStringLiteral( "debug" ) );
  f.pushCommandInfo( QStringLiteral( "command" ) );
  f.pushConsoleInfo( QStringLiteral( "console" ) );

  QCOMPARE( f.htmlLog(), QStringLiteral( "info<br/><span style=\"color:red\">error</span><br/><span style=\"color:#777\">debug</span><br/><code>command</code><br/><code style=\"color:#777\">console</code><br/>" ) );
  QCOMPARE( f.textLog(), QStringLiteral( "info\nerror\ndebug\ncommand\nconsole\n" ) );
}

void TestQgsProcessing::mapLayers()
{
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster = testDataDir + "landsat.tif";
  QString vector = testDataDir + "points.shp";

  // test loadMapLayerFromString with raster
  QgsMapLayer *l = QgsProcessingUtils::loadMapLayerFromString( raster, QgsCoordinateTransformContext() );
  QVERIFY( l->isValid() );
  QCOMPARE( l->type(), QgsMapLayerType::RasterLayer );
  QCOMPARE( l->name(), QStringLiteral( "landsat" ) );

  delete l;

  // use encoded provider/uri string
  l = QgsProcessingUtils::loadMapLayerFromString( QStringLiteral( "gdal://%1" ).arg( raster ), QgsCoordinateTransformContext() );
  QVERIFY( l->isValid() );
  QCOMPARE( l->type(), QgsMapLayerType::RasterLayer );
  QCOMPARE( l->name(), QStringLiteral( "landsat" ) );
  delete l;

  //test with vector
  l = QgsProcessingUtils::loadMapLayerFromString( vector, QgsCoordinateTransformContext() );
  QVERIFY( l->isValid() );
  QCOMPARE( l->type(), QgsMapLayerType::VectorLayer );
  QCOMPARE( l->name(), QStringLiteral( "points" ) );
  delete l;

  // use encoded provider/uri string
  l = QgsProcessingUtils::loadMapLayerFromString( QStringLiteral( "ogr://%1" ).arg( vector ), QgsCoordinateTransformContext() );
  QVERIFY( l->isValid() );
  QCOMPARE( l->type(), QgsMapLayerType::VectorLayer );
  QCOMPARE( l->name(), QStringLiteral( "points" ) );
  delete l;

  l = QgsProcessingUtils::loadMapLayerFromString( QString(), QgsCoordinateTransformContext() );
  QVERIFY( !l );
  l = QgsProcessingUtils::loadMapLayerFromString( QStringLiteral( "so much room for activities!" ), QgsCoordinateTransformContext() );
  QVERIFY( !l );
  l = QgsProcessingUtils::loadMapLayerFromString( testDataDir + "multipoint.shp", QgsCoordinateTransformContext() );
  QVERIFY( l->isValid() );
  QCOMPARE( l->type(), QgsMapLayerType::VectorLayer );
  QCOMPARE( l->name(), QStringLiteral( "multipoint" ) );
  delete l;

  // Test layers from a string with parameters
  QString osmFilePath = testDataDir + "openstreetmap/testdata.xml";
  std::unique_ptr< QgsVectorLayer > osm( qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::loadMapLayerFromString( osmFilePath, QgsCoordinateTransformContext() ) ) );
  QVERIFY( osm->isValid() );
  QCOMPARE( osm->geometryType(), QgsWkbTypes::PointGeometry );

  osm.reset( qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::loadMapLayerFromString( osmFilePath + "|layerid=3", QgsCoordinateTransformContext() ) ) );
  QVERIFY( osm->isValid() );
  QCOMPARE( osm->geometryType(), QgsWkbTypes::PolygonGeometry );

  osm.reset( qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::loadMapLayerFromString( osmFilePath + "|layerid=3|subset=\"building\" is not null", QgsCoordinateTransformContext() ) ) );
  QVERIFY( osm->isValid() );
  QCOMPARE( osm->geometryType(), QgsWkbTypes::PolygonGeometry );
  QCOMPARE( osm->subsetString(), QStringLiteral( "\"building\" is not null" ) );
}

void TestQgsProcessing::mapLayerFromStore()
{
  // test mapLayerFromStore

  QgsMapLayerStore store;

  // add a bunch of layers to a project
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QVERIFY( r1->isValid() );
  QFileInfo fi2( raster2 );
  QgsRasterLayer *r2 = new QgsRasterLayer( fi2.filePath(), "ar2" );
  QVERIFY( r2->isValid() );

  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon", "V4", "memory" );
  QgsVectorLayer *v2 = new QgsVectorLayer( "Point", "v1", "memory" );
  store.addMapLayers( QList<QgsMapLayer *>() << r1 << r2 << v1 << v2 );

  QVERIFY( ! QgsProcessingUtils::mapLayerFromStore( QString(), nullptr ) );
  QVERIFY( ! QgsProcessingUtils::mapLayerFromStore( QStringLiteral( "v1" ), nullptr ) );
  QVERIFY( ! QgsProcessingUtils::mapLayerFromStore( QString(), &store ) );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( raster1, &store ), r1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( raster2, &store ), r2 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( "R1", &store ), r1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( "ar2", &store ), r2 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( "V4", &store ), v1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( "v1", &store ), v2 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( r1->id(), &store ), r1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromStore( v1->id(), &store ), v1 );
}

void TestQgsProcessing::mapLayerFromString()
{
  // test mapLayerFromString

  QgsProcessingContext c;
  QgsProject p;

  // add a bunch of layers to a project
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QVERIFY( r1->isValid() );
  QFileInfo fi2( raster2 );
  QgsRasterLayer *r2 = new QgsRasterLayer( fi2.filePath(), "ar2" );
  QVERIFY( r2->isValid() );

  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon", "V4", "memory" );
  QgsVectorLayer *v2 = new QgsVectorLayer( "Point", "v1", "memory" );
  p.addMapLayers( QList<QgsMapLayer *>() << r1 << r2 << v1 << v2 );

  // no project set yet
  QVERIFY( ! QgsProcessingUtils::mapLayerFromString( QString(), c ) );
  QVERIFY( !c.getMapLayer( QString() ) );
  QVERIFY( ! QgsProcessingUtils::mapLayerFromString( QStringLiteral( "v1" ), c ) );
  QVERIFY( !c.getMapLayer( QStringLiteral( "v1" ) ) );

  c.setProject( &p );

  // layers from current project
  QVERIFY( ! QgsProcessingUtils::mapLayerFromString( QString(), c ) );
  QVERIFY( !c.getMapLayer( QString() ) );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( raster1, c ), r1 );
  QCOMPARE( c.getMapLayer( raster1 ), r1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( raster1, c, true, QgsProcessingUtils::LayerHint::Raster ), r1 );
  QVERIFY( !QgsProcessingUtils::mapLayerFromString( raster1, c, true, QgsProcessingUtils::LayerHint::Vector ) );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( raster2, c ), r2 );
  QCOMPARE( c.getMapLayer( raster2 ), r2 );
  QVERIFY( !QgsProcessingUtils::mapLayerFromString( raster2, c, true, QgsProcessingUtils::LayerHint::Vector ) );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "R1", c ), r1 );
  QCOMPARE( c.getMapLayer( "R1" ), r1 );
  QVERIFY( !QgsProcessingUtils::mapLayerFromString( "R1", c, true, QgsProcessingUtils::LayerHint::Vector ) );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "ar2", c ), r2 );
  QCOMPARE( c.getMapLayer( "ar2" ), r2 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "V4", c ), v1 );
  QCOMPARE( c.getMapLayer( "V4" ), v1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "V4", c, true, QgsProcessingUtils::LayerHint::Vector ), v1 );
  QVERIFY( !QgsProcessingUtils::mapLayerFromString( "V4", c, true, QgsProcessingUtils::LayerHint::Raster ) );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "v1", c ), v2 );
  QCOMPARE( c.getMapLayer( "v1" ), v2 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( r1->id(), c ), r1 );
  QCOMPARE( c.getMapLayer( r1->id() ), r1 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( v1->id(), c ), v1 );
  QCOMPARE( c.getMapLayer( v1->id() ), v1 );

  // check that layers in context temporary store are used
  QgsVectorLayer *v5 = new QgsVectorLayer( "Polygon", "V5", "memory" );
  QgsVectorLayer *v6 = new QgsVectorLayer( "Point", "v6", "memory" );
  c.temporaryLayerStore()->addMapLayers( QList<QgsMapLayer *>() << v5 << v6 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "V5", c ), v5 );
  QCOMPARE( c.getMapLayer( "V5" ), v5 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "V5", c, true, QgsProcessingUtils::LayerHint::Vector ), v5 );
  QVERIFY( !QgsProcessingUtils::mapLayerFromString( "V5", c, true, QgsProcessingUtils::LayerHint::Raster ) );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( "v6", c ), v6 );
  QCOMPARE( c.getMapLayer( "v6" ), v6 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( v5->id(), c ), v5 );
  QCOMPARE( c.getMapLayer( v5->id() ), v5 );
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( v6->id(), c ), v6 );
  QCOMPARE( c.getMapLayer( v6->id() ), v6 );
  QVERIFY( ! QgsProcessingUtils::mapLayerFromString( "aaaaa", c ) );
  QVERIFY( !c.getMapLayer( "aaaa" ) );

  // if specified, check that layers can be loaded
  QVERIFY( ! QgsProcessingUtils::mapLayerFromString( "aaaaa", c ) );
  QString newRaster = testDataDir + "requires_warped_vrt.tif";
  // don't allow loading
  QVERIFY( ! QgsProcessingUtils::mapLayerFromString( newRaster, c, false ) );
  QVERIFY( !c.getMapLayer( newRaster ) );
  // allow loading
  QgsMapLayer *loadedLayer = QgsProcessingUtils::mapLayerFromString( newRaster, c, true );
  QVERIFY( loadedLayer->isValid() );
  QCOMPARE( loadedLayer->type(), QgsMapLayerType::RasterLayer );
  // should now be in temporary store
  QCOMPARE( c.temporaryLayerStore()->mapLayer( loadedLayer->id() ), loadedLayer );

  // since it's now in temporary store, should be accessible even if we deny loading new layers
  QCOMPARE( QgsProcessingUtils::mapLayerFromString( newRaster, c, false ), loadedLayer );
  QCOMPARE( c.getMapLayer( newRaster ), loadedLayer );
}

void TestQgsProcessing::algorithm()
{
  DummyAlgorithm alg( "test" );
  DummyProvider *p = new DummyProvider( "p1" );
  QCOMPARE( alg.id(), QString( "test" ) );
  alg.setProvider( p );
  QCOMPARE( alg.provider(), p );
  QCOMPARE( alg.id(), QString( "p1:test" ) );

  QVERIFY( p->algorithms().isEmpty() );

  QSignalSpy providerRefreshed( p, &DummyProvider::algorithmsLoaded );
  p->refreshAlgorithms();
  QCOMPARE( providerRefreshed.count(), 1 );

  for ( int i = 0; i < 2; ++i )
  {
    QCOMPARE( p->algorithms().size(), 2 );
    QCOMPARE( p->algorithm( "alg1" )->name(), QStringLiteral( "alg1" ) );
    QCOMPARE( p->algorithm( "alg1" )->provider(), p );
    QCOMPARE( p->algorithm( "alg2" )->provider(), p );
    QCOMPARE( p->algorithm( "alg2" )->name(), QStringLiteral( "alg2" ) );
    QVERIFY( !p->algorithm( "aaaa" ) );
    QVERIFY( p->algorithms().contains( p->algorithm( "alg1" ) ) );
    QVERIFY( p->algorithms().contains( p->algorithm( "alg2" ) ) );

    // reload, then retest on next loop
    // must be safe for providers to reload their algorithms
    p->refreshAlgorithms();
    QCOMPARE( providerRefreshed.count(), 2 + i );
  }

  // inactive provider, should not load algorithms
  p->active = false;
  p->refreshAlgorithms();
  QCOMPARE( providerRefreshed.count(), 3 );
  QVERIFY( p->algorithms().empty() );
  p->active = true;
  p->refreshAlgorithms();
  QCOMPARE( providerRefreshed.count(), 4 );
  QVERIFY( !p->algorithms().empty() );

  QgsProcessingRegistry r;
  QVERIFY( r.addProvider( p ) );
  QCOMPARE( r.algorithms().size(), 2 );
  QVERIFY( r.algorithms().contains( p->algorithm( "alg1" ) ) );
  QVERIFY( r.algorithms().contains( p->algorithm( "alg2" ) ) );

  // algorithmById
  QCOMPARE( r.algorithmById( "p1:alg1" ), p->algorithm( "alg1" ) );
  QCOMPARE( r.algorithmById( "p1:alg2" ), p->algorithm( "alg2" ) );
  QVERIFY( !r.algorithmById( "p1:alg3" ) );
  QVERIFY( !r.algorithmById( "px:alg1" ) );

  // alias support
  QVERIFY( !r.algorithmById( QStringLiteral( "fake:fakealg" ) ) );
  r.addAlgorithmAlias( QStringLiteral( "fake:fakealg" ), QStringLiteral( "nope:none" ) );
  QVERIFY( !r.algorithmById( QStringLiteral( "fake:fakealg" ) ) );
  r.addAlgorithmAlias( QStringLiteral( "fake:fakealg" ), QStringLiteral( "p1:alg1" ) );
  QCOMPARE( r.algorithmById( QStringLiteral( "fake:fakealg" ) ), p->algorithm( "alg1" ) );

  // test that algorithmById can transparently map 'qgis' algorithms across to matching 'native' algorithms
  // this allows us the freedom to convert qgis python algs to c++ without breaking api or existing models
  QCOMPARE( QgsApplication::processingRegistry()->algorithmById( QStringLiteral( "qgis:dissolve" ) )->id(), QStringLiteral( "native:dissolve" ) );
  QCOMPARE( QgsApplication::processingRegistry()->algorithmById( QStringLiteral( "qgis:clip" ) )->id(), QStringLiteral( "native:clip" ) );
  QVERIFY( !QgsApplication::processingRegistry()->algorithmById( QStringLiteral( "qgis:notanalg" ) ) );

  // createAlgorithmById
  QVERIFY( !r.createAlgorithmById( "p1:alg3" ) );
  std::unique_ptr< QgsProcessingAlgorithm > creation( r.createAlgorithmById( "p1:alg1" ) );
  QVERIFY( creation.get() );
  QCOMPARE( creation->provider()->id(), QStringLiteral( "p1" ) );
  QCOMPARE( creation->id(), QStringLiteral( "p1:alg1" ) );
  creation.reset( r.createAlgorithmById( "p1:alg2" ) );
  QVERIFY( creation.get() );
  QCOMPARE( creation->provider()->id(), QStringLiteral( "p1" ) );
  QCOMPARE( creation->id(), QStringLiteral( "p1:alg2" ) );

  //test that loading a provider triggers an algorithm refresh
  DummyProvider *p2 = new DummyProvider( "p2" );
  QVERIFY( p2->algorithms().isEmpty() );
  p2->load();
  QCOMPARE( p2->algorithms().size(), 2 );
  delete p2;

  // test that adding a provider to the registry automatically refreshes algorithms (via load)
  DummyProvider *p3 = new DummyProvider( "p3" );
  QVERIFY( p3->algorithms().isEmpty() );
  QVERIFY( r.addProvider( p3 ) );
  QCOMPARE( p3->algorithms().size(), 2 );
}

void TestQgsProcessing::features()
{
  QgsVectorLayer *layer = new QgsVectorLayer( "Point", "v1", "memory" );
  for ( int i = 1; i < 6; ++i )
  {
    QgsFeature f( i );
    f.setGeometry( QgsGeometry( new QgsPoint( 1, 2 ) ) );
    layer->dataProvider()->addFeatures( QgsFeatureList() << f );
  }

  QgsProject p;
  p.addMapLayer( layer );

  QgsProcessingContext context;
  context.setProject( &p );
  // disable check for geometry validity
  context.setFlags( QgsProcessingContext::Flags() );

  std::function< QgsFeatureIds( QgsFeatureIterator it ) > getIds = []( QgsFeatureIterator it )
  {
    QgsFeature f;
    QgsFeatureIds ids;
    while ( it.nextFeature( f ) )
    {
      ids << f.id();
    }
    return ids;
  };

  std::unique_ptr< QgsProcessingParameterDefinition > def( new QgsProcessingParameterString( QStringLiteral( "layer" ) ) );
  QVariantMap params;
  params.insert( QStringLiteral( "layer" ), layer->id() );

  std::unique_ptr< QgsFeatureSource > source( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );

  // test with all features
  QgsFeatureIds ids = getIds( source->getFeatures() );
  QCOMPARE( ids, QgsFeatureIds() << 1 << 2 << 3 << 4 << 5 );
  QCOMPARE( source->featureCount(), 5L );

  // test with selected features
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), true ) ) );
  layer->selectByIds( QgsFeatureIds() << 2 << 4 );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures() );
  QCOMPARE( ids, QgsFeatureIds() << 2 << 4 );
  QCOMPARE( source->featureCount(), 2L );

  // selection, but not using selected features
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), false ) ) );
  layer->selectByIds( QgsFeatureIds() << 2 << 4 );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures() );
  QCOMPARE( ids, QgsFeatureIds() << 1 << 2 << 3 << 4 << 5 );
  QCOMPARE( source->featureCount(), 5L );

  // using selected features, but no selection
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), true ) ) );
  layer->removeSelection();
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures() );
  QVERIFY( ids.isEmpty() );
  QCOMPARE( source->featureCount(), 0L );

  // feature limit
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), false, 3 ) ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures() );
  QCOMPARE( ids.size(), 3 );
  QCOMPARE( source->featureCount(), 3L );

  // test that feature request is honored
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), false ) ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures( QgsFeatureRequest().setFilterFids( QgsFeatureIds() << 1 << 3 << 5 ) ) );
  QCOMPARE( ids, QgsFeatureIds() << 1 << 3 << 5 );

  // count is only rough - but we expect (for now) to see full layer count
  QCOMPARE( source->featureCount(), 5L );

  //test that feature request is honored when using selections
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), true ) ) );
  layer->selectByIds( QgsFeatureIds() << 2 << 4 );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures( QgsFeatureRequest().setFlags( QgsFeatureRequest::NoGeometry ) ) );
  QCOMPARE( ids, QgsFeatureIds() << 2 << 4 );

  // test callback is hit when filtering invalid geoms
  bool encountered = false;
  std::function< void( const QgsFeature & ) > callback = [ &encountered ]( const QgsFeature & )
  {
    encountered = true;
  };

  context.setInvalidGeometryCheck( QgsFeatureRequest::GeometryAbortOnInvalid );
  context.setInvalidGeometryCallback( callback );
  QgsVectorLayer *polyLayer = new QgsVectorLayer( "Polygon", "v2", "memory" );
  QgsFeature f;
  f.setGeometry( QgsGeometry::fromWkt( QStringLiteral( "Polygon((0 0, 1 0, 0 1, 1 1, 0 0))" ) ) );
  polyLayer->dataProvider()->addFeatures( QgsFeatureList() << f );
  p.addMapLayer( polyLayer );
  params.insert( QStringLiteral( "layer" ), polyLayer->id() );

  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures() );
  QVERIFY( encountered );

  encountered = false;
  context.setInvalidGeometryCheck( QgsFeatureRequest::GeometryNoCheck );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures() );
  QVERIFY( !encountered );

  // context wants to filter, but filtering disabled on source definition
  context.setInvalidGeometryCheck( QgsFeatureRequest::GeometryAbortOnInvalid );
  context.setInvalidGeometryCallback( callback );
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( polyLayer->id(), false, -1, QgsProcessingFeatureSourceDefinition::Flag::FlagOverrideDefaultGeometryCheck, QgsFeatureRequest::GeometryNoCheck ) ) );

  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  ids = getIds( source->getFeatures() );
  QVERIFY( !encountered );

  QgsProcessingContext context2;
  // context wants to skip, source wants to abort
  context2.setInvalidGeometryCheck( QgsFeatureRequest::GeometrySkipInvalid );
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( polyLayer->id(), false, -1, QgsProcessingFeatureSourceDefinition::Flag::FlagOverrideDefaultGeometryCheck, QgsFeatureRequest::GeometryAbortOnInvalid ) ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  try
  {
    ids = getIds( source->getFeatures() );
    QVERIFY( false );
  }
  catch ( QgsProcessingException & )
  {}

  // equality operator
  QVERIFY( QgsProcessingFeatureSourceDefinition( layer->id(), true ) == QgsProcessingFeatureSourceDefinition( layer->id(), true ) );
  QVERIFY( QgsProcessingFeatureSourceDefinition( layer->id(), true ) != QgsProcessingFeatureSourceDefinition( "b", true ) );
  QVERIFY( QgsProcessingFeatureSourceDefinition( layer->id(), true ) != QgsProcessingFeatureSourceDefinition( layer->id(), false ) );
  QVERIFY( QgsProcessingFeatureSourceDefinition( layer->id(), true ) != QgsProcessingFeatureSourceDefinition( layer->id(), true, 5 ) );
  QVERIFY( QgsProcessingFeatureSourceDefinition( layer->id(), true, 5, QgsProcessingFeatureSourceDefinition::Flags() ) != QgsProcessingFeatureSourceDefinition( layer->id(), true, 5, QgsProcessingFeatureSourceDefinition::Flag::FlagOverrideDefaultGeometryCheck ) );
  QVERIFY( QgsProcessingFeatureSourceDefinition( layer->id(), true, 5, QgsProcessingFeatureSourceDefinition::Flag::FlagOverrideDefaultGeometryCheck, QgsFeatureRequest::GeometrySkipInvalid ) != QgsProcessingFeatureSourceDefinition( layer->id(), true, 5, QgsProcessingFeatureSourceDefinition::Flag::FlagOverrideDefaultGeometryCheck, QgsFeatureRequest::GeometryAbortOnInvalid ) );
}

void TestQgsProcessing::uniqueValues()
{
  QgsVectorLayer *layer = new QgsVectorLayer( "Point?field=a:integer&field=b:string", "v1", "memory" );
  for ( int i = 0; i < 6; ++i )
  {
    QgsFeature f( i );
    f.setAttributes( QgsAttributes() << i % 3 + 1 << QString( QChar( ( i % 3 ) + 65 ) ) );
    layer->dataProvider()->addFeatures( QgsFeatureList() << f );
  }

  QgsProcessingContext context;
  context.setFlags( QgsProcessingContext::Flags() );

  QgsProject p;
  p.addMapLayer( layer );
  context.setProject( &p );

  std::unique_ptr< QgsProcessingParameterDefinition > def( new QgsProcessingParameterString( QStringLiteral( "layer" ) ) );
  QVariantMap params;
  params.insert( QStringLiteral( "layer" ), layer->id() );

  std::unique_ptr< QgsFeatureSource > source( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );

  // some bad checks
  QVERIFY( source->uniqueValues( -1 ).isEmpty() );
  QVERIFY( source->uniqueValues( 10001 ).isEmpty() );

  // good checks
  QSet< QVariant > vals = source->uniqueValues( 0 );
  QCOMPARE( vals.count(), 3 );
  QVERIFY( vals.contains( 1 ) );
  QVERIFY( vals.contains( 2 ) );
  QVERIFY( vals.contains( 3 ) );
  vals = source->uniqueValues( 1 );
  QCOMPARE( vals.count(), 3 );
  QVERIFY( vals.contains( QString( "A" ) ) );
  QVERIFY( vals.contains( QString( "B" ) ) );
  QVERIFY( vals.contains( QString( "C" ) ) );

  //using only selected features
  layer->selectByIds( QgsFeatureIds() << 1 << 2 << 4 );
  // but not using selection yet...
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  vals = source->uniqueValues( 0 );
  QCOMPARE( vals.count(), 3 );
  QVERIFY( vals.contains( 1 ) );
  QVERIFY( vals.contains( 2 ) );
  QVERIFY( vals.contains( 3 ) );
  vals = source->uniqueValues( 1 );
  QCOMPARE( vals.count(), 3 );
  QVERIFY( vals.contains( QString( "A" ) ) );
  QVERIFY( vals.contains( QString( "B" ) ) );
  QVERIFY( vals.contains( QString( "C" ) ) );

  // selection and using selection
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), true ) ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  QVERIFY( source->uniqueValues( -1 ).isEmpty() );
  QVERIFY( source->uniqueValues( 10001 ).isEmpty() );
  vals = source->uniqueValues( 0 );
  QCOMPARE( vals.count(), 2 );
  QVERIFY( vals.contains( 1 ) );
  QVERIFY( vals.contains( 2 ) );
  vals = source->uniqueValues( 1 );
  QCOMPARE( vals.count(), 2 );
  QVERIFY( vals.contains( QString( "A" ) ) );
  QVERIFY( vals.contains( QString( "B" ) ) );
}

void TestQgsProcessing::createIndex()
{
  QgsVectorLayer *layer = new QgsVectorLayer( "Point", "v1", "memory" );
  for ( int i = 1; i < 6; ++i )
  {
    QgsFeature f( i );
    f.setGeometry( QgsGeometry( new QgsPoint( i, 2 ) ) );
    layer->dataProvider()->addFeatures( QgsFeatureList() << f );
  }

  QgsProcessingContext context;
  QgsProject p;
  p.addMapLayer( layer );
  context.setProject( &p );

  std::unique_ptr< QgsProcessingParameterDefinition > def( new QgsProcessingParameterString( QStringLiteral( "layer" ) ) );
  QVariantMap params;
  params.insert( QStringLiteral( "layer" ), layer->id() );

  // disable selected features check
  std::unique_ptr< QgsFeatureSource > source( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  QVERIFY( source.get() );
  QgsSpatialIndex index( *source );
  QList<QgsFeatureId> ids = index.nearestNeighbor( QgsPointXY( 2.1, 2 ), 1 );
  QCOMPARE( ids, QList<QgsFeatureId>() << 2 );

  // selected features check, but none selected
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), true ) ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  index = QgsSpatialIndex( *source );
  ids = index.nearestNeighbor( QgsPointXY( 2.1, 2 ), 1 );
  QCOMPARE( ids, QList<QgsFeatureId>() );

  // create selection
  layer->selectByIds( QgsFeatureIds() << 4 << 5 );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  index = QgsSpatialIndex( *source );
  ids = index.nearestNeighbor( QgsPointXY( 2.1, 2 ), 1 );
  QCOMPARE( ids, QList<QgsFeatureId>() << 4 );

  // selection but not using selection mode
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( QgsProcessingFeatureSourceDefinition( layer->id(), false ) ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  index = QgsSpatialIndex( *source );
  ids = index.nearestNeighbor( QgsPointXY( 2.1, 2 ), 1 );
  QCOMPARE( ids, QList<QgsFeatureId>() << 2 );
}

void TestQgsProcessing::generateTemporaryDestination()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QgsProcessingContext context;
  context.setProject( &p );

  // destination vector with "." in it's name
  std::unique_ptr< QgsProcessingParameterVectorDestination > def( new QgsProcessingParameterVectorDestination( "with.inside", QString(), QgsProcessing::TypeVectorAnyGeometry, QString(), false ) );

  // check that temporary destination does not have dot at the end when there is no extension
  QVERIFY( !def->generateTemporaryDestination().endsWith( QLatin1Char( '.' ) ) );
  // check that temporary destination starts with tempFolder
  QVERIFY( def->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );
  // check that extension with QFileInfo::completeSuffix is "gpkg"
  QFileInfo f = QFileInfo( def->generateTemporaryDestination() );
  QCOMPARE( f.completeSuffix(), QString( "gpkg" ) );

  // destination raster with "." in it's name
  std::unique_ptr< QgsProcessingParameterRasterDestination > def2( new QgsProcessingParameterRasterDestination( "with.inside", QString(), QString(), false ) );

  // check that temporary destination does not have dot at the end when there is no extension
  QVERIFY( !def2->generateTemporaryDestination().endsWith( QLatin1Char( '.' ) ) );
  // check that temporary destination starts with tempFolder
  QVERIFY( def2->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );
  // check that extension with QFileInfo::completeSuffix is "tif"
  f = QFileInfo( def2->generateTemporaryDestination() );
  QCOMPARE( f.completeSuffix(), QString( "tif" ) );

  // destination vector without "." in it's name
  std::unique_ptr< QgsProcessingParameterVectorDestination > def3( new QgsProcessingParameterVectorDestination( "without_inside", QString(), QgsProcessing::TypeVectorAnyGeometry, QString(), false ) );

  // check that temporary destination does not have dot at the end when there is no extension
  QVERIFY( !def3->generateTemporaryDestination().endsWith( QLatin1Char( '.' ) ) );
  // check that temporary destination starts with tempFolder
  QVERIFY( def3->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );
  // check that extension with QFileInfo::completeSuffix is "gpkg"
  f = QFileInfo( def3->generateTemporaryDestination() );
  QCOMPARE( f.completeSuffix(), QString( "gpkg" ) );

}

void TestQgsProcessing::parseDestinationString()
{
  QString providerKey;
  QString uri;
  QString layerName;
  QString format;
  QVariantMap options;
  QString extension;
  bool useWriter = false;

  // simple shapefile output
  QString destination = QStringLiteral( "d:/test.shp" );
  QgsProcessingUtils::parseDestinationString( destination, providerKey, uri, layerName, format, options, useWriter, extension );
  QCOMPARE( destination, QStringLiteral( "d:/test.shp" ) );
  QCOMPARE( providerKey, QStringLiteral( "ogr" ) );
  QCOMPARE( uri, QStringLiteral( "d:/test.shp" ) );
  QCOMPARE( format, QStringLiteral( "ESRI Shapefile" ) );
  QCOMPARE( extension, QStringLiteral( "shp" ) );
  QVERIFY( useWriter );

  // postgis output
  destination = QStringLiteral( "postgis:dbname='db' host=DBHOST port=5432 table=\"calcs\".\"output\" (geom) sql=" );
  QgsProcessingUtils::parseDestinationString( destination, providerKey, uri, layerName, format, options, useWriter, extension );
  QCOMPARE( providerKey, QStringLiteral( "postgres" ) );
  QCOMPARE( uri, QStringLiteral( "dbname='db' host=DBHOST port=5432 table=\"calcs\".\"output\" (geom) sql=" ) );
  QVERIFY( !useWriter );
  QVERIFY( extension.isEmpty() );
  // postgres key should also work
  destination = QStringLiteral( "postgres:dbname='db' host=DBHOST port=5432 table=\"calcs\".\"output\" (geom) sql=" );
  QgsProcessingUtils::parseDestinationString( destination, providerKey, uri, layerName, format, options, useWriter, extension );
  QCOMPARE( providerKey, QStringLiteral( "postgres" ) );
  QCOMPARE( uri, QStringLiteral( "dbname='db' host=DBHOST port=5432 table=\"calcs\".\"output\" (geom) sql=" ) );
  QVERIFY( !useWriter );
  QVERIFY( extension.isEmpty() );

  // newer format
  destination = QStringLiteral( "postgres://dbname='db' host=DBHOST port=5432 table=\"calcs\".\"output\" (geom) sql=" );
  QgsProcessingUtils::parseDestinationString( destination, providerKey, uri, layerName, format, options, useWriter, extension );
  QCOMPARE( providerKey, QStringLiteral( "postgres" ) );
  QCOMPARE( uri, QStringLiteral( "dbname='db' host=DBHOST port=5432 table=\"calcs\".\"output\" (geom) sql=" ) );
  QVERIFY( !useWriter );
  QVERIFY( extension.isEmpty() );
  //mssql
  destination = QStringLiteral( "mssql://dbname='db' host=DBHOST port=5432 table=\"calcs\".\"output\" (geom) sql=" );
  QgsProcessingUtils::parseDestinationString( destination, providerKey, uri, layerName, format, options, useWriter, extension );
  QCOMPARE( providerKey, QStringLiteral( "mssql" ) );
  QCOMPARE( uri, QStringLiteral( "dbname='db' host=DBHOST port=5432 table=\"calcs\".\"output\" (geom) sql=" ) );
  QVERIFY( !useWriter );
  QVERIFY( extension.isEmpty() );

  // full uri shp output
  options.clear();
  destination = QStringLiteral( "ogr:d:/test.shp" );
  QgsProcessingUtils::parseDestinationString( destination, providerKey, uri, layerName, format, options, useWriter, extension );
  QCOMPARE( providerKey, QStringLiteral( "ogr" ) );
  QCOMPARE( uri, QStringLiteral( "d:/test.shp" ) );
  QCOMPARE( options.value( QStringLiteral( "update" ) ).toBool(), true );
  QCOMPARE( options.value( QStringLiteral( "driverName" ) ).toString(), QStringLiteral( "ESRI Shapefile" ) );
  QVERIFY( !options.contains( QStringLiteral( "layerName" ) ) );
  QVERIFY( !useWriter );
  QCOMPARE( extension, QStringLiteral( "shp" ) );

  // full uri geopackage output
  options.clear();
  destination = QStringLiteral( "ogr:d:/test.gpkg" );
  QgsProcessingUtils::parseDestinationString( destination, providerKey, uri, layerName, format, options, useWriter, extension );
  QCOMPARE( providerKey, QStringLiteral( "ogr" ) );
  QCOMPARE( uri, QStringLiteral( "d:/test.gpkg" ) );
  QCOMPARE( options.value( QStringLiteral( "update" ) ).toBool(), true );
  QVERIFY( !options.contains( QStringLiteral( "layerName" ) ) );
  QCOMPARE( options.value( QStringLiteral( "driverName" ) ).toString(), QStringLiteral( "GPKG" ) );
  QVERIFY( !useWriter );
  QCOMPARE( extension, QStringLiteral( "gpkg" ) );

  // full uri geopackage table output with layer name
  options.clear();
  destination = QStringLiteral( "ogr:dbname='d:/package.gpkg' table=\"mylayer\" (geom) sql=" );
  QgsProcessingUtils::parseDestinationString( destination, providerKey, uri, layerName, format, options, useWriter, extension );
  QCOMPARE( providerKey, QStringLiteral( "ogr" ) );
  QCOMPARE( uri, QStringLiteral( "d:/package.gpkg" ) );
  QCOMPARE( options.value( QStringLiteral( "update" ) ).toBool(), true );
  QCOMPARE( options.value( QStringLiteral( "layerName" ) ).toString(), QStringLiteral( "mylayer" ) );
  QCOMPARE( options.value( QStringLiteral( "driverName" ) ).toString(), QStringLiteral( "GPKG" ) );
  QVERIFY( !useWriter );
  QCOMPARE( extension, QStringLiteral( "gpkg" ) );
}

void TestQgsProcessing::createFeatureSink()
{
  QgsProcessingContext context;

  // empty destination
  QString destination;
  destination = QString();
  QgsVectorLayer *layer = nullptr;

  // should create a memory layer
  std::unique_ptr< QgsFeatureSink > sink( QgsProcessingUtils::createFeatureSink( destination, context, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem() ) );
  QVERIFY( sink.get() );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination, context, false ) );
  QVERIFY( layer );
  QCOMPARE( static_cast< QgsProxyFeatureSink *>( sink.get() )->destinationSink(), layer->dataProvider() );
  QCOMPARE( layer->dataProvider()->name(), QStringLiteral( "memory" ) );
  QCOMPARE( destination, layer->id() );
  QVERIFY( !layer->customProperty( QStringLiteral( "OnConvertFormatRegeneratePrimaryKey" ) ).toBool() );
  QCOMPARE( context.temporaryLayerStore()->mapLayer( layer->id() ), layer ); // layer should be in store
  QgsFeature f;
  QCOMPARE( layer->featureCount(), 0L );
  QVERIFY( sink->addFeature( f ) );
  QCOMPARE( layer->featureCount(), 1L );
  context.temporaryLayerStore()->removeAllMapLayers();
  layer = nullptr;

  // specific memory layer output
  destination = QStringLiteral( "memory:mylayer" );
  sink.reset( QgsProcessingUtils::createFeatureSink( destination, context, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem() ) );
  QVERIFY( sink.get() );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination, context, false ) );
  QVERIFY( layer );
  QCOMPARE( static_cast< QgsProxyFeatureSink *>( sink.get() )->destinationSink(), layer->dataProvider() );
  QCOMPARE( layer->dataProvider()->name(), QStringLiteral( "memory" ) );
  QCOMPARE( layer->name(), QStringLiteral( "mylayer" ) );
  QCOMPARE( destination, layer->id() );
  QCOMPARE( context.temporaryLayerStore()->mapLayer( layer->id() ), layer ); // layer should be in store
  QCOMPARE( layer->featureCount(), 0L );
  QVERIFY( sink->addFeature( f ) );
  QCOMPARE( layer->featureCount(), 1L );
  context.temporaryLayerStore()->removeAllMapLayers();
  layer = nullptr;

  // nameless memory layer
  destination = QStringLiteral( "memory:" );
  sink.reset( QgsProcessingUtils::createFeatureSink( destination, context, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem() ) );
  QVERIFY( sink.get() );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination, context, false ) );
  QVERIFY( layer );
  QCOMPARE( static_cast< QgsProxyFeatureSink *>( sink.get() )->destinationSink(), layer->dataProvider() );
  QCOMPARE( layer->dataProvider()->name(), QStringLiteral( "memory" ) );
  QCOMPARE( layer->name(), QString( "output" ) ); // should fall back to "output" name
  QCOMPARE( destination, layer->id() );
  QCOMPARE( context.temporaryLayerStore()->mapLayer( layer->id() ), layer ); // layer should be in store
  QCOMPARE( layer->featureCount(), 0L );
  QVERIFY( sink->addFeature( f ) );
  QCOMPARE( layer->featureCount(), 1L );
  context.temporaryLayerStore()->removeAllMapLayers();
  layer = nullptr;

  // memory layer parameters
  destination = QStringLiteral( "memory:mylayer" );
  QgsFields fields;
  fields.append( QgsField( QStringLiteral( "my_field" ), QVariant::String, QString(), 100 ) );
  sink.reset( QgsProcessingUtils::createFeatureSink( destination, context, fields, QgsWkbTypes::PointZM, QgsCoordinateReferenceSystem::fromEpsgId( 3111 ), QVariantMap(), QStringList(), QStringList(), QgsFeatureSink::RegeneratePrimaryKey ) );
  QVERIFY( sink.get() );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination, context, false ) );
  QVERIFY( layer );
  QCOMPARE( static_cast< QgsProxyFeatureSink *>( sink.get() )->destinationSink(), layer->dataProvider() );
  QCOMPARE( layer->dataProvider()->name(), QStringLiteral( "memory" ) );
  QCOMPARE( layer->name(), QStringLiteral( "mylayer" ) );
  QVERIFY( layer->customProperty( QStringLiteral( "OnConvertFormatRegeneratePrimaryKey" ) ).toBool() );
  QCOMPARE( layer->wkbType(), QgsWkbTypes::PointZM );
  QCOMPARE( layer->crs().authid(), QStringLiteral( "EPSG:3111" ) );
  QCOMPARE( layer->fields().size(), 1 );
  QCOMPARE( layer->fields().at( 0 ).name(), QStringLiteral( "my_field" ) );
  QCOMPARE( layer->fields().at( 0 ).type(), QVariant::String );
  QCOMPARE( destination, layer->id() );
  QCOMPARE( context.temporaryLayerStore()->mapLayer( layer->id() ), layer ); // layer should be in store
  QCOMPARE( layer->featureCount(), 0L );
  QVERIFY( sink->addFeature( f ) );
  QCOMPARE( layer->featureCount(), 1L );
  context.temporaryLayerStore()->removeAllMapLayers();

  // non memory layer output
  destination = QDir::tempPath() + "/create_feature_sink.tab";
  QString prevDest = destination;
  sink.reset( QgsProcessingUtils::createFeatureSink( destination, context, fields, QgsWkbTypes::Polygon, QgsCoordinateReferenceSystem::fromEpsgId( 3111 ) ) );
  QVERIFY( sink.get() );
  f = QgsFeature( fields );
  f.setGeometry( QgsGeometry::fromWkt( QStringLiteral( "Polygon((0 0, 0 1, 1 1, 1 0, 0 0 ))" ) ) );
  f.setAttributes( QgsAttributes() << "val" );
  QVERIFY( sink->addFeature( f ) );
  QCOMPARE( destination, prevDest );
  sink.reset( nullptr );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination, context, true ) );
  QVERIFY( layer->isValid() );
  QCOMPARE( layer->crs().authid(), QStringLiteral( "EPSG:3111" ) );
  QCOMPARE( layer->fields().size(), 1 );
  QCOMPARE( layer->fields().at( 0 ).name(), QStringLiteral( "my_field" ) );
  QCOMPARE( layer->fields().at( 0 ).type(), QVariant::String );
  QCOMPARE( layer->featureCount(), 1L );

  // no extension, should default to shp
  destination = QDir::tempPath() + "/create_feature_sink2";
  prevDest = QDir::tempPath() + "/create_feature_sink2.gpkg";
  sink.reset( QgsProcessingUtils::createFeatureSink( destination, context, fields, QgsWkbTypes::PointZ, QgsCoordinateReferenceSystem::fromEpsgId( 3111 ) ) );
  QVERIFY( sink.get() );
  f = QgsFeature( fields );
  f.setAttributes( QgsAttributes() << "val" );
  f.setGeometry( QgsGeometry::fromWkt( QStringLiteral( "PointZ(1 2 3)" ) ) );
  QVERIFY( sink->addFeature( f ) );
  QCOMPARE( destination, prevDest );
  sink.reset( nullptr );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination, context, true ) );
  QCOMPARE( layer->wkbType(), QgsWkbTypes::PointZ );
  QCOMPARE( layer->crs().authid(), QStringLiteral( "EPSG:3111" ) );
  QCOMPARE( layer->fields().size(), 2 );
  QCOMPARE( layer->fields().at( 0 ).name(), QStringLiteral( "fid" ) );
  QCOMPARE( layer->fields().at( 1 ).name(), QStringLiteral( "my_field" ) );
  QCOMPARE( layer->fields().at( 1 ).type(), QVariant::String );
  QCOMPARE( layer->featureCount(), 1L );
  // append to existing OGR layer
  QgsRemappingSinkDefinition remapDef;
  remapDef.setDestinationFields( layer->fields() );
  remapDef.setDestinationCrs( layer->crs() );
  remapDef.setSourceCrs( QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  remapDef.setDestinationWkbType( QgsWkbTypes::Polygon );
  remapDef.addMappedField( QStringLiteral( "my_field" ), QgsProperty::fromExpression( QStringLiteral( "field2 || @extra" ) ) );
  QgsFields fields2;
  fields2.append( QgsField( "field2", QVariant::String ) );
  context.expressionContext().appendScope( new QgsExpressionContextScope() );
  context.expressionContext().scope( 0 )->setVariable( QStringLiteral( "extra" ), 2 );
  sink.reset( QgsProcessingUtils::createFeatureSink( destination, context, fields2, QgsWkbTypes::Point, QgsCoordinateReferenceSystem::fromEpsgId( 4326 ), QVariantMap(), QStringList(), QStringList(), QgsFeatureSink::SinkFlags(), &remapDef ) );
  QVERIFY( sink.get() );
  f = QgsFeature( fields2 );
  f.setGeometry( QgsGeometry::fromWkt( QStringLiteral( "Point(10 0)" ) ) );
  f.setAttributes( QgsAttributes() << "val" );
  QVERIFY( sink->addFeature( f ) );
  QCOMPARE( destination, prevDest );
  sink.reset( nullptr );
  layer = new QgsVectorLayer( destination );
  QVERIFY( layer->isValid() );
  QCOMPARE( layer->featureCount(), 2L );
  QgsFeatureIterator it = layer->getFeatures();
  QVERIFY( it.nextFeature( f ) );
  QCOMPARE( f.attributes().at( 1 ).toString(), QStringLiteral( "val" ) );
  QCOMPARE( f.geometry().asWkt( 1 ), QStringLiteral( "PointZ (1 2 3)" ) );
  QVERIFY( it.nextFeature( f ) );
  QCOMPARE( f.attributes().at( 1 ).toString(), QStringLiteral( "val2" ) );
  QCOMPARE( f.geometry().asWkt( 0 ), QStringLiteral( "Point (-10199761 -4017774)" ) );
  delete layer;
  //windows style path
  destination = "d:\\temp\\create_feature_sink.tab";
  sink.reset( QgsProcessingUtils::createFeatureSink( destination, context, fields, QgsWkbTypes::Polygon, QgsCoordinateReferenceSystem::fromEpsgId( 3111 ) ) );
  QVERIFY( sink.get() );

  // save to geopackage
  QString geopackagePath = QDir::tempPath() + "/packaged.gpkg";
  if ( QFileInfo::exists( geopackagePath ) )
    QFile::remove( geopackagePath );
  destination = QStringLiteral( "ogr:dbname='%1' table=\"polygons\" (geom) sql=" ).arg( geopackagePath );
  sink.reset( QgsProcessingUtils::createFeatureSink( destination, context, fields, QgsWkbTypes::Polygon, QgsCoordinateReferenceSystem::fromEpsgId( 3111 ) ) );
  QVERIFY( sink.get() );
  QCOMPARE( destination, QStringLiteral( "%1|layername=polygons" ).arg( geopackagePath ) );
  f = QgsFeature( fields );
  f.setGeometry( QgsGeometry::fromWkt( QStringLiteral( "Polygon((0 0, 0 1, 1 1, 1 0, 0 0 ))" ) ) );
  f.setAttributes( QgsAttributes() << "val" );
  QVERIFY( sink->addFeature( f ) );
  sink.reset( nullptr );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination, context, true ) );
  QVERIFY( layer->isValid() );
  QCOMPARE( layer->wkbType(), QgsWkbTypes::Polygon );
  QVERIFY( layer->getFeatures().nextFeature( f ) );
  QCOMPARE( f.attribute( "my_field" ).toString(), QStringLiteral( "val" ) );

  // add another output to the same geopackage
  QString destination2 = QStringLiteral( "ogr:dbname='%1' table=\"points\" (geom) sql=" ).arg( geopackagePath );
  sink.reset( QgsProcessingUtils::createFeatureSink( destination2, context, fields, QgsWkbTypes::Point, QgsCoordinateReferenceSystem::fromEpsgId( 3111 ) ) );
  QVERIFY( sink.get() );
  QCOMPARE( destination2, QStringLiteral( "%1|layername=points" ).arg( geopackagePath ) );
  f = QgsFeature( fields );
  f.setGeometry( QgsGeometry::fromWkt( QStringLiteral( "Point(0 0)" ) ) );
  f.setAttributes( QgsAttributes() << "val2" );
  QVERIFY( sink->addFeature( f ) );
  sink.reset( nullptr );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination2, context, true ) );
  QVERIFY( layer->isValid() );
  QCOMPARE( layer->wkbType(), QgsWkbTypes::Point );
  QVERIFY( layer->getFeatures().nextFeature( f ) );
  QCOMPARE( f.attribute( "my_field" ).toString(), QStringLiteral( "val2" ) );

  // original polygon layer should remain
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination, context, true ) );
  QVERIFY( layer->isValid() );
  QCOMPARE( layer->wkbType(), QgsWkbTypes::Polygon );
  QVERIFY( layer->getFeatures().nextFeature( f ) );
  QCOMPARE( f.attribute( "my_field" ).toString(), QStringLiteral( "val" ) );

  // now append to that second layer
  remapDef.setDestinationFields( layer->fields() );
  remapDef.setDestinationCrs( layer->crs() );

  remapDef.setSourceCrs( QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  remapDef.setDestinationWkbType( QgsWkbTypes::Point );
  remapDef.addMappedField( QStringLiteral( "my_field" ), QgsProperty::fromExpression( QStringLiteral( "field2 || @extra" ) ) );
  destination2 = QStringLiteral( "ogr:dbname='%1' table=\"points\" (geom) sql=" ).arg( geopackagePath );
  sink.reset( QgsProcessingUtils::createFeatureSink( destination2, context, fields2, QgsWkbTypes::PointZ, QgsCoordinateReferenceSystem::fromEpsgId( 4326 ), QVariantMap(), QStringList(), QStringList(), QgsFeatureSink::SinkFlags(), &remapDef ) );
  QVERIFY( sink.get() );
  f = QgsFeature( fields );
  f.setGeometry( QgsGeometry::fromWkt( QStringLiteral( "PointZ(3 4 5)" ) ) );
  f.setAttributes( QgsAttributes() << "v" );
  QVERIFY( sink->addFeature( f ) );
  sink.reset( nullptr );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destination2, context, true ) );
  QVERIFY( layer->isValid() );
  QCOMPARE( layer->wkbType(), QgsWkbTypes::Point );
  QCOMPARE( layer->featureCount(), 2L );
  QVERIFY( layer->getFeatures().nextFeature( f ) );
  QCOMPARE( f.attribute( "my_field" ).toString(), QStringLiteral( "val2" ) );
}

void TestQgsProcessing::source()
{
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QgsVectorLayer *invalidLayer = new QgsVectorLayer( testDataDir + "invalidgeometries.gml", QString(), "ogr" );
  QVERIFY( invalidLayer->isValid() );

  QgsProcessingContext context;
  context.setInvalidGeometryCheck( QgsFeatureRequest::GeometryAbortOnInvalid );
  QgsProcessingFeatureSource source( invalidLayer, context );
  // expect an exception, we should be using the context's "abort on invalid" setting
  QgsFeatureIterator it = source.getFeatures();
  QgsFeature f;
  try
  {
    it.nextFeature( f );
    QVERIFY( false );
  }
  catch ( QgsProcessingException & )
  {

  }

  // now try with a source overriding the context's setting
  source.setInvalidGeometryCheck( QgsFeatureRequest::GeometryNoCheck );
  it = source.getFeatures();
  QVERIFY( it.nextFeature( f ) );
  QVERIFY( !f.geometry().isGeosValid() );
  // all good!

  QgsVectorLayer *polygonLayer = new QgsVectorLayer( testDataDir + "polys.shp", QString(), "ogr" );
  QVERIFY( polygonLayer->isValid() );

  QgsProcessingFeatureSource source2( polygonLayer, context );
  QCOMPARE( source2.featureCount(), 10L );
  int i = 0;
  it = source2.getFeatures();
  while ( it.nextFeature( f ) )
    i++;
  QCOMPARE( i, 10 );

  // now with a limit on features
  QgsProcessingFeatureSource source3( polygonLayer, context, false, 5 );
  QCOMPARE( source3.featureCount(), 5L );
  i = 0;
  it = source3.getFeatures();
  while ( it.nextFeature( f ) )
    i++;
  QCOMPARE( i, 5 );

  // feature request has a lower limit than source
  it = source3.getFeatures( QgsFeatureRequest().setLimit( 2 ) );
  i = 0;
  while ( it.nextFeature( f ) )
    i++;
  QCOMPARE( i, 2 );

  // feature request has a higher limit than source
  it = source3.getFeatures( QgsFeatureRequest().setLimit( 12 ) );
  i = 0;
  while ( it.nextFeature( f ) )
    i++;
  QCOMPARE( i, 5 );

}

void TestQgsProcessing::parameters()
{
  // test parameter utilities

  std::unique_ptr< QgsProcessingParameterDefinition > def;

  QVariantMap params;
  params.insert( QStringLiteral( "prop" ), QgsProperty::fromField( "a_field" ) );
  params.insert( QStringLiteral( "string" ), QStringLiteral( "a string" ) );
  params.insert( QStringLiteral( "double" ), 5.2 );
  params.insert( QStringLiteral( "int" ), 15 );
  params.insert( QStringLiteral( "ints" ), QVariant( QList<QVariant>() << 3 << 2 << 1 ) );
  params.insert( QStringLiteral( "bool" ), true );

  QgsProcessingContext context;

  // isDynamic
  QVERIFY( QgsProcessingParameters::isDynamic( params, QStringLiteral( "prop" ) ) );
  QVERIFY( !QgsProcessingParameters::isDynamic( params, QStringLiteral( "string" ) ) );
  QVERIFY( !QgsProcessingParameters::isDynamic( params, QStringLiteral( "bad" ) ) );

  // parameterAsString
  def.reset( new QgsProcessingParameterString( QStringLiteral( "string" ), QStringLiteral( "desc" ) ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QStringLiteral( "a string" ) );
  def->setName( QStringLiteral( "double" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ).left( 3 ), QStringLiteral( "5.2" ) );
  def->setName( QStringLiteral( "int" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QStringLiteral( "15" ) );
  def->setName( QStringLiteral( "bool" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QStringLiteral( "true" ) );
  def->setName( QStringLiteral( "bad" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString() );

  def->setIsDynamic( true );
  QVERIFY( def->isDynamic() );
  def->setDynamicPropertyDefinition( QgsPropertyDefinition( QStringLiteral( "Distance" ), QObject::tr( "Buffer distance" ), QgsPropertyDefinition::Double ) );
  QCOMPARE( def->dynamicPropertyDefinition().name(), QStringLiteral( "Distance" ) );
  def->setDynamicLayerParameterName( "parent" );
  QCOMPARE( def->dynamicLayerParameterName(), QStringLiteral( "parent" ) );

  // string with dynamic property (feature not set)
  def->setName( QStringLiteral( "prop" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString() );

  // correctly setup feature
  QgsFields fields;
  fields.append( QgsField( "a_field", QVariant::String, QString(), 30 ) );
  QgsFeature f( fields );
  f.setAttribute( 0, QStringLiteral( "field value" ) );
  context.expressionContext().setFeature( f );
  context.expressionContext().setFields( fields );
  def->setName( QStringLiteral( "prop" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QStringLiteral( "field value" ) );

  // as double
  def->setName( QStringLiteral( "double" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsDouble( def.get(), params, context ), 5.2 );
  def->setName( QStringLiteral( "int" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsDouble( def.get(), params, context ), 15.0 );
  f.setAttribute( 0, QStringLiteral( "6.2" ) );
  context.expressionContext().setFeature( f );
  def->setName( QStringLiteral( "prop" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsDouble( def.get(), params, context ), 6.2 );

  // as int
  def->setName( QStringLiteral( "double" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsInt( def.get(), params, context ), 5 );
  def->setName( QStringLiteral( "int" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsInt( def.get(), params, context ), 15 );
  def->setName( QStringLiteral( "prop" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsInt( def.get(), params, context ), 6 );

  // as ints
  def->setName( QStringLiteral( "int" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsInts( def.get(), params, context ), QList<int>() << 15 );
  def->setName( QStringLiteral( "ints" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsInts( def.get(), params, context ), QList<int>() << 3 << 2 << 1 );

  // as bool
  def->setName( QStringLiteral( "double" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  def->setName( QStringLiteral( "int" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  def->setName( QStringLiteral( "bool" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  def->setName( QStringLiteral( "prop" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  f.setAttribute( 0, false );
  context.expressionContext().setFeature( f );
  def->setName( QStringLiteral( "prop" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), false );

  // as layer
  def->setName( QStringLiteral( "double" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayer( def.get(), params, context ) );
  def->setName( QStringLiteral( "int" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayer( def.get(),  params, context ) );
  def->setName( QStringLiteral( "bool" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayer( def.get(), params, context ) );
  def->setName( QStringLiteral( "prop" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayer( def.get(), params, context ) );

  QVERIFY( context.temporaryLayerStore()->mapLayers().isEmpty() );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  f.setAttribute( 0, QString( testDataDir + "/raster/band1_float32_noct_epsg4326.tif" ) );
  context.expressionContext().setFeature( f );
  def->setName( QStringLiteral( "prop" ) );
  QVERIFY( QgsProcessingParameters::parameterAsLayer( def.get(), params, context ) );
  // make sure layer was loaded
  QVERIFY( !context.temporaryLayerStore()->mapLayers().isEmpty() );

  // parameters as sinks

  QgsWkbTypes::Type wkbType = QgsWkbTypes::PolygonM;
  QgsCoordinateReferenceSystem crs = QgsCoordinateReferenceSystem( QStringLiteral( "epsg:3111" ) );
  QString destId;
  def->setName( QStringLiteral( "string" ) );
  params.insert( QStringLiteral( "string" ), QStringLiteral( "memory:mem" ) );
  std::unique_ptr< QgsFeatureSink > sink;
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, fields, wkbType, crs, context, destId ) );
  QVERIFY( sink.get() );
  QgsVectorLayer *layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destId, context ) );
  QVERIFY( layer );
  QVERIFY( layer->isValid() );
  QCOMPARE( layer->fields().count(), 1 );
  QCOMPARE( layer->fields().at( 0 ).name(), QStringLiteral( "a_field" ) );
  QCOMPARE( layer->wkbType(), wkbType );
  QCOMPARE( layer->crs(), crs );

  // property defined sink destination
  params.insert( QStringLiteral( "prop" ), QgsProperty::fromExpression( "'memory:mem2'" ) );
  def->setName( QStringLiteral( "prop" ) );
  crs = QgsCoordinateReferenceSystem( QStringLiteral( "epsg:3113" ) );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, fields, wkbType, crs, context, destId ) );
  QVERIFY( sink.get() );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destId, context ) );
  QVERIFY( layer );
  QVERIFY( layer->isValid() );
  QCOMPARE( layer->fields().count(), 1 );
  QCOMPARE( layer->fields().at( 0 ).name(), QStringLiteral( "a_field" ) );
  QCOMPARE( layer->wkbType(), wkbType );
  QCOMPARE( layer->crs(), crs );

  // QgsProcessingFeatureSinkDefinition as parameter
  QgsProject p;
  QgsProcessingOutputLayerDefinition fs( QStringLiteral( "test.shp" ) );
  fs.destinationProject = &p;
  QVERIFY( context.layersToLoadOnCompletion().isEmpty() );
  params.insert( QStringLiteral( "fs" ), QVariant::fromValue( fs ) );
  def->setName( QStringLiteral( "fs" ) );
  crs = QgsCoordinateReferenceSystem( QStringLiteral( "epsg:28356" ) );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, fields, wkbType, crs, context, destId ) );
  QVERIFY( sink.get() );
  QgsVectorFileWriter *writer = dynamic_cast< QgsVectorFileWriter *>( dynamic_cast< QgsProcessingFeatureSink * >( sink.get() )->destinationSink() );
  QVERIFY( writer );
  layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( destId, context ) );
  QVERIFY( layer );
  QVERIFY( layer->isValid() );
  QCOMPARE( layer->wkbType(), QgsWkbTypes::MultiPolygonM ); // shapefile Polygon[XX] get promoted to Multi
  QCOMPARE( layer->crs(), crs );

  // make sure layer was automatically added to list to load on completion
  QCOMPARE( context.layersToLoadOnCompletion().size(), 1 );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 0 ), destId );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "desc" ) );

  // with name overloading
  QgsProcessingContext context2;
  fs = QgsProcessingOutputLayerDefinition( QStringLiteral( "test.shp" ) );
  fs.destinationProject = &p;
  fs.destinationName = QStringLiteral( "my_dest" );
  params.insert( QStringLiteral( "fs" ), QVariant::fromValue( fs ) );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, fields, wkbType, crs, context2, destId ) );
  QVERIFY( sink.get() );
  QCOMPARE( context2.layersToLoadOnCompletion().size(), 1 );
  QCOMPARE( context2.layersToLoadOnCompletion().keys().at( 0 ), destId );
  QCOMPARE( context2.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "my_dest" ) );
  QCOMPARE( context2.layersToLoadOnCompletion().values().at( 0 ).outputName, QStringLiteral( "fs" ) );

  // setting layer name to match...
  context2.layersToLoadOnCompletion().values().at( 0 ).setOutputLayerName( nullptr );
  std::unique_ptr< QgsVectorLayer > vl = qgis::make_unique< QgsVectorLayer >( QStringLiteral( "Point" ), QString(), QStringLiteral( "memory" ) );
  QVERIFY( vl->isValid() );
  context2.layersToLoadOnCompletion().values().at( 0 ).setOutputLayerName( vl.get() );
  // temporary layer, must use output name as layer name
  QCOMPARE( vl->name(), QStringLiteral( "my_dest" ) );
  // otherwise expect to use path
  std::unique_ptr< QgsRasterLayer > rl = qgis::make_unique< QgsRasterLayer >( QStringLiteral( TEST_DATA_DIR ) + "/landsat.tif", QString() );
  context2.layersToLoadOnCompletion().values().at( 0 ).setOutputLayerName( rl.get() );
  QCOMPARE( rl->name(), QStringLiteral( "landsat" ) );
  // unless setting prohibits it...
  QgsSettings().setValue( QStringLiteral( "Processing/Configuration/PREFER_FILENAME_AS_LAYER_NAME" ), false );
  context2.layersToLoadOnCompletion().values().at( 0 ).setOutputLayerName( rl.get() );
  QCOMPARE( rl->name(), QStringLiteral( "my_dest" ) );
  // if layer has a layername, we should use that instead of the base file name...
  QgsSettings().setValue( QStringLiteral( "Processing/Configuration/PREFER_FILENAME_AS_LAYER_NAME" ), true );
  vl = qgis::make_unique< QgsVectorLayer >( QStringLiteral( TEST_DATA_DIR ) + "/points_gpkg.gpkg|layername=points_small", QString() );
  context2.layersToLoadOnCompletion().values().at( 0 ).setOutputLayerName( vl.get() );
  QCOMPARE( vl->name(), QStringLiteral( "points_small" ) );
  // if forced name is true, that should always be used, regardless of the user's local setting
  QgsProcessingContext::LayerDetails details( QStringLiteral( "my name" ), context2.project(), QStringLiteral( "my name" ) );
  details.forceName = false;
  details.setOutputLayerName( vl.get() );
  QCOMPARE( vl->name(), QStringLiteral( "points_small" ) );
  details.forceName = true;
  details.setOutputLayerName( vl.get() );
  QCOMPARE( vl->name(), QStringLiteral( "my name" ) );
}

void TestQgsProcessing::algorithmParameters()
{
  DummyAlgorithm *alg = new DummyAlgorithm( "test" );
  DummyProvider p( "test" );
  alg->runParameterChecks();

  p.addAlgorithm( alg );
  alg->runParameterChecks2();
}

void TestQgsProcessing::algorithmOutputs()
{
  DummyAlgorithm alg( "test" );
  alg.runOutputChecks();
}

void TestQgsProcessing::parameterGeneral()
{
  // test constructor
  QgsProcessingParameterBoolean param( "p1", "desc", true, true );
  QCOMPARE( param.name(), QString( "p1" ) );
  QCOMPARE( param.description(), QString( "desc" ) );
  QCOMPARE( param.defaultValue(), QVariant( true ) );
  QVERIFY( param.flags() & QgsProcessingParameterDefinition::FlagOptional );
  QVERIFY( param.dependsOnOtherParameters().isEmpty() );
  QVERIFY( param.help().isEmpty() );

  // test getters and setters
  param.setDescription( "p2" );
  QCOMPARE( param.description(), QString( "p2" ) );
  param.setDefaultValue( false );
  QCOMPARE( param.defaultValue(), QVariant( false ) );
  param.setFlags( QgsProcessingParameterDefinition::FlagHidden );
  QCOMPARE( param.flags(), QgsProcessingParameterDefinition::FlagHidden );
  param.setDefaultValue( true );
  QCOMPARE( param.defaultValue(), QVariant( true ) );
  QCOMPARE( param.defaultValueForGui(), QVariant( true ) );
  QVERIFY( !param.guiDefaultValueOverride().isValid() );
  param.setGuiDefaultValueOverride( false );
  QCOMPARE( param.guiDefaultValueOverride(), QVariant( false ) );
  QCOMPARE( param.defaultValueForGui(), QVariant( false ) );

  param.setDefaultValue( QVariant() );
  QCOMPARE( param.defaultValue(), QVariant() );
  param.setHelp( QStringLiteral( "my help" ) );
  QCOMPARE( param.help(), QStringLiteral( "my help" ) );

  QVariantMap metadata;
  metadata.insert( "p1", 5 );
  metadata.insert( "p2", 7 );
  param.setMetadata( metadata );
  QCOMPARE( param.metadata(), metadata );
  param.metadata().insert( "p3", 9 );
  QCOMPARE( param.metadata().value( "p3" ).toInt(), 9 );

  QVERIFY( param.additionalExpressionContextVariables().isEmpty() );
  param.setAdditionalExpressionContextVariables( QStringList() << "a" << "b" );
  QCOMPARE( param.additionalExpressionContextVariables(), QStringList() << "a" << "b" );
  std::unique_ptr< QgsProcessingParameterDefinition > param2( param.clone() );
  QCOMPARE( param2->guiDefaultValueOverride(), param.guiDefaultValueOverride() );
  QCOMPARE( param2->additionalExpressionContextVariables(), QStringList() << "a" << "b" );

  QVariantMap map = param.toVariantMap();
  QgsProcessingParameterBoolean fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), param.name() );
  QCOMPARE( fromMap.description(), param.description() );
  QCOMPARE( fromMap.flags(), param.flags() );
  QCOMPARE( fromMap.defaultValue(), param.defaultValue() );
  QCOMPARE( fromMap.guiDefaultValueOverride(), param.guiDefaultValueOverride() );
  QCOMPARE( fromMap.metadata(), param.metadata() );
  QCOMPARE( fromMap.help(), QStringLiteral( "my help" ) );
}

void TestQgsProcessing::parameterBoolean()
{
  QgsProcessingContext context;

  // test no def
  QVariantMap params;
  params.insert( "no_def",  false );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( nullptr, params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( nullptr, params, context ), false );
  params.insert( "no_def",  "false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( nullptr, params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( nullptr, params, context ), false );
  params.insert( "no_def",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( nullptr, params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( nullptr, params, context ), false );
  params.remove( "no_def" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( nullptr, params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( nullptr, params, context ), false );

  // with defs

  std::unique_ptr< QgsProcessingParameterDefinition > def( new QgsProcessingParameterBoolean( "non_optional_default_false" ) );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( "false" ) );
  QVERIFY( def->checkValueIsAcceptable( "true" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "non_optional_default_false",  false );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), false );
  params.insert( "non_optional_default_false",  true );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  params.insert( "non_optional_default_false",  "true" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  params.insert( "non_optional_default_false",  "false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), false );

  //non-optional - behavior is undefined, but internally default to false
  params.insert( "non_optional_default_false",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), false );
  params.remove( "non_optional_default_false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), false );

  QCOMPARE( def->valueAsPythonString( false, context ), QStringLiteral( "False" ) );
  QCOMPARE( def->valueAsPythonString( true, context ), QStringLiteral( "True" ) );
  QCOMPARE( def->valueAsPythonString( "false", context ), QStringLiteral( "False" ) );
  QCOMPARE( def->valueAsPythonString( "true", context ), QStringLiteral( "True" ) );
  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterBoolean('non_optional_default_false', '', defaultValue=None)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional_default_false=boolean false" ) );
  std::unique_ptr< QgsProcessingParameterBoolean > fromCode( dynamic_cast< QgsProcessingParameterBoolean * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional default false" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toBool(), false );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterBoolean fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( QgsProcessingParameters::parameterFromVariantMap( map ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterBoolean *>( def.get() ) );


  def.reset( new QgsProcessingParameterBoolean( "optional_default_true", QString(), true, true ) );

  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( "false" ) );
  QVERIFY( def->checkValueIsAcceptable( "true" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional_default_true",  false );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), false );
  params.insert( "optional_default_true",  true );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  params.insert( "optional_default_true",  "true" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  params.insert( "optional_default_true",  "false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), false );
  //optional - should be default
  params.insert( "optional_default_true",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  params.remove( "optional_default_true" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterBoolean('optional_default_true', '', optional=True, defaultValue=True)" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional_default_true=optional boolean true" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterBoolean * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional default true" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toBool(), true );

  def.reset( new QgsProcessingParameterBoolean( "optional_default_false", QString(), false, true ) );

  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( "false" ) );
  QVERIFY( def->checkValueIsAcceptable( "true" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( "false" ) );
  QVERIFY( def->checkValueIsAcceptable( "true" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional_default_false",  false );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), false );
  params.insert( "optional_default_false",  true );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  params.insert( "optional_default_false",  "true" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  params.insert( "optional_default_false",  "false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), false );
  //optional - should be default
  params.insert( "optional_default_false",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), false );
  params.remove( "optional_default_false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), false );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterBoolean('optional_default_false', '', optional=True, defaultValue=False)" ) );

  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterBoolean * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional default false" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toBool(), false );

  def.reset( new QgsProcessingParameterBoolean( "non_optional_default_true", QString(), true, false ) );

  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( "false" ) );
  QVERIFY( def->checkValueIsAcceptable( "true" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be acceptable, because it falls back to default value

  params.insert( "non_optional_default_true",  false );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), false );
  params.insert( "non_optional_default_true",  true );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  params.insert( "non_optional_default_true",  "true" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  params.insert( "non_optional_default_true",  "false" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), false );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), false );
  //non-optional - behavior is undefined, but internally fallback to default
  params.insert( "non_optional_default_true",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );
  params.remove( "non_optional_default_true" );
  QCOMPARE( QgsProcessingParameters::parameterAsBool( def.get(), params, context ), true );
  QCOMPARE( QgsProcessingParameters::parameterAsBoolean( def.get(), params, context ), true );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterBoolean('non_optional_default_true', '', defaultValue=True)" ) );

  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterBoolean * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional default true" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toBool(), true );

  def.reset( new QgsProcessingParameterBoolean( "non_optional_no_default", QString(),  QVariant(), false ) );

  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( "false" ) );
  QVERIFY( def->checkValueIsAcceptable( "true" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) ); // should NOT be acceptable, because it falls back to invalid default value
}

void TestQgsProcessing::parameterCrs()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "landsat_4326.tif";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon?crs=EPSG:3111", "V4", "memory" );
  p.addMapLayers( QList<QgsMapLayer *>() << v1 << r1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterCrs > def( new QgsProcessingParameterCrs( "non_optional", QString(), QString( "EPSG:3113" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "EPSG:12003" ) );
  QVERIFY( def->checkValueIsAcceptable( "EPSG:3111" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsCoordinateReferenceSystem() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:3111" ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( r1->id() ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( QgsProperty::fromValue( QVariant::fromValue( r1 ) ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( r1->id() ) ) );

  // using map layer
  QVariantMap params;
  params.insert( "non_optional",  v1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:3111" ) );
  QVERIFY( def->checkValueIsAcceptable( v1->id() ) );
  params.insert( "non_optional",  QVariant::fromValue( v1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:3111" ) );

  // using QgsCoordinateReferenceSystem
  params.insert( "non_optional",  QgsCoordinateReferenceSystem( "EPSG:28356" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:28356" ) );
  params.insert( "non_optional",  QgsCoordinateReferenceSystem() );
  QVERIFY( !QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).isValid() );

  // special ProjectCrs string
  params.insert( "non_optional",  QStringLiteral( "ProjectCrs" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:28353" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "ProjectCrs" ) ) );

  // string representing a project layer source
  params.insert( "non_optional", raster1 );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:4326" ) );
  QVERIFY( def->checkValueIsAcceptable( raster1 ) );

  // string representing a non-project layer source
  params.insert( "non_optional", raster2 );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:32633" ) );
  QVERIFY( def->checkValueIsAcceptable( raster2 ) );

  // string representation of a crs
  params.insert( "non_optional", QString( "EPSG:28355" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:28355" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "EPSG:28355" ) ) );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a crs, and nothing you can do will make me one" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).isValid() );

  // using feature source definition
  params.insert( "non_optional",  QgsProcessingFeatureSourceDefinition( v1->id() ) );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:3111" ) );
  params.insert( "non_optional",  QgsProcessingFeatureSourceDefinition( QgsProperty::fromValue( QVariant::fromValue( v1 ) ) ) );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:3111" ) );
  params.insert( "non_optional",  QgsProcessingOutputLayerDefinition( v1->id() ) );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:3111" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QgsCoordinateReferenceSystem( "EPSG:3111" ), context ), QStringLiteral( "QgsCoordinateReferenceSystem('EPSG:3111')" ) );
  QCOMPARE( def->valueAsPythonString( QgsCoordinateReferenceSystem(), context ), QStringLiteral( "QgsCoordinateReferenceSystem()" ) );
  QCOMPARE( def->valueAsPythonString( "EPSG:12003", context ), QStringLiteral( "'EPSG:12003'" ) );
  QCOMPARE( def->valueAsPythonString( "ProjectCrs", context ), QStringLiteral( "'ProjectCrs'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );
  QCOMPARE( def->valueAsPythonString( raster1, context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "landsat_4326.tif'" ) ) );
  QCOMPARE( def->valueAsPythonString( r1->id(), context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "landsat_4326.tif'" ) ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterCrs fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterCrs *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterCrs *>( def.get() ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterCrs('non_optional', '', defaultValue='EPSG:3113')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=crs EPSG:3113" ) );
  std::unique_ptr< QgsProcessingParameterCrs > fromCode( dynamic_cast< QgsProcessingParameterCrs * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // optional
  def.reset( new QgsProcessingParameterCrs( "optional", QString(), QString( "EPSG:3113" ), true ) );
  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsCrs( def.get(), params, context ).authid(), QString( "EPSG:3113" ) );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "EPSG:12003" ) );
  QVERIFY( def->checkValueIsAcceptable( "EPSG:3111" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterCrs('optional', '', optional=True, defaultValue='EPSG:3113')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional crs EPSG:3113" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterCrs * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  code = QStringLiteral( "##optional=optional crs None" );
  fromCode.reset( dynamic_cast< QgsProcessingParameterCrs * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
}

void TestQgsProcessing::parameterMapLayer()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon?crs=EPSG:3111", "V4", "memory" );
  p.addMapLayers( QList<QgsMapLayer *>() << v1 << r1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterMapLayer > def( new QgsProcessingParameterMapLayer( "non_optional", QString(), QString(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.shp" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.tif" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.2dm" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.*" ) ) );

  // should be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  // using existing map layer ID
  QVariantMap params;
  params.insert( "non_optional",  v1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayer( def.get(), params, context )->id(), v1->id() );
  QVERIFY( def->checkValueIsAcceptable( v1->id() ) );
  QVERIFY( def->checkValueIsAcceptable( v1->id(), &context ) );

  // string representing a project layer source
  params.insert( "non_optional", raster1 );
  QVERIFY( def->checkValueIsAcceptable( raster1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayer( def.get(), params, context )->id(), r1->id() );
  // string representing a non-project layer source
  params.insert( "non_optional", raster2 );
  QVERIFY( def->checkValueIsAcceptable( raster2 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayer( def.get(), params, context )->publicSource(), raster2 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a layer, and nothing you can do will make me one" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayer( def.get(), params, context ) );

  // layer
  params.insert( "non_optional", QVariant::fromValue( r1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayer( def.get(), params, context ), r1 );
  params.insert( "non_optional", QVariant::fromValue( v1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayer( def.get(), params, context ), v1 );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( raster1, context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) ) );
  QCOMPARE( def->valueAsPythonString( r1->id(), context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( r1 ), context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMapLayer('non_optional', '', defaultValue='')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=layer" ) );
  std::unique_ptr< QgsProcessingParameterMapLayer > fromCode( dynamic_cast< QgsProcessingParameterMapLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterMapLayer fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterMapLayer *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterMapLayer *>( def.get() ) );

  def->setDataTypes( QList< int >() << QgsProcessing::TypeVectorPoint );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMapLayer('non_optional', '', defaultValue='', types=[QgsProcessing.TypeVectorPoint])" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=layer point" ) );
  def->setDataTypes( QList< int >() << QgsProcessing::TypeVectorLine );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMapLayer('non_optional', '', defaultValue='', types=[QgsProcessing.TypeVectorLine])" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=layer line" ) );
  def->setDataTypes( QList< int >() << QgsProcessing::TypeVectorPolygon );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMapLayer('non_optional', '', defaultValue='', types=[QgsProcessing.TypeVectorPolygon])" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=layer polygon" ) );
  def->setDataTypes( QList< int >() << QgsProcessing::TypeVectorAnyGeometry );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMapLayer('non_optional', '', defaultValue='', types=[QgsProcessing.TypeVectorAnyGeometry])" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=layer hasgeometry" ) );
  def->setDataTypes( QList< int >() << QgsProcessing::TypeVectorPoint << QgsProcessing::TypeVectorLine );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMapLayer('non_optional', '', defaultValue='', types=[QgsProcessing.TypeVectorPoint,QgsProcessing.TypeVectorLine])" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=layer point line" ) );
  def->setDataTypes( QList< int >() << QgsProcessing::TypeVectorPoint << QgsProcessing::TypeVectorPolygon );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMapLayer('non_optional', '', defaultValue='', types=[QgsProcessing.TypeVectorPoint,QgsProcessing.TypeVectorPolygon])" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=layer point polygon" ) );
  def->setDataTypes( QList< int >() << QgsProcessing::TypeRaster << QgsProcessing::TypeVectorPoint );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMapLayer('non_optional', '', defaultValue='', types=[QgsProcessing.TypeRaster,QgsProcessing.TypeVectorPoint])" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=layer raster point" ) );

  // optional
  def.reset( new QgsProcessingParameterMapLayer( "optional", QString(), v1->id(), true ) );
  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayer( def.get(), params, context )->id(), v1->id() );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QString( QStringLiteral( "QgsProcessingParameterMapLayer('optional', '', optional=True, defaultValue='" ) + v1->id() + "')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QString( QStringLiteral( "##optional=optional layer " ) + v1->id() ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMapLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // check if can manage QgsProcessingOutputLayerDefinition
  // as QVariat value in parameters (e.g. coming from an input of
  // another algorithm)

  // all ok
  def.reset( new QgsProcessingParameterMapLayer( "non_optional", QString(), r1->id(), true ) );
  QString sink_name( r1->id() );
  QgsProcessingOutputLayerDefinition val( sink_name );
  params.insert( "non_optional", QVariant::fromValue( val ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayer( def.get(), params, context )->id(), r1->id() );

  // not ok, e.g. source name is not a layer and it's not possible to generate a layer from it source
  def.reset( new QgsProcessingParameterMapLayer( "non_optional", QString(), r1->id(), true ) );
  sink_name = QString( "i'm not a layer, and nothing you can do will make me one" );
  QgsProcessingOutputLayerDefinition val2( sink_name );
  params.insert( "non_optional", QVariant::fromValue( val2 ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayer( def.get(), params, context ) );
}

void TestQgsProcessing::parameterExtent()
{
  // setup a context
  QgsProject p;
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "landsat_4326.tif";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  p.addMapLayers( QList<QgsMapLayer *>() << r1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterExtent > def( new QgsProcessingParameterExtent( "non_optional", QString(), QString( "1,2,3,4" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2,3,4" ) );
  QVERIFY( def->checkValueIsAcceptable( "    1, 2   ,3  , 4  " ) );
  QVERIFY( def->checkValueIsAcceptable( "    1, 2   ,3  , 4  ", &context ) );
  QVERIFY( def->checkValueIsAcceptable( "-1.1,2,-3,-4" ) );
  QVERIFY( def->checkValueIsAcceptable( "-1.1,2,-3,-4", &context ) );
  QVERIFY( def->checkValueIsAcceptable( "-1.1,-2.2,-3.3,-4.4" ) );
  QVERIFY( def->checkValueIsAcceptable( "-1.1,-2.2,-3.3,-4.4", &context ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2,3,4.4[EPSG:4326]" ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2,3,4.4[EPSG:4326]", &context ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2,3,4.4 [EPSG:4326]" ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2,3,4.4 [EPSG:4326]", &context ) );
  QVERIFY( def->checkValueIsAcceptable( "  -1.1,   -2,    -3,   -4.4   [EPSG:4326]    " ) );
  QVERIFY( def->checkValueIsAcceptable( "  -1.1,   -2,    -3,   -4.4   [EPSG:4326]    ", &context ) );
  QVERIFY( def->checkValueIsAcceptable( "121774.38859446358,948723.6921024882,-264546.200347173,492749.6672022904 [EPSG:3785]" ) );
  QVERIFY( def->checkValueIsAcceptable( "121774.38859446358,948723.6921024882,-264546.200347173,492749.6672022904 [EPSG:3785]", &context ) );

  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsRectangle( 1, 2, 3, 4 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsRectangle() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsReferencedRectangle( QgsRectangle( 1, 2, 3, 4 ), QgsCoordinateReferenceSystem( "EPSG:4326" ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsReferencedRectangle( QgsRectangle(), QgsCoordinateReferenceSystem( "EPSG:4326" ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsGeometry::fromRect( QgsRectangle( 1, 2, 3, 4 ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsGeometry::fromWkt( QStringLiteral( "LineString(10 10, 20 20)" ) ) ) );

  // these checks require a context - otherwise we could potentially be referring to a layer source
  QVERIFY( def->checkValueIsAcceptable( "1,2,3" ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2,3,a" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1,2,3", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( "1,2,3,a", &context ) );

  QVERIFY( def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( r1->id() ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( r1->id() ) ) );

  // using map layer
  QVariantMap params;
  params.insert( "non_optional",  r1->id() );
  QVERIFY( def->checkValueIsAcceptable( r1->id() ) );
  QgsRectangle ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context );
  QCOMPARE( ext, r1->extent() );

  // string representing a project layer source
  params.insert( "non_optional", raster1 );
  QVERIFY( def->checkValueIsAcceptable( raster1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExtent( def.get(), params, context ),  r1->extent() );
  QCOMPARE( QgsProcessingParameters::parameterAsExtentCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  QGSCOMPARENEAR( ext.xMinimum(), 17.942777, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 17.944704, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  30.229681, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 30.231616, 0.001 );

  // layer as parameter
  params.insert( "non_optional", QVariant::fromValue( r1 ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExtent( def.get(), params, context ),  r1->extent() );
  QCOMPARE( QgsProcessingParameters::parameterAsExtentCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  QGSCOMPARENEAR( ext.xMinimum(), 17.942777, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 17.944704, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  30.229681, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 30.231616, 0.001 );
  QgsGeometry gExt = QgsProcessingParameters::parameterAsExtentGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  QCOMPARE( gExt.constGet()->vertexCount(), 5 );
  ext = gExt.boundingBox();
  QGSCOMPARENEAR( ext.xMinimum(), 17.942777, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 17.944704, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  30.229681, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 30.231616, 0.001 );

  // using feature source definition
  params.insert( "non_optional",  QgsProcessingFeatureSourceDefinition( r1->id() ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExtentCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  QGSCOMPARENEAR( ext.xMinimum(), 17.942777, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 17.944704, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  30.229681, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 30.231616, 0.001 );
  gExt = QgsProcessingParameters::parameterAsExtentGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  QCOMPARE( gExt.constGet()->vertexCount(), 5 );
  ext = gExt.boundingBox();
  QGSCOMPARENEAR( ext.xMinimum(), 17.942777, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 17.944704, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  30.229681, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 30.231616, 0.001 );
  params.insert( "non_optional",  QgsProcessingFeatureSourceDefinition( QgsProperty::fromValue( QVariant::fromValue( r1 ) ) ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExtentCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  QGSCOMPARENEAR( ext.xMinimum(), 17.942777, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 17.944704, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  30.229681, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 30.231616, 0.001 );
  gExt = QgsProcessingParameters::parameterAsExtentGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  QCOMPARE( gExt.constGet()->vertexCount(), 5 );
  ext = gExt.boundingBox();
  QGSCOMPARENEAR( ext.xMinimum(), 17.942777, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 17.944704, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  30.229681, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 30.231616, 0.001 );

  // using output layer definition, e.g. from a previous model child algorithm
  params.insert( "non_optional",  QgsProcessingOutputLayerDefinition( r1->id() ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExtentCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  QGSCOMPARENEAR( ext.xMinimum(), 17.942777, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 17.944704, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  30.229681, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 30.231616, 0.001 );
  gExt = QgsProcessingParameters::parameterAsExtentGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  QCOMPARE( gExt.constGet()->vertexCount(), 5 );
  ext = gExt.boundingBox();
  QGSCOMPARENEAR( ext.xMinimum(), 17.942777, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 17.944704, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  30.229681, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 30.231616, 0.001 );

  // string representing a non-project layer source
  params.insert( "non_optional", raster2 );
  QVERIFY( def->checkValueIsAcceptable( raster2 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExtentCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:32633" ) );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context );
  QGSCOMPARENEAR( ext.xMinimum(), 781662.375000, 10 );
  QGSCOMPARENEAR( ext.xMaximum(), 793062.375000, 10 );
  QGSCOMPARENEAR( ext.yMinimum(),  3339523.125000, 10 );
  QGSCOMPARENEAR( ext.yMaximum(), 3350923.125000, 10 );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  QGSCOMPARENEAR( ext.xMinimum(), 17.924273, 0.01 );
  QGSCOMPARENEAR( ext.xMaximum(), 18.045658, 0.01 );
  QGSCOMPARENEAR( ext.yMinimum(),  30.151856, 0.01 );
  QGSCOMPARENEAR( ext.yMaximum(), 30.257289, 0.01 );
  gExt = QgsProcessingParameters::parameterAsExtentGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:4326" ) );
  QCOMPARE( gExt.constGet()->vertexCount(), 85 );
  ext = gExt.boundingBox();
  QGSCOMPARENEAR( ext.xMinimum(), 17.924273, 0.01 );
  QGSCOMPARENEAR( ext.xMaximum(), 18.045658, 0.01 );
  QGSCOMPARENEAR( ext.yMinimum(),  30.151856, 0.01 );
  QGSCOMPARENEAR( ext.yMaximum(), 30.257289, 0.01 );

  // string representation of an extent
  params.insert( "non_optional", QString( "1.1,2.2,3.3,4.4" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "1.1,2.2,3.3,4.4" ) ) );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context );
  QGSCOMPARENEAR( ext.xMinimum(), 1.1, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 2.2, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  3.3, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 4.4, 0.001 );

  // with target CRS - should make no difference, because source CRS is unknown
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QGSCOMPARENEAR( ext.xMinimum(), 1.1, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 2.2, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  3.3, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 4.4, 0.001 );

  // with crs in string
  params.insert( "non_optional", QString( "1.1,3.3,2.2,4.4 [EPSG:4326]" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExtentCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context );
  QGSCOMPARENEAR( ext.xMinimum(), 1.1, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 3.3, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  2.2, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 4.4, 0.001 );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QGSCOMPARENEAR( ext.xMinimum(), 122451, 100 );
  QGSCOMPARENEAR( ext.xMaximum(), 367354, 100 );
  QGSCOMPARENEAR( ext.yMinimum(),  244963, 100 );
  QGSCOMPARENEAR( ext.yMaximum(), 490287, 100 );
  gExt = QgsProcessingParameters::parameterAsExtentGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QCOMPARE( gExt.constGet()->vertexCount(), 85 );
  ext = gExt.boundingBox();
  QGSCOMPARENEAR( ext.xMinimum(), 122451, 100 );
  QGSCOMPARENEAR( ext.xMaximum(), 367354, 100 );
  QGSCOMPARENEAR( ext.yMinimum(),  244963, 100 );
  QGSCOMPARENEAR( ext.yMaximum(), 490287, 100 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a crs, and nothing you can do will make me one" ) );
  QVERIFY( QgsProcessingParameters::parameterAsExtent( def.get(), params, context ).isNull() );

  // QgsRectangle
  params.insert( "non_optional", QgsRectangle( 11.1, 12.2, 13.3, 14.4 ) );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context );
  QGSCOMPARENEAR( ext.xMinimum(), 11.1, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 13.3, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  12.2, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 14.4, 0.001 );

  // with target CRS - should make no difference, because source CRS is unknown
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QGSCOMPARENEAR( ext.xMinimum(), 11.1, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 13.3, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  12.2, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 14.4, 0.001 );

  gExt = QgsProcessingParameters::parameterAsExtentGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QCOMPARE( gExt.asWkt( 1 ), QStringLiteral( "Polygon ((11.1 12.2, 13.3 12.2, 13.3 14.4, 11.1 14.4, 11.1 12.2))" ) );

  p.setCrs( QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExtentCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:3785" ) );

  // QgsGeometry
  params.insert( "non_optional", QgsGeometry::fromRect( QgsRectangle( 13, 14, 15, 16 ) ) );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context );
  QGSCOMPARENEAR( ext.xMinimum(), 13, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 15, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  14, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 16, 0.001 );
  // with target CRS - should make no difference, because source CRS is unknown
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context,  QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QGSCOMPARENEAR( ext.xMinimum(), 13, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 15, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  14, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 16, 0.001 );

  // QgsReferencedRectangle
  params.insert( "non_optional", QgsReferencedRectangle( QgsRectangle( 1.1, 2.2, 3.3, 4.4 ), QgsCoordinateReferenceSystem( "EPSG:4326" ) ) );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context );
  QGSCOMPARENEAR( ext.xMinimum(), 1.1, 0.001 );
  QGSCOMPARENEAR( ext.xMaximum(), 3.3, 0.001 );
  QGSCOMPARENEAR( ext.yMinimum(),  2.2, 0.001 );
  QGSCOMPARENEAR( ext.yMaximum(), 4.4, 0.001 );
  QCOMPARE( QgsProcessingParameters::parameterAsExtentCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );

  // with target CRS
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QGSCOMPARENEAR( ext.xMinimum(), 122451, 100 );
  QGSCOMPARENEAR( ext.xMaximum(), 367354, 100 );
  QGSCOMPARENEAR( ext.yMinimum(),  244963, 100 );
  QGSCOMPARENEAR( ext.yMaximum(), 490287, 100 );

  // as reprojected geometry
  gExt = QgsProcessingParameters::parameterAsExtentGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QCOMPARE( gExt.constGet()->vertexCount(), 85 );
  ext = gExt.boundingBox();
  QGSCOMPARENEAR( ext.xMinimum(), 122451, 100 );
  QGSCOMPARENEAR( ext.xMaximum(), 367354, 100 );
  QGSCOMPARENEAR( ext.yMinimum(),  244963, 100 );
  QGSCOMPARENEAR( ext.yMaximum(), 490287, 100 );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( "1,2,3,4", context ), QStringLiteral( "'1,2,3,4'" ) );
  QCOMPARE( def->valueAsPythonString( r1->id(), context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "landsat_4326.tif'" ) ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( r1 ), context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "landsat_4326.tif'" ) ) );
  QCOMPARE( def->valueAsPythonString( raster2, context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "landsat.tif'" ) ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( QgsRectangle( 11, 12, 13, 14 ), context ), QStringLiteral( "'11, 13, 12, 14'" ) );
  QCOMPARE( def->valueAsPythonString( QgsReferencedRectangle( QgsRectangle( 11, 12, 13, 14 ), QgsCoordinateReferenceSystem( "epsg:4326" ) ), context ), QStringLiteral( "'11, 13, 12, 14 [EPSG:4326]'" ) );
  QCOMPARE( def->valueAsPythonString( "1,2,3,4 [EPSG:4326]", context ), QStringLiteral( "'1,2,3,4 [EPSG:4326]'" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );
  QCOMPARE( def->valueAsPythonString( QgsGeometry::fromWkt( QStringLiteral( "LineString( 10 10, 20 20)" ) ), context ), QStringLiteral( "QgsGeometry.fromWkt('LineString (10 10, 20 20)')" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterExtent('non_optional', '', defaultValue='1,2,3,4')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=extent 1,2,3,4" ) );
  std::unique_ptr< QgsProcessingParameterExtent > fromCode( dynamic_cast< QgsProcessingParameterExtent * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterExtent fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterExtent *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterExtent *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterExtent( "optional", QString(), QString( "5,6,7,8" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2,3,4" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  // Extent is unique in that it will let you set invalid, whereas other
  // optional parameters become "default" when assigning invalid.
  params.insert( "optional",  QVariant() );
  ext = QgsProcessingParameters::parameterAsExtent( def.get(), params, context );
  QVERIFY( ext.isNull() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterExtent('optional', '', optional=True, defaultValue='5,6,7,8')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional extent 5,6,7,8" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterExtent * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
}

void TestQgsProcessing::parameterPoint()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterPoint > def( new QgsProcessingParameterPoint( "non_optional", QString(), QString( "1,2" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( "(1.1,2)" ) );
  QVERIFY( def->checkValueIsAcceptable( "    1.1,  2  " ) );
  QVERIFY( def->checkValueIsAcceptable( " (    1.1,  2 ) " ) );
  QVERIFY( def->checkValueIsAcceptable( "-1.1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,-2" ) );
  QVERIFY( def->checkValueIsAcceptable( "-1.1,-2" ) );
  QVERIFY( def->checkValueIsAcceptable( "(-1.1,-2)" ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2[EPSG:4326]" ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2 [EPSG:4326]" ) );
  QVERIFY( def->checkValueIsAcceptable( "(1.1,2 [EPSG:4326] )" ) );
  QVERIFY( def->checkValueIsAcceptable( "  -1.1,   -2   [EPSG:4326]    " ) );
  QVERIFY( def->checkValueIsAcceptable( "  (  -1.1,   -2   [EPSG:4326]  )  " ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1,a" ) );
  QVERIFY( !def->checkValueIsAcceptable( "(1.1,a)" ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "(layer12312312)" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( "()" ) );
  QVERIFY( !def->checkValueIsAcceptable( " (  ) " ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsPointXY( 1, 2 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsReferencedPointXY( QgsPointXY( 1, 2 ), QgsCoordinateReferenceSystem( "EPSG:4326" ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsGeometry::fromPointXY( QgsPointXY( 1, 2 ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsGeometry::fromWkt( QStringLiteral( "LineString(10 10, 20 20)" ) ) ) );

  // string representing a point
  QVariantMap params;
  params.insert( "non_optional", QString( "1.1,2.2" ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2.2" ) );
  QgsPointXY point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context );
  QGSCOMPARENEAR( point.x(), 1.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 2.2, 0.001 );

  // with target CRS - should make no difference, because source CRS is unknown
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QGSCOMPARENEAR( point.x(), 1.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 2.2, 0.001 );

  // with optional brackets
  params.insert( "non_optional", QString( "(1.1,2.2)" ) );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context );
  QGSCOMPARENEAR( point.x(), 1.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 2.2, 0.001 );

  params.insert( "non_optional", QString( "  (   -1.1  ,-2.2  )  " ) );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context );
  QGSCOMPARENEAR( point.x(), -1.1, 0.001 );
  QGSCOMPARENEAR( point.y(), -2.2, 0.001 );

  // with CRS as string
  params.insert( "non_optional", QString( "1.1,2.2[EPSG:4326]" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsPointCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QGSCOMPARENEAR( point.x(), 122451, 100 );
  QGSCOMPARENEAR( point.y(), 244963, 100 );
  params.insert( "non_optional", QString( "1.1,2.2 [EPSG:4326]" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsPointCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QGSCOMPARENEAR( point.x(), 122451, 100 );
  QGSCOMPARENEAR( point.y(), 244963, 100 );

  params.insert( "non_optional", QString( "  ( 1.1,2.2   [EPSG:4326]   ) " ) );
  QCOMPARE( QgsProcessingParameters::parameterAsPointCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QGSCOMPARENEAR( point.x(), 122451, 100 );
  QGSCOMPARENEAR( point.y(), 244963, 100 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a crs, and nothing you can do will make me one" ) );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context );
  QVERIFY( point.isEmpty() );
  QGSCOMPARENEAR( point.x(), 0.0, 0.001 );
  QGSCOMPARENEAR( point.y(), 0.0, 0.001 );

  params.insert( "non_optional", QString( "   (   )  " ) );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context );
  QVERIFY( point.isEmpty() );
  QGSCOMPARENEAR( point.x(), 0.0, 0.001 );
  QGSCOMPARENEAR( point.y(), 0.0, 0.001 );

  // QgsPointXY
  params.insert( "non_optional", QgsPointXY( 11.1, 12.2 ) );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context );
  QGSCOMPARENEAR( point.x(), 11.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 12.2, 0.001 );

  // with target CRS - should make no difference, because source CRS is unknown
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QGSCOMPARENEAR( point.x(), 11.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 12.2, 0.001 );

  // QgsReferencedPointXY
  params.insert( "non_optional", QgsReferencedPointXY( QgsPointXY( 1.1, 2.2 ), QgsCoordinateReferenceSystem( "EPSG:4326" ) ) );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context );
  QGSCOMPARENEAR( point.x(), 1.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 2.2, 0.001 );
  QCOMPARE( QgsProcessingParameters::parameterAsPointCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );

  // with target CRS
  params.insert( "non_optional", QgsReferencedPointXY( QgsPointXY( 1.1, 2.2 ), QgsCoordinateReferenceSystem( "EPSG:4326" ) ) );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QGSCOMPARENEAR( point.x(), 122451, 100 );
  QGSCOMPARENEAR( point.y(), 244963, 100 );

  // QgsGeometry
  params.insert( "non_optional", QgsGeometry::fromPointXY( QgsPointXY( 13.1, 14.2 ) ) );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context );
  QGSCOMPARENEAR( point.x(), 13.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 14.2, 0.001 );
  // non point geometry should use centroid
  params.insert( "non_optional", QgsGeometry::fromWkt( QStringLiteral( "LineString( 10 10, 20 10)" ) ) );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context );
  QGSCOMPARENEAR( point.x(), 15.0, 0.001 );
  QGSCOMPARENEAR( point.y(), 10.0, 0.001 );
  // with target CRS - should make no difference, because source CRS is unknown
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QGSCOMPARENEAR( point.x(), 15.0, 0.001 );
  QGSCOMPARENEAR( point.y(), 10.0, 0.001 );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( "1,2", context ), QStringLiteral( "'1,2'" ) );
  QCOMPARE( def->valueAsPythonString( "1,2 [EPSG:4326]", context ), QStringLiteral( "'1,2 [EPSG:4326]'" ) );
  QCOMPARE( def->valueAsPythonString( QgsPointXY( 11, 12 ), context ), QStringLiteral( "'11,12'" ) );
  QCOMPARE( def->valueAsPythonString( QgsReferencedPointXY( QgsPointXY( 11, 12 ), QgsCoordinateReferenceSystem( "epsg:4326" ) ), context ), QStringLiteral( "'11,12 [EPSG:4326]'" ) );
  QCOMPARE( def->valueAsPythonString( QgsGeometry::fromWkt( QStringLiteral( "LineString( 10 10, 20 20)" ) ), context ), QStringLiteral( "QgsGeometry.fromWkt('LineString (10 10, 20 20)')" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterPoint('non_optional', '', defaultValue='1,2')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=point 1,2" ) );
  std::unique_ptr< QgsProcessingParameterPoint > fromCode( dynamic_cast< QgsProcessingParameterPoint * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterPoint fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterPoint *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterPoint *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterPoint( "optional", QString(), QString( "5.1,6.2" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1,a" ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  point = QgsProcessingParameters::parameterAsPoint( def.get(), params, context );
  QGSCOMPARENEAR( point.x(), 5.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 6.2, 0.001 );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterPoint('optional', '', optional=True, defaultValue='5.1,6.2')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional point 5.1,6.2" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterPoint * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
}

void TestQgsProcessing::parameterGeometry()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterGeometry > def( new QgsProcessingParameterGeometry( "non_optional", QString(), QString( "Point(1 2)" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "Nonsense string" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( QString( "LineString(10 10, 20 a)" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QString( "LineString(10 10, 20 20)" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsGeometry::fromPointXY( QgsPointXY( 1, 2 ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsGeometry::fromWkt( QStringLiteral( "LineString(10 10, 20 20)" ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsPointXY( 1, 2 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsReferencedPointXY( QgsPointXY( 1, 2 ), QgsCoordinateReferenceSystem( "EPSG:4326" ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsRectangle( 10, 10, 20, 20 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsReferencedRectangle( QgsRectangle( 10, 10, 20, 20 ), QgsCoordinateReferenceSystem( "EPSG:4326" ) ) ) );

  // string representing a geometry
  QVariantMap params;
  params.insert( "non_optional", QString( "LineString(10 10, 20 20)" ) );
  QVERIFY( def->checkValueIsAcceptable( "LineString(10 10, 20 20)" ) );
  QgsGeometry geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context );
  QCOMPARE( geometry.asWkt(), QStringLiteral( "LineString (10 10, 20 20)" ) );

  // with target CRS - should make no difference, because source CRS is unknown
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QCOMPARE( geometry.asWkt(), QStringLiteral( "LineString (10 10, 20 20)" ) );

  // with CRS as string
  params.insert( "non_optional", QString( "CRS=EPSG:4326;Point ( 1.1 2.2 )" ) );
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context );
  QPointF point = geometry.asQPointF();
  QGSCOMPARENEAR( point.x(), 1.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 2.2, 0.001 );
  QCOMPARE( QgsProcessingParameters::parameterAsGeometryCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  point = geometry.asQPointF();
  QGSCOMPARENEAR( point.x(), 122451, 100 );
  QGSCOMPARENEAR( point.y(), 244963, 100 );

  // QgsReferencedGeometry
  params.insert( "non_optional", QgsReferencedGeometry( QgsGeometry::fromPointXY( QgsPointXY( 1.1, 2.2 ) ), QgsCoordinateReferenceSystem( "EPSG:4326" ) ) );
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context );
  point = geometry.asQPointF();
  QGSCOMPARENEAR( point.x(), 1.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 2.2, 0.001 );
  QCOMPARE( QgsProcessingParameters::parameterAsGeometryCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  point = geometry.asQPointF();
  QGSCOMPARENEAR( point.x(), 122451, 100 );
  QGSCOMPARENEAR( point.y(), 244963, 100 );

  // QgsPointXY
  params.insert( "non_optional", QgsPointXY( 11.1, 12.2 ) );
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context );
  point = geometry.asQPointF();
  QGSCOMPARENEAR( point.x(), 11.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 12.2, 0.001 );

  // with target CRS - should make no difference, because source CRS is unknown
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  point = geometry.asQPointF();
  QGSCOMPARENEAR( point.x(), 11.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 12.2, 0.001 );

  // QgsReferencedPointXY
  params.insert( "non_optional", QgsReferencedPointXY( QgsPointXY( 1.1, 2.2 ), QgsCoordinateReferenceSystem( "EPSG:4326" ) ) );
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context );
  point = geometry.asQPointF();
  QGSCOMPARENEAR( point.x(), 1.1, 0.001 );
  QGSCOMPARENEAR( point.y(), 2.2, 0.001 );
  QCOMPARE( QgsProcessingParameters::parameterAsGeometryCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );

  // with target CRS
  params.insert( "non_optional", QgsReferencedPointXY( QgsPointXY( 1.1, 2.2 ), QgsCoordinateReferenceSystem( "EPSG:4326" ) ) );
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  point = geometry.asQPointF();
  QGSCOMPARENEAR( point.x(), 122451, 100 );
  QGSCOMPARENEAR( point.y(), 244963, 100 );

  // QgsRectangle
  params.insert( "non_optional", QgsRectangle( 11.1, 12.2, 13.3, 14.4 ) );
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context );
  QCOMPARE( geometry.asWkt( 1 ), QStringLiteral( "Polygon ((11.1 12.2, 13.3 12.2, 13.3 14.4, 11.1 14.4, 11.1 12.2))" ) );

  // with target CRS - should make no difference, because source CRS is unknown
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QCOMPARE( geometry.asWkt( 1 ), QStringLiteral( "Polygon ((11.1 12.2, 13.3 12.2, 13.3 14.4, 11.1 14.4, 11.1 12.2))" ) );

  // QgsReferenced Rectangle
  params.insert( "non_optional", QgsReferencedRectangle( QgsRectangle( 11.1, 12.2, 13.3, 14.4 ), QgsCoordinateReferenceSystem( "EPSG:4326" ) ) );
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context );
  QCOMPARE( geometry.asWkt( 1 ), QStringLiteral( "Polygon ((11.1 12.2, 13.3 12.2, 13.3 14.4, 11.1 14.4, 11.1 12.2))" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsGeometryCrs( def.get(), params, context ).authid(), QStringLiteral( "EPSG:4326" ) );

  // with target CRS
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context, QgsCoordinateReferenceSystem( "EPSG:3785" ) );
  QCOMPARE( geometry.constGet()->vertexCount(), 85 );
  QgsRectangle ext = geometry.boundingBox();
  QGSCOMPARENEAR( ext.xMinimum(), 1235646, 100 );
  QGSCOMPARENEAR( ext.xMaximum(), 1480549, 100 );
  QGSCOMPARENEAR( ext.yMinimum(), 1368478, 100 );
  QGSCOMPARENEAR( ext.yMaximum(), 1620147, 100 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a geometry, and nothing you can do will make me one" ) );
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context );
  QVERIFY( geometry.isNull() );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( "LineString( 10 10, 20 20)", context ), QStringLiteral( "'LineString( 10 10, 20 20)'" ) );
  QCOMPARE( def->valueAsPythonString( QgsGeometry::fromWkt( QStringLiteral( "LineString( 10 10, 20 20)" ) ), context ), QStringLiteral( "'LineString (10 10, 20 20)'" ) );

  // With Srid as string
  QCOMPARE( def->valueAsPythonString( QgsReferencedGeometry( QgsGeometry::fromWkt( QStringLiteral( "LineString( 10 10, 20 20)" ) ),
                                      QgsCoordinateReferenceSystem( "EPSG:4326" ) ), context ),
            QStringLiteral( "'CRS=EPSG:4326;LineString (10 10, 20 20)'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterGeometry('non_optional', '', defaultValue='Point(1 2)')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=geometry Point(1 2)" ) );
  std::unique_ptr< QgsProcessingParameterGeometry > fromCode( dynamic_cast< QgsProcessingParameterGeometry * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterGeometry fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterGeometry *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterGeometry *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterGeometry( "optional", QString(), QString( "Point(-1 3)" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( "LineString(10 10, 20 20)" ) );
  QVERIFY( !def->checkValueIsAcceptable( "Point(-1 a)" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  geometry = QgsProcessingParameters::parameterAsGeometry( def.get(), params, context );
  QCOMPARE( geometry.asWkt(), QStringLiteral( "Point (-1 3)" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterGeometry('optional', '', optional=True, defaultValue='Point(-1 3)')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional geometry Point(-1 3)" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterGeometry * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // non optional with filter
  def.reset( new QgsProcessingParameterGeometry( "filtered", QString(), QString( "Point(-1 3)" ), false,
  { QgsWkbTypes::LineGeometry } ) );
  QVERIFY( def->geometryTypes().contains( QgsWkbTypes::LineGeometry ) );
  QVERIFY( def->checkValueIsAcceptable( "LineString(10 10, 20 20)" ) );
  QVERIFY( !def->checkValueIsAcceptable( "Point(1 2)" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterGeometry('filtered', '', geometryTypes=[ QgsWkbTypes.LineGeometry ], defaultValue='Point(-1 3)')" ) );

  QVariantMap map2 = def->toVariantMap();
  QgsProcessingParameterGeometry fromMap2( "x" );
  QVERIFY( fromMap2.fromVariantMap( map2 ) );
  QCOMPARE( fromMap2.name(), def->name() );
  QCOMPARE( fromMap2.description(), def->description() );
  QCOMPARE( fromMap2.flags(), def->flags() );
  QCOMPARE( fromMap2.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap2.geometryTypes(), def->geometryTypes() );
  def.reset( dynamic_cast< QgsProcessingParameterGeometry *>( QgsProcessingParameters::parameterFromVariantMap( map2 ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterGeometry *>( def.get() ) );


}



void TestQgsProcessing::parameterFile()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterFile > def( new QgsProcessingParameterFile( "non_optional", QString(), QgsProcessingParameterFile::File, QString(), QString( "abc.bmp" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "bricks.bmp" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( "  " ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string representing a file
  QVariantMap params;
  params.insert( "non_optional", QString( "def.bmp" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsFile( def.get(), params, context ), QString( "def.bmp" ) );

  // with extension
  def.reset( new QgsProcessingParameterFile( "non_optional", QString(), QgsProcessingParameterFile::File, QStringLiteral( ".bmp" ), QString( "abc.bmp" ), false ) );
  QVERIFY( def->checkValueIsAcceptable( "bricks.bmp" ) );
  QVERIFY( def->checkValueIsAcceptable( "bricks.BMP" ) );
  QVERIFY( !def->checkValueIsAcceptable( "bricks.pcx" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( "bricks.bmp", context ), QStringLiteral( "'bricks.bmp'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFile('non_optional', '', behavior=QgsProcessingParameterFile.File, extension='.bmp', defaultValue='abc.bmp')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=file abc.bmp" ) );
  std::unique_ptr< QgsProcessingParameterFile > fromCode( dynamic_cast< QgsProcessingParameterFile * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->behavior(), def->behavior() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterFile fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.extension(), def->extension() );
  QCOMPARE( fromMap.fileFilter(), def->fileFilter() );
  QCOMPARE( fromMap.behavior(), def->behavior() );
  def.reset( dynamic_cast< QgsProcessingParameterFile *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterFile *>( def.get() ) );

  // with file filter
  def.reset( new QgsProcessingParameterFile( "non_optional", QString(), QgsProcessingParameterFile::File, QStringLiteral( ".bmp" ), QString( "abc.bmp" ), false, QStringLiteral( "PNG Files (*.png)" ) ) );
  QCOMPARE( def->fileFilter(), QStringLiteral( "PNG Files (*.png)" ) );
  QVERIFY( def->extension().isEmpty() );
  QVERIFY( def->checkValueIsAcceptable( "bricks.png" ) );
  QVERIFY( def->checkValueIsAcceptable( "bricks.PNG" ) );
  QVERIFY( !def->checkValueIsAcceptable( "bricks.pcx" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( "bricks.png", context ), QStringLiteral( "'bricks.png'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFile('non_optional', '', behavior=QgsProcessingParameterFile.File, fileFilter='PNG Files (*.png)', defaultValue='abc.bmp')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=file abc.bmp" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFile * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->behavior(), def->behavior() );

  map = def->toVariantMap();
  fromMap = QgsProcessingParameterFile( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.extension(), def->extension() );
  QCOMPARE( fromMap.fileFilter(), def->fileFilter() );
  QCOMPARE( fromMap.behavior(), def->behavior() );
  def.reset( dynamic_cast< QgsProcessingParameterFile *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterFile *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterFile( "optional", QString(), QgsProcessingParameterFile::File, QString(), QString( "gef.bmp" ),  true ) );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "bricks.bmp" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( "  " ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsFile( def.get(), params, context ), QString( "gef.bmp" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFile('optional', '', optional=True, behavior=QgsProcessingParameterFile.File, fileFilter='All files (*.*)', defaultValue='gef.bmp')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional file gef.bmp" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFile * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->behavior(), def->behavior() );

  // folder
  def.reset( new QgsProcessingParameterFile( "optional", QString(), QgsProcessingParameterFile::Folder, QString(), QString( "/home/me" ),  true ) );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFile('optional', '', optional=True, behavior=QgsProcessingParameterFile.Folder, fileFilter='All files (*.*)', defaultValue='/home/me')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional folder /home/me" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFile * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->behavior(), def->behavior() );
}

void TestQgsProcessing::parameterMatrix()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterMatrix > def( new QgsProcessingParameterMatrix( "non_optional", QString(), 3, false, QStringList(), QString(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2,3" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1 << 2 << 3 ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << ( QVariantList() << 1 << 2 << 3 ) << ( QVariantList() << 1 << 2 << 3 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // list
  QVariantMap params;
  params.insert( "non_optional", QVariantList() << 1 << 2 << 3 );
  QCOMPARE( QgsProcessingParameters::parameterAsMatrix( def.get(), params, context ), QVariantList() << 1 << 2 << 3 );

  //string
  params.insert( "non_optional", QString( "4,5,6" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsMatrix( def.get(), params, context ), QVariantList() << 4 << 5 << 6 );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "[5]" ) );
  QCOMPARE( def->valueAsPythonString( QVariantList() << 1 << 2 << 3, context ), QStringLiteral( "[1,2,3]" ) );
  QCOMPARE( def->valueAsPythonString( QVariantList() << ( QVariantList() << 1 << 2 << 3 ) << ( QVariantList() << 1 << 2 << 3 ), context ), QStringLiteral( "[1,2,3,1,2,3]" ) );
  QCOMPARE( def->valueAsPythonString( QVariantList() << ( QVariantList() << 1 << QVariant() << 3 ) << ( QVariantList() << QVariant() << 2 << 3 ), context ), QStringLiteral( "[1,None,3,None,2,3]" ) );
  QCOMPARE( def->valueAsPythonString( QVariantList() << ( QVariantList() << 1 << QString( "" ) << 3 ) << ( QVariantList() << 1 << 2 << QString( "" ) ), context ), QStringLiteral( "[1,'',3,1,2,'']" ) );
  QCOMPARE( def->valueAsPythonString( "1,2,3", context ), QStringLiteral( "[1,2,3]" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMatrix('non_optional', '', numberRows=, hasFixedNumberRows=, headers=[], defaultValue=[None])" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=matrix" ) );
  std::unique_ptr< QgsProcessingParameterMatrix > fromCode( dynamic_cast< QgsProcessingParameterMatrix * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterMatrix fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.headers(), def->headers() );
  QCOMPARE( fromMap.numberRows(), def->numberRows() );
  QCOMPARE( fromMap.hasFixedNumberRows(), def->hasFixedNumberRows() );
  def.reset( dynamic_cast< QgsProcessingParameterMatrix *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterMatrix *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterMatrix( "optional", QString(), 3, false, QStringList(), QVariantList() << 4 << 5 << 6,  true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2,3" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1 << 2 << 3 ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMatrix('optional', '', optional=True, numberRows=, hasFixedNumberRows=, headers=[], defaultValue=[4,5,6])" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional matrix" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMatrix * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsMatrix( def.get(), params, context ), QVariantList() << 4 << 5 << 6 );
  def.reset( new QgsProcessingParameterMatrix( "optional", QString(), 3, false, QStringList(), QString( "1,2,3" ),  true ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMatrix('optional', '', optional=True, numberRows=, hasFixedNumberRows=, headers=[], defaultValue=[1,2,3])" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional matrix 1,2,3" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMatrix * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsMatrix( def.get(), params, context ), QVariantList() << 1 << 2 << 3 );
}

void TestQgsProcessing::parameterLayerList()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon?crs=EPSG:3111", "V4", "memory" );
  QgsVectorLayer *v2 = new QgsVectorLayer( "Polygon?crs=EPSG:3111", "V5", "memory" );
  p.addMapLayers( QList<QgsMapLayer *>() << v1 << v2 << r1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterMultipleLayers > def( new QgsProcessingParameterMultipleLayers( "non_optional", QString(), QgsProcessing::TypeMapLayer, QString(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );

  // should be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );

  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.shp" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.tif" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.2dm" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.*" ) ) );

  // using existing map layer ID
  QVariantMap params;
  params.insert( "non_optional",  v1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 );
  // using existing map layer
  params.insert( "non_optional",  QVariant::fromValue( v1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 );

  // using two existing map layer ID
  params.insert( "non_optional",  QVariantList() << v1->id() << r1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 << r1 );

  // using two existing map layers
  params.insert( "non_optional",  QVariantList() << QVariant::fromValue( v1 ) << QVariant::fromValue( r1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 << r1 );

  // mix of list and single layers (happens from models)
  params.insert( "non_optional",  QVariantList() << QVariant( QVariantList() << QVariant::fromValue( v1 ) << QVariant::fromValue( v2 ) ) << QVariant::fromValue( r1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 << v2 << r1 );

  // mix of two lists (happens from models)
  params.insert( "non_optional",  QVariantList() << QVariant( QVariantList() << QVariant::fromValue( v1 ) << QVariant::fromValue( v2 ) ) << QVariant( QVariantList() << QVariant::fromValue( r1 ) ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 << v2 << r1 );

  // mix of existing layers and non project layer string
  params.insert( "non_optional",  QVariantList() << v1->id() << raster2 );
  QList< QgsMapLayer *> layers = QgsProcessingParameters::parameterAsLayerList( def.get(), params, context );
  QCOMPARE( layers.at( 0 ), v1 );
  QCOMPARE( layers.at( 1 )->publicSource(), raster2 );

  // mix of existing layer and ID (and check we keep order)
  params.insert( "non_optional",  QVariantList() << QVariant::fromValue( v1 ) << v2->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 << v2 );

  params.insert( "non_optional",  QVariantList() << v1->id() << QVariant::fromValue( v2 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 << v2 );

  // empty string
  params.insert( "non_optional",  QString( "" ) );
  QVERIFY( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ).isEmpty() );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a layer, and nothing you can do will make me one" ) );
  QVERIFY( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ).isEmpty() );

  // with 2 min inputs
  def->setMinimumNumberInputs( 2 );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "layer12312312" << "layerB" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "layer12312312" << "layerB" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  def->setMinimumNumberInputs( 3 );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "layer12312312" << "layerB" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "layer12312312" << "layerB" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( "layer12312312", context ), QStringLiteral( "'layer12312312'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( r1 ), context ), QString( QStringLiteral( "['" ) + testDataDir + QStringLiteral( "tenbytenraster.asc']" ) ) );
  QCOMPARE( def->valueAsPythonString( r1->id(), context ), QString( QStringLiteral( "['" ) + testDataDir + QStringLiteral( "tenbytenraster.asc']" ) ) );
  QCOMPARE( def->valueAsPythonString( QStringList() << r1->id() << raster2, context ), QString( QStringLiteral( "['" ) + testDataDir + QStringLiteral( "tenbytenraster.asc','" ) + testDataDir + QStringLiteral( "landsat.tif']" ) ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMultipleLayers('non_optional', '', layerType=QgsProcessing.TypeMapLayer, defaultValue='')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=multiple vector" ) );
  std::unique_ptr< QgsProcessingParameterMultipleLayers > fromCode( dynamic_cast< QgsProcessingParameterMultipleLayers * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
  QCOMPARE( fromCode->layerType(), QgsProcessing::TypeVectorAnyGeometry );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterMultipleLayers fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.layerType(), def->layerType() );
  QCOMPARE( fromMap.minimumNumberInputs(), def->minimumNumberInputs() );
  def.reset( dynamic_cast< QgsProcessingParameterMultipleLayers *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterMultipleLayers *>( def.get() ) );

  // optional with one default layer
  def.reset( new QgsProcessingParameterMultipleLayers( "optional", QString(), QgsProcessing::TypeMapLayer, v1->id(), true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  // should be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );

  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 );

  params.insert( "optional",  QVariantList() << QVariant::fromValue( r1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << r1 );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QString( QStringLiteral( "QgsProcessingParameterMultipleLayers('optional', '', optional=True, layerType=QgsProcessing.TypeMapLayer, defaultValue='" ) + v1->id() + "')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QString( QStringLiteral( "##optional=optional multiple vector " ) + v1->id() ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMultipleLayers * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->layerType(), QgsProcessing::TypeVectorAnyGeometry );

  // optional with two default layers
  def.reset( new QgsProcessingParameterMultipleLayers( "optional", QString(), QgsProcessing::TypeMapLayer, QVariantList() << v1->id() << r1->publicSource(), true ) );
  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 << r1 );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.shp" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.tif" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.2dm" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.*" ) ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QString( QStringLiteral( "QgsProcessingParameterMultipleLayers('optional', '', optional=True, layerType=QgsProcessing.TypeMapLayer, defaultValue=['" ) + r1->publicSource() + "'])" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QString( QStringLiteral( "##optional=optional multiple vector " ) + v1->id() + "," + r1->publicSource() ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMultipleLayers * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QString( v1->id() + "," + r1->publicSource() ) );
  QCOMPARE( fromCode->layerType(), QgsProcessing::TypeVectorAnyGeometry );

  // optional with one default direct layer
  def.reset( new QgsProcessingParameterMultipleLayers( "optional", QString(), QgsProcessing::TypeMapLayer, QVariant::fromValue( v1 ), true ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 );

  // optional with two default direct layers
  def.reset( new QgsProcessingParameterMultipleLayers( "optional", QString(), QgsProcessing::TypeMapLayer, QVariantList() << QVariant::fromValue( v1 ) << QVariant::fromValue( r1 ), true ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << v1 << r1 );

  def.reset( new QgsProcessingParameterMultipleLayers( "type", QString(), QgsProcessing::TypeRaster ) );
  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.shp" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.tif" ) ) );
  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.2dm" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.*" ) ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMultipleLayers('type', '', layerType=QgsProcessing.TypeRaster, defaultValue=None)" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##type=multiple raster" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMultipleLayers * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "type" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
  QCOMPARE( fromCode->layerType(), QgsProcessing::TypeRaster );

  def.reset( new QgsProcessingParameterMultipleLayers( "type", QString(), QgsProcessing::TypeFile ) );
  QCOMPARE( def->createFileFilter(), QStringLiteral( "All files (*.*)" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMultipleLayers('type', '', layerType=QgsProcessing.TypeFile, defaultValue=None)" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##type=multiple file" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMultipleLayers * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "type" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
  QCOMPARE( fromCode->layerType(), def->layerType() );

  // manage QgsProcessingOutputLayerDefinition as parameter value

  // optional with sink to a QgsMapLayer.id()
  def.reset( new QgsProcessingParameterMultipleLayers( "optional", QString(), QgsProcessing::TypeFile ) );
  params.insert( QString( "optional" ), QgsProcessingOutputLayerDefinition( r1->publicSource() ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() << r1 );

  // optional with sink to an empty string
  def.reset( new QgsProcessingParameterMultipleLayers( "optional", QString(), QgsProcessing::TypeFile ) );
  params.insert( QString( "optional" ), QgsProcessingOutputLayerDefinition( QString() ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() );

  // optional with sink to an nonsense string
  def.reset( new QgsProcessingParameterMultipleLayers( "optional", QString(), QgsProcessing::TypeFile ) );
  params.insert( QString( "optional" ), QgsProcessingOutputLayerDefinition( QString( "i'm not a layer, and nothing you can do will make me one" ) ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayerList( def.get(), params, context ), QList< QgsMapLayer *>() );


  // TypeFile
  def = qgis::make_unique< QgsProcessingParameterMultipleLayers >( "non_optional", QString(), QgsProcessing::TypeFile, QString(), false );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "layer12312312" << "a" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );

  params.clear();
  params.insert( "non_optional",  QString( "a" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsFileList( def.get(), params, context ), QStringList() << QStringLiteral( "a" ) );
  params.insert( "non_optional",  QStringList() << "a" );
  QCOMPARE( QgsProcessingParameters::parameterAsFileList( def.get(), params, context ), QStringList() << QStringLiteral( "a" ) );
  params.insert( "non_optional",  QStringList() << "a" << "b" );
  QCOMPARE( QgsProcessingParameters::parameterAsFileList( def.get(), params, context ), QStringList() << QStringLiteral( "a" ) << QStringLiteral( "b" ) );
  params.insert( "non_optional",  QVariantList() << QStringLiteral( "c" ) << QStringLiteral( "d" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsFileList( def.get(), params, context ), QStringList() << QStringLiteral( "c" ) << QStringLiteral( "d" ) );
  // mix of two lists (happens from models)
  params.insert( "non_optional",  QVariantList() << QVariant( QVariantList() << QStringLiteral( "e" ) << QStringLiteral( "f" ) ) << QVariant( QVariantList() << QStringLiteral( "g" ) ) );
  QCOMPARE( QgsProcessingParameters::parameterAsFileList( def.get(), params, context ), QStringList() << QStringLiteral( "e" ) << QStringLiteral( "f" ) << QStringLiteral( "g" ) );

  // with 2 min inputs
  def->setMinimumNumberInputs( 2 );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "layer12312312" << "layerB" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "layer12312312" << "layerB" ) );

  def->setMinimumNumberInputs( 3 );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "layer12312312" << "layerB" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "layer12312312" << "layerB" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( "layer12312312", context ), QStringLiteral( "'layer12312312'" ) );
  QCOMPARE( def->valueAsPythonString( QStringList() << "a" << "B", context ), QStringLiteral( "['a','B']" ) );
  QCOMPARE( def->valueAsPythonString( QVariantList() << "c" << "d", context ), QStringLiteral( "['c','d']" ) );
}

void TestQgsProcessing::parameterDistance()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterDistance > def( new QgsProcessingParameterDistance( "non_optional", QString(), 5, QStringLiteral( "parent" ), false ) );
  QCOMPARE( def->parentParameterName(), QStringLiteral( "parent" ) );
  def->setParentParameterName( QStringLiteral( "parent2" ) );
  QCOMPARE( def->defaultUnit(), QgsUnitTypes::DistanceUnknownUnit );
  def->setDefaultUnit( QgsUnitTypes::DistanceFeet );
  QCOMPARE( def->defaultUnit(), QgsUnitTypes::DistanceFeet );
  std::unique_ptr< QgsProcessingParameterDistance > clone( def->clone() );
  QCOMPARE( clone->parentParameterName(), QStringLiteral( "parent2" ) );
  QCOMPARE( clone->defaultUnit(), QgsUnitTypes::DistanceFeet );

  QCOMPARE( def->parentParameterName(), QStringLiteral( "parent2" ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be acceptable, falls back to default value

  // string representing a number
  QVariantMap params;
  params.insert( "non_optional", QString( "1.1" ) );
  double number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 1.1, 0.001 );

  // double
  params.insert( "non_optional", 1.1 );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 1.1, 0.001 );
  // int
  params.insert( "non_optional", 1 );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 1, 0.001 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a number, and nothing you can do will make me one" ) );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QCOMPARE( number, 5.0 );

  // with min value
  def->setMinimum( 11 );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( 25 ) );
  QVERIFY( def->checkValueIsAcceptable( "21.1" ) );
  // with max value
  def->setMaximum( 21 );
  QVERIFY( !def->checkValueIsAcceptable( 35 ) );
  QVERIFY( !def->checkValueIsAcceptable( "31.1" ) );
  QVERIFY( def->checkValueIsAcceptable( 15 ) );
  QVERIFY( def->checkValueIsAcceptable( "11.1" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "5" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "1.1" ), context ), QStringLiteral( "1.1" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterDistance fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.minimum(), def->minimum() );
  QCOMPARE( fromMap.maximum(), def->maximum() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  QCOMPARE( fromMap.parentParameterName(), QStringLiteral( "parent2" ) );
  QCOMPARE( fromMap.defaultUnit(), QgsUnitTypes::DistanceFeet );
  def.reset( dynamic_cast< QgsProcessingParameterDistance *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterDistance *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterDistance( "optional", QString(), 5.4, QStringLiteral( "parent" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 5.4, 0.001 );
  // unconvertible string
  params.insert( "optional",  QVariant( "aaaa" ) );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 5.4, 0.001 );

  // non-optional, invalid default
  def.reset( new QgsProcessingParameterDistance( "non_optional", QString(), QVariant(), QStringLiteral( "parent" ), false ) );
  QCOMPARE( def->parentParameterName(), QStringLiteral( "parent" ) );
  def->setParentParameterName( QStringLiteral( "parent2" ) );
  QCOMPARE( def->parentParameterName(), QStringLiteral( "parent2" ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) ); // should NOT be acceptable, falls back to invalid default value
}

void TestQgsProcessing::parameterScale()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterScale > def( new QgsProcessingParameterScale( "non_optional", QString(), 5, false ) );

  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be acceptable, falls back to default value

  // string representing a number
  QVariantMap params;
  params.insert( "non_optional", QString( "1.1" ) );
  double number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 1.1, 0.001 );

  // double
  params.insert( "non_optional", 1.1 );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 1.1, 0.001 );
  // int
  params.insert( "non_optional", 1 );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 1, 0.001 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a number, and nothing you can do will make me one" ) );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QCOMPARE( number, 5.0 );

  // with min value
  def->setMinimum( 11 );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( 25 ) );
  QVERIFY( def->checkValueIsAcceptable( "21.1" ) );
  // with max value
  def->setMaximum( 21 );
  QVERIFY( !def->checkValueIsAcceptable( 35 ) );
  QVERIFY( !def->checkValueIsAcceptable( "31.1" ) );
  QVERIFY( def->checkValueIsAcceptable( 15 ) );
  QVERIFY( def->checkValueIsAcceptable( "11.1" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "5" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "1.1" ), context ), QStringLiteral( "1.1" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterScale fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterScale *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterScale *>( def.get() ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterScale('non_optional', '', defaultValue=5)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=scale 5" ) );
  std::unique_ptr< QgsProcessingParameterScale > fromCode( dynamic_cast< QgsProcessingParameterScale * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // optional
  def.reset( new QgsProcessingParameterScale( "optional", QString(), 5.4, true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 5.4, 0.001 );
  // unconvertible string
  params.insert( "optional",  QVariant( "aaaa" ) );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 5.4, 0.001 );

  // non-optional, invalid default
  def.reset( new QgsProcessingParameterScale( "non_optional", QString(), QVariant(), false ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) ); // should NOT be acceptable, falls back to invalid default value
}

void TestQgsProcessing::parameterNumber()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterNumber > def( new QgsProcessingParameterNumber( "non_optional", QString(), QgsProcessingParameterNumber::Double, 5, false ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be acceptable, falls back to default value

  // string representing a number
  QVariantMap params;
  params.insert( "non_optional", QString( "1.1" ) );
  double number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 1.1, 0.001 );
  int iNumber = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( iNumber, 1 );

  // double
  params.insert( "non_optional", 1.1 );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 1.1, 0.001 );
  iNumber = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( iNumber, 1 );

  // int
  params.insert( "non_optional", 1 );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 1, 0.001 );
  iNumber = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( iNumber, 1 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a number, and nothing you can do will make me one" ) );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QCOMPARE( number, 5.0 );
  iNumber = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( iNumber, 5 );

  // with min value
  def->setMinimum( 11 );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( 25 ) );
  QVERIFY( def->checkValueIsAcceptable( "21.1" ) );
  // with max value
  def->setMaximum( 21 );
  QVERIFY( !def->checkValueIsAcceptable( 35 ) );
  QVERIFY( !def->checkValueIsAcceptable( "31.1" ) );
  QVERIFY( def->checkValueIsAcceptable( 15 ) );
  QVERIFY( def->checkValueIsAcceptable( "11.1" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "5" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "1.1" ), context ), QStringLiteral( "1.1" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterNumber('non_optional', '', type=QgsProcessingParameterNumber.Double, minValue=11, maxValue=21, defaultValue=5)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=number 5" ) );
  std::unique_ptr< QgsProcessingParameterNumber > fromCode( dynamic_cast< QgsProcessingParameterNumber * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );


  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterNumber fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.minimum(), def->minimum() );
  QCOMPARE( fromMap.maximum(), def->maximum() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  def.reset( dynamic_cast< QgsProcessingParameterNumber *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterNumber *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterNumber( "optional", QString(), QgsProcessingParameterNumber::Double, 5.4, true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 5.4, 0.001 );
  iNumber = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( iNumber, 5 );
  // unconvertible string
  params.insert( "optional",  QVariant( "aaaa" ) );
  number = QgsProcessingParameters::parameterAsDouble( def.get(), params, context );
  QGSCOMPARENEAR( number, 5.4, 0.001 );
  iNumber = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( iNumber, 5 );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterNumber('optional', '', optional=True, type=QgsProcessingParameterNumber.Double, defaultValue=5.4)" ) );

  code = def->asScriptCode();
  QCOMPARE( code.left( 30 ), QStringLiteral( "##optional=optional number 5.4" ) ); // truncate code to 30, to avoid Qt 5.6 rounding issues
  fromCode.reset( dynamic_cast< QgsProcessingParameterNumber * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterNumber * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##optional=optional number None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );

  // non-optional, invalid default
  def.reset( new QgsProcessingParameterNumber( "non_optional", QString(), QgsProcessingParameterNumber::Double, QVariant(), false ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) ); // should NOT be acceptable, falls back to invalid default value
}

void TestQgsProcessing::parameterRange()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterRange > def( new QgsProcessingParameterRange( "non_optional", QString(), QgsProcessingParameterNumber::Double, QString( "5,6" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1,2,3" ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1,a" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1.1 << 2 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << 1.1 << 2 << 3 ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string representing a range of numbers
  QVariantMap params;
  params.insert( "non_optional", QString( "1.1,1.2" ) );
  QList< double > range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QGSCOMPARENEAR( range.at( 0 ), 1.1, 0.001 );
  QGSCOMPARENEAR( range.at( 1 ), 1.2, 0.001 );

  // list
  params.insert( "non_optional", QVariantList() << 1.1 << 1.2 );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QGSCOMPARENEAR( range.at( 0 ), 1.1, 0.001 );
  QGSCOMPARENEAR( range.at( 1 ), 1.2, 0.001 );

  // too many elements:
  params.insert( "non_optional", QString( "1.1,1.2,1.3" ) );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QGSCOMPARENEAR( range.at( 0 ), 1.1, 0.001 );
  QGSCOMPARENEAR( range.at( 1 ), 1.2, 0.001 );
  params.insert( "non_optional", QVariantList() << 1.1 << 1.2 << 1.3 );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params,  context );
  QGSCOMPARENEAR( range.at( 0 ), 1.1, 0.001 );
  QGSCOMPARENEAR( range.at( 1 ), 1.2, 0.001 );

  // not enough elements - don't care about the result, just don't crash!
  params.insert( "non_optional", QString( "1.1" ) );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  params.insert( "non_optional", QVariantList() << 1.1 );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( "1.1,2", context ), QStringLiteral( "[1.1,2]" ) );
  QCOMPARE( def->valueAsPythonString( QVariantList() << 1.1 << 2, context ), QStringLiteral( "[1.1,2]" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterRange('non_optional', '', type=QgsProcessingParameterNumber.Double, defaultValue=[5,6])" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=range 5,6" ) );
  std::unique_ptr< QgsProcessingParameterRange > fromCode( dynamic_cast< QgsProcessingParameterRange * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterRange fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  def.reset( dynamic_cast< QgsProcessingParameterRange *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterRange *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterRange( "optional", QString(), QgsProcessingParameterNumber::Double, QString( "5.4,7.4" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1.1 << 2 ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QGSCOMPARENEAR( range.at( 0 ), 5.4, 0.001 );
  QGSCOMPARENEAR( range.at( 1 ), 7.4, 0.001 );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterRange('optional', '', optional=True, type=QgsProcessingParameterNumber.Double, defaultValue=[5.4,7.4])" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional range 5.4,7.4" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterRange * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // optional, no default value
  def.reset( new QgsProcessingParameterRange( "optional", QString(), QgsProcessingParameterNumber::Double, QVariant(), true ) );
  QVERIFY( def->checkValueIsAcceptable( "1.1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1.1 << 2 ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QVERIFY( std::isnan( range.at( 0 ) ) );
  QVERIFY( std::isnan( range.at( 1 ) ) );

  params.insert( "optional",  QStringLiteral( "None,2" ) );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QVERIFY( std::isnan( range.at( 0 ) ) );
  QGSCOMPARENEAR( range.at( 1 ), 2, 0.001 );

  params.insert( "optional",  QStringLiteral( "1.2,None" ) );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QGSCOMPARENEAR( range.at( 0 ), 1.2, 0.001 );
  QVERIFY( std::isnan( range.at( 1 ) ) );

  params.insert( "optional",  QStringLiteral( "None,None" ) );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QVERIFY( std::isnan( range.at( 0 ) ) );
  QVERIFY( std::isnan( range.at( 1 ) ) );

  params.insert( "optional",  QStringLiteral( "None" ) );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QVERIFY( std::isnan( range.at( 0 ) ) );
  QVERIFY( std::isnan( range.at( 1 ) ) );

  params.insert( "optional",  QVariant() );
  range = QgsProcessingParameters::parameterAsRange( def.get(), params, context );
  QVERIFY( std::isnan( range.at( 0 ) ) );
  QVERIFY( std::isnan( range.at( 1 ) ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterRange('optional', '', optional=True, type=QgsProcessingParameterNumber.Double, defaultValue=None)" ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterRange * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##optional=optional range None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
}

void TestQgsProcessing::parameterRasterLayer()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon?crs=EPSG:3111", "V4", "memory" );
  p.addMapLayers( QList<QgsMapLayer *>() << v1 << r1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterRasterLayer > def( new QgsProcessingParameterRasterLayer( "non_optional", QString(), QVariant(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );

  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.shp" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.tif" ) ) );
  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.2dm" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.*" ) ) );

  // should be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.tif" ) );
  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.tif", &context ) );

  // using existing map layer ID
  QVariantMap params;
  params.insert( "non_optional",  r1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->id(), r1->id() );

  // using existing map layer
  params.insert( "non_optional",  QVariant::fromValue( r1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->id(), r1->id() );

  // not raster layer
  params.insert( "non_optional",  v1->id() );
  QVERIFY( !QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context ) );

  // using existing vector layer
  params.insert( "non_optional",  QVariant::fromValue( v1 ) );
  QVERIFY( !QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context ) );

  // string representing a project layer source
  params.insert( "non_optional", raster1 );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->id(), r1->id() );
  // string representing a non-project layer source
  params.insert( "non_optional", raster2 );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->publicSource(), raster2 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a layer, and nothing you can do will make me one" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( raster1, context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) ) );
  QCOMPARE( def->valueAsPythonString( r1->id(), context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( r1 ), context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "tenbytenraster.asc'" ) ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterRasterLayer('non_optional', '', defaultValue=None)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=raster" ) );
  std::unique_ptr< QgsProcessingParameterRasterLayer > fromCode( dynamic_cast< QgsProcessingParameterRasterLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // optional
  def.reset( new QgsProcessingParameterRasterLayer( "optional", QString(), r1->id(), true ) );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->id(), r1->id() );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.tif" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->id(), r1->id() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QString( QStringLiteral( "QgsProcessingParameterRasterLayer('optional', '', optional=True, defaultValue='" ) + r1->id() + "')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QString( QStringLiteral( "##optional=optional raster " ) + r1->id() ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterRasterLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterRasterLayer fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterRasterLayer *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterRasterLayer *>( def.get() ) );

  // optional with direct layer
  def.reset( new QgsProcessingParameterRasterLayer( "optional", QString(), QVariant::fromValue( r1 ), true ) );
  QCOMPARE( QgsProcessingParameters::parameterAsRasterLayer( def.get(), params, context )->id(), r1->id() );

  // invalidRasterError
  params.clear();
  QCOMPARE( QgsProcessingAlgorithm::invalidRasterError( params, QStringLiteral( "MISSING" ) ), QStringLiteral( "Could not load source layer for MISSING: no value specified for parameter" ) );
  params.insert( QStringLiteral( "INPUT" ), QStringLiteral( "my layer" ) );
  QCOMPARE( QgsProcessingAlgorithm::invalidRasterError( params, QStringLiteral( "INPUT" ) ), QStringLiteral( "Could not load source layer for INPUT: my layer not found" ) );
  params.insert( QStringLiteral( "INPUT" ), QgsProperty::fromValue( "my prop layer" ) );
  QCOMPARE( QgsProcessingAlgorithm::invalidRasterError( params, QStringLiteral( "INPUT" ) ), QStringLiteral( "Could not load source layer for INPUT: my prop layer not found" ) );
  params.insert( QStringLiteral( "INPUT" ), QVariant::fromValue( v1 ) );
  QCOMPARE( QgsProcessingAlgorithm::invalidRasterError( params, QStringLiteral( "INPUT" ) ), QStringLiteral( "Could not load source layer for INPUT: invalid value" ) );
}

void TestQgsProcessing::parameterEnum()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterEnum > def( new QgsProcessingParameterEnum( "non_optional", QString(), QStringList() << "A" << "B" << "C", false, 2, false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( 0 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" ) );
  QVERIFY( !def->checkValueIsAcceptable( 15 ) );
  QVERIFY( !def->checkValueIsAcceptable( -1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be acceptable, because falls back to default value
  QVERIFY( !def->checkValueIsAcceptable( "B" ) ); // should not be acceptable, because static strings flag is not set
  QVERIFY( !def->checkValueIsAcceptable( "Z" ) ); // should not be acceptable, because static strings flag is not set

  // string representing a number
  QVariantMap params;
  params.insert( "non_optional", QString( "1" ) );
  int iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 1 );

  // double
  params.insert( "non_optional", 2.2 );
  iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 2 );

  // int
  params.insert( "non_optional", 1 );
  iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 1 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a number, and nothing you can do will make me one" ) );
  iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 2 );

  // out of range
  params.insert( "non_optional", 4 );
  iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 2 );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "5" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "1.1" ), context ), QStringLiteral( "1" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterEnum('non_optional', '', options=['A','B','C'], allowMultiple=False, usesStaticStrings=False, defaultValue=2)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=enum A;B;C 2" ) );
  std::unique_ptr< QgsProcessingParameterEnum > fromCode( dynamic_cast< QgsProcessingParameterEnum * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->options(), def->options() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->usesStaticStrings(), def->usesStaticStrings() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterEnum fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.options(), def->options() );
  QCOMPARE( fromMap.allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromMap.usesStaticStrings(), def->usesStaticStrings() );
  def.reset( dynamic_cast< QgsProcessingParameterEnum *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterEnum *>( def.get() ) );

  // multiple
  def.reset( new QgsProcessingParameterEnum( "non_optional", QString(), QStringList() << "A" << "B" << "C", true, 5, false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "1" ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( 0 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() ) ); // since non-optional, empty list not allowed
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" ) );
  QVERIFY( !def->checkValueIsAcceptable( 15 ) );
  QVERIFY( !def->checkValueIsAcceptable( -1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( "B" ) ); // should not be acceptable, because static strings flag is not set
  QVERIFY( !def->checkValueIsAcceptable( "Z" ) ); // should not be acceptable, because static strings flag is not set

  params.insert( "non_optional", QString( "1,2" ) );
  QList< int > iNumbers = QgsProcessingParameters::parameterAsEnums( def.get(), params, context );
  QCOMPARE( iNumbers, QList<int>() << 1 << 2 );
  params.insert( "non_optional", QVariantList() << 0 << 2 );
  iNumbers = QgsProcessingParameters::parameterAsEnums( def.get(), params, context );
  QCOMPARE( iNumbers, QList<int>() << 0 << 2 );

  // empty list
  params.insert( "non_optional", QVariantList() );
  iNumbers = QgsProcessingParameters::parameterAsEnums( def.get(), params, context );
  QCOMPARE( iNumbers, QList<int>() );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QVariantList() << 1 << 2, context ), QStringLiteral( "[1,2]" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "1,2" ), context ), QStringLiteral( "[1,2]" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterEnum('non_optional', '', options=['A','B','C'], allowMultiple=True, usesStaticStrings=False, defaultValue=5)" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=enum multiple A;B;C 5" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterEnum * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->options(), def->options() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->usesStaticStrings(), def->usesStaticStrings() );

  // optional
  def.reset( new QgsProcessingParameterEnum( "optional", QString(), QStringList() << "a" << "b", false, 5, true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( 0 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" ) );
  QVERIFY( !def->checkValueIsAcceptable( 15 ) );
  QVERIFY( !def->checkValueIsAcceptable( -1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterEnum('optional', '', optional=True, options=['a','b'], allowMultiple=False, usesStaticStrings=False, defaultValue=5)" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional enum a;b 5" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterEnum * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->options(), def->options() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->usesStaticStrings(), def->usesStaticStrings() );

  params.insert( "optional",  QVariant() );
  iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 5 );
  // unconvertible string
  params.insert( "optional",  QVariant( "aaaa" ) );
  iNumber = QgsProcessingParameters::parameterAsEnum( def.get(), params, context );
  QCOMPARE( iNumber, 5 );
  //optional with multiples
  def.reset( new QgsProcessingParameterEnum( "optional", QString(), QStringList() << "A" << "B" << "C", true, QVariantList() << 1 << 2, true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "1" ) );
  QVERIFY( def->checkValueIsAcceptable( "1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( 0 ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" ) );
  QVERIFY( !def->checkValueIsAcceptable( 15 ) );
  QVERIFY( !def->checkValueIsAcceptable( -1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( "B" ) ); // should not be acceptable, because static strings flag is not set
  QVERIFY( !def->checkValueIsAcceptable( "Z" ) ); // should not be acceptable, because static strings flag is not set

  params.insert( "optional",  QVariant() );
  iNumbers = QgsProcessingParameters::parameterAsEnums( def.get(), params, context );
  QCOMPARE( iNumbers, QList<int>() << 1 << 2 );
  def.reset( new QgsProcessingParameterEnum( "optional", QString(), QStringList() << "A" << "B" << "C", true, "1,2", true ) );
  params.insert( "optional",  QVariant() );
  iNumbers = QgsProcessingParameters::parameterAsEnums( def.get(), params, context );
  QCOMPARE( iNumbers, QList<int>() << 1 << 2 );
  // empty list
  params.insert( "optional", QVariantList() );
  iNumbers = QgsProcessingParameters::parameterAsEnums( def.get(), params, context );
  QCOMPARE( iNumbers, QList<int>() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterEnum('optional', '', optional=True, options=['A','B','C'], allowMultiple=True, usesStaticStrings=False, defaultValue=[1,2])" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional enum multiple A;B;C 1,2" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterEnum * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->options(), def->options() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->usesStaticStrings(), def->usesStaticStrings() );

  // non optional, no default
  def.reset( new QgsProcessingParameterEnum( "non_optional", QString(), QStringList() << "A" << "B" << "C", false, QVariant(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1,2" ) );
  QVERIFY( def->checkValueIsAcceptable( 0 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" ) );
  QVERIFY( !def->checkValueIsAcceptable( 15 ) );
  QVERIFY( !def->checkValueIsAcceptable( -1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) ); // should NOT be acceptable, because falls back to invalid default value
  QVERIFY( !def->checkValueIsAcceptable( "B" ) ); // should not be acceptable, because static strings flag is not set
  QVERIFY( !def->checkValueIsAcceptable( "Z" ) ); // should not be acceptable, because static strings flag is not set

  // not optional with static strings
  def.reset( new QgsProcessingParameterEnum( "non_optional", QString(), QStringList() << "A" << "B" << "C", false, "B", false, true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( 0 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" ) );
  QVERIFY( !def->checkValueIsAcceptable( 15 ) );
  QVERIFY( !def->checkValueIsAcceptable( -1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be acceptable, because falls back to default value
  QVERIFY( def->checkValueIsAcceptable( "B" ) ); // should be acceptable, because this is a valid enum value
  QVERIFY( !def->checkValueIsAcceptable( "b" ) ); // should not be acceptable, because values are case sensitive
  QVERIFY( !def->checkValueIsAcceptable( "Z" ) ); // should not be acceptable, because value is not in the list of enum values

  // valid enum value
  params.insert( "non_optional", QString( "A" ) );
  QString iString = QgsProcessingParameters::parameterAsEnumString( def.get(), params, context );
  QCOMPARE( iString, QStringLiteral( "A" ) );

  // invalid enum value
  params.insert( "non_optional", QString( "Z" ) );
  iString = QgsProcessingParameters::parameterAsEnumString( def.get(), params, context );
  QCOMPARE( iString, QStringLiteral( "B" ) );

  // lowercase
  params.insert( "non_optional", QString( "a" ) );
  iString = QgsProcessingParameters::parameterAsEnumString( def.get(), params, context );
  QCOMPARE( iString, QStringLiteral( "B" ) );

  // empty string
  params.insert( "non_optional", QString() );
  iString = QgsProcessingParameters::parameterAsEnumString( def.get(), params, context );
  QCOMPARE( iString, QStringLiteral( "B" ) );

  // number
  params.insert( "non_optional", 1 );
  iString = QgsProcessingParameters::parameterAsEnumString( def.get(), params, context );
  QCOMPARE( iString, QStringLiteral( "B" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterEnum('non_optional', '', options=['A','B','C'], allowMultiple=False, usesStaticStrings=True, defaultValue='B')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=enum static A;B;C B" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterEnum * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->options(), def->options() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->usesStaticStrings(), def->usesStaticStrings() );

  // multiple with static strings
  def.reset( new QgsProcessingParameterEnum( "non_optional", QString(), QStringList() << "A" << "B" << "C", true, "B", false, true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "1,2" ) );
  QVERIFY( !def->checkValueIsAcceptable( 0 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "A" ) );
  QVERIFY( !def->checkValueIsAcceptable( 15 ) );
  QVERIFY( !def->checkValueIsAcceptable( -1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be acceptable, because falls back to default value
  QVERIFY( def->checkValueIsAcceptable( "B" ) ); // should be acceptable, because this is a valid enum value
  QVERIFY( !def->checkValueIsAcceptable( "b" ) ); // should not be acceptable, because values are case sensitive
  QVERIFY( !def->checkValueIsAcceptable( "Z" ) ); // should not be acceptable, because value is not in the list of enum values

  // comma-separated string
  params.insert( "non_optional", QString( "A,B" ) );
  QStringList iStrings = QgsProcessingParameters::parameterAsEnumStrings( def.get(), params, context );
  QCOMPARE( iStrings, QStringList() << "A" << "B" );

  // list
  params.insert( "non_optional", QVariantList() << "A" << "C" );
  iStrings = QgsProcessingParameters::parameterAsEnumStrings( def.get(), params, context );
  QCOMPARE( iStrings, QStringList() << "A" << "C" );

  // empty list
  params.insert( "non_optional", QVariantList() );
  iStrings = QgsProcessingParameters::parameterAsEnumStrings( def.get(), params, context );
  QCOMPARE( iStrings, QStringList() << "B" );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterEnum('non_optional', '', options=['A','B','C'], allowMultiple=True, usesStaticStrings=True, defaultValue='B')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=enum multiple static A;B;C B" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterEnum * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->options(), def->options() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->usesStaticStrings(), def->usesStaticStrings() );
}

void TestQgsProcessing::parameterString()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterString > def( new QgsProcessingParameterString( "non_optional", QString(), QString(), false, false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string
  QVariantMap params;
  params.insert( "non_optional", QString( "abcdef" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString( "abcdef" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "'5'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc\ndef" ), context ), QStringLiteral( "'abc\\ndef'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterString('non_optional', '', multiLine=False, defaultValue=None)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=string" ) );
  std::unique_ptr< QgsProcessingParameterString > fromCode( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterString fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.multiLine(), def->multiLine() );
  def.reset( dynamic_cast< QgsProcessingParameterString *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterString *>( def.get() ) );

  def->setMultiLine( true );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterString('non_optional', '', multiLine=True, defaultValue=None)" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=string long" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );
  def->setMultiLine( false );

  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=string None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=string it's mario" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "it's mario" ) );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  def->setDefaultValue( QStringLiteral( "it's mario" ) );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterString('non_optional', '', multiLine=False, defaultValue='it\\'s mario')" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=string 'my val'" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=string \"my val\"" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  // optional
  def.reset( new QgsProcessingParameterString( "optional", QString(), QString( "default" ), false, true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString( "default" ) );
  params.insert( "optional",  QString() ); //empty string should not result in default value
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterString('optional', '', optional=True, multiLine=False, defaultValue='default')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional string default" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  def->setMultiLine( true );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterString('optional', '', optional=True, multiLine=True, defaultValue='default')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional string long default" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterString * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->multiLine(), def->multiLine() );

  // not optional, valid default!
  def.reset( new QgsProcessingParameterString( "non_optional", QString(), QString( "def" ), false, false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be valid, falls back to valid default
}


void TestQgsProcessing::parameterAuthConfig()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterAuthConfig > def( new QgsProcessingParameterAuthConfig( "non_optional", QString(), QString(), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string
  QVariantMap params;
  params.insert( "non_optional", QString( "abcdef" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString( "abcdef" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "'5'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc\ndef" ), context ), QStringLiteral( "'abc\\ndef'" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterAuthConfig('non_optional', '', defaultValue='')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=authcfg" ) );
  std::unique_ptr< QgsProcessingParameterAuthConfig > fromCode( dynamic_cast< QgsProcessingParameterAuthConfig * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterAuthConfig fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterAuthConfig *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterAuthConfig *>( def.get() ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterAuthConfig * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=authcfg None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterAuthConfig * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=authcfg it's mario" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "it's mario" ) );

  def->setDefaultValue( QStringLiteral( "it's mario" ) );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterAuthConfig('non_optional', '', defaultValue='it\\'s mario')" ) );

  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterAuthConfig * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterAuthConfig * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=authcfg 'my val'" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterAuthConfig * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=authcfg \"my val\"" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );

  // optional
  def.reset( new QgsProcessingParameterAuthConfig( "optional", QString(), QString( "default" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString( "default" ) );
  params.insert( "optional",  QString() ); //empty string should not result in default value
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterAuthConfig('optional', '', optional=True, defaultValue='default')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional authcfg default" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterAuthConfig * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // not optional, valid default!
  def.reset( new QgsProcessingParameterAuthConfig( "non_optional", QString(), QString( "def" ), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be valid, falls back to valid default
}

void TestQgsProcessing::parameterExpression()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterExpression > def( new QgsProcessingParameterExpression( "non_optional", QString(), QString( "1+1" ), QString(), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be acceptable, because it will fallback to default value

  // string
  QVariantMap params;
  params.insert( "non_optional", QString( "abcdef" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExpression( def.get(), params, context ), QString( "abcdef" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "'5'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc\ndef" ), context ), QStringLiteral( "'abc\\ndef'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterExpression('non_optional', '', parentLayerParameterName='', defaultValue='1+1')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=expression 1+1" ) );
  std::unique_ptr< QgsProcessingParameterExpression > fromCode( dynamic_cast< QgsProcessingParameterExpression * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterExpression fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.parentLayerParameterName(), def->parentLayerParameterName() );
  def.reset( dynamic_cast< QgsProcessingParameterExpression *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterExpression *>( def.get() ) );

  QVERIFY( def->dependsOnOtherParameters().isEmpty() );
  def->setParentLayerParameterName( QStringLiteral( "test_layer" ) );
  QCOMPARE( def->dependsOnOtherParameters(), QStringList() << QStringLiteral( "test_layer" ) );

  // optional
  def.reset( new QgsProcessingParameterExpression( "optional", QString(), QString( "default" ), QString(), true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsExpression( def.get(), params, context ), QString( "default" ) );
  // valid expression, should not fallback
  params.insert( "optional",  QVariant( "1+2" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExpression( def.get(), params, context ), QString( "1+2" ) );
  // invalid expression, should fallback
  params.insert( "optional",  QVariant( "1+" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsExpression( def.get(), params, context ), QString( "default" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterExpression('optional', '', optional=True, parentLayerParameterName='', defaultValue='default')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional expression default" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterExpression * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // non optional, no default
  def.reset( new QgsProcessingParameterExpression( "non_optional", QString(), QString(), QString(), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) ); // should NOT be acceptable, because it will fallback to invalid default value

}

void TestQgsProcessing::parameterField()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterField > def( new QgsProcessingParameterField( "non_optional", QString(), QVariant(), QString(), QgsProcessingParameterField::Any, false, false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string
  QVariantMap params;
  params.insert( "non_optional", QString( "a" ) );
  QStringList fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "a" );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "probably\'invalid\"field", context ), QStringLiteral( "'probably\\'invalid\\\"field'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterField('non_optional', '', type=QgsProcessingParameterField.Any, parentLayerParameterName='', allowMultiple=False, defaultValue=None)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=field" ) );
  std::unique_ptr< QgsProcessingParameterField > fromCode( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameterName(), def->parentLayerParameterName() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->defaultToAllFields(), def->defaultToAllFields() );

  QVERIFY( def->dependsOnOtherParameters().isEmpty() );
  def->setParentLayerParameterName( "my_parent" );
  QCOMPARE( def->dependsOnOtherParameters(), QStringList() << QStringLiteral( "my_parent" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterField('non_optional', '', type=QgsProcessingParameterField.Any, parentLayerParameterName='my_parent', allowMultiple=False, defaultValue=None)" ) );

  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameterName(), def->parentLayerParameterName() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->defaultToAllFields(), def->defaultToAllFields() );

  def->setDataType( QgsProcessingParameterField::Numeric );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterField('non_optional', '', type=QgsProcessingParameterField.Numeric, parentLayerParameterName='my_parent', allowMultiple=False, defaultValue=None)" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameterName(), def->parentLayerParameterName() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->defaultToAllFields(), def->defaultToAllFields() );

  def->setDataType( QgsProcessingParameterField::String );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterField('non_optional', '', type=QgsProcessingParameterField.String, parentLayerParameterName='my_parent', allowMultiple=False, defaultValue=None)" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameterName(), def->parentLayerParameterName() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->defaultToAllFields(), def->defaultToAllFields() );

  def->setDataType( QgsProcessingParameterField::DateTime );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterField('non_optional', '', type=QgsProcessingParameterField.DateTime, parentLayerParameterName='my_parent', allowMultiple=False, defaultValue=None)" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameterName(), def->parentLayerParameterName() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->defaultToAllFields(), def->defaultToAllFields() );

  // multiple
  def.reset( new QgsProcessingParameterField( "non_optional", QString(), QVariant(), QString(), QgsProcessingParameterField::Any, true, false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "a" << "b" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() ) );

  params.insert( "non_optional", QString( "a;b" ) );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "a" << "b" );
  params.insert( "non_optional", QVariantList() << "a" << "b" );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "a" << "b" );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QStringList() << "a" << "b", context ), QStringLiteral( "['a','b']" ) );
  QCOMPARE( def->valueAsPythonString( QStringList() << "a" << "b", context ), QStringLiteral( "['a','b']" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterField fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.parentLayerParameterName(), def->parentLayerParameterName() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  QCOMPARE( fromMap.allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromMap.defaultToAllFields(), def->defaultToAllFields() );
  def.reset( dynamic_cast< QgsProcessingParameterField *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterField *>( def.get() ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterField('non_optional', '', type=QgsProcessingParameterField.Any, parentLayerParameterName='', allowMultiple=True, defaultValue=None)" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameterName(), def->parentLayerParameterName() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->defaultToAllFields(), def->defaultToAllFields() );

  // default to all fields
  def.reset( new QgsProcessingParameterField( "non_optional", QString(), QVariant(), QString(), QgsProcessingParameterField::Any, true, false, true ) );
  map = def->toVariantMap();
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.parentLayerParameterName(), def->parentLayerParameterName() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  QCOMPARE( fromMap.allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromMap.defaultToAllFields(), def->defaultToAllFields() );
  def.reset( dynamic_cast< QgsProcessingParameterField *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterField *>( def.get() ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterField('non_optional', '', type=QgsProcessingParameterField.Any, parentLayerParameterName='', allowMultiple=True, defaultValue=None, defaultToAllFields=True)" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameterName(), def->parentLayerParameterName() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->defaultToAllFields(), def->defaultToAllFields() );

  // optional
  def.reset( new QgsProcessingParameterField( "optional", QString(), QString( "def" ), QString(), QgsProcessingParameterField::Any, false, true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" << "b" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "def" );

  // optional with string list default
  def.reset( new QgsProcessingParameterField( "optional", QString(), QStringList() << QStringLiteral( "def" ) << QStringLiteral( "abc" ), QString(), QgsProcessingParameterField::Any, true, true ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "a" << "b" ) );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "def" << "abc" );
  params.insert( "optional",  QVariantList() << "f" << "h" );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "f" << "h" );
  params.insert( "optional",  QStringList() << "g" << "h" );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "g" << "h" );

  // optional, no default
  def.reset( new QgsProcessingParameterField( "optional", QString(), QVariant(), QString(), QgsProcessingParameterField::Any, false, true ) );
  params.insert( "optional",  QVariant() );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QVERIFY( fields.isEmpty() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterField('optional', '', optional=True, type=QgsProcessingParameterField.Any, parentLayerParameterName='', allowMultiple=False, defaultValue=None)" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional field" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterField * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameterName(), def->parentLayerParameterName() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );
  QCOMPARE( fromCode->defaultToAllFields(), def->defaultToAllFields() );

  //optional with multiples
  def.reset( new QgsProcessingParameterField( "optional", QString(), QString( "abc;def" ), QString(), QgsProcessingParameterField::Any, true, true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "a" << "b" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "a" << "b" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "abc" << "def" );
  def.reset( new QgsProcessingParameterField( "optional", QString(), QVariantList() << "abc" << "def", QString(), QgsProcessingParameterField::Any, true, true ) );
  params.insert( "optional",  QVariant() );
  fields = QgsProcessingParameters::parameterAsFields( def.get(), params, context );
  QCOMPARE( fields, QStringList() << "abc" << "def" );
}

void TestQgsProcessing::parameterVectorLayer()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString vector1 = testDataDir + "multipoint.shp";
  QString raster = testDataDir + "landsat.tif";
  QFileInfo fi1( raster );
  QFileInfo fi2( vector1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QgsVectorLayer *v1 = new QgsVectorLayer( fi2.filePath(), "V4", "ogr" );
  p.addMapLayers( QList<QgsMapLayer *>() << v1 << r1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterVectorLayer > def( new QgsProcessingParameterVectorLayer( "non_optional", QString(), QList< int >(), QString( "somelayer" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( "layer1231123" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromValue( QStringLiteral( "layer12312312" ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProperty::fromValue( QString() ) ) );

  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.shp" ) ) );
  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.tif" ) ) );
  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.2dm" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.*" ) ) );

  // should be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  // using existing map layer ID
  QVariantMap params;
  params.insert( "non_optional",  v1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context )->id(), v1->id() );

  // using existing layer
  params.insert( "non_optional",  QVariant::fromValue( v1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context )->id(), v1->id() );

  // not vector layer
  params.insert( "non_optional",  r1->id() );
  QVERIFY( !QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context ) );

  // using existing non-vector layer
  params.insert( "non_optional",  QVariant::fromValue( r1 ) );
  QVERIFY( !QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context ) );

  // string representing a layer source
  params.insert( "non_optional", vector1 );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context )->publicSource(), vector1 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a layer, and nothing you can do will make me one" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( vector1, context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "multipoint.shp'" ) ) );
  QCOMPARE( def->valueAsPythonString( v1->id(), context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "multipoint.shp'" ) ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( v1 ), context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "multipoint.shp'" ) ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterVectorLayer('non_optional', '', defaultValue='somelayer')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=vector somelayer" ) );
  std::unique_ptr< QgsProcessingParameterVectorLayer > fromCode( dynamic_cast< QgsProcessingParameterVectorLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterVectorLayer fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterVectorLayer *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterVectorLayer *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterVectorLayer( "optional", QString(), QList< int >(), v1->id(), true ) );
  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params,  context )->id(), v1->id() );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( "layer1231123" ) ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QString( QStringLiteral( "QgsProcessingParameterVectorLayer('optional', '', optional=True, defaultValue='" ) + v1->id() + "')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QString( QStringLiteral( "##optional=optional vector " ) + v1->id() ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterVectorLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  //optional with direct layer default
  def.reset( new QgsProcessingParameterVectorLayer( "optional", QString(), QList< int >(), QVariant::fromValue( v1 ), true ) );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params,  context )->id(), v1->id() );
}

void TestQgsProcessing::parameterMeshLayer()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString vector1 = testDataDir + "multipoint.shp";
  QString raster = testDataDir + "landsat.tif";
  QString mesh = testDataDir + "mesh/quad_and_triangle.2dm";
  QFileInfo fi1( raster );
  QFileInfo fi2( vector1 );
  QFileInfo fi3( mesh );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QgsVectorLayer *v1 = new QgsVectorLayer( fi2.filePath(), "V4", "ogr" );
  QgsMeshLayer *m1 = new QgsMeshLayer( fi3.filePath(), "M1", "mdal" );
  Q_ASSERT( m1 );
  p.addMapLayers( QList<QgsMapLayer *>() << v1 << r1 << m1 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterMeshLayer > def( new QgsProcessingParameterMeshLayer( "non_optional", QString(), QString( "somelayer" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( "layer1231123" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( m1 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromValue( QStringLiteral( "layer12312312" ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProperty::fromValue( QString() ) ) );

  // should be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.2dm" ) );
  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.2dm", &context ) );

  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.shp" ) ) );
  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.tif" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.2dm" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.*" ) ) );

  // using existing map layer ID
  QVariantMap params;
  params.insert( "non_optional",  m1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsMeshLayer( def.get(), params, context )->id(), m1->id() );

  // using existing layer
  params.insert( "non_optional",  QVariant::fromValue( m1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsMeshLayer( def.get(), params, context )->id(), m1->id() );

  // not mesh layer
  params.insert( "non_optional",  r1->id() );
  QVERIFY( !QgsProcessingParameters::parameterAsMeshLayer( def.get(), params, context ) );

  // using existing non-mesh layer
  params.insert( "non_optional",  QVariant::fromValue( r1 ) );
  QVERIFY( !QgsProcessingParameters::parameterAsMeshLayer( def.get(), params, context ) );

  // string representing a layer source
  params.insert( "non_optional", mesh );
  QCOMPARE( QgsProcessingParameters::parameterAsMeshLayer( def.get(), params, context )->publicSource(), mesh );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a layer, and nothing you can do will make me one" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( mesh, context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "mesh/quad_and_triangle.2dm'" ) ) );
  QCOMPARE( def->valueAsPythonString( m1->id(), context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "mesh/quad_and_triangle.2dm'" ) ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( m1 ), context ), QString( QString( "'" ) + testDataDir + QStringLiteral( "mesh/quad_and_triangle.2dm'" ) ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.2dm" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.2dm'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMeshLayer('non_optional', '', defaultValue='somelayer')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=mesh somelayer" ) );
  std::unique_ptr< QgsProcessingParameterMeshLayer > fromCode( dynamic_cast< QgsProcessingParameterMeshLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterMeshLayer fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterMeshLayer *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterMeshLayer *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterMeshLayer( "optional", QString(), m1->id(), true ) );
  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsMeshLayer( def.get(), params,  context )->id(), m1->id() );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.2dm" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( "layer1231123" ) ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QString( QStringLiteral( "QgsProcessingParameterMeshLayer('optional', '', optional=True, defaultValue='" ) + m1->id() + "')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QString( QStringLiteral( "##optional=optional mesh " ) + m1->id() ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMeshLayer * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  //optional with direct layer default
  def.reset( new QgsProcessingParameterMeshLayer( "optional", QString(), QVariant::fromValue( m1 ), true ) );
  QCOMPARE( QgsProcessingParameters::parameterAsMeshLayer( def.get(), params,  context )->id(), m1->id() );
}

void TestQgsProcessing::parameterFeatureSource()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString vector1 = testDataDir + "multipoint.shp";
  QString vector2 = testDataDir + "lines.shp";
  QString raster = testDataDir + "landsat.tif";
  QFileInfo fi1( raster );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QgsVectorLayer *v1 = new QgsVectorLayer( "Polygon?crs=EPSG:3111", "V4", "memory" );
  QgsVectorLayer *v2 = new QgsVectorLayer( vector2, "V5", "ogr" );
  p.addMapLayers( QList<QgsMapLayer *>() << v1 << r1 << v2 );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterFeatureSource > def( new QgsProcessingParameterFeatureSource( "non_optional", QString(), QList< int >() << QgsProcessing::TypeVectorAnyGeometry, QString(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( "layer1231123" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( "" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( v1 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant::fromValue( r1 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromValue( QStringLiteral( "layer12312312" ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProperty::fromValue( QString() ) ) );

  // should be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.shp" ) ) );
  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.tif" ) ) );
  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.2dm" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.*" ) ) );

  // using existing map layer ID
  QVariantMap params;
  params.insert( "non_optional",  v1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context )->id(), v1->id() );

  // using existing layer
  params.insert( "non_optional",  QVariant::fromValue( v1 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context )->id(), v1->id() );

  // not vector layer
  params.insert( "non_optional",  r1->id() );
  QVERIFY( !QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context ) );

  // using existing non-vector layer
  params.insert( "non_optional",  QVariant::fromValue( r1 ) );
  QVERIFY( !QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context ) );

  // string representing a layer source
  params.insert( "non_optional", vector1 );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context )->publicSource(), vector1 );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a layer, and nothing you can do will make me one" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsVectorLayer( def.get(), params, context ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( "abc" ) ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( v2->id() ) ), context ), QStringLiteral( "'%1'" ).arg( vector2 ) );
  QCOMPARE( def->valueAsPythonString( v2->id(), context ), QStringLiteral( "'%1'" ).arg( vector2 ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromValue( "abc" ), true ) ), context ), QStringLiteral( "QgsProcessingFeatureSourceDefinition('abc', selectedFeaturesOnly=True, featureLimit=-1, geometryCheck=QgsFeatureRequest.GeometryAbortOnInvalid)" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ) ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"abc\" || \"def\"')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ), true ) ), context ), QStringLiteral( "QgsProcessingFeatureSourceDefinition(QgsProperty.fromExpression('\"abc\" || \"def\"'), selectedFeaturesOnly=True, featureLimit=-1, geometryCheck=QgsFeatureRequest.GeometryAbortOnInvalid)" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromValue( "abc" ), false, 11 ) ), context ), QStringLiteral( "QgsProcessingFeatureSourceDefinition('abc', selectedFeaturesOnly=False, featureLimit=11, geometryCheck=QgsFeatureRequest.GeometryAbortOnInvalid)" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ), false, 11 ) ), context ), QStringLiteral( "QgsProcessingFeatureSourceDefinition(QgsProperty.fromExpression('\"abc\" || \"def\"'), selectedFeaturesOnly=False, featureLimit=11, geometryCheck=QgsFeatureRequest.GeometryAbortOnInvalid)" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromValue( "abc" ), false, -1, QgsProcessingFeatureSourceDefinition::Flags(), QgsFeatureRequest::GeometrySkipInvalid ) ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ), false, -1, QgsProcessingFeatureSourceDefinition::Flags(), QgsFeatureRequest::GeometrySkipInvalid ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"abc\" || \"def\"')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromValue( "abc" ), false, -1, QgsProcessingFeatureSourceDefinition::Flag::FlagOverrideDefaultGeometryCheck, QgsFeatureRequest::GeometrySkipInvalid ) ), context ), QStringLiteral( "QgsProcessingFeatureSourceDefinition('abc', selectedFeaturesOnly=False, featureLimit=-1, flags=QgsProcessingFeatureSourceDefinition.FlagOverrideDefaultGeometryCheck, geometryCheck=QgsFeatureRequest.GeometrySkipInvalid)" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ), false, -1, QgsProcessingFeatureSourceDefinition::Flag::FlagOverrideDefaultGeometryCheck, QgsFeatureRequest::GeometrySkipInvalid ) ), context ), QStringLiteral( "QgsProcessingFeatureSourceDefinition(QgsProperty.fromExpression('\"abc\" || \"def\"'), selectedFeaturesOnly=False, featureLimit=-1, flags=QgsProcessingFeatureSourceDefinition.FlagOverrideDefaultGeometryCheck, geometryCheck=QgsFeatureRequest.GeometrySkipInvalid)" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromValue( "abc" ), false, -1, QgsProcessingFeatureSourceDefinition::Flag::FlagCreateIndividualOutputPerInputFeature, QgsFeatureRequest::GeometrySkipInvalid ) ), context ), QStringLiteral( "QgsProcessingFeatureSourceDefinition('abc', selectedFeaturesOnly=False, featureLimit=-1, flags=QgsProcessingFeatureSourceDefinition.FlagCreateIndividualOutputPerInputFeature, geometryCheck=QgsFeatureRequest.GeometrySkipInvalid)" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ), false, -1, QgsProcessingFeatureSourceDefinition::Flag::FlagCreateIndividualOutputPerInputFeature, QgsFeatureRequest::GeometrySkipInvalid ) ), context ), QStringLiteral( "QgsProcessingFeatureSourceDefinition(QgsProperty.fromExpression('\"abc\" || \"def\"'), selectedFeaturesOnly=False, featureLimit=-1, flags=QgsProcessingFeatureSourceDefinition.FlagCreateIndividualOutputPerInputFeature, geometryCheck=QgsFeatureRequest.GeometrySkipInvalid)" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromValue( "abc" ), false, -1, QgsProcessingFeatureSourceDefinition::Flag::FlagOverrideDefaultGeometryCheck | QgsProcessingFeatureSourceDefinition::Flag::FlagCreateIndividualOutputPerInputFeature, QgsFeatureRequest::GeometrySkipInvalid ) ), context ), QStringLiteral( "QgsProcessingFeatureSourceDefinition('abc', selectedFeaturesOnly=False, featureLimit=-1, flags=QgsProcessingFeatureSourceDefinition.FlagOverrideDefaultGeometryCheck | QgsProcessingFeatureSourceDefinition.FlagCreateIndividualOutputPerInputFeature, geometryCheck=QgsFeatureRequest.GeometrySkipInvalid)" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingFeatureSourceDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ), false, -1, QgsProcessingFeatureSourceDefinition::Flag::FlagOverrideDefaultGeometryCheck | QgsProcessingFeatureSourceDefinition::Flag::FlagCreateIndividualOutputPerInputFeature, QgsFeatureRequest::GeometrySkipInvalid ) ), context ), QStringLiteral( "QgsProcessingFeatureSourceDefinition(QgsProperty.fromExpression('\"abc\" || \"def\"'), selectedFeaturesOnly=False, featureLimit=-1, flags=QgsProcessingFeatureSourceDefinition.FlagOverrideDefaultGeometryCheck | QgsProcessingFeatureSourceDefinition.FlagCreateIndividualOutputPerInputFeature, geometryCheck=QgsFeatureRequest.GeometrySkipInvalid)" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( v2 ), context ), QStringLiteral( "'%1'" ).arg( vector2 ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "postgres://uri='complex' username=\"complex\"" ), context ), QStringLiteral( "'postgres://uri=\\'complex\\' username=\\\"complex\\\"'" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterFeatureSource fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.dataTypes(), def->dataTypes() );
  def.reset( dynamic_cast< QgsProcessingParameterFeatureSource *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterFeatureSource *>( def.get() ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSource('non_optional', '', types=[QgsProcessing.TypeVectorAnyGeometry], defaultValue='')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=source" ) );
  std::unique_ptr< QgsProcessingParameterFeatureSource > fromCode( dynamic_cast< QgsProcessingParameterFeatureSource * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  def->setDataTypes( QList< int >() << QgsProcessing::TypeVectorPoint );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSource('non_optional', '', types=[QgsProcessing.TypeVectorPoint], defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=source point" ) );
  def->setDataTypes( QList< int >() << QgsProcessing::TypeVectorLine );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSource('non_optional', '', types=[QgsProcessing.TypeVectorLine], defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=source line" ) );
  def->setDataTypes( QList< int >() << QgsProcessing::TypeVectorPolygon );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSource('non_optional', '', types=[QgsProcessing.TypeVectorPolygon], defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=source polygon" ) );
  def->setDataTypes( QList< int >() << QgsProcessing::TypeVectorPoint << QgsProcessing::TypeVectorLine );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSource('non_optional', '', types=[QgsProcessing.TypeVectorPoint,QgsProcessing.TypeVectorLine], defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=source point line" ) );
  def->setDataTypes( QList< int >() << QgsProcessing::TypeVectorPoint << QgsProcessing::TypeVectorPolygon );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSource('non_optional', '', types=[QgsProcessing.TypeVectorPoint,QgsProcessing.TypeVectorPolygon], defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=source point polygon" ) );


  // optional
  def.reset( new QgsProcessingParameterFeatureSource( "optional", QString(), QList< int >() << QgsProcessing::TypeVectorAnyGeometry, v1->id(), true ) );
  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params,  context )->id(), v1->id() );
  QVERIFY( def->checkValueIsAcceptable( false ) );
  QVERIFY( def->checkValueIsAcceptable( true ) );
  QVERIFY( def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingFeatureSourceDefinition( "layer1231123" ) ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QString( QStringLiteral( "QgsProcessingParameterFeatureSource('optional', '', optional=True, types=[QgsProcessing.TypeVectorAnyGeometry], defaultValue='" ) + v1->id() + "')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QString( QStringLiteral( "##optional=optional source " ) + v1->id() ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFeatureSource * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );


  //optional with direct layer default
  def.reset( new QgsProcessingParameterFeatureSource( "optional", QString(), QList< int >() << QgsProcessing::TypeVectorAnyGeometry, QVariant::fromValue( v1 ), true ) );
  QCOMPARE( QgsProcessingParameters::parameterAsVectorLayer( def.get(), params,  context )->id(), v1->id() );

  // invalidSourceError
  params.clear();
  QCOMPARE( QgsProcessingAlgorithm::invalidSourceError( params, QStringLiteral( "MISSING" ) ), QStringLiteral( "Could not load source layer for MISSING: no value specified for parameter" ) );
  params.insert( QStringLiteral( "INPUT" ), QStringLiteral( "my layer" ) );
  QCOMPARE( QgsProcessingAlgorithm::invalidSourceError( params, QStringLiteral( "INPUT" ) ), QStringLiteral( "Could not load source layer for INPUT: my layer not found" ) );
  params.insert( QStringLiteral( "INPUT" ), QgsProperty::fromValue( "my prop layer" ) );
  QCOMPARE( QgsProcessingAlgorithm::invalidSourceError( params, QStringLiteral( "INPUT" ) ), QStringLiteral( "Could not load source layer for INPUT: my prop layer not found" ) );
  params.insert( QStringLiteral( "INPUT" ), QgsProcessingFeatureSourceDefinition( QStringLiteral( "my prop layer" ) ) );
  QCOMPARE( QgsProcessingAlgorithm::invalidSourceError( params, QStringLiteral( "INPUT" ) ), QStringLiteral( "Could not load source layer for INPUT: my prop layer not found" ) );
  params.insert( QStringLiteral( "INPUT" ), QVariant::fromValue( v1 ) );
  QCOMPARE( QgsProcessingAlgorithm::invalidSourceError( params, QStringLiteral( "INPUT" ) ), QStringLiteral( "Could not load source layer for INPUT: invalid value" ) );
  params.insert( QStringLiteral( "INPUT" ), QgsProcessingOutputLayerDefinition( QStringLiteral( "my prop layer" ) ) );
  QCOMPARE( QgsProcessingAlgorithm::invalidSourceError( params, QStringLiteral( "INPUT" ) ), QStringLiteral( "Could not load source layer for INPUT: my prop layer not found" ) );
}

void TestQgsProcessing::parameterFeatureSink()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterFeatureSink > def( new QgsProcessingParameterFeatureSink( "non_optional", QString(), QgsProcessing::TypeVectorAnyGeometry, QString(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromValue( QStringLiteral( "layer12312312" ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProperty::fromValue( QString() ) ) );

  // should be OK with or without context - it's an output layer!
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( "abc" ) ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromValue( "abc" ) ) ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ) ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"abc\" || \"def\"')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "gpkg" ) );
  QCOMPARE( def->generateTemporaryDestination(), QStringLiteral( "memory:" ) );
  def->setSupportsNonFileBasedOutput( false );
  QVERIFY( def->generateTemporaryDestination().endsWith( QLatin1String( ".gpkg" ) ) );
  QVERIFY( def->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );

  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.shp" ) ) );
  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.tif" ) ) );
  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.2dm" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.*" ) ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterFeatureSink fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  QCOMPARE( fromMap.supportsNonFileBasedOutput(), def->supportsNonFileBasedOutput() );
  def.reset( dynamic_cast< QgsProcessingParameterFeatureSink *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterFeatureSink *>( def.get() ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSink('non_optional', '', type=QgsProcessing.TypeVectorAnyGeometry, createByDefault=True, defaultValue='')" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=sink" ) );
  std::unique_ptr< QgsProcessingParameterFeatureSink > fromCode( dynamic_cast< QgsProcessingParameterFeatureSink * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->dataType(), def->dataType() );

  def->setDataType( QgsProcessing::TypeVectorPoint );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSink('non_optional', '', type=QgsProcessing.TypeVectorPoint, createByDefault=True, defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=sink point" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFeatureSink * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  def->setDataType( QgsProcessing::TypeVectorLine );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSink('non_optional', '', type=QgsProcessing.TypeVectorLine, createByDefault=True, defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=sink line" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFeatureSink * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  def->setDataType( QgsProcessing::TypeVectorPolygon );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSink('non_optional', '', type=QgsProcessing.TypeVectorPolygon, createByDefault=True, defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=sink polygon" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFeatureSink * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  def->setDataType( QgsProcessing::TypeVector );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSink('non_optional', '', type=QgsProcessing.TypeVector, createByDefault=True, defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=sink table" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFeatureSink * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );

  // optional
  def.reset( new QgsProcessingParameterFeatureSink( "optional", QString(), QgsProcessing::TypeVectorAnyGeometry, QString(), true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );
  def->setCreateByDefault( false );
  QVERIFY( !def->createByDefault() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSink('optional', '', optional=True, type=QgsProcessing.TypeVectorAnyGeometry, createByDefault=False, defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional sink" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFeatureSink * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  QVERIFY( !def->createByDefault() );

  // test hasGeometry
  QVERIFY( QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessing::TypeMapLayer ).hasGeometry() );
  QVERIFY( QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessing::TypeVectorAnyGeometry ).hasGeometry() );
  QVERIFY( QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessing::TypeVectorPoint ).hasGeometry() );
  QVERIFY( QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessing::TypeVectorLine ).hasGeometry() );
  QVERIFY( QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessing::TypeVectorPolygon ).hasGeometry() );
  QVERIFY( !QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessing::TypeRaster ).hasGeometry() );
  QVERIFY( !QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessing::TypeFile ).hasGeometry() );
  QVERIFY( !QgsProcessingParameterFeatureSink( "test", QString(), QgsProcessing::TypeVector ).hasGeometry() );

  // invalidSinkError
  QVariantMap params;
  QCOMPARE( QgsProcessingAlgorithm::invalidSinkError( params, QStringLiteral( "MISSING" ) ), QStringLiteral( "Could not create destination layer for MISSING: no value specified for parameter" ) );
  params.insert( QStringLiteral( "OUTPUT" ), QStringLiteral( "d:/test.shp" ) );
  QCOMPARE( QgsProcessingAlgorithm::invalidSinkError( params, QStringLiteral( "OUTPUT" ) ), QStringLiteral( "Could not create destination layer for OUTPUT: d:/test.shp" ) );
  params.insert( QStringLiteral( "OUTPUT" ), QgsProperty::fromValue( QStringLiteral( "d:/test2.shp" ) ) );
  QCOMPARE( QgsProcessingAlgorithm::invalidSinkError( params, QStringLiteral( "OUTPUT" ) ), QStringLiteral( "Could not create destination layer for OUTPUT: d:/test2.shp" ) );
  params.insert( QStringLiteral( "OUTPUT" ), QgsProcessingOutputLayerDefinition( QStringLiteral( "d:/test3.shp" ) ) );
  QCOMPARE( QgsProcessingAlgorithm::invalidSinkError( params, QStringLiteral( "OUTPUT" ) ), QStringLiteral( "Could not create destination layer for OUTPUT: d:/test3.shp" ) );
  params.insert( QStringLiteral( "OUTPUT" ), QgsProcessingFeatureSourceDefinition( QStringLiteral( "source" ) ) );
  QCOMPARE( QgsProcessingAlgorithm::invalidSinkError( params, QStringLiteral( "OUTPUT" ) ), QStringLiteral( "Could not create destination layer for OUTPUT: invalid value" ) );

  // test supported output vector layer extensions

  def.reset( new QgsProcessingParameterFeatureSink( "with_geom", QString(), QgsProcessing::TypeVectorAnyGeometry, QString(), true ) );
  DummyProvider3 provider;
  provider.loadAlgorithms();
  def->mOriginalProvider = &provider;
  QCOMPARE( def->supportedOutputVectorLayerExtensions().count(), 2 );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().at( 0 ), QStringLiteral( "mif" ) );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().at( 1 ), QStringLiteral( "tab" ) );
  def->mOriginalProvider = nullptr;
  def->mAlgorithm = const_cast< QgsProcessingAlgorithm * >( provider.algorithms().at( 0 ) );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().count(), 2 );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().at( 0 ), QStringLiteral( "mif" ) );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().at( 1 ), QStringLiteral( "tab" ) );

  def.reset( new QgsProcessingParameterFeatureSink( "no_geom", QString(), QgsProcessing::TypeVector, QString(), true ) );
  def->mOriginalProvider = &provider;
  QCOMPARE( def->supportedOutputVectorLayerExtensions().count(), 1 );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().at( 0 ), QStringLiteral( "dbf" ) );
  def->mOriginalProvider = nullptr;
  def->mAlgorithm = const_cast< QgsProcessingAlgorithm * >( provider.algorithms().at( 0 ) );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().count(), 1 );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().at( 0 ), QStringLiteral( "dbf" ) );
}

void TestQgsProcessing::parameterVectorOut()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterVectorDestination > def( new QgsProcessingParameterVectorDestination( "non_optional", QString(), QgsProcessing::TypeVectorAnyGeometry, QString(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromValue( QStringLiteral( "layer1231123" ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProperty::fromValue( QString() ) ) );

  // should be OK with or without context - it's an output layer!
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( "abc" ) ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromValue( "abc" ) ) ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ) ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"abc\" || \"def\"')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "gpkg" ) );
  QVERIFY( def->generateTemporaryDestination().endsWith( QLatin1String( ".gpkg" ) ) );
  QVERIFY( def->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );

  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.shp" ) ) );
  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.tif" ) ) );
  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.2dm" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.*" ) ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterVectorDestination fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  def.reset( dynamic_cast< QgsProcessingParameterVectorDestination *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterVectorDestination *>( def.get() ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterVectorDestination('non_optional', '', type=QgsProcessing.TypeVectorAnyGeometry, createByDefault=True, defaultValue='')" ) );
  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=vectorDestination" ) );
  std::unique_ptr< QgsProcessingParameterVectorDestination > fromCode( dynamic_cast< QgsProcessingParameterVectorDestination * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->dataType(), def->dataType() );

  def->setDataType( QgsProcessing::TypeVectorPoint );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterVectorDestination('non_optional', '', type=QgsProcessing.TypeVectorPoint, createByDefault=True, defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=vectorDestination point" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterVectorDestination * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  def->setDataType( QgsProcessing::TypeVectorLine );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterVectorDestination('non_optional', '', type=QgsProcessing.TypeVectorLine, createByDefault=True, defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=vectorDestination line" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterVectorDestination * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );
  def->setDataType( QgsProcessing::TypeVectorPolygon );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterVectorDestination('non_optional', '', type=QgsProcessing.TypeVectorPolygon, createByDefault=True, defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=vectorDestination polygon" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterVectorDestination * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->dataType(), def->dataType() );

  // optional
  def.reset( new QgsProcessingParameterVectorDestination( "optional", QString(), QgsProcessing::TypeVectorAnyGeometry, QString(), true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterVectorDestination('optional', '', optional=True, type=QgsProcessing.TypeVectorAnyGeometry, createByDefault=True, defaultValue='')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional vectorDestination" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterVectorDestination * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->dataType(), def->dataType() );

  // test hasGeometry
  QVERIFY( QgsProcessingParameterVectorDestination( "test", QString(), QgsProcessing::TypeMapLayer ).hasGeometry() );
  QVERIFY( QgsProcessingParameterVectorDestination( "test", QString(), QgsProcessing::TypeVectorAnyGeometry ).hasGeometry() );
  QVERIFY( QgsProcessingParameterVectorDestination( "test", QString(), QgsProcessing::TypeVectorPoint ).hasGeometry() );
  QVERIFY( QgsProcessingParameterVectorDestination( "test", QString(), QgsProcessing::TypeVectorLine ).hasGeometry() );
  QVERIFY( QgsProcessingParameterVectorDestination( "test", QString(), QgsProcessing::TypeVectorPolygon ).hasGeometry() );
  QVERIFY( !QgsProcessingParameterVectorDestination( "test", QString(), QgsProcessing::TypeRaster ).hasGeometry() );
  QVERIFY( !QgsProcessingParameterVectorDestination( "test", QString(), QgsProcessing::TypeFile ).hasGeometry() );
  QVERIFY( !QgsProcessingParameterVectorDestination( "test", QString(), QgsProcessing::TypeVector ).hasGeometry() );

  // test layers to load on completion
  def.reset( new QgsProcessingParameterVectorDestination( "x", QStringLiteral( "desc" ), QgsProcessing::TypeVectorAnyGeometry, QString(), true ) );
  QgsProcessingOutputLayerDefinition fs = QgsProcessingOutputLayerDefinition( QStringLiteral( "test.shp" ) );
  fs.destinationProject = &p;
  QVariantMap params;
  params.insert( QStringLiteral( "x" ), QVariant::fromValue( fs ) );
  QCOMPARE( QgsProcessingParameters::parameterAsOutputLayer( def.get(), params, context ), QStringLiteral( "test.shp" ) );

  // make sure layer was automatically added to list to load on completion
  QCOMPARE( context.layersToLoadOnCompletion().size(), 1 );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 0 ), QStringLiteral( "test.shp" ) );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "desc" ) );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).layerTypeHint, QgsProcessingUtils::LayerHint::Vector );

  // with name overloading
  QgsProcessingContext context2;
  fs = QgsProcessingOutputLayerDefinition( QStringLiteral( "test.shp" ) );
  fs.destinationProject = &p;
  fs.destinationName = QStringLiteral( "my_dest" );
  params.insert( QStringLiteral( "x" ), QVariant::fromValue( fs ) );
  QCOMPARE( QgsProcessingParameters::parameterAsOutputLayer( def.get(), params, context2 ), QStringLiteral( "test.shp" ) );
  QCOMPARE( context2.layersToLoadOnCompletion().size(), 1 );
  QCOMPARE( context2.layersToLoadOnCompletion().keys().at( 0 ), QStringLiteral( "test.shp" ) );
  QCOMPARE( context2.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "my_dest" ) );
  QCOMPARE( context2.layersToLoadOnCompletion().values().at( 0 ).outputName, QStringLiteral( "x" ) );
  QCOMPARE( context2.layersToLoadOnCompletion().values().at( 0 ).layerTypeHint, QgsProcessingUtils::LayerHint::Vector );

  QgsProcessingContext context3;
  params.insert( QStringLiteral( "x" ), QgsProcessing::TEMPORARY_OUTPUT );
  QCOMPARE( QgsProcessingParameters::parameterAsOutputLayer( def.get(), params, context3 ).right( 6 ), QStringLiteral( "x.gpkg" ) );

  QgsProcessingContext context4;
  fs.sink = QgsProperty::fromValue( QgsProcessing::TEMPORARY_OUTPUT );
  params.insert( QStringLiteral( "x" ), QVariant::fromValue( fs ) );
  QCOMPARE( QgsProcessingParameters::parameterAsOutputLayer( def.get(), params, context4 ).right( 6 ), QStringLiteral( "x.gpkg" ) );

  // test supported output vector layer extensions

  def.reset( new QgsProcessingParameterVectorDestination( "with_geom", QString(), QgsProcessing::TypeVectorAnyGeometry, QString(), true ) );
  DummyProvider3 provider;
  QString error;
  QVERIFY( provider.isSupportedOutputValue( QVariant(), def.get(), context, error ) ); // optional
  QVERIFY( provider.isSupportedOutputValue( QString(), def.get(), context, error ) ); // optional
  QVERIFY( !provider.isSupportedOutputValue( "d:/test.shp", def.get(), context, error ) );
  QVERIFY( !provider.isSupportedOutputValue( "d:/test.SHP", def.get(), context, error ) );
  QVERIFY( !provider.isSupportedOutputValue( "ogr:d:/test.shp", def.get(), context, error ) );
  QVERIFY( !provider.isSupportedOutputValue( QgsProcessingOutputLayerDefinition( "d:/test.SHP" ), def.get(), context, error ) );
  QVERIFY( provider.isSupportedOutputValue( "d:/test.mif", def.get(), context, error ) );
  QVERIFY( provider.isSupportedOutputValue( "d:/test.MIF", def.get(), context, error ) );
  QVERIFY( provider.isSupportedOutputValue( "ogr:d:/test.MIF", def.get(), context, error ) );
  QVERIFY( provider.isSupportedOutputValue( QgsProcessingOutputLayerDefinition( "d:/test.MIF" ), def.get(), context, error ) );
  def.reset( new QgsProcessingParameterVectorDestination( "with_geom", QString(), QgsProcessing::TypeVectorAnyGeometry, QString(), false ) );
  QVERIFY( !provider.isSupportedOutputValue( QVariant(), def.get(), context, error ) ); // non-optional
  QVERIFY( !provider.isSupportedOutputValue( QString(), def.get(), context, error ) ); // non-optional

  provider.loadAlgorithms();
  def->mOriginalProvider = &provider;
  QCOMPARE( def->supportedOutputVectorLayerExtensions().count(), 2 );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().at( 0 ), QStringLiteral( "mif" ) );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().at( 1 ), QStringLiteral( "tab" ) );
  def->mOriginalProvider = nullptr;
  def->mAlgorithm = const_cast< QgsProcessingAlgorithm * >( provider.algorithms().at( 0 ) );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().count(), 2 );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().at( 0 ), QStringLiteral( "mif" ) );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().at( 1 ), QStringLiteral( "tab" ) );

  def.reset( new QgsProcessingParameterVectorDestination( "no_geom", QString(), QgsProcessing::TypeVector, QString(), true ) );
  def->mOriginalProvider = &provider;
  QCOMPARE( def->supportedOutputVectorLayerExtensions().count(), 1 );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().at( 0 ), QStringLiteral( "dbf" ) );
  def->mOriginalProvider = nullptr;
  def->mAlgorithm = const_cast< QgsProcessingAlgorithm * >( provider.algorithms().at( 0 ) );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().count(), 1 );
  QCOMPARE( def->supportedOutputVectorLayerExtensions().at( 0 ), QStringLiteral( "dbf" ) );
}

void TestQgsProcessing::parameterRasterOut()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterRasterDestination > def( new QgsProcessingParameterRasterDestination( "non_optional", QString(), QVariant(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromValue( QStringLiteral( "layer12312312" ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProperty::fromValue( QString() ) ) );

  // should be OK with or without context - it's an output layer!
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.tif" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.tif", &context ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "tif" ) );
  QVERIFY( def->generateTemporaryDestination().endsWith( QLatin1String( ".tif" ) ) );
  QVERIFY( def->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );

  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.shp" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.tif" ) ) );
  QVERIFY( !def->createFileFilter().contains( QStringLiteral( "*.2dm" ) ) );
  QVERIFY( def->createFileFilter().contains( QStringLiteral( "*.*" ) ) );

  QVariantMap params;
  params.insert( "non_optional", "test.tif" );
  QCOMPARE( QgsProcessingParameters::parameterAsOutputLayer( def.get(), params, context ), QStringLiteral( "test.tif" ) );
  params.insert( "non_optional", QgsProcessingOutputLayerDefinition( "test.tif" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsOutputLayer( def.get(), params, context ), QStringLiteral( "test.tif" ) );

  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( "abc" ) ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromValue( "abc" ) ) ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ) ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"abc\" || \"def\"')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterRasterDestination fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.supportsNonFileBasedOutput(), def->supportsNonFileBasedOutput() );
  def.reset( dynamic_cast< QgsProcessingParameterRasterDestination *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterRasterDestination *>( def.get() ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterRasterDestination('non_optional', '', createByDefault=True, defaultValue=None)" ) );
  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=rasterDestination" ) );
  std::unique_ptr< QgsProcessingParameterRasterDestination > fromCode( dynamic_cast< QgsProcessingParameterRasterDestination * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // optional
  def.reset( new QgsProcessingParameterRasterDestination( "optional", QString(), QString( "default.tif" ), true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.tif" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );

  params.insert( "optional", QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsOutputLayer( def.get(), params, context ), QStringLiteral( "default.tif" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterRasterDestination('optional', '', optional=True, createByDefault=True, defaultValue='default.tif')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional rasterDestination default.tif" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterRasterDestination * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  DummyProvider3 provider;
  QString error;
  QVERIFY( !provider.isSupportedOutputValue( QVariant(), def.get(), context, error ) );
  QVERIFY( !provider.isSupportedOutputValue( QString(), def.get(), context, error ) );
  QVERIFY( !provider.isSupportedOutputValue( "d:/test.tif", def.get(), context, error ) );
  QVERIFY( !provider.isSupportedOutputValue( "d:/test.TIF", def.get(), context, error ) );
  QVERIFY( !provider.isSupportedOutputValue( QgsProcessingOutputLayerDefinition( "d:/test.tif" ), def.get(), context, error ) );
  QVERIFY( provider.isSupportedOutputValue( "d:/test.mig", def.get(), context, error ) );
  QVERIFY( provider.isSupportedOutputValue( "d:/test.MIG", def.get(), context, error ) );
  QVERIFY( provider.isSupportedOutputValue( QgsProcessingOutputLayerDefinition( "d:/test.MIG" ), def.get(), context, error ) );

  // test layers to load on completion
  def.reset( new QgsProcessingParameterRasterDestination( "x", QStringLiteral( "desc" ), QStringLiteral( "default.tif" ), true ) );
  QgsProcessingOutputLayerDefinition fs = QgsProcessingOutputLayerDefinition( QStringLiteral( "test.tif" ) );
  fs.destinationProject = &p;
  params.insert( QStringLiteral( "x" ), QVariant::fromValue( fs ) );
  QCOMPARE( QgsProcessingParameters::parameterAsOutputLayer( def.get(), params, context ), QStringLiteral( "test.tif" ) );

  // make sure layer was automatically added to list to load on completion
  QCOMPARE( context.layersToLoadOnCompletion().size(), 1 );
  QCOMPARE( context.layersToLoadOnCompletion().keys().at( 0 ), QStringLiteral( "test.tif" ) );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "desc" ) );
  QCOMPARE( context.layersToLoadOnCompletion().values().at( 0 ).layerTypeHint, QgsProcessingUtils::LayerHint::Raster );

  // with name overloading
  QgsProcessingContext context2;
  fs = QgsProcessingOutputLayerDefinition( QStringLiteral( "test.tif" ) );
  fs.destinationProject = &p;
  fs.destinationName = QStringLiteral( "my_dest" );
  params.insert( QStringLiteral( "x" ), QVariant::fromValue( fs ) );
  QCOMPARE( QgsProcessingParameters::parameterAsOutputLayer( def.get(), params, context2 ), QStringLiteral( "test.tif" ) );
  QCOMPARE( context2.layersToLoadOnCompletion().size(), 1 );
  QCOMPARE( context2.layersToLoadOnCompletion().keys().at( 0 ), QStringLiteral( "test.tif" ) );
  QCOMPARE( context2.layersToLoadOnCompletion().values().at( 0 ).name, QStringLiteral( "my_dest" ) );
  QCOMPARE( context2.layersToLoadOnCompletion().values().at( 0 ).outputName, QStringLiteral( "x" ) );
  QCOMPARE( context2.layersToLoadOnCompletion().values().at( 0 ).layerTypeHint, QgsProcessingUtils::LayerHint::Raster );
}

void TestQgsProcessing::parameterFileOut()
{
  // setup a context
  QgsProject p;
  p.setCrs( QgsCoordinateReferenceSystem::fromEpsgId( 28353 ) );
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterFileDestination > def( new QgsProcessingParameterFileDestination( "non_optional", QString(), QStringLiteral( "BMP files (*.bmp)" ), QVariant(), false ) );
  QCOMPARE( def->fileFilter(), QStringLiteral( "BMP files (*.bmp)" ) );
  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "bmp" ) );
  QVERIFY( def->generateTemporaryDestination().endsWith( QLatin1String( ".bmp" ) ) );
  QVERIFY( def->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );
  def->setFileFilter( QStringLiteral( "PCX files (*.pcx)" ) );
  QCOMPARE( def->fileFilter(), QStringLiteral( "PCX files (*.pcx)" ) );
  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "pcx" ) );
  def->setFileFilter( QStringLiteral( "PCX files (*.pcx *.picx)" ) );
  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "pcx" ) );
  def->setFileFilter( QStringLiteral( "PCX files (*.pcx *.picx);;BMP files (*.bmp)" ) );
  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "pcx" ) );
  QCOMPARE( def->createFileFilter(), QStringLiteral( "PCX files (*.pcx *.picx);;BMP files (*.bmp);;All files (*.*)" ) );

  def->setFileFilter( QString() );
  QCOMPARE( def->defaultFileExtension(), QStringLiteral( "file" ) );
  QVERIFY( def->generateTemporaryDestination().endsWith( QLatin1String( ".file" ) ) );
  QVERIFY( def->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );

  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromValue( QStringLiteral( "layer12312312" ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProperty::fromValue( QString() ) ) );

  // should be OK with or without context - it's an output file!
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.txt" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.txt", &context ) );

  QCOMPARE( def->createFileFilter(), QStringLiteral( "All files (*.*)" ) );

  QVariantMap params;
  params.insert( "non_optional", "test.txt" );
  QCOMPARE( QgsProcessingParameters::parameterAsFileOutput( def.get(), params, context ), QStringLiteral( "test.txt" ) );
  params.insert( "non_optional", QgsProcessingOutputLayerDefinition( "test.txt" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsFileOutput( def.get(), params, context ), QStringLiteral( "test.txt" ) );

  params.insert( QStringLiteral( "non_optional" ), QgsProcessing::TEMPORARY_OUTPUT );
  QCOMPARE( QgsProcessingParameters::parameterAsFileOutput( def.get(), params, context ).right( 18 ), QStringLiteral( "/non_optional.file" ) );

  QgsProcessingOutputLayerDefinition fs;
  fs.sink = QgsProperty::fromValue( QgsProcessing::TEMPORARY_OUTPUT );
  params.insert( QStringLiteral( "non_optional" ), QVariant::fromValue( fs ) );
  QCOMPARE( QgsProcessingParameters::parameterAsFileOutput( def.get(), params, context ).right( 18 ), QStringLiteral( "/non_optional.file" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( "abc" ) ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromValue( "abc" ) ) ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProcessingOutputLayerDefinition( QgsProperty::fromExpression( "\"abc\" || \"def\"" ) ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"abc\" || \"def\"')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterFileDestination fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.fileFilter(), def->fileFilter() );
  QCOMPARE( fromMap.supportsNonFileBasedOutput(), def->supportsNonFileBasedOutput() );
  def.reset( dynamic_cast< QgsProcessingParameterFileDestination *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterFileDestination *>( def.get() ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFileDestination('non_optional', '', fileFilter='', createByDefault=True, defaultValue=None)" ) );
  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=fileDestination" ) );
  std::unique_ptr< QgsProcessingParameterFileDestination > fromCode( dynamic_cast< QgsProcessingParameterFileDestination * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // optional
  def.reset( new QgsProcessingParameterFileDestination( "optional", QString(), QString(), QString( "default.txt" ), true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.txt" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "layer1231123" ) ) );

  params.insert( "optional", QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsFileOutput( def.get(), params, context ), QStringLiteral( "default.txt" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFileDestination('optional', '', optional=True, fileFilter='All files (*.*)', createByDefault=True, defaultValue='default.txt')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional fileDestination default.txt" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFileDestination * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // outputs definition test
  def.reset( new QgsProcessingParameterFileDestination( "html", QString(), QString( "HTML files" ), QString(), false ) );
  std::unique_ptr< QgsProcessingOutputDefinition > outputDef( def->toOutputDefinition() );
  QVERIFY( dynamic_cast< QgsProcessingOutputHtml *>( outputDef.get() ) );
  def.reset( new QgsProcessingParameterFileDestination( "html", QString(), QString( "Text files (*.htm)" ), QString(), false ) );
  outputDef.reset( def->toOutputDefinition() );
  QVERIFY( dynamic_cast< QgsProcessingOutputHtml *>( outputDef.get() ) );
  def.reset( new QgsProcessingParameterFileDestination( "file", QString(), QString( "Text files (*.txt)" ), QString(), false ) );
  outputDef.reset( def->toOutputDefinition() );
  QVERIFY( dynamic_cast< QgsProcessingOutputFile *>( outputDef.get() ) );
  def.reset( new QgsProcessingParameterFileDestination( "file", QString(), QString(), QString(), false ) );
  outputDef.reset( def->toOutputDefinition() );
  QVERIFY( dynamic_cast< QgsProcessingOutputFile *>( outputDef.get() ) );
}

void TestQgsProcessing::parameterFolderOut()
{
  // setup a context
  QgsProject p;
  QgsProcessingContext context;
  context.setProject( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterFolderDestination > def( new QgsProcessingParameterFolderDestination( "non_optional", QString(), QVariant(), false ) );

  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "asdasd" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromValue( QStringLiteral( "asdasdas" ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QgsProperty::fromValue( QString() ) ) );

  // should be OK with or without context - it's an output folder!
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/", &context ) );

  // check that temporary destination does not have dot at the end when there is no extension
  QVERIFY( !def->generateTemporaryDestination().endsWith( QLatin1Char( '.' ) ) );
  QVERIFY( def->generateTemporaryDestination().startsWith( QgsProcessingUtils::tempFolder() ) );

  QVariantMap params;
  params.insert( "non_optional", "c:/mine" );
  QCOMPARE( QgsProcessingParameters::parameterAsFileOutput( def.get(), params, context ), QStringLiteral( "c:/mine" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\'" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterFolderDestination fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.supportsNonFileBasedOutput(), def->supportsNonFileBasedOutput() );
  def.reset( dynamic_cast< QgsProcessingParameterFolderDestination *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterFolderDestination *>( def.get() ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFolderDestination('non_optional', '', createByDefault=True, defaultValue=None)" ) );
  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=folderDestination" ) );
  std::unique_ptr< QgsProcessingParameterFolderDestination > fromCode( dynamic_cast< QgsProcessingParameterFolderDestination * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // optional
  def.reset( new QgsProcessingParameterFolderDestination( "optional", QString(), QString( "c:/junk" ), true ) );
  QVERIFY( !def->checkValueIsAcceptable( false ) );
  QVERIFY( !def->checkValueIsAcceptable( true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional", QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsFileOutput( def.get(), params, context ), QStringLiteral( "c:/junk" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFolderDestination('optional', '', optional=True, createByDefault=True, defaultValue='c:/junk')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional folderDestination c:/junk" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterFolderDestination * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // temporary directory
  def.reset( new QgsProcessingParameterFolderDestination( "junkdir", QString(), QgsProcessing::TEMPORARY_OUTPUT ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ).right( 8 ), QStringLiteral( "/junkdir" ) );
}

void TestQgsProcessing::parameterBand()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterBand > def( new QgsProcessingParameterBand( "non_optional", QString(), QVariant(), QString(), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "1" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string representing a band
  QVariantMap params;
  params.insert( "non_optional", "1" );
  int band = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( band, 1 );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "5" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterBand('non_optional', '', parentLayerParameterName='', allowMultiple=False, defaultValue=None)" ) );
  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=band" ) );
  std::unique_ptr< QgsProcessingParameterBand > fromCode( dynamic_cast< QgsProcessingParameterBand * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameterName(), def->parentLayerParameterName() );

  QVERIFY( def->dependsOnOtherParameters().isEmpty() );
  def->setParentLayerParameterName( "my_parent" );
  QCOMPARE( def->dependsOnOtherParameters(), QStringList() << QStringLiteral( "my_parent" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterBand('non_optional', '', parentLayerParameterName='my_parent', allowMultiple=False, defaultValue=None)" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterBand * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameterName(), def->parentLayerParameterName() );

  // multiple
  def.reset( new QgsProcessingParameterBand( "non_optional", QString(), QVariant(), QString(), false, true ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "1" << "2" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << 1 << 2 ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() ) );

  params.insert( "non_optional", QString( "1;2" ) );
  QList<int> bands = QgsProcessingParameters::parameterAsInts( def.get(), params, context );
  QCOMPARE( bands, QList<int>() << 1 << 2 );
  params.insert( "non_optional", QVariantList() << 1 << 2 );
  bands = QgsProcessingParameters::parameterAsInts( def.get(), params, context );
  QCOMPARE( bands, QList<int>() << 1 << 2 );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QStringList() << "1" << "2", context ), QStringLiteral( "[1,2]" ) );
  QCOMPARE( def->valueAsPythonString( QVariantList() << 1 << 2, context ), QStringLiteral( "[1,2]" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterBand fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.parentLayerParameterName(), def->parentLayerParameterName() );
  QCOMPARE( fromMap.allowMultiple(), def->allowMultiple() );
  def.reset( dynamic_cast< QgsProcessingParameterBand *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterBand *>( def.get() ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterBand('non_optional', '', parentLayerParameterName='', allowMultiple=True, defaultValue=None)" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterBand * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameterName(), def->parentLayerParameterName() );
  QCOMPARE( fromCode->allowMultiple(), def->allowMultiple() );

  // optional
  def.reset( new QgsProcessingParameterBand( "optional", QString(), 1, QString(), true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "1" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  band = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( band, 1 );

  // optional, no default
  def.reset( new QgsProcessingParameterBand( "optional", QString(), QVariant(), QString(), true ) );
  params.insert( "optional",  QVariant() );
  band = QgsProcessingParameters::parameterAsInt( def.get(), params, context );
  QCOMPARE( band, 0 );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterBand('optional', '', optional=True, parentLayerParameterName='', allowMultiple=False, defaultValue=None)" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional band" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterBand * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayerParameterName(), def->parentLayerParameterName() );
}

void TestQgsProcessing::parameterLayout()
{
  QgsProcessingContext context;

  QgsProject p;
  QgsPrintLayout *l = new QgsPrintLayout( &p );
  l->setName( "l1" );
  QgsPrintLayout *l2 = new QgsPrintLayout( &p );
  l2->setName( "l2" );
  p.layoutManager()->addLayout( l );
  p.layoutManager()->addLayout( l2 );

  // not optional!
  std::unique_ptr< QgsProcessingParameterLayout > def( new QgsProcessingParameterLayout( "non_optional", QString(), QString(), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string
  QVariantMap params;
  params.insert( "non_optional", QString( "abcdef" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString( "abcdef" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayout( def.get(), params, context ) );
  params.insert( "non_optional", QString( "l1" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayout( def.get(), params, context ) );
  context.setProject( &p );
  params.insert( "non_optional", QString( "abcdef" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayout( def.get(), params, context ) );
  params.insert( "non_optional", QString( "l1" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayout( def.get(), params, context ), l );
  params.insert( "non_optional", QString( "l2" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayout( def.get(), params, context ), l2 );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "'5'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc\ndef" ), context ), QStringLiteral( "'abc\\ndef'" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterLayout('non_optional', '', defaultValue=None)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=layout" ) );
  std::unique_ptr< QgsProcessingParameterLayout > fromCode( dynamic_cast< QgsProcessingParameterLayout * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterLayout fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterLayout *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterLayout *>( def.get() ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterLayout * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=layout None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterLayout * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=layout it's mario" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "it's mario" ) );

  def->setDefaultValue( QStringLiteral( "it's mario" ) );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterLayout('non_optional', '', defaultValue='it\\'s mario')" ) );

  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterLayout * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterLayout * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=layout 'my val'" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterLayout * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=layout \"my val\"" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );

  // optional
  def.reset( new QgsProcessingParameterLayout( "optional", QString(), QString( "default" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString( "default" ) );
  params.insert( "optional",  QString() ); //empty string should not result in default value
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterLayout('optional', '', optional=True, defaultValue='default')" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional layout default" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterLayout * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // not optional, valid default!
  def.reset( new QgsProcessingParameterLayout( "non_optional", QString(), QString( "def" ), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be valid, falls back to valid default
}

void TestQgsProcessing::parameterLayoutItem()
{
  QgsProcessingContext context;

  QgsProject p;
  QgsPrintLayout *l = new QgsPrintLayout( &p );
  l->setName( "l1" );
  QgsLayoutItemLabel *label1 = new QgsLayoutItemLabel( l );
  label1->setId( "a" );
  l->addLayoutItem( label1 );
  QgsLayoutItemLabel *label2 = new QgsLayoutItemLabel( l );
  label2->setId( "b" );
  l->addLayoutItem( label2 );

  QgsPrintLayout *l2 = new QgsPrintLayout( &p );

  // not optional!
  std::unique_ptr< QgsProcessingParameterLayoutItem > def( new QgsProcessingParameterLayoutItem( "non_optional", QString(), QVariant(), QString(), -1, false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string
  QVariantMap params;
  params.insert( "non_optional", QString( "aaaa" ) );
  QString f = QgsProcessingParameters::parameterAsString( def.get(), params, context );
  QCOMPARE( f, QStringLiteral( "aaaa" ) );

  QVERIFY( !QgsProcessingParameters::parameterAsLayoutItem( def.get(), params, context, nullptr ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayoutItem( def.get(), params, context, l ) );
  params.insert( "non_optional", label1->uuid() );
  QVERIFY( !QgsProcessingParameters::parameterAsLayoutItem( def.get(), params, context, nullptr ) );
  params.insert( "non_optional", QString( "abcdef" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayoutItem( def.get(), params, context, nullptr ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayoutItem( def.get(), params, context, l ) );
  params.insert( "non_optional", label1->uuid() );
  QVERIFY( !QgsProcessingParameters::parameterAsLayoutItem( def.get(), params, context, nullptr ) );
  QVERIFY( !QgsProcessingParameters::parameterAsLayoutItem( def.get(), params, context, l2 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsLayoutItem( def.get(), params, context, l ), label1 );
  params.insert( "non_optional", label1->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayoutItem( def.get(), params, context, l ), label1 );
  params.insert( "non_optional", label2->uuid() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayoutItem( def.get(), params, context, l ), label2 );
  params.insert( "non_optional", label2->id() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayoutItem( def.get(), params, context, l ), label2 );
  // UUID matching must take precedence
  label1->setId( label2->uuid() );
  params.insert( "non_optional", label2->uuid() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayoutItem( def.get(), params, context, l ), label2 );
  label2->setId( label1->uuid() );
  params.insert( "non_optional", label1->uuid() );
  QCOMPARE( QgsProcessingParameters::parameterAsLayoutItem( def.get(), params, context, l ), label1 );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "probably\'invalid\"item", context ), QStringLiteral( "'probably\\'invalid\\\"item'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterLayoutItem('non_optional', '', parentLayoutParameterName='', defaultValue=None)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=layoutitem" ) );
  std::unique_ptr< QgsProcessingParameterLayoutItem > fromCode( dynamic_cast< QgsProcessingParameterLayoutItem * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayoutParameterName(), def->parentLayoutParameterName() );
  QCOMPARE( fromCode->itemType(), def->itemType() );

  QVERIFY( def->dependsOnOtherParameters().isEmpty() );
  def->setParentLayoutParameterName( "my_parent" );
  QCOMPARE( def->dependsOnOtherParameters(), QStringList() << QStringLiteral( "my_parent" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterLayoutItem('non_optional', '', parentLayoutParameterName='my_parent', defaultValue=None)" ) );

  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterLayoutItem * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayoutParameterName(), def->parentLayoutParameterName() );
  QCOMPARE( fromCode->itemType(), def->itemType() );

  def->setItemType( 100 );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterLayoutItem('non_optional', '', itemType=100, parentLayoutParameterName='my_parent', defaultValue=None)" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterLayoutItem * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayoutParameterName(), def->parentLayoutParameterName() );
  QCOMPARE( fromCode->itemType(), def->itemType() );

  def->setItemType( 101 );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterLayoutItem('non_optional', '', itemType=101, parentLayoutParameterName='my_parent', defaultValue=None)" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterLayoutItem * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayoutParameterName(), def->parentLayoutParameterName() );
  QCOMPARE( fromCode->itemType(), def->itemType() );

  // optional
  def.reset( new QgsProcessingParameterLayoutItem( "optional", QString(), QString( "def" ), QString(), -1, true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  f = QgsProcessingParameters::parameterAsString( def.get(), params, context );
  QCOMPARE( f, QStringLiteral( "def" ) );

  // optional, no default
  def.reset( new QgsProcessingParameterLayoutItem( "optional", QString(), QVariant(), QString(), -1, true ) );
  params.insert( "optional",  QVariant() );
  f = QgsProcessingParameters::parameterAsString( def.get(), params, context );
  QVERIFY( f.isEmpty() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterLayoutItem('optional', '', optional=True, parentLayoutParameterName='', defaultValue=None)" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional layoutitem" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterLayoutItem * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentLayoutParameterName(), def->parentLayoutParameterName() );
  QCOMPARE( fromCode->itemType(), def->itemType() );

}

void TestQgsProcessing::parameterColor()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterColor > def( new QgsProcessingParameterColor( "non_optional", QString(), QString(), true, false ) );
  QVERIFY( def->opacityEnabled() );
  def->setOpacityEnabled( false );
  QVERIFY( !def->opacityEnabled() );
  def->setOpacityEnabled( true );

  QVERIFY( !def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "#ff0000" ) );
  QVERIFY( !def->checkValueIsAcceptable( "bbbbbbbbbbbbbbbbbbbb" ) );
  QVERIFY( def->checkValueIsAcceptable( QColor( 255, 0, 0 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string
  QVariantMap params;
  params.insert( "non_optional", QString( "xxx" ) );
  QVERIFY( !QgsProcessingParameters::parameterAsColor( def.get(), params, context ).isValid() );
  params.insert( "non_optional", QString( "#ff0000" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsColor( def.get(), params, context ).name(), QString( "#ff0000" ) );
  params.insert( "non_optional", QString( "rgba(255,0,0,0.1" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsColor( def.get(), params, context ).name(), QString( "#ff0000" ) );
  params.insert( "non_optional", QColor( 255, 255, 0 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsColor( def.get(), params, context ).name(), QString( "#ffff00" ) );
  params.insert( "non_optional", QColor( 255, 255, 0, 100 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsColor( def.get(), params, context ), QColor( 255, 255, 0, 100 ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "#ff0000" ), context ), QStringLiteral( "QColor(255, 0, 0)" ) );
  QCOMPARE( def->valueAsPythonString( QColor(), context ), QStringLiteral( "QColor()" ) );
  QCOMPARE( def->valueAsPythonString( QColor( 255, 0, 0 ), context ), QStringLiteral( "QColor(255, 0, 0)" ) );
  QCOMPARE( def->valueAsPythonString( QColor( 255, 0, 0, 100 ), context ), QStringLiteral( "QColor(255, 0, 0, 100)" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterColor('non_optional', '', opacityEnabled=True, defaultValue=None)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=color withopacity" ) );
  std::unique_ptr< QgsProcessingParameterColor > fromCode( dynamic_cast< QgsProcessingParameterColor * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QVERIFY( fromCode->opacityEnabled() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterColor fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QVERIFY( fromMap.opacityEnabled() );
  def.reset( dynamic_cast< QgsProcessingParameterColor *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterColor *>( def.get() ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterColor * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=color withopacity None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
  QVERIFY( fromCode->opacityEnabled() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterColor * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=color #aabbcc" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "#aabbcc" ) );
  QVERIFY( !fromCode->opacityEnabled() );

  def->setDefaultValue( QColor( 10, 20, 30 ) );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterColor('non_optional', '', opacityEnabled=True, defaultValue=QColor(10, 20, 30))" ) );

  def->setOpacityEnabled( false );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterColor('non_optional', '', opacityEnabled=False, defaultValue=QColor(10, 20, 30))" ) );
  def->setOpacityEnabled( true );

  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterColor * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QVERIFY( fromCode->opacityEnabled() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterColor * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=color 'my val'" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );
  QVERIFY( !fromCode->opacityEnabled() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterColor * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=color withopacity \"my val\"" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );
  QVERIFY( fromCode->opacityEnabled() );

  // optional
  def.reset( new QgsProcessingParameterColor( "optional", QString(), QString( "#ff00ff" ), false, true ) );
  QVERIFY( def->checkValueIsAcceptable( "#ff0000" ) );
  QVERIFY( def->checkValueIsAcceptable( QColor( 255, 0, 0 ) ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->opacityEnabled() );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsColor( def.get(), params, context ).name(), QString( "#ff00ff" ) );
  params.insert( "optional",  QColor() ); //invalid color should not result in default value
  QVERIFY( !QgsProcessingParameters::parameterAsColor( def.get(), params, context ).isValid() );
  params.insert( "optional",  QString() ); //empty string should not result in default value
  QVERIFY( !QgsProcessingParameters::parameterAsColor( def.get(), params, context ).isValid() );

  // not opacity enabled, should be stripped off
  params.insert( "optional", QColor( 255, 255, 0, 100 ) );
  QCOMPARE( QgsProcessingParameters::parameterAsColor( def.get(), params, context ), QColor( 255, 255, 0 ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterColor('optional', '', optional=True, opacityEnabled=False, defaultValue=QColor(255, 0, 255))" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional color #ff00ff" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterColor * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QVERIFY( !fromCode->opacityEnabled() );

  // not optional, valid default!
  def.reset( new QgsProcessingParameterColor( "non_optional", QString(), QString( "#ff00ff" ), true, false ) );
  QVERIFY( def->checkValueIsAcceptable( "#dddddd" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be valid, falls back to valid default
}

void TestQgsProcessing::parameterCoordinateOperation()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterCoordinateOperation > def( new QgsProcessingParameterCoordinateOperation( "non_optional", QString(), QString(), QString(), QString(), QVariant(), QVariant(), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->dependsOnOtherParameters().isEmpty() );
  def->setSourceCrsParameterName( QStringLiteral( "src" ) );
  QCOMPARE( def->sourceCrsParameterName(), QStringLiteral( "src" ) );
  QCOMPARE( def->dependsOnOtherParameters(), QStringList() << QStringLiteral( "src" ) );
  def->setDestinationCrsParameterName( QStringLiteral( "dest" ) );
  QCOMPARE( def->destinationCrsParameterName(), QStringLiteral( "dest" ) );
  QCOMPARE( def->dependsOnOtherParameters(), QStringList() << QStringLiteral( "src" ) << QStringLiteral( "dest" ) );
  def->setSourceCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:7855" ) ) );
  QCOMPARE( def->sourceCrs().value< QgsCoordinateReferenceSystem >().authid(), QStringLiteral( "EPSG:7855" ) );
  def->setDestinationCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:28355" ) ) );
  QCOMPARE( def->destinationCrs().value< QgsCoordinateReferenceSystem >().authid(), QStringLiteral( "EPSG:28355" ) );

  // string value
  QVariantMap params;
  params.insert( "non_optional", QStringLiteral( "abcdef" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString( "abcdef" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "'5'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc\ndef" ), context ), QStringLiteral( "'abc\\ndef'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterCoordinateOperation('non_optional', '', sourceCrsParameterName='src', destinationCrsParameterName='dest', staticSourceCrs=QgsCoordinateReferenceSystem('EPSG:7855'), staticDestinationCrs=QgsCoordinateReferenceSystem('EPSG:28355'), defaultValue=None)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=coordinateoperation" ) );
  std::unique_ptr< QgsProcessingParameterCoordinateOperation > fromCode( dynamic_cast< QgsProcessingParameterCoordinateOperation * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterCoordinateOperation fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.sourceCrsParameterName(), def->sourceCrsParameterName() );
  QCOMPARE( fromMap.destinationCrsParameterName(), def->destinationCrsParameterName() );
  QCOMPARE( fromMap.sourceCrs().value< QgsCoordinateReferenceSystem >().authid(), def->sourceCrs().value< QgsCoordinateReferenceSystem >().authid() );
  QCOMPARE( fromMap.destinationCrs().value< QgsCoordinateReferenceSystem >().authid(), def->destinationCrs().value< QgsCoordinateReferenceSystem >().authid() );
  def.reset( dynamic_cast< QgsProcessingParameterCoordinateOperation *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterCoordinateOperation *>( def.get() ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterCoordinateOperation * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=coordinateoperation None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterCoordinateOperation * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=coordinateoperation it's mario" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "it's mario" ) );

  def->setDefaultValue( QStringLiteral( "it's mario" ) );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterCoordinateOperation('non_optional', '', sourceCrsParameterName='src', destinationCrsParameterName='dest', staticSourceCrs=QgsCoordinateReferenceSystem('EPSG:7855'), staticDestinationCrs=QgsCoordinateReferenceSystem('EPSG:28355'), defaultValue='it\\'s mario')" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterCoordinateOperation * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterCoordinateOperation * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=coordinateoperation 'my val'" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterCoordinateOperation * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=coordinateoperation \"my val\"" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );

  // optional
  def.reset( new QgsProcessingParameterCoordinateOperation( "optional", QString(), QString( "default" ), QString(), QString(), QVariant(), QVariant(), true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString( "default" ) );
  params.insert( "optional",  QString() ); //empty string should not result in default value
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterCoordinateOperation('optional', '', optional=True, defaultValue='default')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional coordinateoperation default" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterCoordinateOperation * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // not optional, valid default!
  def.reset( new QgsProcessingParameterCoordinateOperation( "non_optional", QString(), QString( "def" ), QString(), QString(), QVariant(), QVariant(), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be valid, falls back to valid default
}

void TestQgsProcessing::parameterMapTheme()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterMapTheme > def( new QgsProcessingParameterMapTheme( "non_optional", QString(), QVariant(), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string
  QVariantMap params;
  params.insert( "non_optional", QString( "abcdef" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString( "abcdef" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "'5'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc\ndef" ), context ), QStringLiteral( "'abc\\ndef'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMapTheme('non_optional', '', defaultValue=None)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=maptheme" ) );
  std::unique_ptr< QgsProcessingParameterMapTheme > fromCode( dynamic_cast< QgsProcessingParameterMapTheme * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterMapTheme fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  def.reset( dynamic_cast< QgsProcessingParameterMapTheme *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterMapTheme *>( def.get() ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterMapTheme * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=maptheme None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterMapTheme * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=maptheme it's mario" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "it's mario" ) );

  def->setDefaultValue( QStringLiteral( "it's mario" ) );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMapTheme('non_optional', '', defaultValue='it\\'s mario')" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterMapTheme * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterMapTheme * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=maptheme 'my val'" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterMapTheme * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=maptheme \"my val\"" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );

  // optional
  def.reset( new QgsProcessingParameterMapTheme( "optional", QString(), QString( "default" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString( "default" ) );
  params.insert( "optional",  QString() ); //empty string should not result in default value
  QCOMPARE( QgsProcessingParameters::parameterAsString( def.get(), params, context ), QString() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMapTheme('optional', '', optional=True, defaultValue='default')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional maptheme default" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterMapTheme * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  // not optional, valid default!
  def.reset( new QgsProcessingParameterMapTheme( "non_optional", QString(), QString( "def" ), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be valid, falls back to valid default
}

void TestQgsProcessing::parameterProviderConnection()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterProviderConnection > def( new QgsProcessingParameterProviderConnection( "non_optional", QString(), QStringLiteral( "ogr" ), QVariant(), false ) );
  QCOMPARE( def->providerId(), QStringLiteral( "ogr" ) );
  def->setProviderId( QStringLiteral( "postgres" ) );
  QCOMPARE( def->providerId(), QStringLiteral( "postgres" ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string value
  QVariantMap params;
  params.insert( "non_optional", QString( "abcdef" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsConnectionName( def.get(), params, context ), QString( "abcdef" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "'5'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc\ndef" ), context ), QStringLiteral( "'abc\\ndef'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "uri='complex' username=\"complex\"", context ), QStringLiteral( "'uri=\\'complex\\' username=\\\"complex\\\"'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "c:\\test\\new data\\test.dat" ), context ), QStringLiteral( "'c:\\\\test\\\\new data\\\\test.dat'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterProviderConnection('non_optional', '', 'postgres', defaultValue=None)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=providerconnection postgres" ) );
  std::unique_ptr< QgsProcessingParameterProviderConnection > fromCode( dynamic_cast< QgsProcessingParameterProviderConnection * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->providerId(), QStringLiteral( "postgres" ) );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterProviderConnection fromMap( "x", QString(), QString() );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.providerId(), QStringLiteral( "postgres" ) );
  def.reset( dynamic_cast< QgsProcessingParameterProviderConnection *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterProviderConnection *>( def.get() ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterProviderConnection * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=providerconnection postgres None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
  QCOMPARE( fromCode->providerId(), QStringLiteral( "postgres" ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterProviderConnection * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=providerconnection postgres it's mario" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "it's mario" ) );
  QCOMPARE( fromCode->providerId(), QStringLiteral( "postgres" ) );

  def->setDefaultValue( QStringLiteral( "it's mario" ) );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterProviderConnection('non_optional', '', 'postgres', defaultValue='it\\'s mario')" ) );
  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterProviderConnection * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->providerId(), QStringLiteral( "postgres" ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterProviderConnection * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=providerconnection postgres 'my val'" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );
  QCOMPARE( fromCode->providerId(), QStringLiteral( "postgres" ) );

  fromCode.reset( dynamic_cast< QgsProcessingParameterProviderConnection * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##non_optional=providerconnection postgres \"my val\"" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue().toString(), QStringLiteral( "my val" ) );
  QCOMPARE( fromCode->providerId(), QStringLiteral( "postgres" ) );

  // optional
  def.reset( new QgsProcessingParameterProviderConnection( "optional", QString(), QStringLiteral( "ogr" ), QString( "default" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsConnectionName( def.get(), params, context ), QString( "default" ) );
  params.insert( "optional",  QString() ); //empty string should not result in default value
  QCOMPARE( QgsProcessingParameters::parameterAsConnectionName( def.get(), params, context ), QString() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterProviderConnection('optional', '', 'ogr', optional=True, defaultValue='default')" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional providerconnection ogr default" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterProviderConnection * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->providerId(), QStringLiteral( "ogr" ) );

  // not optional, valid default!
  def.reset( new QgsProcessingParameterProviderConnection( "non_optional", QString(), QStringLiteral( "ogr" ), QString( "def" ), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be valid, falls back to valid default
}

void TestQgsProcessing::parameterDatabaseSchema()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterDatabaseSchema > def( new QgsProcessingParameterDatabaseSchema( "non_optional", QString(), QString(), QVariant(), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string
  QVariantMap params;
  params.insert( "non_optional", QString( "a" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsSchema( def.get(), params, context ), QStringLiteral( "a" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "probably\'invalid\"schema", context ), QStringLiteral( "'probably\\'invalid\\\"schema'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDatabaseSchema('non_optional', '', connectionParameterName='', defaultValue=None)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=databaseschema" ) );
  std::unique_ptr< QgsProcessingParameterDatabaseSchema > fromCode( dynamic_cast< QgsProcessingParameterDatabaseSchema * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentConnectionParameterName(), def->parentConnectionParameterName() );

  QVERIFY( def->dependsOnOtherParameters().isEmpty() );
  def->setParentConnectionParameterName( "my_parent" );
  QCOMPARE( def->dependsOnOtherParameters(), QStringList() << QStringLiteral( "my_parent" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDatabaseSchema('non_optional', '', connectionParameterName='my_parent', defaultValue=None)" ) );

  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterDatabaseSchema * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentConnectionParameterName(), def->parentConnectionParameterName() );

  // optional
  def.reset( new QgsProcessingParameterDatabaseSchema( "optional", QString(), QString(), QStringLiteral( "def" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsSchema( def.get(), params, context ), QStringLiteral( "def" ) );

  // optional, no default
  def.reset( new QgsProcessingParameterDatabaseSchema( "optional", QString(), QString(), QVariant(), true ) );
  params.insert( "optional",  QVariant() );
  QVERIFY( QgsProcessingParameters::parameterAsSchema( def.get(), params, context ).isEmpty() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDatabaseSchema('optional', '', optional=True, connectionParameterName='', defaultValue=None)" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional databaseschema" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterDatabaseSchema * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentConnectionParameterName(), def->parentConnectionParameterName() );
}

void TestQgsProcessing::parameterDatabaseTable()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterDatabaseTable > def( new QgsProcessingParameterDatabaseTable( "non_optional", QString(), QString(), QString(), QVariant(), false ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  // string
  QVariantMap params;
  params.insert( "non_optional", QString( "a" ) );
  QCOMPARE( QgsProcessingParameters::parameterAsDatabaseTableName( def.get(), params, context ), QStringLiteral( "a" ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( "probably\'invalid\"schema", context ), QStringLiteral( "'probably\\'invalid\\\"schema'" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDatabaseTable('non_optional', '', connectionParameterName='', schemaParameterName='', defaultValue=None)" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##non_optional=databasetable none none" ) );
  std::unique_ptr< QgsProcessingParameterDatabaseTable > fromCode( dynamic_cast< QgsProcessingParameterDatabaseTable * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentConnectionParameterName(), def->parentConnectionParameterName() );
  QCOMPARE( fromCode->parentSchemaParameterName(), def->parentSchemaParameterName() );

  QVERIFY( def->dependsOnOtherParameters().isEmpty() );
  def->setParentConnectionParameterName( "my_parent" );
  QCOMPARE( def->dependsOnOtherParameters(), QStringList() << QStringLiteral( "my_parent" ) );
  def->setParentSchemaParameterName( "my_schema" );
  QCOMPARE( def->dependsOnOtherParameters(), QStringList() << QStringLiteral( "my_parent" ) << QStringLiteral( "my_schema" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDatabaseTable('non_optional', '', connectionParameterName='my_parent', schemaParameterName='my_schema', defaultValue=None)" ) );

  code = def->asScriptCode();
  fromCode.reset( dynamic_cast< QgsProcessingParameterDatabaseTable * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentConnectionParameterName(), def->parentConnectionParameterName() );
  QCOMPARE( fromCode->parentSchemaParameterName(), def->parentSchemaParameterName() );

  // optional
  def.reset( new QgsProcessingParameterDatabaseTable( "optional", QString(), QString(), QString(), QStringLiteral( "def" ), true ) );
  QVERIFY( def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  QCOMPARE( QgsProcessingParameters::parameterAsDatabaseTableName( def.get(), params, context ), QStringLiteral( "def" ) );

  // optional, no default
  def.reset( new QgsProcessingParameterDatabaseTable( "optional", QString(), QString(), QString(), QVariant(), true ) );
  params.insert( "optional",  QVariant() );
  QVERIFY( QgsProcessingParameters::parameterAsDatabaseTableName( def.get(), params, context ).isEmpty() );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDatabaseTable('optional', '', optional=True, connectionParameterName='', schemaParameterName='', defaultValue=None)" ) );
  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional databasetable none none" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterDatabaseTable * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );
  QCOMPARE( fromCode->parentConnectionParameterName(), def->parentConnectionParameterName() );
  QCOMPARE( fromCode->parentSchemaParameterName(), def->parentSchemaParameterName() );

  // allow new table names
  def.reset( new QgsProcessingParameterDatabaseTable( "new", QString(), QStringLiteral( "con" ), QStringLiteral( "schema" ), QVariant(), false, true ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDatabaseTable('new', '', allowNewTableNames=True, connectionParameterName='con', schemaParameterName='schema', defaultValue=None)" ) );
  QVariantMap var = def->toVariantMap();
  def.reset( dynamic_cast<QgsProcessingParameterDatabaseTable *>( QgsProcessingParameters::parameterFromVariantMap( var ) ) );
  QCOMPARE( def->parentConnectionParameterName(), QStringLiteral( "con" ) );
  QCOMPARE( def->parentSchemaParameterName(), QStringLiteral( "schema" ) );
  QVERIFY( def->allowNewTableNames() );
}

void TestQgsProcessing::parameterFieldMapping()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterFieldMapping > def( new QgsProcessingParameterFieldMapping( "non_optional", QString(), QStringLiteral( "parent" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() ) );
  QVariantMap map;
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << map ) );
  map.insert( QStringLiteral( "name" ), QStringLiteral( "n" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << map ) );
  map.insert( QStringLiteral( "type" ), QStringLiteral( "t" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << map ) );
  map.insert( QStringLiteral( "expression" ), QStringLiteral( "e" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << map ) );
  QVariantMap map2;
  map2.insert( QStringLiteral( "name" ), QStringLiteral( "n2" ) );
  map2.insert( QStringLiteral( "type" ), QStringLiteral( "t2" ) );
  map2.insert( QStringLiteral( "expression" ), QStringLiteral( "e2" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << map << map2 ) );

  QVariantMap params;
  params.insert( "non_optional", QVariantList() << map << map2 );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "5" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc\ndef" ), context ), QStringLiteral( "'abc\\ndef'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant( QVariantList() << map << map2 ), context ), QStringLiteral( "[{'expression': 'e','name': 'n','type': 't'},{'expression': 'e2','name': 'n2','type': 't2'}]" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFieldMapping('non_optional', '', parentLayerParameterName='parent')" ) );

  QVariantMap mapDef = def->toVariantMap();
  QgsProcessingParameterFieldMapping fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( mapDef ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.parentLayerParameterName(), def->parentLayerParameterName() );
  def.reset( dynamic_cast< QgsProcessingParameterFieldMapping *>( QgsProcessingParameters::parameterFromVariantMap( mapDef ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterFieldMapping *>( def.get() ) );

  def->setParentLayerParameterName( QString() );
  QVERIFY( def->dependsOnOtherParameters().isEmpty() );
  def->setParentLayerParameterName( QStringLiteral( "test_layer" ) );
  QCOMPARE( def->dependsOnOtherParameters(), QStringList() << QStringLiteral( "test_layer" ) );


  // optional
  def.reset( new QgsProcessingParameterFieldMapping( "non_optional", QString(), QStringLiteral( "parent" ), true ) );
  QVERIFY( !def->checkValueIsAcceptable( 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << map << map2 ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFieldMapping('non_optional', '', parentLayerParameterName='parent', optional=True)" ) );
}

void TestQgsProcessing::parameterAggregate()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterAggregate > def( new QgsProcessingParameterAggregate( "non_optional", QString(), QStringLiteral( "parent" ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() ) );
  QVariantMap map;
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << map ) );
  map.insert( QStringLiteral( "name" ), QStringLiteral( "n" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << map ) );
  map.insert( QStringLiteral( "type" ), QStringLiteral( "t" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << map ) );
  map.insert( QStringLiteral( "aggregate" ), QStringLiteral( "e" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << map ) );
  map.insert( QStringLiteral( "input" ), QStringLiteral( "i" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << map ) );
  QVariantMap map2;
  map2.insert( QStringLiteral( "name" ), QStringLiteral( "n2" ) );
  map2.insert( QStringLiteral( "type" ), QStringLiteral( "t2" ) );
  map2.insert( QStringLiteral( "aggregate" ), QStringLiteral( "e2" ) );
  map2.insert( QStringLiteral( "input" ), QStringLiteral( "i2" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << map << map2 ) );

  QVariantMap params;
  params.insert( "non_optional", QVariantList() << map << map2 );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( 5, context ), QStringLiteral( "5" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc" ), context ), QStringLiteral( "'abc'" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "abc\ndef" ), context ), QStringLiteral( "'abc\\ndef'" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );
  QCOMPARE( def->valueAsPythonString( QVariant( QVariantList() << map << map2 ), context ), QStringLiteral( "[{'aggregate': 'e','input': 'i','name': 'n','type': 't'},{'aggregate': 'e2','input': 'i2','name': 'n2','type': 't2'}]" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterAggregate('non_optional', '', parentLayerParameterName='parent')" ) );

  QVariantMap mapDef = def->toVariantMap();
  QgsProcessingParameterAggregate fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( mapDef ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.parentLayerParameterName(), def->parentLayerParameterName() );
  def.reset( dynamic_cast< QgsProcessingParameterAggregate *>( QgsProcessingParameters::parameterFromVariantMap( mapDef ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterAggregate *>( def.get() ) );

  def->setParentLayerParameterName( QString() );
  QVERIFY( def->dependsOnOtherParameters().isEmpty() );
  def->setParentLayerParameterName( QStringLiteral( "test_layer" ) );
  QCOMPARE( def->dependsOnOtherParameters(), QStringList() << QStringLiteral( "test_layer" ) );


  // optional
  def.reset( new QgsProcessingParameterAggregate( "non_optional", QString(), QStringLiteral( "parent" ), true ) );
  QVERIFY( !def->checkValueIsAcceptable( 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << map << map2 ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterAggregate('non_optional', '', parentLayerParameterName='parent', optional=True)" ) );
}

void TestQgsProcessing::parameterTinInputLayers()
{
  QgsProcessingContext context;
  QgsProject project;
  context.setProject( &project );
  QgsVectorLayer *vectorLayer = new QgsVectorLayer( QStringLiteral( "Point" ),
      QStringLiteral( "PointLayerForTin" ),
      QStringLiteral( "memory" ) );
  project.addMapLayer( vectorLayer );

  std::unique_ptr< QgsProcessingParameterTinInputLayers > def( new QgsProcessingParameterTinInputLayers( "tin input layer" ) );
  QVERIFY( !def->checkValueIsAcceptable( 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVariantList layerList;
  QVERIFY( !def->checkValueIsAcceptable( layerList ) );
  QVariantMap layerMap;
  layerList.append( layerMap );
  QVERIFY( !def->checkValueIsAcceptable( layerList ) );
  layerMap["source"] = "layerName";
  layerMap["type"] = 0;
  layerMap["attributeIndex"] = -1;
  layerList[0] = layerMap;
  QVERIFY( def->checkValueIsAcceptable( layerList ) );
  QVERIFY( !def->checkValueIsAcceptable( layerList, &context ) ); //no corresponding layer in the context's project
  layerMap["source"] = "PointLayerForTin";
  layerMap["attributeIndex"] = 1; //change for invalid attribute index
  layerList[0] = layerMap;
  QVERIFY( !def->checkValueIsAcceptable( layerList, &context ) );

  layerMap["attributeIndex"] = -1; //valid attribute index (-1 is for Z coordinate of features)
  layerList[0] = layerMap;
  QVERIFY( def->checkValueIsAcceptable( layerList, &context ) );

  QString valueAsPythonString = def->valueAsPythonString( layerList, context );
  QCOMPARE( valueAsPythonString, QStringLiteral( "[{'source': 'PointLayerForTin','type': 0,'attributeIndex': -1}]" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterTinInputLayers('tin input layer', '')" ) );
}

void TestQgsProcessing::parameterMeshDatasetGroups()
{
  QgsProcessingContext context;
  QgsProject project;
  context.setProject( &project );

  QSet<int> supportedData;
  supportedData << QgsMeshDatasetGroupMetadata::DataOnVertices;
  std::unique_ptr< QgsProcessingParameterMeshDatasetGroups> def(
    new QgsProcessingParameterMeshDatasetGroups( QStringLiteral( "dataset groups" ), QStringLiteral( "groups" ), QString(), supportedData ) );

  QVERIFY( def->type() == QStringLiteral( "meshdatasetgroups" ) );
  QVERIFY( def->isDataTypeSupported( QgsMeshDatasetGroupMetadata::DataOnVertices ) );
  QVERIFY( !def->checkValueIsAcceptable( 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( 1.0 ) );
  QVERIFY( !def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVariantList groupsList;
  QVERIFY( !def->checkValueIsAcceptable( groupsList ) );
  groupsList.append( 0 );
  QVERIFY( def->checkValueIsAcceptable( groupsList ) );
  groupsList.append( 5 );
  QVERIFY( def->checkValueIsAcceptable( groupsList ) );

  QVERIFY( def->dependsOnOtherParameters().isEmpty() );

  QString valueAsPythonString = def->valueAsPythonString( groupsList, context );
  QCOMPARE( valueAsPythonString, QStringLiteral( "[0,5]" ) );
  QCOMPARE( QgsProcessingParameterMeshDatasetGroups::valueAsDatasetGroup( groupsList ), QList<int>() << 0 << 5 );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMeshDatasetGroups('dataset groups', 'groups', dataType=[QgsMeshDatasetGroupMetadata.DataOnVertices])" ) );

  // optional, layer parameter and data on faces
  supportedData << QgsMeshDatasetGroupMetadata::DataOnFaces;
  def.reset( new QgsProcessingParameterMeshDatasetGroups(
               QStringLiteral( "dataset groups" ),
               QStringLiteral( "groups" ),
               QStringLiteral( "layer parameter" ),
               supportedData, true ) );
  QVERIFY( def->isDataTypeSupported( QgsMeshDatasetGroupMetadata::DataOnFaces ) );
  QVERIFY( !def->checkValueIsAcceptable( 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( 1.0 ) );
  QVERIFY( !def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  groupsList = QVariantList();
  QVERIFY( def->checkValueIsAcceptable( groupsList ) );
  groupsList.append( 2 );
  QVERIFY( def->checkValueIsAcceptable( groupsList ) );
  groupsList.append( 6 );
  QVERIFY( def->checkValueIsAcceptable( groupsList ) );

  valueAsPythonString = def->valueAsPythonString( groupsList, context );
  QCOMPARE( valueAsPythonString, QStringLiteral( "[2,6]" ) );
  QCOMPARE( QgsProcessingParameterMeshDatasetGroups::valueAsDatasetGroup( groupsList ), QList<int>() << 2 << 6 );

  QVERIFY( !def->dependsOnOtherParameters().isEmpty() );
  QCOMPARE( def->meshLayerParameterName(), QStringLiteral( "layer parameter" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMeshDatasetGroups('dataset groups', 'groups', meshLayerParameterName='layer parameter', dataType=[QgsMeshDatasetGroupMetadata.DataOnFaces,QgsMeshDatasetGroupMetadata.DataOnVertices], optional=True)" ) );
}

void TestQgsProcessing::parameterMeshDatasetTime()
{
  QgsProcessingContext context;
  QgsProject project;
  context.setProject( &project );

  std::unique_ptr< QgsProcessingParameterMeshDatasetTime> def( new QgsProcessingParameterMeshDatasetTime( QStringLiteral( "dataset groups" ), QStringLiteral( "groups" ) ) );
  QVERIFY( def->type() == QStringLiteral( "meshdatasettime" ) );
  QVERIFY( !def->checkValueIsAcceptable( 1 ) );
  QVERIFY( !def->checkValueIsAcceptable( 1.0 ) );
  QVERIFY( !def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "a" << "b" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );

  QVariantMap value;
  value[QStringLiteral( "test" )] = QStringLiteral( "test" );
  QVERIFY( !def->checkValueIsAcceptable( value ) );

  value.clear();
  value[QStringLiteral( "type" )] = QStringLiteral( "test" );
  QVERIFY( !def->checkValueIsAcceptable( value ) );

  value[QStringLiteral( "type" )] = QStringLiteral( "static" );
  QVERIFY( def->checkValueIsAcceptable( value ) );
  QCOMPARE( def->valueAsPythonString( value, context ), QStringLiteral( "{'type': 'static'}" ) );
  QCOMPARE( QgsProcessingParameterMeshDatasetTime::valueAsTimeType( value ), QStringLiteral( "static" ) );


  value[QStringLiteral( "type" )] = QStringLiteral( "current-context-time" );
  QVERIFY( def->checkValueIsAcceptable( value ) );
  QCOMPARE( def->valueAsPythonString( value, context ), QStringLiteral( "{'type': 'current-context-time'}" ) );
  QCOMPARE( QgsProcessingParameterMeshDatasetTime::valueAsTimeType( value ), QStringLiteral( "current-context-time" ) );

  value[QStringLiteral( "type" )] = QStringLiteral( "defined-date-time" );
  QVERIFY( !def->checkValueIsAcceptable( value ) );
  value[QStringLiteral( "value" )] = QDateTime( QDate( 2123, 1, 2 ), QTime( 1, 2, 3 ) );
  QVERIFY( def->checkValueIsAcceptable( value ) );
  QCOMPARE( def->valueAsPythonString( value, context ), QStringLiteral( "{'type': 'defined-date-time','value': QDateTime(QDate(2123, 1, 2), QTime(1, 2, 3))}" ) );
  QCOMPARE( QgsProcessingParameterMeshDatasetTime::valueAsTimeType( value ), QStringLiteral( "defined-date-time" ) );
  QCOMPARE( QgsProcessingParameterMeshDatasetTime::timeValueAsDefinedDateTime( value ), QDateTime( QDate( 2123, 1, 2 ), QTime( 1, 2, 3 ) ) );
  QVERIFY( !QgsProcessingParameterMeshDatasetTime::timeValueAsDatasetIndex( value ).isValid() );

  value.clear();
  value[QStringLiteral( "type" )] = QStringLiteral( "dataset-time-step" );
  QVERIFY( !def->checkValueIsAcceptable( value ) );
  value[QStringLiteral( "value" )] = QVariantList() << 1 << 5;
  QVERIFY( def->checkValueIsAcceptable( value ) );
  QCOMPARE( def->valueAsPythonString( value, context ), QStringLiteral( "{'type': 'dataset-time-step','value': QgsMeshDatasetIndex(1,5)}" ) );
  QCOMPARE( QgsProcessingParameterMeshDatasetTime::valueAsTimeType( value ), QStringLiteral( "dataset-time-step" ) );
  QVERIFY( !QgsProcessingParameterMeshDatasetTime::timeValueAsDefinedDateTime( value ).isValid() );
  QVERIFY( QgsProcessingParameterMeshDatasetTime::timeValueAsDatasetIndex( value ) == QgsMeshDatasetIndex( 1, 5 ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMeshDatasetTime('dataset groups', 'groups')" ) );

  QVERIFY( def->dependsOnOtherParameters().isEmpty() );

  def.reset( new QgsProcessingParameterMeshDatasetTime( QStringLiteral( "dataset groups" ), QStringLiteral( "groups" ), QStringLiteral( "layer parameter" ), QStringLiteral( "dataset group parameter" ) ) );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterMeshDatasetTime('dataset groups', 'groups', meshLayerParameterName='layer parameter', datasetGroupParameterName='dataset group parameter')" ) );

  QVERIFY( !def->dependsOnOtherParameters().isEmpty() );
  QCOMPARE( def->meshLayerParameterName(), QStringLiteral( "layer parameter" ) );
  QCOMPARE( def->datasetGroupParameterName(), QStringLiteral( "dataset group parameter" ) );
}

void TestQgsProcessing::parameterDateTime()
{
  QgsProcessingContext context;

  // not optional!
  std::unique_ptr< QgsProcessingParameterDateTime > def( new QgsProcessingParameterDateTime( "non_optional", QString(), QgsProcessingParameterDateTime::DateTime, QDateTime( QDate( 2010, 4, 3 ), QTime( 12, 11, 10 ) ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( QDateTime( QDate( 2020, 2, 2 ), QTime( 0, 0, 0 ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QDate( 2020, 2, 2 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QTime( 13, 14, 15 ) ) );
  QVERIFY( def->checkValueIsAcceptable( "2020-02-03" ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromExpression( "to_date( '2010-02-03') +  to_interval( '2 days')" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be acceptable, falls back to default value

  // QDateTime value
  QVariantMap params;
  params.insert( "non_optional", QDateTime( QDate( 2010, 3, 4 ), QTime( 12, 11, 10 ) ) );
  QDateTime d = QgsProcessingParameters::parameterAsDateTime( def.get(), params, context );
  QCOMPARE( d, QDateTime( QDate( 2010, 3, 4 ), QTime( 12, 11, 10 ) ) );
  QTime t = QgsProcessingParameters::parameterAsTime( def.get(), params, context );
  QCOMPARE( t, QTime( 12, 11, 10 ) );

  params.insert( "non_optional", QDateTime( QDate( 2010, 3, 4 ), QTime( 0, 0, 0 ) ) );
  d = QgsProcessingParameters::parameterAsDateTime( def.get(), params, context );
  QCOMPARE( d, QDateTime( QDate( 2010, 3, 4 ), QTime() ) );

  // string representing a datetime
  params.insert( "non_optional", QString( "2010-03-04" ) );
  d = QgsProcessingParameters::parameterAsDateTime( def.get(), params, context );
  QCOMPARE( d, QDateTime( QDate( 2010, 3, 4 ), QTime() ) );

  // expression
  params.insert( "non_optional", QgsProperty::fromExpression( "to_date( '2010-02-03') +  to_interval( '2 days')" ) );
  d = QgsProcessingParameters::parameterAsDateTime( def.get(), params, context );
  QCOMPARE( d, QDateTime( QDate( 2010, 2, 5 ), QTime() ) );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a datetime, and nothing you can do will make me one" ) );
  d = QgsProcessingParameters::parameterAsDateTime( def.get(), params, context );
  QCOMPARE( d, QDateTime( QDate( 2010, 4, 3 ), QTime( 12, 11, 10 ) ) );

  // with min value
  def->setMinimum( QDateTime( QDate( 2015, 1, 1 ), QTime( 0, 0, 0 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QDateTime( QDate( 2014, 12, 31 ), QTime( 0, 0, 0 ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringLiteral( "2014-12-31" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QDateTime( QDate( 2020, 12, 31 ), QTime( 0, 0, 0 ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "2020-12-31" ) ) );
  // with max value
  def->setMaximum( QDateTime( QDate( 2015, 12, 31 ), QTime( 0, 0, 0 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QDateTime( QDate( 2014, 12, 31 ), QTime( 0, 0, 0 ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringLiteral( "2014-12-31" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QDateTime( QDate( 2016, 1, 1 ), QTime( 0, 0, 0 ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringLiteral( "2016-01-01" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QDateTime( QDate( 2015, 12, 31 ), QTime( 0, 0, 0 ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "2015-12-31" ) ) );

  QCOMPARE( def->valueAsPythonString( QVariant(), context ), QStringLiteral( "None" ) );
  QCOMPARE( def->valueAsPythonString( QDateTime( QDate( 2014, 12, 31 ), QTime( 0, 0, 0 ) ), context ), QStringLiteral( "QDateTime(QDate(2014, 12, 31), QTime(0, 0, 0))" ) );
  QCOMPARE( def->valueAsPythonString( QDateTime( QDate( 2014, 12, 31 ), QTime( 12, 11, 10 ) ), context ), QStringLiteral( "QDateTime(QDate(2014, 12, 31), QTime(12, 11, 10))" ) );
  QCOMPARE( def->valueAsPythonString( QStringLiteral( "2015-12-31" ), context ), QStringLiteral( "2015-12-31" ) );
  QCOMPARE( def->valueAsPythonString( QVariant::fromValue( QgsProperty::fromExpression( "\"a\"=1" ) ), context ), QStringLiteral( "QgsProperty.fromExpression('\"a\"=1')" ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDateTime('non_optional', '', type=QgsProcessingParameterDateTime.DateTime, minValue=QDateTime(QDate(2015, 1, 1), QTime(0, 0, 0)), maxValue=QDateTime(QDate(2015, 12, 31), QTime(0, 0, 0)), defaultValue=QDateTime(QDate(2010, 4, 3), QTime(12, 11, 10)))" ) );

  QString code = def->asScriptCode();
  QCOMPARE( code.left( 43 ), QStringLiteral( "##non_optional=datetime 2010-04-03T12:11:10" ) );
  std::unique_ptr< QgsProcessingParameterDateTime > fromCode( dynamic_cast< QgsProcessingParameterDateTime * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "non optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  QVariantMap map = def->toVariantMap();
  QgsProcessingParameterDateTime fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.minimum(), def->minimum() );
  QCOMPARE( fromMap.maximum(), def->maximum() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  def.reset( dynamic_cast< QgsProcessingParameterDateTime *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterDateTime *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterDateTime( "optional", QString(), QgsProcessingParameterDateTime::DateTime, QDateTime( QDate( 2018, 5, 6 ), QTime( 4, 5, 6 ) ), true ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( QDateTime( QDate( 2020, 2, 2 ), QTime( 0, 0, 0 ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QDate( 2020, 2, 2 ) ) );
  QVERIFY( def->checkValueIsAcceptable( "2020-02-03" ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromExpression( "to_date( '2010-02-03') +  to_interval( '2 days')" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );

  params.insert( "optional",  QVariant() );
  d = QgsProcessingParameters::parameterAsDateTime( def.get(), params, context );
  QCOMPARE( d, QDateTime( QDate( 2018, 5, 6 ), QTime( 4, 5, 6 ) ) );
  params.insert( "optional",  QString() );
  d = QgsProcessingParameters::parameterAsDateTime( def.get(), params, context );
  QCOMPARE( d, QDateTime( QDate( 2018, 5, 6 ), QTime( 4, 5, 6 ) ) );
  params.insert( "optional",  QVariant( "aaaa" ) );
  d = QgsProcessingParameters::parameterAsDateTime( def.get(), params, context );
  QCOMPARE( d, QDateTime( QDate( 2018, 5, 6 ), QTime( 4, 5, 6 ) ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDateTime('optional', '', optional=True, type=QgsProcessingParameterDateTime.DateTime, defaultValue=QDateTime(QDate(2018, 5, 6), QTime(4, 5, 6)))" ) );

  code = def->asScriptCode();
  QCOMPARE( code.left( 48 ), QStringLiteral( "##optional=optional datetime 2018-05-06T04:05:06" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterDateTime * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterDateTime * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##optional=optional datetime None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );

  // non-optional, invalid default
  def.reset( new QgsProcessingParameterDateTime( "non_optional", QString(), QgsProcessingParameterDateTime::DateTime, QVariant(), false ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( QDateTime( QDate( 2020, 2, 2 ), QTime( 0, 0, 0 ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QDate( 2020, 2, 2 ) ) );
  QVERIFY( def->checkValueIsAcceptable( "2020-02-03" ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromExpression( "to_date( '2010-02-03') +  to_interval( '2 days')" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) ); // should NOT be acceptable, falls back to invalid default value


  // date only mode

  // not optional!
  def.reset( new QgsProcessingParameterDateTime( "non_optional", QString(), QgsProcessingParameterDateTime::Date, QDate( 2010, 4, 3 ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( def->checkValueIsAcceptable( QDateTime( QDate( 2020, 2, 2 ), QTime( 0, 0, 0 ) ) ) );
  QVERIFY( def->checkValueIsAcceptable( QDate( 2020, 2, 2 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QTime( 13, 14, 15 ) ) );
  QVERIFY( def->checkValueIsAcceptable( "2020-02-03" ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromExpression( "to_date( '2010-02-03') +  to_interval( '2 days')" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be acceptable, falls back to default value

  // QDateTime value
  params.insert( "non_optional", QDateTime( QDate( 2010, 3, 4 ), QTime( 12, 11, 10 ) ) );
  QDate dt = QgsProcessingParameters::parameterAsDate( def.get(), params, context );
  QCOMPARE( dt, QDate( 2010, 3, 4 ) );

  params.insert( "non_optional", QDate( 2010, 3, 4 ) );
  dt = QgsProcessingParameters::parameterAsDate( def.get(), params, context );
  QCOMPARE( dt, QDate( 2010, 3, 4 ) );

  // string representing a date
  params.insert( "non_optional", QString( "2010-03-04" ) );
  dt = QgsProcessingParameters::parameterAsDate( def.get(), params, context );
  QCOMPARE( dt, QDate( 2010, 3, 4 ) );

  // expression
  params.insert( "non_optional", QgsProperty::fromExpression( "to_date( '2010-02-03') +  to_interval( '2 days')" ) );
  dt = QgsProcessingParameters::parameterAsDate( def.get(), params, context );
  QCOMPARE( dt, QDate( 2010, 2, 5 ) );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a datetime, and nothing you can do will make me one" ) );
  dt = QgsProcessingParameters::parameterAsDate( def.get(), params, context );
  QCOMPARE( dt, QDate( QDate( 2010, 4, 3 ) ) );

  // with min value
  def->setMinimum( QDateTime( QDate( 2015, 1, 1 ), QTime( 0, 0, 0 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QDate( 2014, 12, 31 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringLiteral( "2014-12-31" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QDate( 2020, 12, 31 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "2020-12-31" ) ) );
  // with max value
  def->setMaximum( QDateTime( QDate( 2015, 12, 31 ), QTime( 0, 0, 0 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QDate( 2014, 12, 31 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringLiteral( "2014-12-31" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QDate( 2016, 1, 1 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringLiteral( "2016-01-01" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QDate( 2015, 12, 31 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "2015-12-31" ) ) );

  QCOMPARE( def->valueAsPythonString( QDate( 2014, 12, 31 ), context ), QStringLiteral( "QDate(2014, 12, 31)" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDateTime('non_optional', '', type=QgsProcessingParameterDateTime.Date, minValue=QDateTime(QDate(2015, 1, 1), QTime(0, 0, 0)), maxValue=QDateTime(QDate(2015, 12, 31), QTime(0, 0, 0)), defaultValue=QDate(2010, 4, 3))" ) );

  map = def->toVariantMap();
  fromMap =  QgsProcessingParameterDateTime( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.minimum(), def->minimum() );
  QCOMPARE( fromMap.maximum(), def->maximum() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  def.reset( dynamic_cast< QgsProcessingParameterDateTime *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterDateTime *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterDateTime( "optional", QString(), QgsProcessingParameterDateTime::Date, QDate( 2018, 5, 6 ), true ) );

  params.insert( "optional",  QVariant() );
  dt = QgsProcessingParameters::parameterAsDate( def.get(), params, context );
  QCOMPARE( dt, QDate( 2018, 5, 6 ) );
  params.insert( "optional",  QString() );
  dt = QgsProcessingParameters::parameterAsDate( def.get(), params, context );
  QCOMPARE( dt, QDate( 2018, 5, 6 ) );
  params.insert( "optional",  QVariant( "aaaa" ) );
  dt = QgsProcessingParameters::parameterAsDate( def.get(), params, context );
  QCOMPARE( dt, QDate( 2018, 5, 6 ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDateTime('optional', '', optional=True, type=QgsProcessingParameterDateTime.Date, defaultValue=QDate(2018, 5, 6))" ) );

  code = def->asScriptCode();
  QCOMPARE( code, QStringLiteral( "##optional=optional datetime 2018-05-06" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterDateTime * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterDateTime * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##optional=optional datetime None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );

  // time only mode

  // not optional!
  def.reset( new QgsProcessingParameterDateTime( "non_optional", QString(), QgsProcessingParameterDateTime::Time, QTime( 12, 11, 13 ), false ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  QVERIFY( !def->checkValueIsAcceptable( "1.1" ) );
  QVERIFY( !def->checkValueIsAcceptable( QDateTime( QDate( 2020, 2, 2 ), QTime( 0, 0, 0 ) ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QDate( 2020, 2, 2 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QTime( 13, 14, 15 ) ) );
  QVERIFY( def->checkValueIsAcceptable( "13:14:15" ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromExpression( "to_time('12:30:01') +  to_interval( '2 minutes')" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( "layer12312312" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) ); // should be acceptable, falls back to default value

  // QTime value
  params.insert( "non_optional", QTime( 12, 11, 10 ) );
  t = QgsProcessingParameters::parameterAsTime( def.get(), params, context );
  QCOMPARE( t, QTime( 12, 11, 10 ) );

  params.insert( "non_optional", QDate( 2010, 3, 4 ) );
  t = QgsProcessingParameters::parameterAsTime( def.get(), params, context );
  QCOMPARE( t, QTime( 12, 11, 13 ) );

  // string representing a time
  params.insert( "non_optional", QString( "13:14:15" ) );
  t = QgsProcessingParameters::parameterAsTime( def.get(), params, context );
  QCOMPARE( t, QTime( 13, 14, 15 ) );

  // expression
  params.insert( "non_optional", QgsProperty::fromExpression( "to_time('12:30:01') +  to_interval( '2 minutes')" ) );
  t = QgsProcessingParameters::parameterAsTime( def.get(), params, context );
  QCOMPARE( t, QTime( 12, 32, 1 ) );

  // nonsense string
  params.insert( "non_optional", QString( "i'm not a datetime, and nothing you can do will make me one" ) );
  t = QgsProcessingParameters::parameterAsTime( def.get(), params, context );
  QCOMPARE( t, QTime( 12, 11, 13 ) );

  // with min value
  def->setMinimum( QDateTime( QDate( 1, 1, 1 ), QTime( 10, 0, 0 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QTime( 9, 0, 0 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringLiteral( "9:00:00" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QTime( 12, 0, 0 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "12:00:00" ) ) );
  // with max value
  def->setMaximum( QDateTime( QDate( 1, 1, 1 ), QTime( 11, 0, 0 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QTime( 9, 0, 0 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringLiteral( "9:00:00" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QTime( 11, 0, 1 ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringLiteral( "11:00:01" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QTime( 10, 40, 1 ) ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "10:40:01" ) ) );

  QCOMPARE( def->valueAsPythonString( QTime( 13, 14, 15 ), context ), QStringLiteral( "QTime(13, 14, 15)" ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDateTime('non_optional', '', type=QgsProcessingParameterDateTime.Time, minValue=QDateTime(QDate(1, 1, 1), QTime(10, 0, 0)), maxValue=QDateTime(QDate(1, 1, 1), QTime(11, 0, 0)), defaultValue=QTime(12, 11, 13))" ) );

  map = def->toVariantMap();
  fromMap =  QgsProcessingParameterDateTime( "x" );
  QVERIFY( fromMap.fromVariantMap( map ) );
  QCOMPARE( fromMap.name(), def->name() );
  QCOMPARE( fromMap.description(), def->description() );
  QCOMPARE( fromMap.flags(), def->flags() );
  QCOMPARE( fromMap.defaultValue(), def->defaultValue() );
  QCOMPARE( fromMap.minimum(), def->minimum() );
  QCOMPARE( fromMap.maximum(), def->maximum() );
  QCOMPARE( fromMap.dataType(), def->dataType() );
  def.reset( dynamic_cast< QgsProcessingParameterDateTime *>( QgsProcessingParameters::parameterFromVariantMap( map ) ) );
  QVERIFY( dynamic_cast< QgsProcessingParameterDateTime *>( def.get() ) );

  // optional
  def.reset( new QgsProcessingParameterDateTime( "optional", QString(), QgsProcessingParameterDateTime::Time, QTime( 14, 15, 16 ), true ) );

  params.insert( "optional",  QVariant() );
  t = QgsProcessingParameters::parameterAsTime( def.get(), params, context );
  QCOMPARE( t, QTime( 14, 15, 16 ) );
  params.insert( "optional",  QString() );
  t = QgsProcessingParameters::parameterAsTime( def.get(), params, context );
  QCOMPARE( t, QTime( 14, 15, 16 ) );
  params.insert( "optional",  QVariant( "aaaa" ) );
  t = QgsProcessingParameters::parameterAsTime( def.get(), params, context );
  QCOMPARE( t, QTime( 14, 15, 16 ) );

  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDateTime('optional', '', optional=True, type=QgsProcessingParameterDateTime.Time, defaultValue=QTime(14, 15, 16))" ) );

  code = def->asScriptCode();
  QCOMPARE( code.left( 37 ), QStringLiteral( "##optional=optional datetime 14:15:16" ) );
  fromCode.reset( dynamic_cast< QgsProcessingParameterDateTime * >( QgsProcessingParameters::parameterFromScriptCode( code ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QCOMPARE( fromCode->defaultValue(), def->defaultValue() );

  fromCode.reset( dynamic_cast< QgsProcessingParameterDateTime * >( QgsProcessingParameters::parameterFromScriptCode( QStringLiteral( "##optional=optional datetime None" ) ) ) );
  QVERIFY( fromCode.get() );
  QCOMPARE( fromCode->name(), def->name() );
  QCOMPARE( fromCode->description(), QStringLiteral( "optional" ) );
  QCOMPARE( fromCode->flags(), def->flags() );
  QVERIFY( !fromCode->defaultValue().isValid() );
}

void TestQgsProcessing::parameterDxfLayers()
{
  QgsProcessingContext context;
  QgsProject project;
  context.setProject( &project );
  QgsVectorLayer *vectorLayer = new QgsVectorLayer( QStringLiteral( "Point" ),
      QStringLiteral( "PointLayer" ),
      QStringLiteral( "memory" ) );
  project.addMapLayer( vectorLayer );

  std::unique_ptr< QgsProcessingParameterDxfLayers > def( new QgsProcessingParameterDxfLayers( "dxf input layer" ) );
  QVERIFY( !def->checkValueIsAcceptable( 1 ) );
  QVERIFY( def->checkValueIsAcceptable( "test" ) );
  QVERIFY( !def->checkValueIsAcceptable( "" ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant::fromValue( vectorLayer ) ) );

  // should also be OK
  QVERIFY( def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  QVERIFY( def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp" ) );
  // ... unless we use context, when the check that the layer actually exists is performed
  QVERIFY( !def->checkValueIsAcceptable( "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QStringList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariantList() << "c:/Users/admin/Desktop/roads_clipped_transformed_v1_reprojected_final_clipped_aAAA.shp", &context ) );

  QVariantList layerList;
  QVERIFY( !def->checkValueIsAcceptable( layerList ) );
  QVariantMap layerMap;
  layerList.append( layerMap );
  QVERIFY( !def->checkValueIsAcceptable( layerList ) );
  layerMap["layer"] = "layerName";
  layerMap["attributeIndex"] = -1;
  layerList[0] = layerMap;
  QVERIFY( def->checkValueIsAcceptable( layerList ) );
  QVERIFY( !def->checkValueIsAcceptable( layerList, &context ) ); //no corresponding layer in the context's project
  layerMap["layer"] = "PointLayer";
  layerMap["attributeIndex"] = 1; //change for invalid attribute index
  layerList[0] = layerMap;
  QVERIFY( !def->checkValueIsAcceptable( layerList, &context ) );

  layerMap["attributeIndex"] = -1;
  layerList[0] = layerMap;
  QVERIFY( def->checkValueIsAcceptable( layerList, &context ) );

  QString valueAsPythonString = def->valueAsPythonString( layerList, context );
  QCOMPARE( valueAsPythonString, QStringLiteral( "[{'layer': '%1','attributeIndex': -1}]" ).arg( vectorLayer->source() ) );

  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterDxfLayers('dxf input layer', '')" ) );

  QgsDxfExport::DxfLayer dxfLayer( vectorLayer );
  QList<QgsDxfExport::DxfLayer> dxfList = def->parameterAsLayers( QVariant( vectorLayer->source() ), context );
  QCOMPARE( dxfList.at( 0 ).layer()->source(), dxfLayer.layer()->source() );
  QCOMPARE( dxfList.at( 0 ).layerOutputAttributeIndex(), dxfLayer.layerOutputAttributeIndex() );
  dxfList = def->parameterAsLayers( QVariant( QStringList() << vectorLayer->source() ), context );
  QCOMPARE( dxfList.at( 0 ).layer()->source(), dxfLayer.layer()->source() );
  QCOMPARE( dxfList.at( 0 ).layerOutputAttributeIndex(), dxfLayer.layerOutputAttributeIndex() );
  dxfList = def->parameterAsLayers( layerList, context );
  QCOMPARE( dxfList.at( 0 ).layer()->source(), dxfLayer.layer()->source() );
  QCOMPARE( dxfList.at( 0 ).layerOutputAttributeIndex(), dxfLayer.layerOutputAttributeIndex() );
}

void TestQgsProcessing::checkParamValues()
{
  DummyAlgorithm a( "asd" );
  a.checkParameterVals();
}

void TestQgsProcessing::combineLayerExtent()
{
  QgsProcessingContext context;
  QgsRectangle ext = QgsProcessingUtils::combineLayerExtents( QList< QgsMapLayer *>(), QgsCoordinateReferenceSystem(), context );
  QVERIFY( ext.isNull() );

  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt

  QString raster1 = testDataDir + "tenbytenraster.asc";
  QString raster2 = testDataDir + "landsat.tif";
  QFileInfo fi1( raster1 );
  std::unique_ptr< QgsRasterLayer > r1( new QgsRasterLayer( fi1.filePath(), "R1" ) );
  QFileInfo fi2( raster2 );
  std::unique_ptr< QgsRasterLayer > r2( new QgsRasterLayer( fi2.filePath(), "R2" ) );

  ext = QgsProcessingUtils::combineLayerExtents( QList< QgsMapLayer *>() << r1.get(), QgsCoordinateReferenceSystem(), context );
  QGSCOMPARENEAR( ext.xMinimum(), 1535375.000000, 10 );
  QGSCOMPARENEAR( ext.xMaximum(), 1535475, 10 );
  QGSCOMPARENEAR( ext.yMinimum(), 5083255, 10 );
  QGSCOMPARENEAR( ext.yMaximum(), 5083355, 10 );

  ext = QgsProcessingUtils::combineLayerExtents( QList< QgsMapLayer *>() << r1.get() << r2.get(), QgsCoordinateReferenceSystem(), context );
  QGSCOMPARENEAR( ext.xMinimum(), 781662, 10 );
  QGSCOMPARENEAR( ext.xMaximum(), 1535475, 10 );
  QGSCOMPARENEAR( ext.yMinimum(), 3339523, 10 );
  QGSCOMPARENEAR( ext.yMaximum(), 5083355, 10 );

  // with reprojection
  ext = QgsProcessingUtils::combineLayerExtents( QList< QgsMapLayer *>() << r1.get() << r2.get(), QgsCoordinateReferenceSystem::fromEpsgId( 3785 ), context );
  QGSCOMPARENEAR( ext.xMinimum(), 1535375.0, 10 );
  QGSCOMPARENEAR( ext.xMaximum(), 2008833, 10 );
  QGSCOMPARENEAR( ext.yMinimum(), 3523084, 10 );
  QGSCOMPARENEAR( ext.yMaximum(), 5083355, 10 );
}

void TestQgsProcessing::processingFeatureSource()
{
  QString sourceString = QStringLiteral( "test.shp" );
  QgsProcessingFeatureSourceDefinition fs( sourceString, true, 21, QgsProcessingFeatureSourceDefinition::Flag::FlagOverrideDefaultGeometryCheck, QgsFeatureRequest::GeometrySkipInvalid );
  QCOMPARE( fs.source.staticValue().toString(), sourceString );
  QVERIFY( fs.selectedFeaturesOnly );
  QCOMPARE( fs.featureLimit, 21LL );
  QCOMPARE( fs.flags, QgsProcessingFeatureSourceDefinition::Flag::FlagOverrideDefaultGeometryCheck );
  QCOMPARE( fs.geometryCheck, QgsFeatureRequest::GeometrySkipInvalid );

  // test storing QgsProcessingFeatureSource in variant and retrieving
  QVariant fsInVariant = QVariant::fromValue( fs );
  QVERIFY( fsInVariant.isValid() );

  // test converting to variant map and back
  QVariant res = fs.toVariant();
  QgsProcessingFeatureSourceDefinition dd;
  QVERIFY( dd.loadVariant( res.toMap() ) );
  QCOMPARE( dd.source.staticValue().toString(), sourceString );
  QVERIFY( dd.selectedFeaturesOnly );
  QCOMPARE( dd.featureLimit, 21LL );
  QCOMPARE( dd.flags, QgsProcessingFeatureSourceDefinition::Flag::FlagOverrideDefaultGeometryCheck );
  QCOMPARE( dd.geometryCheck, QgsFeatureRequest::GeometrySkipInvalid );

  QgsProcessingFeatureSourceDefinition fromVar = qvariant_cast<QgsProcessingFeatureSourceDefinition>( fsInVariant );
  QCOMPARE( fromVar.source.staticValue().toString(), sourceString );
  QVERIFY( fromVar.selectedFeaturesOnly );
  QCOMPARE( fromVar.featureLimit, 21LL );
  QCOMPARE( fromVar.flags, QgsProcessingFeatureSourceDefinition::Flag::FlagOverrideDefaultGeometryCheck );
  QCOMPARE( fromVar.geometryCheck, QgsFeatureRequest::GeometrySkipInvalid );

  // test evaluating parameter as source
  QgsVectorLayer *layer = new QgsVectorLayer( "Point", "v1", "memory" );
  QgsFeature f( 10001 );
  f.setGeometry( QgsGeometry( new QgsPoint( 1, 2 ) ) );
  layer->dataProvider()->addFeatures( QgsFeatureList() << f );

  QgsProject p;
  p.addMapLayer( layer );
  QgsProcessingContext context;
  context.setProject( &p );

  // first using static string definition
  std::unique_ptr< QgsProcessingParameterDefinition > def( new QgsProcessingParameterString( QStringLiteral( "layer" ) ) );
  QVariantMap params;
  params.insert( QStringLiteral( "layer" ), QgsProcessingFeatureSourceDefinition( layer->id(), false ) );
  std::unique_ptr< QgsFeatureSource > source( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  // can't directly match it to layer, so instead just get the feature and test that it matches what we expect
  QgsFeature f2;
  QVERIFY( source.get() );
  QVERIFY( source->getFeatures().nextFeature( f2 ) );
  QCOMPARE( f2.geometry().asWkt(), f.geometry().asWkt() );

  // direct map layer
  params.insert( QStringLiteral( "layer" ), QVariant::fromValue( layer ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  // can't directly match it to layer, so instead just get the feature and test that it matches what we expect
  QVERIFY( source.get() );
  QVERIFY( source->getFeatures().nextFeature( f2 ) );
  QCOMPARE( f2.geometry().asWkt(), f.geometry().asWkt() );

  // next using property based definition
  params.insert( QStringLiteral( "layer" ), QgsProcessingFeatureSourceDefinition( QgsProperty::fromExpression( QStringLiteral( "trim('%1' + ' ')" ).arg( layer->id() ) ), false ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  // can't directly match it to layer, so instead just get the feature and test that it matches what we expect
  QVERIFY( source.get() );
  QVERIFY( source->getFeatures().nextFeature( f2 ) );
  QCOMPARE( f2.geometry().asWkt(), f.geometry().asWkt() );

  // we also must accept QgsProcessingOutputLayerDefinition - e.g. to allow outputs from earlier child algorithms in models
  params.insert( QStringLiteral( "layer" ), QgsProcessingOutputLayerDefinition( QgsProperty::fromValue( layer->id() ) ) );
  source.reset( QgsProcessingParameters::parameterAsSource( def.get(), params, context ) );
  // can't directly match it to layer, so instead just get the feature and test that it matches what we expect
  QVERIFY( source.get() );
  QVERIFY( source->getFeatures().nextFeature( f2 ) );
  QCOMPARE( f2.geometry().asWkt(), f.geometry().asWkt() );
}

void TestQgsProcessing::processingFeatureSink()
{
  QString sinkString( QStringLiteral( "test.shp" ) );
  QgsProject p;
  QgsProcessingOutputLayerDefinition fs( sinkString, &p );
  QgsRemappingSinkDefinition remap;
  QVERIFY( !fs.useRemapping() );
  remap.setDestinationWkbType( QgsWkbTypes::Point );
  fs.setRemappingDefinition( remap );
  QVERIFY( fs.useRemapping() );

  QCOMPARE( fs.sink.staticValue().toString(), sinkString );
  QCOMPARE( fs.destinationProject, &p );
  QCOMPARE( fs.remappingDefinition().destinationWkbType(), QgsWkbTypes::Point );

  // test storing QgsProcessingFeatureSink in variant and retrieving
  QVariant fsInVariant = QVariant::fromValue( fs );
  QVERIFY( fsInVariant.isValid() );

  QgsProcessingOutputLayerDefinition fromVar = qvariant_cast<QgsProcessingOutputLayerDefinition>( fsInVariant );
  QCOMPARE( fromVar.sink.staticValue().toString(), sinkString );
  QCOMPARE( fromVar.destinationProject, &p );
  QCOMPARE( fromVar.remappingDefinition().destinationWkbType(), QgsWkbTypes::Point );

  // test evaluating parameter as sink
  QgsProcessingContext context;
  context.setProject( &p );

  // first using static string definition
  std::unique_ptr< QgsProcessingParameterFeatureSink > def( new QgsProcessingParameterFeatureSink( QStringLiteral( "layer" ) ) );
  QVariantMap params;
  params.insert( QStringLiteral( "layer" ), QgsProcessingOutputLayerDefinition( "memory:test", nullptr ) );
  QString dest;
  std::unique_ptr< QgsFeatureSink > sink( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:3111" ), context, dest ) );
  QVERIFY( sink.get() );
  QgsVectorLayer *layer = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( dest, context, false ) );
  QVERIFY( layer );
  QCOMPARE( layer->crs().authid(), QStringLiteral( "EPSG:3111" ) );

  // next using property based definition
  params.insert( QStringLiteral( "layer" ), QgsProcessingOutputLayerDefinition( QgsProperty::fromExpression( QStringLiteral( "trim('memory' + ':test2')" ) ), nullptr ) );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:3113" ), context, dest ) );
  QVERIFY( sink.get() );
  QgsVectorLayer *layer2 = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( dest, context, false ) );
  QVERIFY( layer2 );
  QCOMPARE( layer2->crs().authid(), QStringLiteral( "EPSG:3113" ) );

  // temporary sink
  params.insert( QStringLiteral( "layer" ), QgsProcessing::TEMPORARY_OUTPUT );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:28356" ), context, dest ) );
  QVERIFY( sink.get() );
  QgsVectorLayer *layer3 = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( dest, context, false ) );
  QVERIFY( layer3 );
  QCOMPARE( layer3->crs().authid(), QStringLiteral( "EPSG:28356" ) );
  QCOMPARE( layer3->dataProvider()->name(), QStringLiteral( "memory" ) );

  params.insert( QStringLiteral( "layer" ), QgsProcessingOutputLayerDefinition( QgsProperty::fromValue( QgsProcessing::TEMPORARY_OUTPUT ), nullptr ) );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:28354" ), context, dest ) );
  QVERIFY( sink.get() );
  QgsVectorLayer *layer4 = qobject_cast< QgsVectorLayer *>( QgsProcessingUtils::mapLayerFromString( dest, context, false ) );
  QVERIFY( layer4 );
  QCOMPARE( layer4->crs().authid(), QStringLiteral( "EPSG:28354" ) );
  QCOMPARE( layer4->dataProvider()->name(), QStringLiteral( "memory" ) );

  // non optional sink
  def.reset( new QgsProcessingParameterFeatureSink( QStringLiteral( "layer" ), QString(), QgsProcessing::TypeMapLayer, QVariant(), false ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "memory:test" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "memory:test" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromValue( "memory:test" ) ) );
  QVERIFY( !def->checkValueIsAcceptable( QString() ) );
  QVERIFY( !def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  params.insert( QStringLiteral( "layer" ), QStringLiteral( "memory:test" ) );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:3113" ), context, dest ) );
  QVERIFY( sink.get() );

  // optional sink
  def.reset( new QgsProcessingParameterFeatureSink( QStringLiteral( "layer" ), QString(), QgsProcessing::TypeMapLayer, QVariant(), true ) );
  QVERIFY( def->checkValueIsAcceptable( QStringLiteral( "memory:test" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProcessingOutputLayerDefinition( "memory:test" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QgsProperty::fromValue( "memory:test" ) ) );
  QVERIFY( def->checkValueIsAcceptable( QString() ) );
  QVERIFY( def->checkValueIsAcceptable( QVariant() ) );
  QVERIFY( !def->checkValueIsAcceptable( 5 ) );
  params.insert( QStringLiteral( "layer" ), QStringLiteral( "memory:test" ) );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:3113" ), context, dest ) );
  QVERIFY( sink.get() );
  // optional sink, not set - should be no sink
  params.insert( QStringLiteral( "layer" ), QVariant() );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:3113" ), context, dest ) );
  QVERIFY( !sink.get() );

  //.... unless there's a default set
  def.reset( new QgsProcessingParameterFeatureSink( QStringLiteral( "layer" ), QString(), QgsProcessing::TypeMapLayer, QStringLiteral( "memory:defaultlayer" ), true ) );
  params.insert( QStringLiteral( "layer" ), QVariant() );
  sink.reset( QgsProcessingParameters::parameterAsSink( def.get(), params, QgsFields(), QgsWkbTypes::Point, QgsCoordinateReferenceSystem( "EPSG:3113" ), context, dest ) );
  QVERIFY( sink.get() );

  // appendable
  def->setSupportsAppend( true );
  QVERIFY( def->supportsAppend() );
  QString pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSink('layer', '', optional=True, type=QgsProcessing.TypeMapLayer, createByDefault=True, supportsAppend=True, defaultValue='memory:defaultlayer')" ) );

  QVariantMap val = def->toVariantMap();
  QgsProcessingParameterFeatureSink fromMap( "x" );
  QVERIFY( fromMap.fromVariantMap( val ) );
  QVERIFY( fromMap.supportsAppend() );

  def->setSupportsAppend( false );
  QVERIFY( !def->supportsAppend() );
  pythonCode = def->asPythonString();
  QCOMPARE( pythonCode, QStringLiteral( "QgsProcessingParameterFeatureSink('layer', '', optional=True, type=QgsProcessing.TypeMapLayer, createByDefault=True, defaultValue='memory:defaultlayer')" ) );
}

void TestQgsProcessing::algorithmScope()
{
  QgsProcessingContext pc;

  // no alg
  std::unique_ptr< QgsExpressionContextScope > scope( QgsExpressionContextUtils::processingAlgorithmScope( nullptr, QVariantMap(), pc ) );
  QVERIFY( scope.get() );

  // with alg
  std::unique_ptr< QgsProcessingAlgorithm > alg( new DummyAlgorithm( "alg1" ) );
  QVariantMap params;
  params.insert( QStringLiteral( "a_param" ), 5 );
  scope.reset( QgsExpressionContextUtils::processingAlgorithmScope( alg.get(), params, pc ) );
  QVERIFY( scope.get() );
  QCOMPARE( scope->variable( QStringLiteral( "algorithm_id" ) ).toString(), alg->id() );

  QgsExpressionContext context;
  context.appendScope( scope.release() );
  QgsExpression exp( "parameter('bad')" );
  QVERIFY( !exp.evaluate( &context ).isValid() );
  QgsExpression exp2( "parameter('a_param')" );
  QCOMPARE( exp2.evaluate( &context ).toInt(), 5 );
}

void TestQgsProcessing::modelScope()
{
  QgsProcessingContext pc;

  QgsProcessingModelAlgorithm alg( "test", "testGroup" );

  QVariantMap variables;
  variables.insert( QStringLiteral( "v1" ), 5 );
  variables.insert( QStringLiteral( "v2" ), QStringLiteral( "aabbccd" ) );
  alg.setVariables( variables );

  QVariantMap params;
  params.insert( QStringLiteral( "a_param" ), 5 );
  std::unique_ptr< QgsExpressionContextScope > scope( QgsExpressionContextUtils::processingModelAlgorithmScope( &alg, params, pc ) );
  QVERIFY( scope.get() );
  QCOMPARE( scope->variable( QStringLiteral( "model_name" ) ).toString(), QStringLiteral( "test" ) );
  QCOMPARE( scope->variable( QStringLiteral( "model_group" ) ).toString(), QStringLiteral( "testGroup" ) );
  QVERIFY( scope->hasVariable( QStringLiteral( "model_path" ) ) );
  QVERIFY( scope->hasVariable( QStringLiteral( "model_folder" ) ) );
  QCOMPARE( scope->variable( QStringLiteral( "model_path" ) ).toString(), QString() );
  QCOMPARE( scope->variable( QStringLiteral( "model_folder" ) ).toString(), QString() );
  QCOMPARE( scope->variable( QStringLiteral( "v1" ) ).toInt(), 5 );
  QCOMPARE( scope->variable( QStringLiteral( "v2" ) ).toString(), QStringLiteral( "aabbccd" ) );

  QgsProject p;
  pc.setProject( &p );
  p.setFileName( TEST_DATA_DIR + QStringLiteral( "/test_file.qgs" ) );
  scope.reset( QgsExpressionContextUtils::processingModelAlgorithmScope( &alg, params, pc ) );
  QCOMPARE( scope->variable( QStringLiteral( "model_path" ) ).toString(), QString( QStringLiteral( TEST_DATA_DIR ) + QStringLiteral( "/test_file.qgs" ) ) );
  QCOMPARE( scope->variable( QStringLiteral( "model_folder" ) ).toString(), QStringLiteral( TEST_DATA_DIR ) );

  alg.setSourceFilePath( TEST_DATA_DIR + QStringLiteral( "/processing/my_model.model3" ) );
  scope.reset( QgsExpressionContextUtils::processingModelAlgorithmScope( &alg, params, pc ) );
  QCOMPARE( scope->variable( QStringLiteral( "model_path" ) ).toString(), QString( QStringLiteral( TEST_DATA_DIR ) + QStringLiteral( "/processing/my_model.model3" ) ) );
  QCOMPARE( scope->variable( QStringLiteral( "model_folder" ) ).toString(), QString( QStringLiteral( TEST_DATA_DIR ) + QStringLiteral( "/processing" ) ) );

  QgsExpressionContext ctx = alg.createExpressionContext( QVariantMap(), pc );
  QVERIFY( scope->hasVariable( QStringLiteral( "model_path" ) ) );
  QVERIFY( scope->hasVariable( QStringLiteral( "model_folder" ) ) );
}

void TestQgsProcessing::validateInputCrs()
{
  DummyAlgorithm alg( "test" );
  alg.runValidateInputCrsChecks();
}

void TestQgsProcessing::generateIteratingDestination()
{
  QgsProcessingContext context;
  QCOMPARE( QgsProcessingUtils::generateIteratingDestination( "memory:x", 1, context ).toString(), QStringLiteral( "memory:x_1" ) );
  QCOMPARE( QgsProcessingUtils::generateIteratingDestination( "memory:x", 2, context ).toString(), QStringLiteral( "memory:x_2" ) );
  QCOMPARE( QgsProcessingUtils::generateIteratingDestination( "ape.shp", 1, context ).toString(), QStringLiteral( "ape_1.shp" ) );
  QCOMPARE( QgsProcessingUtils::generateIteratingDestination( "ape.shp", 2, context ).toString(), QStringLiteral( "ape_2.shp" ) );
  QCOMPARE( QgsProcessingUtils::generateIteratingDestination( "/home/bif.o/ape.shp", 2, context ).toString(), QStringLiteral( "/home/bif.o/ape_2.shp" ) );
  QCOMPARE( QgsProcessingUtils::generateIteratingDestination( QgsProcessing::TEMPORARY_OUTPUT, 2, context ).toString(), QgsProcessing::TEMPORARY_OUTPUT );
  QCOMPARE( QgsProcessingUtils::generateIteratingDestination( QgsProperty::fromValue( QgsProcessing::TEMPORARY_OUTPUT ), 2, context ).toString(), QgsProcessing::TEMPORARY_OUTPUT );

  QgsProject p;
  QgsProcessingOutputLayerDefinition def;
  def.sink = QgsProperty::fromValue( "ape.shp" );
  def.destinationProject = &p;
  QVariant res = QgsProcessingUtils::generateIteratingDestination( def, 2, context );
  QVERIFY( res.canConvert<QgsProcessingOutputLayerDefinition>() );
  QgsProcessingOutputLayerDefinition fromVar = qvariant_cast<QgsProcessingOutputLayerDefinition>( res );
  QCOMPARE( fromVar.sink.staticValue().toString(), QStringLiteral( "ape_2.shp" ) );
  QCOMPARE( fromVar.destinationProject, &p );

  def.sink = QgsProperty::fromExpression( "'ape' || '.shp'" );
  res = QgsProcessingUtils::generateIteratingDestination( def, 2, context );
  QVERIFY( res.canConvert<QgsProcessingOutputLayerDefinition>() );
  fromVar = qvariant_cast<QgsProcessingOutputLayerDefinition>( res );
  QCOMPARE( fromVar.sink.staticValue().toString(), QStringLiteral( "ape_2.shp" ) );
  QCOMPARE( fromVar.destinationProject, &p );

  QgsProcessingOutputLayerDefinition def2;
  def2.sink = QgsProperty::fromValue( QgsProcessing::TEMPORARY_OUTPUT );
  def2.destinationProject = &p;
  res = QgsProcessingUtils::generateIteratingDestination( def2, 2, context );
  QVERIFY( res.canConvert<QgsProcessingOutputLayerDefinition>() );
  fromVar = qvariant_cast<QgsProcessingOutputLayerDefinition>( res );
  QCOMPARE( fromVar.sink.staticValue().toString(), QgsProcessing::TEMPORARY_OUTPUT );
  QCOMPARE( fromVar.destinationProject, &p );
}

void TestQgsProcessing::asPythonCommand()
{
  DummyAlgorithm alg( "test" );
  alg.runAsPythonCommandChecks();
}

void TestQgsProcessing::modelerAlgorithm()
{
  //static value source
  QgsProcessingModelChildParameterSource svSource = QgsProcessingModelChildParameterSource::fromStaticValue( 5 );
  QCOMPARE( svSource.source(), QgsProcessingModelChildParameterSource::StaticValue );
  QCOMPARE( svSource.staticValue().toInt(), 5 );
  svSource.setStaticValue( 7 );
  QCOMPARE( svSource.staticValue().toInt(), 7 );
  QMap< QString, QString > friendlyNames;
  QCOMPARE( svSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "7" ) );
  svSource = QgsProcessingModelChildParameterSource::fromModelParameter( "a" );
  // check that calling setStaticValue flips source to StaticValue
  QCOMPARE( svSource.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( svSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "parameters['a']" ) );
  svSource.setStaticValue( 7 );
  QCOMPARE( svSource.staticValue().toInt(), 7 );
  QCOMPARE( svSource.source(), QgsProcessingModelChildParameterSource::StaticValue );
  QCOMPARE( svSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "7" ) );

  // model parameter source
  QgsProcessingModelChildParameterSource mpSource = QgsProcessingModelChildParameterSource::fromModelParameter( "a" );
  QCOMPARE( mpSource.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( mpSource.parameterName(), QStringLiteral( "a" ) );
  QCOMPARE( mpSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "parameters['a']" ) );
  mpSource.setParameterName( "b" );
  QCOMPARE( mpSource.parameterName(), QStringLiteral( "b" ) );
  QCOMPARE( mpSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "parameters['b']" ) );
  mpSource = QgsProcessingModelChildParameterSource::fromStaticValue( 5 );
  // check that calling setParameterName flips source to ModelParameter
  QCOMPARE( mpSource.source(), QgsProcessingModelChildParameterSource::StaticValue );
  QCOMPARE( mpSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "5" ) );
  mpSource.setParameterName( "c" );
  QCOMPARE( mpSource.parameterName(), QStringLiteral( "c" ) );
  QCOMPARE( mpSource.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( mpSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "parameters['c']" ) );

  // child alg output source
  QgsProcessingModelChildParameterSource oSource = QgsProcessingModelChildParameterSource::fromChildOutput( "a", "b" );
  QCOMPARE( oSource.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( oSource.outputChildId(), QStringLiteral( "a" ) );
  QCOMPARE( oSource.outputName(), QStringLiteral( "b" ) );
  QCOMPARE( oSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "outputs['a']['b']" ) );
  // with friendly name
  friendlyNames.insert( QStringLiteral( "a" ), QStringLiteral( "alga" ) );
  QCOMPARE( oSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "outputs['alga']['b']" ) );
  oSource.setOutputChildId( "c" );
  QCOMPARE( oSource.outputChildId(), QStringLiteral( "c" ) );
  QCOMPARE( oSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "outputs['c']['b']" ) );
  oSource.setOutputName( "d" );
  QCOMPARE( oSource.outputName(), QStringLiteral( "d" ) );
  QCOMPARE( oSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "outputs['c']['d']" ) );
  oSource = QgsProcessingModelChildParameterSource::fromStaticValue( 5 );
  // check that calling setOutputChildId flips source to ChildOutput
  QCOMPARE( oSource.source(), QgsProcessingModelChildParameterSource::StaticValue );
  QCOMPARE( oSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "5" ) );
  oSource.setOutputChildId( "c" );
  QCOMPARE( oSource.outputChildId(), QStringLiteral( "c" ) );
  QCOMPARE( oSource.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  oSource = QgsProcessingModelChildParameterSource::fromStaticValue( 5 );
  // check that calling setOutputName flips source to ChildOutput
  QCOMPARE( oSource.source(), QgsProcessingModelChildParameterSource::StaticValue );
  QCOMPARE( oSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "5" ) );
  oSource.setOutputName( "d" );
  QCOMPARE( oSource.outputName(), QStringLiteral( "d" ) );
  QCOMPARE( oSource.source(), QgsProcessingModelChildParameterSource::ChildOutput );

  // expression source
  QgsProcessingModelChildParameterSource expSource = QgsProcessingModelChildParameterSource::fromExpression( "1+2" );
  QCOMPARE( expSource.source(), QgsProcessingModelChildParameterSource::Expression );
  QCOMPARE( expSource.expression(), QStringLiteral( "1+2" ) );
  QCOMPARE( expSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "QgsExpression('1+2').evaluate()" ) );
  expSource.setExpression( "1+3" );
  QCOMPARE( expSource.expression(), QStringLiteral( "1+3" ) );
  QCOMPARE( expSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "QgsExpression('1+3').evaluate()" ) );
  expSource.setExpression( "'a' || 'b\\'c'" );
  QCOMPARE( expSource.expression(), QStringLiteral( "'a' || 'b\\'c'" ) );
  QCOMPARE( expSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "QgsExpression('\\'a\\' || \\'b\\\\\\'c\\'').evaluate()" ) );
  expSource = QgsProcessingModelChildParameterSource::fromStaticValue( 5 );
  // check that calling setExpression flips source to Expression
  QCOMPARE( expSource.source(), QgsProcessingModelChildParameterSource::StaticValue );
  QCOMPARE( expSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "5" ) );
  expSource.setExpression( "1+4" );
  QCOMPARE( expSource.expression(), QStringLiteral( "1+4" ) );
  QCOMPARE( expSource.source(), QgsProcessingModelChildParameterSource::Expression );
  QCOMPARE( expSource.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, nullptr, friendlyNames ), QStringLiteral( "QgsExpression('1+4').evaluate()" ) );

  // source equality operator
  QVERIFY( QgsProcessingModelChildParameterSource::fromStaticValue( 5 ) ==
           QgsProcessingModelChildParameterSource::fromStaticValue( 5 ) );
  QVERIFY( QgsProcessingModelChildParameterSource::fromStaticValue( 5 ) !=
           QgsProcessingModelChildParameterSource::fromStaticValue( 7 ) );
  QVERIFY( QgsProcessingModelChildParameterSource::fromStaticValue( 5 ) !=
           QgsProcessingModelChildParameterSource::fromModelParameter( QStringLiteral( "a" ) ) );
  QVERIFY( QgsProcessingModelChildParameterSource::fromModelParameter( QStringLiteral( "a" ) ) ==
           QgsProcessingModelChildParameterSource::fromModelParameter( QStringLiteral( "a" ) ) );
  QVERIFY( QgsProcessingModelChildParameterSource::fromModelParameter( QStringLiteral( "a" ) ) !=
           QgsProcessingModelChildParameterSource::fromModelParameter( QStringLiteral( "b" ) ) );
  QVERIFY( QgsProcessingModelChildParameterSource::fromModelParameter( QStringLiteral( "a" ) ) !=
           QgsProcessingModelChildParameterSource::fromChildOutput( QStringLiteral( "alg" ), QStringLiteral( "out" ) ) );
  QVERIFY( QgsProcessingModelChildParameterSource::fromChildOutput( QStringLiteral( "alg" ), QStringLiteral( "out" ) ) ==
           QgsProcessingModelChildParameterSource::fromChildOutput( QStringLiteral( "alg" ), QStringLiteral( "out" ) ) );
  QVERIFY( QgsProcessingModelChildParameterSource::fromChildOutput( QStringLiteral( "alg" ), QStringLiteral( "out" ) ) !=
           QgsProcessingModelChildParameterSource::fromChildOutput( QStringLiteral( "alg2" ), QStringLiteral( "out" ) ) );
  QVERIFY( QgsProcessingModelChildParameterSource::fromChildOutput( QStringLiteral( "alg" ), QStringLiteral( "out" ) ) !=
           QgsProcessingModelChildParameterSource::fromChildOutput( QStringLiteral( "alg" ), QStringLiteral( "out2" ) ) );
  QVERIFY( QgsProcessingModelChildParameterSource::fromExpression( QStringLiteral( "a" ) ) ==
           QgsProcessingModelChildParameterSource::fromExpression( QStringLiteral( "a" ) ) );
  QVERIFY( QgsProcessingModelChildParameterSource::fromExpression( QStringLiteral( "a" ) ) !=
           QgsProcessingModelChildParameterSource::fromExpression( QStringLiteral( "b" ) ) );
  QVERIFY( QgsProcessingModelChildParameterSource::fromExpression( QStringLiteral( "a" ) ) !=
           QgsProcessingModelChildParameterSource::fromStaticValue( QStringLiteral( "b" ) ) );

  // a comment
  QgsProcessingModelComment comment;
  comment.setSize( QSizeF( 9, 8 ) );
  QCOMPARE( comment.size(), QSizeF( 9, 8 ) );
  comment.setPosition( QPointF( 11, 14 ) );
  QCOMPARE( comment.position(), QPointF( 11, 14 ) );
  comment.setDescription( QStringLiteral( "a comment" ) );
  QCOMPARE( comment.description(), QStringLiteral( "a comment" ) );
  comment.setColor( QColor( 123, 45, 67 ) );
  QCOMPARE( comment.color(), QColor( 123, 45, 67 ) );
  std::unique_ptr< QgsProcessingModelComment > commentClone( comment.clone() );
  QCOMPARE( commentClone->toVariant(), comment.toVariant() );
  QCOMPARE( commentClone->size(), QSizeF( 9, 8 ) );
  QCOMPARE( commentClone->position(), QPointF( 11, 14 ) );
  QCOMPARE( commentClone->description(), QStringLiteral( "a comment" ) );
  QCOMPARE( commentClone->color(), QColor( 123, 45, 67 ) );
  QgsProcessingModelComment comment2;
  comment2.loadVariant( comment.toVariant().toMap() );
  QCOMPARE( comment2.size(), QSizeF( 9, 8 ) );
  QCOMPARE( comment2.position(), QPointF( 11, 14 ) );
  QCOMPARE( comment2.description(), QStringLiteral( "a comment" ) );
  QCOMPARE( comment2.color(), QColor( 123, 45, 67 ) );

  // group boxes
  QgsProcessingModelGroupBox groupBox;
  groupBox.setSize( QSizeF( 9, 8 ) );
  QCOMPARE( groupBox.size(), QSizeF( 9, 8 ) );
  groupBox.setPosition( QPointF( 11, 14 ) );
  QCOMPARE( groupBox.position(), QPointF( 11, 14 ) );
  groupBox.setDescription( QStringLiteral( "a comment" ) );
  QCOMPARE( groupBox.description(), QStringLiteral( "a comment" ) );
  groupBox.setColor( QColor( 123, 45, 67 ) );
  QCOMPARE( groupBox.color(), QColor( 123, 45, 67 ) );
  std::unique_ptr< QgsProcessingModelGroupBox > groupClone( groupBox.clone() );
  QCOMPARE( groupClone->toVariant(), groupBox.toVariant() );
  QCOMPARE( groupClone->size(), QSizeF( 9, 8 ) );
  QCOMPARE( groupClone->position(), QPointF( 11, 14 ) );
  QCOMPARE( groupClone->description(), QStringLiteral( "a comment" ) );
  QCOMPARE( groupClone->color(), QColor( 123, 45, 67 ) );
  QCOMPARE( groupClone->uuid(), groupBox.uuid() );
  QgsProcessingModelGroupBox groupBox2;
  groupBox2.loadVariant( groupBox.toVariant().toMap() );
  QCOMPARE( groupBox2.size(), QSizeF( 9, 8 ) );
  QCOMPARE( groupBox2.position(), QPointF( 11, 14 ) );
  QCOMPARE( groupBox2.description(), QStringLiteral( "a comment" ) );
  QCOMPARE( groupBox2.color(), QColor( 123, 45, 67 ) );
  QCOMPARE( groupBox2.uuid(), groupBox.uuid() );

  QMap< QString, QString > friendlyOutputNames;
  QgsProcessingModelChildAlgorithm child( QStringLiteral( "some_id" ) );
  QCOMPARE( child.algorithmId(), QStringLiteral( "some_id" ) );
  QVERIFY( !child.algorithm() );
  QVERIFY( !child.setAlgorithmId( QStringLiteral( "blah" ) ) );
  QVERIFY( !child.reattach() );
  QVERIFY( child.setAlgorithmId( QStringLiteral( "native:centroids" ) ) );
  QVERIFY( child.algorithm() );
  QCOMPARE( child.algorithm()->id(), QStringLiteral( "native:centroids" ) );
  QCOMPARE( child.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, QgsStringMap(), 4, 2, friendlyNames, friendlyOutputNames ).join( '\n' ), QStringLiteral( "    alg_params = {\n    }\n    outputs[''] = processing.run('native:centroids', alg_params, context=context, feedback=feedback, is_child_algorithm=True)" ) );
  QgsStringMap extraParams;
  extraParams[QStringLiteral( "SOMETHING" )] = QStringLiteral( "SOMETHING_ELSE" );
  extraParams[QStringLiteral( "SOMETHING2" )] = QStringLiteral( "SOMETHING_ELSE2" );
  QCOMPARE( child.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, extraParams, 4, 2, friendlyNames, friendlyOutputNames ).join( '\n' ), QStringLiteral( "    alg_params = {\n      'SOMETHING': SOMETHING_ELSE,\n      'SOMETHING2': SOMETHING_ELSE2\n    }\n    outputs[''] = processing.run('native:centroids', alg_params, context=context, feedback=feedback, is_child_algorithm=True)" ) );
  // bit of a hack -- but try to simulate an algorithm not originally available!
  child.mAlgorithm.reset();
  QVERIFY( !child.algorithm() );
  QVERIFY( child.reattach() );
  QVERIFY( child.algorithm() );
  QCOMPARE( child.algorithm()->id(), QStringLiteral( "native:centroids" ) );

  QVariantMap myConfig;
  myConfig.insert( QStringLiteral( "some_key" ), 11 );
  child.setConfiguration( myConfig );
  QCOMPARE( child.configuration(), myConfig );

  child.setDescription( QStringLiteral( "desc" ) );
  QCOMPARE( child.description(), QStringLiteral( "desc" ) );
  QVERIFY( child.isActive() );
  child.setActive( false );
  QVERIFY( !child.isActive() );
  child.setPosition( QPointF( 1, 2 ) );
  QCOMPARE( child.position(), QPointF( 1, 2 ) );
  child.setSize( QSizeF( 3, 4 ) );
  QCOMPARE( child.size(), QSizeF( 3, 4 ) );
  QVERIFY( child.linksCollapsed( Qt::TopEdge ) );
  child.setLinksCollapsed( Qt::TopEdge, false );
  QVERIFY( !child.linksCollapsed( Qt::TopEdge ) );
  QVERIFY( child.linksCollapsed( Qt::BottomEdge ) );
  child.setLinksCollapsed( Qt::BottomEdge, false );
  QVERIFY( !child.linksCollapsed( Qt::BottomEdge ) );
  child.comment()->setDescription( QStringLiteral( "com" ) );
  QCOMPARE( child.comment()->description(), QStringLiteral( "com" ) );
  child.comment()->setSize( QSizeF( 56, 78 ) );
  child.comment()->setPosition( QPointF( 111, 122 ) );

  QgsProcessingModelChildAlgorithm other;
  other.setChildId( QStringLiteral( "diff" ) );
  other.setDescription( QStringLiteral( "d2" ) );
  other.setAlgorithmId( QStringLiteral( "alg33" ) );
  other.setLinksCollapsed( Qt::BottomEdge, true );
  other.setLinksCollapsed( Qt::TopEdge, true );
  other.comment()->setDescription( QStringLiteral( "other comment" ) );
  other.copyNonDefinitionProperties( child );
  // only subset of properties should have been copied!
  QCOMPARE( other.description(), QStringLiteral( "d2" ) );
  QCOMPARE( other.position(), QPointF( 1, 2 ) );
  QCOMPARE( other.size(), QSizeF( 3, 4 ) );
  QVERIFY( !other.linksCollapsed( Qt::TopEdge ) );
  QVERIFY( !other.linksCollapsed( Qt::BottomEdge ) );
  QCOMPARE( other.comment()->description(), QStringLiteral( "other comment" ) );
  QCOMPARE( other.comment()->position(), QPointF( 111, 122 ) );
  QCOMPARE( other.comment()->size(), QSizeF( 56, 78 ) );

  child.comment()->setDescription( QString() );

  child.setChildId( QStringLiteral( "my_id" ) );
  QCOMPARE( child.childId(), QStringLiteral( "my_id" ) );

  child.setDependencies( QList< QgsProcessingModelChildDependency >() << QgsProcessingModelChildDependency( "a" ) << QgsProcessingModelChildDependency( "b" ) );
  QCOMPARE( child.dependencies(), QList< QgsProcessingModelChildDependency >() << QgsProcessingModelChildDependency( "a" ) << QgsProcessingModelChildDependency( "b" ) );

  QMap< QString, QgsProcessingModelChildParameterSources > sources;
  sources.insert( QStringLiteral( "a" ), QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromStaticValue( 5 ) );
  child.setParameterSources( sources );
  QCOMPARE( child.parameterSources().value( QStringLiteral( "a" ) ).at( 0 ).staticValue().toInt(), 5 );
  child.addParameterSources( QStringLiteral( "b" ), QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromStaticValue( 7 ) << QgsProcessingModelChildParameterSource::fromStaticValue( 9 ) );
  QCOMPARE( child.parameterSources().value( QStringLiteral( "a" ) ).at( 0 ).staticValue().toInt(), 5 );
  QCOMPARE( child.parameterSources().value( QStringLiteral( "b" ) ).count(), 2 );
  QCOMPARE( child.parameterSources().value( QStringLiteral( "b" ) ).at( 0 ).staticValue().toInt(), 7 );
  QCOMPARE( child.parameterSources().value( QStringLiteral( "b" ) ).at( 1 ).staticValue().toInt(), 9 );

  QCOMPARE( child.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, extraParams, 4, 2, friendlyNames, friendlyOutputNames ).join( '\n' ), QStringLiteral( "    # desc\n    alg_params = {\n      'a': 5,\n      'b': [7,9],\n      'SOMETHING': SOMETHING_ELSE,\n      'SOMETHING2': SOMETHING_ELSE2\n    }\n    outputs['my_id'] = processing.run('native:centroids', alg_params, context=context, feedback=feedback, is_child_algorithm=True)" ) );
  child.comment()->setDescription( QStringLiteral( "do something useful" ) );
  QCOMPARE( child.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, extraParams, 4, 2, friendlyNames, friendlyOutputNames ).join( '\n' ), QStringLiteral( "    # desc\n    # do something useful\n    alg_params = {\n      'a': 5,\n      'b': [7,9],\n      'SOMETHING': SOMETHING_ELSE,\n      'SOMETHING2': SOMETHING_ELSE2\n    }\n    outputs['my_id'] = processing.run('native:centroids', alg_params, context=context, feedback=feedback, is_child_algorithm=True)" ) );

  std::unique_ptr< QgsProcessingModelChildAlgorithm > childClone( child.clone() );
  QCOMPARE( childClone->toVariant(), child.toVariant() );
  QCOMPARE( childClone->comment()->description(), QStringLiteral( "do something useful" ) );

  QgsProcessingModelOutput testModelOut;
  testModelOut.setChildId( QStringLiteral( "my_id" ) );
  QCOMPARE( testModelOut.childId(), QStringLiteral( "my_id" ) );
  testModelOut.setChildOutputName( QStringLiteral( "my_output" ) );
  QCOMPARE( testModelOut.childOutputName(), QStringLiteral( "my_output" ) );
  testModelOut.setDefaultValue( QStringLiteral( "my_value" ) );
  QCOMPARE( testModelOut.defaultValue().toString(), QStringLiteral( "my_value" ) );
  testModelOut.setMandatory( true );
  QVERIFY( testModelOut.isMandatory() );
  testModelOut.comment()->setDescription( QStringLiteral( "my comm" ) );
  QCOMPARE( testModelOut.comment()->description(), QStringLiteral( "my comm" ) );
  std::unique_ptr< QgsProcessingModelOutput > outputClone( testModelOut.clone() );
  QCOMPARE( outputClone->toVariant(), testModelOut.toVariant() );
  QCOMPARE( outputClone->comment()->description(), QStringLiteral( "my comm" ) );
  QgsProcessingModelOutput testModelOutV;
  testModelOutV.loadVariant( testModelOut.toVariant().toMap() );
  QCOMPARE( testModelOutV.comment()->description(), QStringLiteral( "my comm" ) );

  QgsProcessingOutputLayerDefinition layerDef( QStringLiteral( "my_path" ) );
  layerDef.createOptions["fileEncoding"] = QStringLiteral( "my_encoding" );
  testModelOut.setDefaultValue( layerDef );
  QCOMPARE( testModelOut.defaultValue().value<QgsProcessingOutputLayerDefinition>().sink.staticValue().toString(), QStringLiteral( "my_path" ) );
  QVariantMap map = testModelOut.toVariant().toMap();
  QCOMPARE( map["default_value"].toMap()["sink"].toMap()["val"].toString(), QStringLiteral( "my_path" ) );
  QCOMPARE( map["default_value"].toMap()["create_options"].toMap()["fileEncoding"].toString(), QStringLiteral( "my_encoding" ) );
  QgsProcessingModelOutput out;
  out.loadVariant( map );
  QVERIFY( out.defaultValue().canConvert<QgsProcessingOutputLayerDefinition>() );
  layerDef = out.defaultValue().value<QgsProcessingOutputLayerDefinition>();
  QCOMPARE( layerDef.sink.staticValue().toString(), QStringLiteral( "my_path" ) );
  QCOMPARE( layerDef.createOptions["fileEncoding"].toString(), QStringLiteral( "my_encoding" ) );

  QMap<QString, QgsProcessingModelOutput> outputs;
  QgsProcessingModelOutput out1;
  out1.setDescription( QStringLiteral( "my output" ) );
  outputs.insert( QStringLiteral( "a" ), out1 );
  child.setModelOutputs( outputs );
  QCOMPARE( child.modelOutputs().count(), 1 );
  QCOMPARE( child.modelOutputs().value( QStringLiteral( "a" ) ).description(), QStringLiteral( "my output" ) );
  QCOMPARE( child.modelOutput( "a" ).description(), QStringLiteral( "my output" ) );
  child.modelOutput( "a" ).setDescription( QStringLiteral( "my output 2" ) );
  QCOMPARE( child.modelOutput( "a" ).description(), QStringLiteral( "my output 2" ) );
  qDebug() << child.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, extraParams, 4, 2, friendlyNames, friendlyOutputNames ).join( '\n' );
  QCOMPARE( child.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, extraParams, 4, 2, friendlyNames, friendlyOutputNames ).join( '\n' ), QStringLiteral( "    # desc\n    # do something useful\n    alg_params = {\n      'a': 5,\n      'b': [7,9],\n      'SOMETHING': SOMETHING_ELSE,\n      'SOMETHING2': SOMETHING_ELSE2\n    }\n    outputs['my_id'] = processing.run('native:centroids', alg_params, context=context, feedback=feedback, is_child_algorithm=True)\n    results['my_id:a'] = outputs['my_id']['']" ) );

  // ensure friendly name is used if present
  child.addParameterSources( QStringLiteral( "b" ), QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromChildOutput( "a", "out" ) );
  QCOMPARE( child.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, extraParams, 4, 2, friendlyNames, friendlyOutputNames ).join( '\n' ), QStringLiteral( "    # desc\n    # do something useful\n    alg_params = {\n      'a': 5,\n      'b': outputs['alga']['out'],\n      'SOMETHING': SOMETHING_ELSE,\n      'SOMETHING2': SOMETHING_ELSE2\n    }\n    outputs['my_id'] = processing.run('native:centroids', alg_params, context=context, feedback=feedback, is_child_algorithm=True)\n    results['my_id:a'] = outputs['my_id']['']" ) );
  friendlyNames.remove( "a" );
  QCOMPARE( child.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, extraParams, 4, 2, friendlyNames, friendlyOutputNames ).join( '\n' ), QStringLiteral( "    # desc\n    # do something useful\n    alg_params = {\n      'a': 5,\n      'b': outputs['a']['out'],\n      'SOMETHING': SOMETHING_ELSE,\n      'SOMETHING2': SOMETHING_ELSE2\n    }\n    outputs['my_id'] = processing.run('native:centroids', alg_params, context=context, feedback=feedback, is_child_algorithm=True)\n    results['my_id:a'] = outputs['my_id']['']" ) );

  // no existent
  child.modelOutput( "b" ).setDescription( QStringLiteral( "my output 3" ) );
  QCOMPARE( child.modelOutput( "b" ).description(), QStringLiteral( "my output 3" ) );
  QCOMPARE( child.modelOutputs().count(), 2 );
  child.removeModelOutput( QStringLiteral( "a" ) );
  QCOMPARE( child.modelOutputs().count(), 1 );

  // model algorithm tests

  QgsProcessingModelAlgorithm alg( "test", "testGroup" );
  QCOMPARE( alg.name(), QStringLiteral( "test" ) );
  QCOMPARE( alg.displayName(), QStringLiteral( "test" ) );
  QCOMPARE( alg.group(), QStringLiteral( "testGroup" ) );
  alg.setName( QStringLiteral( "test2" ) );
  QCOMPARE( alg.name(), QStringLiteral( "test2" ) );
  QCOMPARE( alg.displayName(), QStringLiteral( "test2" ) );
  alg.setGroup( QStringLiteral( "group2" ) );
  QCOMPARE( alg.group(), QStringLiteral( "group2" ) );

  QVariantMap help;
  alg.setHelpContent( help );
  QVERIFY( alg.helpContent().isEmpty() );
  QVERIFY( alg.helpUrl().isEmpty() );
  QVERIFY( alg.shortDescription().isEmpty() );
  help.insert( QStringLiteral( "SHORT_DESCRIPTION" ), QStringLiteral( "short" ) );
  help.insert( QStringLiteral( "HELP_URL" ), QStringLiteral( "url" ) );
  alg.setHelpContent( help );
  QCOMPARE( alg.helpContent(), help );
  QCOMPARE( alg.shortDescription(), QStringLiteral( "short" ) );
  QCOMPARE( alg.helpUrl(), QStringLiteral( "url" ) );

  QVERIFY( alg.groupBoxes().isEmpty() );
  alg.addGroupBox( groupBox );
  QCOMPARE( alg.groupBoxes().size(), 1 );
  QCOMPARE( alg.groupBoxes().at( 0 ).uuid(), groupBox.uuid() );
  QCOMPARE( alg.groupBoxes().at( 0 ).uuid(), groupBox.uuid() );
  alg.removeGroupBox( QStringLiteral( "a" ) );
  QCOMPARE( alg.groupBoxes().size(), 1 );
  alg.removeGroupBox( groupBox.uuid() );
  QVERIFY( alg.groupBoxes().isEmpty() );


  QVariantMap lastParams;
  lastParams.insert( QStringLiteral( "a" ), 2 );
  lastParams.insert( QStringLiteral( "b" ), 4 );
  alg.setDesignerParameterValues( lastParams );

  // we expect the result to add in some custom parameters -- namely the verbose log switch
  lastParams.insert( QStringLiteral( "VERBOSE_LOG" ), true );
  QCOMPARE( alg.designerParameterValues(), lastParams );

  // child algorithms
  QMap<QString, QgsProcessingModelChildAlgorithm> algs;
  QgsProcessingModelChildAlgorithm a1;
  a1.setDescription( QStringLiteral( "alg1" ) );
  QgsProcessingModelChildAlgorithm a2;
  a2.setDescription( QStringLiteral( "alg2" ) );
  a2.setPosition( QPointF( 112, 131 ) );
  a2.setSize( QSizeF( 44, 55 ) );
  a2.comment()->setSize( QSizeF( 111, 222 ) );
  a2.comment()->setPosition( QPointF( 113, 114 ) );
  a2.comment()->setDescription( QStringLiteral( "c" ) );
  a2.comment()->setColor( QColor( 255, 254, 253 ) );
  QgsProcessingModelOutput oo;
  oo.setPosition( QPointF( 312, 331 ) );
  oo.setSize( QSizeF( 344, 355 ) );
  oo.comment()->setSize( QSizeF( 311, 322 ) );
  oo.comment()->setPosition( QPointF( 313, 314 ) );
  oo.comment()->setDescription( QStringLiteral( "c3" ) );
  oo.comment()->setColor( QColor( 155, 14, 353 ) );
  QMap< QString, QgsProcessingModelOutput > a2Outs;
  a2Outs.insert( QStringLiteral( "out1" ), oo );
  a2.setModelOutputs( a2Outs );

  algs.insert( QStringLiteral( "a" ), a1 );
  algs.insert( QStringLiteral( "b" ), a2 );
  alg.setChildAlgorithms( algs );
  QCOMPARE( alg.childAlgorithms().count(), 2 );
  QCOMPARE( alg.childAlgorithms().value( QStringLiteral( "a" ) ).description(), QStringLiteral( "alg1" ) );
  QCOMPARE( alg.childAlgorithms().value( QStringLiteral( "b" ) ).description(), QStringLiteral( "alg2" ) );

  QgsProcessingModelChildAlgorithm a2other;
  a2other.setChildId( QStringLiteral( "b" ) );
  a2other.setDescription( QStringLiteral( "alg2 other" ) );
  QgsProcessingModelOutput oo2;
  QMap< QString, QgsProcessingModelOutput > a2Outs2;
  a2Outs2.insert( QStringLiteral( "out1" ), oo2 );
  a2other.setModelOutputs( a2Outs2 );

  a2other.copyNonDefinitionPropertiesFromModel( &alg );
  QCOMPARE( a2other.description(), QStringLiteral( "alg2 other" ) );
  QCOMPARE( a2other.position(), QPointF( 112, 131 ) );
  QCOMPARE( a2other.size(), QSizeF( 44, 55 ) );
  QCOMPARE( a2other.comment()->size(), QSizeF( 111, 222 ) );
  QCOMPARE( a2other.comment()->position(), QPointF( 113, 114 ) );
  // should not be copied
  QCOMPARE( a2other.comment()->description(), QString() );
  QVERIFY( !a2other.comment()->color().isValid() );

  QCOMPARE( a2other.modelOutput( QStringLiteral( "out1" ) ).position(), QPointF( 312, 331 ) );
  QCOMPARE( a2other.modelOutput( QStringLiteral( "out1" ) ).size(), QSizeF( 344, 355 ) );
  QCOMPARE( a2other.modelOutput( QStringLiteral( "out1" ) ).comment()->size(), QSizeF( 311, 322 ) );
  QCOMPARE( a2other.modelOutput( QStringLiteral( "out1" ) ).comment()->position(), QPointF( 313, 314 ) );
  // should be copied for outputs
  QCOMPARE( a2other.modelOutput( QStringLiteral( "out1" ) ).comment()->description(), QStringLiteral( "c3" ) );
  QCOMPARE( a2other.modelOutput( QStringLiteral( "out1" ) ).comment()->color(), QColor( 155, 14, 353 ) );

  QgsProcessingModelChildAlgorithm a3;
  a3.setChildId( QStringLiteral( "c" ) );
  a3.setDescription( QStringLiteral( "alg3" ) );
  QCOMPARE( alg.addChildAlgorithm( a3 ), QStringLiteral( "c" ) );
  QCOMPARE( alg.childAlgorithms().count(), 3 );
  QCOMPARE( alg.childAlgorithms().value( QStringLiteral( "a" ) ).description(), QStringLiteral( "alg1" ) );
  QCOMPARE( alg.childAlgorithms().value( QStringLiteral( "b" ) ).description(), QStringLiteral( "alg2" ) );
  QCOMPARE( alg.childAlgorithms().value( QStringLiteral( "c" ) ).description(), QStringLiteral( "alg3" ) );
  QCOMPARE( alg.childAlgorithm( "a" ).description(), QStringLiteral( "alg1" ) );
  QCOMPARE( alg.childAlgorithm( "b" ).description(), QStringLiteral( "alg2" ) );
  QCOMPARE( alg.childAlgorithm( "c" ).description(), QStringLiteral( "alg3" ) );
  // initially non-existent
  QVERIFY( alg.childAlgorithm( "d" ).description().isEmpty() );
  alg.childAlgorithm( "d" ).setDescription( QStringLiteral( "alg4" ) );
  QCOMPARE( alg.childAlgorithm( "d" ).description(), QStringLiteral( "alg4" ) );
  // overwrite existing
  QgsProcessingModelChildAlgorithm a4a;
  a4a.setChildId( "d" );
  a4a.setDescription( "new" );
  alg.setChildAlgorithm( a4a );
  QCOMPARE( alg.childAlgorithm( "d" ).description(), QStringLiteral( "new" ) );


  // generating child ids
  QgsProcessingModelChildAlgorithm c1;
  c1.setAlgorithmId( QStringLiteral( "buffer" ) );
  c1.generateChildId( alg );
  QCOMPARE( c1.childId(), QStringLiteral( "buffer_1" ) );
  QCOMPARE( alg.addChildAlgorithm( c1 ), QStringLiteral( "buffer_1" ) );
  QgsProcessingModelChildAlgorithm c2;
  c2.setAlgorithmId( QStringLiteral( "buffer" ) );
  c2.generateChildId( alg );
  QCOMPARE( c2.childId(), QStringLiteral( "buffer_2" ) );
  QCOMPARE( alg.addChildAlgorithm( c2 ), QStringLiteral( "buffer_2" ) );
  QgsProcessingModelChildAlgorithm c3;
  c3.setAlgorithmId( QStringLiteral( "centroid" ) );
  c3.generateChildId( alg );
  QCOMPARE( c3.childId(), QStringLiteral( "centroid_1" ) );
  QCOMPARE( alg.addChildAlgorithm( c3 ), QStringLiteral( "centroid_1" ) );
  QgsProcessingModelChildAlgorithm c4;
  c4.setAlgorithmId( QStringLiteral( "centroid" ) );
  c4.setChildId( QStringLiteral( "centroid_1" ) );// dupe id
  QCOMPARE( alg.addChildAlgorithm( c4 ), QStringLiteral( "centroid_2" ) );
  QCOMPARE( alg.childAlgorithm( QStringLiteral( "centroid_2" ) ).childId(), QStringLiteral( "centroid_2" ) );

  // parameter components
  QMap<QString, QgsProcessingModelParameter> pComponents;
  QgsProcessingModelParameter pc1;
  pc1.setParameterName( QStringLiteral( "my_param" ) );
  QCOMPARE( pc1.parameterName(), QStringLiteral( "my_param" ) );
  pc1.comment()->setDescription( QStringLiteral( "my comment" ) );
  QCOMPARE( pc1.comment()->description(), QStringLiteral( "my comment" ) );
  std::unique_ptr< QgsProcessingModelParameter > paramClone( pc1.clone() );
  QCOMPARE( paramClone->toVariant(), pc1.toVariant() );
  QCOMPARE( paramClone->comment()->description(), QStringLiteral( "my comment" ) );
  QgsProcessingModelParameter pcc1;
  pcc1.loadVariant( pc1.toVariant().toMap() );
  QCOMPARE( pcc1.comment()->description(), QStringLiteral( "my comment" ) );
  pComponents.insert( QStringLiteral( "my_param" ), pc1 );
  alg.setParameterComponents( pComponents );
  QCOMPARE( alg.parameterComponents().count(), 1 );
  QCOMPARE( alg.parameterComponents().value( QStringLiteral( "my_param" ) ).parameterName(), QStringLiteral( "my_param" ) );
  QCOMPARE( alg.parameterComponent( "my_param" ).parameterName(), QStringLiteral( "my_param" ) );
  alg.parameterComponent( "my_param" ).setDescription( QStringLiteral( "my param 2" ) );
  QCOMPARE( alg.parameterComponent( "my_param" ).description(), QStringLiteral( "my param 2" ) );
  // no existent
  alg.parameterComponent( "b" ).setDescription( QStringLiteral( "my param 3" ) );
  QCOMPARE( alg.parameterComponent( "b" ).description(), QStringLiteral( "my param 3" ) );
  QCOMPARE( alg.parameterComponent( "b" ).parameterName(), QStringLiteral( "b" ) );
  QCOMPARE( alg.parameterComponents().count(), 2 );

  // parameter definitions
  QgsProcessingModelAlgorithm alg1a( "test", "testGroup" );
  QgsProcessingModelParameter bool1;
  bool1.setPosition( QPointF( 1, 2 ) );
  bool1.setSize( QSizeF( 11, 12 ) );
  alg1a.addModelParameter( new QgsProcessingParameterBoolean( "p1", "desc" ), bool1 );
  QCOMPARE( alg1a.parameterDefinitions().count(), 1 );
  QCOMPARE( alg1a.parameterDefinition( "p1" )->type(), QStringLiteral( "boolean" ) );
  QCOMPARE( alg1a.parameterComponent( "p1" ).position().x(), 1.0 );
  QCOMPARE( alg1a.parameterComponent( "p1" ).position().y(), 2.0 );
  QCOMPARE( alg1a.parameterComponent( "p1" ).size().width(), 11.0 );
  QCOMPARE( alg1a.parameterComponent( "p1" ).size().height(), 12.0 );
  alg1a.updateModelParameter( new QgsProcessingParameterBoolean( "p1", "descx" ) );
  QCOMPARE( alg1a.parameterDefinition( "p1" )->description(), QStringLiteral( "descx" ) );
  alg1a.removeModelParameter( "bad" );
  QCOMPARE( alg1a.parameterDefinitions().count(), 1 );
  alg1a.removeModelParameter( "p1" );
  QVERIFY( alg1a.parameterDefinitions().isEmpty() );
  QVERIFY( alg1a.parameterComponents().isEmpty() );


  // test canExecute
  QgsProcessingModelAlgorithm alg2( "test", "testGroup" );
  QVERIFY( alg2.canExecute() );
  QgsProcessingModelChildAlgorithm c5;
  c5.setAlgorithmId( "native:centroids" );
  alg2.addChildAlgorithm( c5 );
  QVERIFY( alg2.canExecute() );
  // non-existing alg
  QgsProcessingModelChildAlgorithm c6;
  c6.setAlgorithmId( "i'm not an alg" );
  alg2.addChildAlgorithm( c6 );
  QVERIFY( !alg2.canExecute() );

  // test that children are re-attached before testing for canExecute
  QgsProcessingModelAlgorithm alg2a( "test", "testGroup" );
  QgsProcessingModelChildAlgorithm c5a;
  c5a.setAlgorithmId( "native:centroids" );
  alg2a.addChildAlgorithm( c5a );
  // simulate initially missing provider or algorithm (e.g. another model as a child algorithm)
  alg2a.mChildAlgorithms.begin().value().mAlgorithm.reset();
  QVERIFY( alg2a.canExecute() );

  // dependencies
  QgsProcessingModelAlgorithm alg3( "test", "testGroup" );
  QVERIFY( alg3.dependentChildAlgorithms( "notvalid" ).isEmpty() );
  QVERIFY( alg3.dependsOnChildAlgorithms( "notvalid" ).isEmpty() );
  QVERIFY( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "notvalid" ) ).isEmpty() );

  // add a child
  QgsProcessingModelChildAlgorithm c7;
  c7.setChildId( "c7" );
  alg3.addChildAlgorithm( c7 );
  QVERIFY( alg3.dependentChildAlgorithms( "c7" ).isEmpty() );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c7" ).isEmpty() );
  QVERIFY( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c7" ) ).isEmpty() );

  // direct dependency
  QgsProcessingModelChildAlgorithm c8;
  c8.setChildId( "c8" );
  c8.setDependencies( QList< QgsProcessingModelChildDependency >() << QgsProcessingModelChildDependency( "c7" ) );
  alg3.addChildAlgorithm( c8 );
  QVERIFY( alg3.dependentChildAlgorithms( "c8" ).isEmpty() );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c7" ).isEmpty() );
  QCOMPARE( alg3.dependentChildAlgorithms( "c7" ).count(), 1 );
  QVERIFY( alg3.dependentChildAlgorithms( "c7" ).contains( "c8" ) );
  QCOMPARE( alg3.dependsOnChildAlgorithms( "c8" ).count(), 1 );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c8" ).contains( "c7" ) );
  QVERIFY( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c7" ) ).isEmpty() );
  QCOMPARE( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c8" ) ).size(), 1 );
  QCOMPARE( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c8" ) ).at( 0 ).childId, QStringLiteral( "c7" ) );

  // dependency via parameter source
  QgsProcessingModelChildAlgorithm c9;
  c9.setChildId( "c9" );
  c9.addParameterSources( "x", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromChildOutput( "c8", "x" ) );
  alg3.addChildAlgorithm( c9 );
  QVERIFY( alg3.dependentChildAlgorithms( "c9" ).isEmpty() );
  QCOMPARE( alg3.dependentChildAlgorithms( "c8" ).count(), 1 );
  QVERIFY( alg3.dependentChildAlgorithms( "c8" ).contains( "c9" ) );
  QCOMPARE( alg3.dependentChildAlgorithms( "c7" ).count(), 2 );
  QVERIFY( alg3.dependentChildAlgorithms( "c7" ).contains( "c8" ) );
  QVERIFY( alg3.dependentChildAlgorithms( "c7" ).contains( "c9" ) );

  QVERIFY( alg3.dependsOnChildAlgorithms( "c7" ).isEmpty() );
  QCOMPARE( alg3.dependsOnChildAlgorithms( "c8" ).count(), 1 );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c8" ).contains( "c7" ) );
  QCOMPARE( alg3.dependsOnChildAlgorithms( "c9" ).count(), 2 );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c9" ).contains( "c7" ) );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c9" ).contains( "c8" ) );

  QVERIFY( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c7" ) ).isEmpty() );
  QCOMPARE( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c8" ) ).size(), 1 );
  QCOMPARE( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c8" ) ).at( 0 ).childId, QStringLiteral( "c7" ) );
  QCOMPARE( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c9" ) ).size(), 2 );
  QVERIFY( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c9" ) ).contains( QgsProcessingModelChildDependency( QStringLiteral( "c7" ) ) ) );
  QVERIFY( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c9" ) ).contains( QgsProcessingModelChildDependency( QStringLiteral( "c8" ) ) ) );

  QgsProcessingModelChildAlgorithm c9b;
  c9b.setChildId( "c9b" );
  c9b.addParameterSources( "x", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromChildOutput( "c9", "x" ) );
  alg3.addChildAlgorithm( c9b );

  QCOMPARE( alg3.dependentChildAlgorithms( "c9" ).count(), 1 );
  QCOMPARE( alg3.dependentChildAlgorithms( "c8" ).count(), 2 );
  QVERIFY( alg3.dependentChildAlgorithms( "c8" ).contains( "c9" ) );
  QVERIFY( alg3.dependentChildAlgorithms( "c8" ).contains( "c9b" ) );
  QCOMPARE( alg3.dependentChildAlgorithms( "c7" ).count(), 3 );
  QVERIFY( alg3.dependentChildAlgorithms( "c7" ).contains( "c8" ) );
  QVERIFY( alg3.dependentChildAlgorithms( "c7" ).contains( "c9" ) );
  QVERIFY( alg3.dependentChildAlgorithms( "c7" ).contains( "c9b" ) );

  QVERIFY( alg3.dependsOnChildAlgorithms( "c7" ).isEmpty() );
  QCOMPARE( alg3.dependsOnChildAlgorithms( "c8" ).count(), 1 );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c8" ).contains( "c7" ) );
  QCOMPARE( alg3.dependsOnChildAlgorithms( "c9" ).count(), 2 );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c9" ).contains( "c7" ) );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c9" ).contains( "c8" ) );
  QCOMPARE( alg3.dependsOnChildAlgorithms( "c9b" ).count(), 3 );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c9b" ).contains( "c7" ) );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c9b" ).contains( "c8" ) );
  QVERIFY( alg3.dependsOnChildAlgorithms( "c9b" ).contains( "c9" ) );

  QVERIFY( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c7" ) ).isEmpty() );
  QCOMPARE( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c8" ) ).size(), 1 );
  QCOMPARE( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c8" ) ).at( 0 ).childId, QStringLiteral( "c7" ) );
  QCOMPARE( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c9" ) ).size(), 2 );
  QVERIFY( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c9" ) ).contains( QgsProcessingModelChildDependency( QStringLiteral( "c7" ) ) ) );
  QVERIFY( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c9" ) ).contains( QgsProcessingModelChildDependency( QStringLiteral( "c8" ) ) ) );
  QCOMPARE( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c9b" ) ).size(), 3 );
  QVERIFY( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c9b" ) ).contains( QgsProcessingModelChildDependency( QStringLiteral( "c7" ) ) ) );
  QVERIFY( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c9b" ) ).contains( QgsProcessingModelChildDependency( QStringLiteral( "c8" ) ) ) );
  QVERIFY( alg3.availableDependenciesForChildAlgorithm( QStringLiteral( "c9b" ) ).contains( QgsProcessingModelChildDependency( QStringLiteral( "c9" ) ) ) );

  alg3.removeChildAlgorithm( "c9b" );


  // (de)activate child algorithm
  alg3.deactivateChildAlgorithm( "c9" );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( alg3.activateChildAlgorithm( "c9" ) );
  QVERIFY( alg3.childAlgorithm( "c9" ).isActive() );
  alg3.deactivateChildAlgorithm( "c8" );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( !alg3.activateChildAlgorithm( "c9" ) );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( alg3.activateChildAlgorithm( "c8" ) );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( alg3.activateChildAlgorithm( "c9" ) );
  QVERIFY( alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c8" ).isActive() );
  alg3.deactivateChildAlgorithm( "c7" );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c7" ).isActive() );
  QVERIFY( !alg3.activateChildAlgorithm( "c9" ) );
  QVERIFY( !alg3.activateChildAlgorithm( "c8" ) );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c7" ).isActive() );
  QVERIFY( !alg3.activateChildAlgorithm( "c8" ) );
  QVERIFY( alg3.activateChildAlgorithm( "c7" ) );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( !alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c7" ).isActive() );
  QVERIFY( !alg3.activateChildAlgorithm( "c9" ) );
  QVERIFY( alg3.activateChildAlgorithm( "c8" ) );
  QVERIFY( !alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c7" ).isActive() );
  QVERIFY( alg3.activateChildAlgorithm( "c9" ) );
  QVERIFY( alg3.childAlgorithm( "c9" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c8" ).isActive() );
  QVERIFY( alg3.childAlgorithm( "c7" ).isActive() );



  //remove child algorithm
  QVERIFY( !alg3.removeChildAlgorithm( "c7" ) );
  QVERIFY( !alg3.removeChildAlgorithm( "c8" ) );
  QVERIFY( alg3.removeChildAlgorithm( "c9" ) );
  QCOMPARE( alg3.childAlgorithms().count(), 2 );
  QVERIFY( alg3.childAlgorithms().contains( "c7" ) );
  QVERIFY( alg3.childAlgorithms().contains( "c8" ) );
  QVERIFY( !alg3.removeChildAlgorithm( "c7" ) );
  QVERIFY( alg3.removeChildAlgorithm( "c8" ) );
  QCOMPARE( alg3.childAlgorithms().count(), 1 );
  QVERIFY( alg3.childAlgorithms().contains( "c7" ) );
  QVERIFY( alg3.removeChildAlgorithm( "c7" ) );
  QVERIFY( alg3.childAlgorithms().isEmpty() );

  // parameter dependencies
  QgsProcessingModelAlgorithm alg4( "test", "testGroup" );
  QVERIFY( !alg4.childAlgorithmsDependOnParameter( "not a param" ) );
  QgsProcessingModelChildAlgorithm c10;
  c10.setChildId( "c10" );
  alg4.addChildAlgorithm( c10 );
  QVERIFY( !alg4.childAlgorithmsDependOnParameter( "not a param" ) );
  QgsProcessingModelParameter bool2;
  alg4.addModelParameter( new QgsProcessingParameterBoolean( "p1", "desc" ), bool2 );
  QVERIFY( !alg4.childAlgorithmsDependOnParameter( "p1" ) );
  c10.addParameterSources( "x", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromModelParameter( "p2" ) );
  alg4.setChildAlgorithm( c10 );
  QVERIFY( !alg4.childAlgorithmsDependOnParameter( "p1" ) );
  c10.addParameterSources( "y", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromModelParameter( "p1" ) );
  alg4.setChildAlgorithm( c10 );
  QVERIFY( alg4.childAlgorithmsDependOnParameter( "p1" ) );

  QgsProcessingModelParameter vlP;
  alg4.addModelParameter( new QgsProcessingParameterVectorLayer( "layer" ), vlP );
  QgsProcessingModelParameter field;
  alg4.addModelParameter( new QgsProcessingParameterField( "field", QString(), QVariant(), QStringLiteral( "layer" ) ), field );
  QVERIFY( !alg4.otherParametersDependOnParameter( "p1" ) );
  QVERIFY( !alg4.otherParametersDependOnParameter( "field" ) );
  QVERIFY( alg4.otherParametersDependOnParameter( "layer" ) );





  // to/from XML
  QgsProcessingModelAlgorithm alg5( "test", "testGroup" );
  alg5.helpContent().insert( "author", "me" );
  alg5.helpContent().insert( "usage", "run" );
  alg5.addGroupBox( groupBox );
  QVariantMap variables;
  variables.insert( QStringLiteral( "v1" ), 5 );
  variables.insert( QStringLiteral( "v2" ), QStringLiteral( "aabbccd" ) );
  alg5.setVariables( variables );
  QCOMPARE( alg5.variables(), variables );
  QgsProcessingModelChildAlgorithm alg5c1;
  alg5c1.setChildId( "cx1" );
  alg5c1.setAlgorithmId( "buffer" );
  alg5c1.setConfiguration( myConfig );
  alg5c1.addParameterSources( "x", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromModelParameter( "p1" ) );
  alg5c1.addParameterSources( "y", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromChildOutput( "cx2", "out3" ) );
  alg5c1.addParameterSources( "z", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromStaticValue( 5 ) );
  alg5c1.addParameterSources( "a", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromExpression( "2*2" ) );
  alg5c1.addParameterSources( "zm", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromStaticValue( 6 )
                              << QgsProcessingModelChildParameterSource::fromModelParameter( "p2" )
                              << QgsProcessingModelChildParameterSource::fromChildOutput( "cx2", "out4" )
                              << QgsProcessingModelChildParameterSource::fromExpression( "1+2" )
                              << QgsProcessingModelChildParameterSource::fromStaticValue( QgsProperty::fromExpression( "1+8" ) ) );
  alg5c1.setActive( true );
  alg5c1.setLinksCollapsed( Qt::BottomEdge, true );
  alg5c1.setLinksCollapsed( Qt::TopEdge, true );
  alg5c1.setDescription( "child 1" );
  alg5c1.setPosition( QPointF( 1, 2 ) );
  alg5c1.setSize( QSizeF( 11, 21 ) );
  QMap<QString, QgsProcessingModelOutput> alg5c1outputs;
  QgsProcessingModelOutput alg5c1out1;
  alg5c1out1.setDescription( QStringLiteral( "my output" ) );
  alg5c1out1.setPosition( QPointF( 3, 4 ) );
  alg5c1out1.setSize( QSizeF( 31, 41 ) );
  alg5c1outputs.insert( QStringLiteral( "a" ), alg5c1out1 );
  alg5c1.setModelOutputs( alg5c1outputs );
  alg5.addChildAlgorithm( alg5c1 );

  QgsProcessingModelChildAlgorithm alg5c2;
  alg5c2.setChildId( "cx2" );
  alg5c2.setAlgorithmId( QStringLiteral( "native:centroids" ) );
  alg5c2.setActive( false );
  alg5c2.setLinksCollapsed( Qt::BottomEdge, false );
  alg5c2.setLinksCollapsed( Qt::TopEdge, false );
  alg5c2.setDependencies( QList< QgsProcessingModelChildDependency >() << QgsProcessingModelChildDependency( "a" ) << QgsProcessingModelChildDependency( "b" ) );
  alg5.addChildAlgorithm( alg5c2 );

  QgsProcessingModelParameter alg5pc1;
  alg5pc1.setParameterName( QStringLiteral( "my_param" ) );
  alg5pc1.setPosition( QPointF( 11, 12 ) );
  alg5pc1.setSize( QSizeF( 21, 22 ) );
  alg5.addModelParameter( new QgsProcessingParameterBoolean( QStringLiteral( "my_param" ) ), alg5pc1 );
  alg5.setDesignerParameterValues( lastParams );

  QDomDocument doc = QDomDocument( "model" );
  alg5.initAlgorithm();
  const QVariant v = alg5.toVariant();
  // make sure private parameters weren't included in the definition
  QVERIFY( !v.toMap().value( QStringLiteral( "parameterDefinitions" ) ).toMap().contains( QStringLiteral( "VERBOSE_LOG" ) ) );

  QDomElement elem = QgsXmlUtils::writeVariant( v, doc );
  doc.appendChild( elem );

  QgsProcessingModelAlgorithm alg6;
  QVERIFY( alg6.loadVariant( QgsXmlUtils::readVariant( doc.firstChildElement() ) ) );
  QCOMPARE( alg6.name(), QStringLiteral( "test" ) );
  QCOMPARE( alg6.group(), QStringLiteral( "testGroup" ) );
  QCOMPARE( alg6.helpContent(), alg5.helpContent() );
  QCOMPARE( alg6.variables(), variables );
  QCOMPARE( alg6.designerParameterValues(), lastParams );

  QCOMPARE( alg6.groupBoxes().size(), 1 );
  QCOMPARE( alg6.groupBoxes().at( 0 ).size(), QSizeF( 9, 8 ) );
  QCOMPARE( alg6.groupBoxes().at( 0 ).position(), QPointF( 11, 14 ) );
  QCOMPARE( alg6.groupBoxes().at( 0 ).description(), QStringLiteral( "a comment" ) );
  QCOMPARE( alg6.groupBoxes().at( 0 ).color(), QColor( 123, 45, 67 ) );

  QgsProcessingModelChildAlgorithm alg6c1 = alg6.childAlgorithm( "cx1" );
  QCOMPARE( alg6c1.childId(), QStringLiteral( "cx1" ) );
  QCOMPARE( alg6c1.algorithmId(), QStringLiteral( "buffer" ) );
  QCOMPARE( alg6c1.configuration(), myConfig );
  QVERIFY( alg6c1.isActive() );
  QVERIFY( alg6c1.linksCollapsed( Qt::BottomEdge ) );
  QVERIFY( alg6c1.linksCollapsed( Qt::TopEdge ) );
  QCOMPARE( alg6c1.description(), QStringLiteral( "child 1" ) );
  QCOMPARE( alg6c1.position().x(), 1.0 );
  QCOMPARE( alg6c1.position().y(), 2.0 );
  QCOMPARE( alg6c1.size().width(), 11.0 );
  QCOMPARE( alg6c1.size().height(), 21.0 );
  QCOMPARE( alg6c1.parameterSources().count(), 5 );
  QCOMPARE( alg6c1.parameterSources().value( "x" ).at( 0 ).source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( alg6c1.parameterSources().value( "x" ).at( 0 ).parameterName(), QStringLiteral( "p1" ) );
  QCOMPARE( alg6c1.parameterSources().value( "y" ).at( 0 ).source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( alg6c1.parameterSources().value( "y" ).at( 0 ).outputChildId(), QStringLiteral( "cx2" ) );
  QCOMPARE( alg6c1.parameterSources().value( "y" ).at( 0 ).outputName(), QStringLiteral( "out3" ) );
  QCOMPARE( alg6c1.parameterSources().value( "z" ).at( 0 ).source(), QgsProcessingModelChildParameterSource::StaticValue );
  QCOMPARE( alg6c1.parameterSources().value( "z" ).at( 0 ).staticValue().toInt(), 5 );
  QCOMPARE( alg6c1.parameterSources().value( "a" ).at( 0 ).source(), QgsProcessingModelChildParameterSource::Expression );
  QCOMPARE( alg6c1.parameterSources().value( "a" ).at( 0 ).expression(), QStringLiteral( "2*2" ) );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).count(), 5 );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 0 ).source(), QgsProcessingModelChildParameterSource::StaticValue );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 0 ).staticValue().toInt(), 6 );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 1 ).source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 1 ).parameterName(), QStringLiteral( "p2" ) );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 2 ).source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 2 ).outputChildId(), QStringLiteral( "cx2" ) );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 2 ).outputName(), QStringLiteral( "out4" ) );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 3 ).source(), QgsProcessingModelChildParameterSource::Expression );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 3 ).expression(), QStringLiteral( "1+2" ) );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 4 ).source(), QgsProcessingModelChildParameterSource::StaticValue );
  QVERIFY( alg6c1.parameterSources().value( "zm" ).at( 4 ).staticValue().canConvert< QgsProperty >() );
  QCOMPARE( alg6c1.parameterSources().value( "zm" ).at( 4 ).staticValue().value< QgsProperty >().expressionString(), QStringLiteral( "1+8" ) );

  QCOMPARE( alg6c1.modelOutputs().count(), 1 );
  QCOMPARE( alg6c1.modelOutputs().value( QStringLiteral( "a" ) ).description(), QStringLiteral( "my output" ) );
  QCOMPARE( alg6c1.modelOutput( "a" ).description(), QStringLiteral( "my output" ) );
  QCOMPARE( alg6c1.modelOutput( "a" ).position().x(), 3.0 );
  QCOMPARE( alg6c1.modelOutput( "a" ).position().y(), 4.0 );
  QCOMPARE( alg6c1.modelOutput( "a" ).size().width(), 31.0 );
  QCOMPARE( alg6c1.modelOutput( "a" ).size().height(), 41.0 );

  QgsProcessingModelChildAlgorithm alg6c2 = alg6.childAlgorithm( "cx2" );
  QCOMPARE( alg6c2.childId(), QStringLiteral( "cx2" ) );
  QVERIFY( !alg6c2.isActive() );
  QVERIFY( !alg6c2.linksCollapsed( Qt::BottomEdge ) );
  QVERIFY( !alg6c2.linksCollapsed( Qt::TopEdge ) );
  QCOMPARE( alg6c2.dependencies(), QList< QgsProcessingModelChildDependency >() << QgsProcessingModelChildDependency( "a" ) << QgsProcessingModelChildDependency( "b" ) );

  QCOMPARE( alg6.parameterComponents().count(), 1 );
  QCOMPARE( alg6.parameterComponents().value( QStringLiteral( "my_param" ) ).parameterName(), QStringLiteral( "my_param" ) );
  QCOMPARE( alg6.parameterComponent( "my_param" ).parameterName(), QStringLiteral( "my_param" ) );
  QCOMPARE( alg6.parameterComponent( "my_param" ).position().x(), 11.0 );
  QCOMPARE( alg6.parameterComponent( "my_param" ).position().y(), 12.0 );
  QCOMPARE( alg6.parameterComponent( "my_param" ).size().width(), 21.0 );
  QCOMPARE( alg6.parameterComponent( "my_param" ).size().height(), 22.0 );
  QCOMPARE( alg6.parameterDefinitions().count(), 1 );
  QCOMPARE( alg6.parameterDefinitions().at( 0 )->type(), QStringLiteral( "boolean" ) );

  // destination parameters
  QgsProcessingModelAlgorithm alg7( "test", "testGroup" );
  QgsProcessingModelChildAlgorithm alg7c1;
  alg7c1.setChildId( "cx1" );
  alg7c1.setAlgorithmId( "native:centroids" );
  QMap<QString, QgsProcessingModelOutput> alg7c1outputs;
  QgsProcessingModelOutput alg7c1out1( QStringLiteral( "my_output" ) );
  alg7c1out1.setChildId( "cx1" );
  alg7c1out1.setChildOutputName( "OUTPUT" );
  alg7c1out1.setDescription( QStringLiteral( "my output" ) );
  alg7c1outputs.insert( QStringLiteral( "my_output" ), alg7c1out1 );
  alg7c1.setModelOutputs( alg7c1outputs );
  alg7.addChildAlgorithm( alg7c1 );
  // verify that model has destination parameter created
  QCOMPARE( alg7.destinationParameterDefinitions().count(), 1 );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 0 )->name(), QStringLiteral( "cx1:my_output" ) );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 0 )->description(), QStringLiteral( "my output" ) );
  QCOMPARE( static_cast< const QgsProcessingDestinationParameter * >( alg7.destinationParameterDefinitions().at( 0 ) )->originalProvider()->id(), QStringLiteral( "native" ) );
  QCOMPARE( alg7.outputDefinitions().count(), 1 );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->name(), QStringLiteral( "cx1:my_output" ) );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->type(), QStringLiteral( "outputVector" ) );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->description(), QStringLiteral( "my output" ) );

  QgsProcessingModelChildAlgorithm alg7c2;
  alg7c2.setChildId( "cx2" );
  alg7c2.setAlgorithmId( "native:centroids" );
  QMap<QString, QgsProcessingModelOutput> alg7c2outputs;
  QgsProcessingModelOutput alg7c2out1( QStringLiteral( "my_output2" ) );
  alg7c2out1.setChildId( "cx2" );
  alg7c2out1.setChildOutputName( "OUTPUT" );
  alg7c2out1.setDescription( QStringLiteral( "my output2" ) );
  alg7c2out1.setDefaultValue( QStringLiteral( "my value" ) );
  alg7c2out1.setMandatory( true );
  alg7c2outputs.insert( QStringLiteral( "my_output2" ), alg7c2out1 );
  alg7c2.setModelOutputs( alg7c2outputs );
  alg7.addChildAlgorithm( alg7c2 );

  QCOMPARE( alg7.destinationParameterDefinitions().count(), 2 );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 0 )->name(), QStringLiteral( "cx1:my_output" ) );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 0 )->description(), QStringLiteral( "my output" ) );
  QVERIFY( alg7.destinationParameterDefinitions().at( 0 )->defaultValue().isNull() );
  QVERIFY( !( alg7.destinationParameterDefinitions().at( 0 )->flags() & QgsProcessingParameterDefinition::FlagOptional ) );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 1 )->name(), QStringLiteral( "cx2:my_output2" ) );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 1 )->description(), QStringLiteral( "my output2" ) );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 1 )->defaultValue().toString(), QStringLiteral( "my value" ) );
  QVERIFY( !( alg7.destinationParameterDefinitions().at( 1 )->flags() & QgsProcessingParameterDefinition::FlagOptional ) );
  QCOMPARE( alg7.outputDefinitions().count(), 2 );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->name(), QStringLiteral( "cx1:my_output" ) );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->type(), QStringLiteral( "outputVector" ) );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->description(), QStringLiteral( "my output" ) );
  QCOMPARE( alg7.outputDefinitions().at( 1 )->name(), QStringLiteral( "cx2:my_output2" ) );
  QCOMPARE( alg7.outputDefinitions().at( 1 )->type(), QStringLiteral( "outputVector" ) );
  QCOMPARE( alg7.outputDefinitions().at( 1 )->description(), QStringLiteral( "my output2" ) );

  alg7.removeChildAlgorithm( "cx1" );
  QCOMPARE( alg7.destinationParameterDefinitions().count(), 1 );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 0 )->name(), QStringLiteral( "cx2:my_output2" ) );
  QCOMPARE( alg7.destinationParameterDefinitions().at( 0 )->description(), QStringLiteral( "my output2" ) );
  QCOMPARE( alg7.outputDefinitions().count(), 1 );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->name(), QStringLiteral( "cx2:my_output2" ) );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->type(), QStringLiteral( "outputVector" ) );
  QCOMPARE( alg7.outputDefinitions().at( 0 )->description(), QStringLiteral( "my output2" ) );

  // mandatory model output with optional child algorithm parameter
  QgsProcessingModelChildAlgorithm alg7c3;
  alg7c3.setChildId( "cx3" );
  alg7c3.setAlgorithmId( "native:extractbyexpression" );
  QMap<QString, QgsProcessingModelOutput> alg7c3outputs;
  QgsProcessingModelOutput alg7c3out1;
  alg7c3out1.setChildId( "cx3" );
  alg7c3out1.setChildOutputName( "FAIL_OUTPUT" );
  alg7c3out1.setDescription( QStringLiteral( "my_output3" ) );
  alg7c3outputs.insert( QStringLiteral( "my_output3" ), alg7c3out1 );
  alg7c3.setModelOutputs( alg7c3outputs );
  alg7.addChildAlgorithm( alg7c3 );
  QVERIFY( alg7.destinationParameterDefinitions().at( 1 )->flags() & QgsProcessingParameterDefinition::FlagOptional );
  alg7.childAlgorithm( alg7c3.childId() ).modelOutput( QStringLiteral( "my_output3" ) ).setMandatory( true );
  alg7.updateDestinationParameters();
  QVERIFY( !( alg7.destinationParameterDefinitions().at( 1 )->flags() & QgsProcessingParameterDefinition::FlagOptional ) );
}

void TestQgsProcessing::modelExecution()
{
  // test childOutputIsRequired
  QgsProcessingModelAlgorithm model1;
  QgsProcessingModelChildAlgorithm algc1;
  algc1.setChildId( "cx1" );
  algc1.setAlgorithmId( "native:centroids" );
  model1.addChildAlgorithm( algc1 );
  QgsProcessingModelChildAlgorithm algc2;
  algc2.setChildId( "cx2" );
  algc2.setAlgorithmId( "native:centroids" );
  algc2.addParameterSources( "x", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromChildOutput( "cx1", "p1" ) );
  model1.addChildAlgorithm( algc2 );
  QgsProcessingModelChildAlgorithm algc3;
  algc3.setChildId( "cx3" );
  algc3.setAlgorithmId( "native:centroids" );
  algc3.addParameterSources( "x", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromChildOutput( "cx1", "p2" ) );
  algc3.setActive( false );
  model1.addChildAlgorithm( algc3 );

  QVERIFY( model1.childOutputIsRequired( "cx1", "p1" ) ); // cx2 depends on p1
  QVERIFY( !model1.childOutputIsRequired( "cx1", "p2" ) ); // cx3 depends on p2, but cx3 is not active
  QVERIFY( !model1.childOutputIsRequired( "cx1", "p3" ) ); // nothing requires p3
  QVERIFY( !model1.childOutputIsRequired( "cx2", "p1" ) );
  QVERIFY( !model1.childOutputIsRequired( "cx3", "p1" ) );

  // test parametersForChildAlgorithm
  QgsProcessingModelAlgorithm model2;
  QgsProcessingModelParameter sourceParam( "SOURCE_LAYER" );
  sourceParam.comment()->setDescription( QStringLiteral( "an input" ) );
  model2.addModelParameter( new QgsProcessingParameterFeatureSource( "SOURCE_LAYER" ), sourceParam );
  model2.addModelParameter( new QgsProcessingParameterNumber( "DIST", QString(), QgsProcessingParameterNumber::Double ), QgsProcessingModelParameter( "DIST" ) );
  QgsProcessingParameterCrs *p = new QgsProcessingParameterCrs( "CRS", QString(), QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:28355" ) ) );
  p->setFlags( p->flags() | QgsProcessingParameterDefinition::FlagAdvanced );
  model2.addModelParameter( p, QgsProcessingModelParameter( "CRS" ) );
  QgsProcessingModelChildAlgorithm alg2c1;
  QgsExpressionContext expContext;
  QgsExpressionContextScope *scope = new QgsExpressionContextScope();
  scope->setVariable( "myvar", 8 );
  expContext.appendScope( scope );
  alg2c1.setChildId( "cx1" );
  alg2c1.setAlgorithmId( "native:buffer" );
  alg2c1.addParameterSources( "INPUT", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromModelParameter( "SOURCE_LAYER" ) );
  alg2c1.addParameterSources( "DISTANCE", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromModelParameter( "DIST" ) );
  alg2c1.addParameterSources( "SEGMENTS", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromExpression( QStringLiteral( "@myvar*2" ) ) );
  alg2c1.addParameterSources( "END_CAP_STYLE", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromStaticValue( 1 ) );
  alg2c1.addParameterSources( "JOIN_STYLE", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromStaticValue( 2 ) );
  alg2c1.addParameterSources( "DISSOLVE", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromStaticValue( false ) );
  QMap<QString, QgsProcessingModelOutput> outputs1;
  QgsProcessingModelOutput out1( "MODEL_OUT_LAYER" );
  out1.setChildOutputName( "OUTPUT" );
  outputs1.insert( QStringLiteral( "MODEL_OUT_LAYER" ), out1 );
  alg2c1.setModelOutputs( outputs1 );
  model2.addChildAlgorithm( alg2c1 );

  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString vector = testDataDir + "points.shp";

  QVariantMap modelInputs;
  modelInputs.insert( "SOURCE_LAYER", vector );
  modelInputs.insert( "DIST", 271 );
  modelInputs.insert( "cx1:MODEL_OUT_LAYER", "dest.shp" );
  QgsProcessingOutputLayerDefinition layerDef( "memory:" );
  layerDef.destinationName = "my_dest";
  modelInputs.insert( "cx3:MY_OUT", QVariant::fromValue( layerDef ) );
  QVariantMap childResults;
  QVariantMap params = model2.parametersForChildAlgorithm( model2.childAlgorithm( "cx1" ), modelInputs, childResults, expContext );
  QCOMPARE( params.value( "DISSOLVE" ).toBool(), false );
  QCOMPARE( params.value( "DISTANCE" ).toInt(), 271 );
  QCOMPARE( params.value( "SEGMENTS" ).toInt(), 16 );
  QCOMPARE( params.value( "END_CAP_STYLE" ).toInt(), 1 );
  QCOMPARE( params.value( "JOIN_STYLE" ).toInt(), 2 );
  QCOMPARE( params.value( "INPUT" ).toString(), vector );
  QCOMPARE( params.value( "OUTPUT" ).toString(), QStringLiteral( "dest.shp" ) );
  QCOMPARE( params.count(), 7 );

  QgsProcessingContext context;

  // Check variables for child algorithm
  // without values
  QMap<QString, QgsProcessingModelAlgorithm::VariableDefinition> variables = model2.variablesForChildAlgorithm( "cx1", context );
  QCOMPARE( variables.count(), 7 );
  QCOMPARE( variables.value( "DIST" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "CRS" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER_minx" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER_miny" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER_maxx" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER_maxy" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );

  // with values
  variables = model2.variablesForChildAlgorithm( "cx1", context, modelInputs, childResults );
  QCOMPARE( variables.count(), 7 );
  QCOMPARE( variables.value( "DIST" ).value.toInt(), 271 );
  QCOMPARE( variables.value( "SOURCE_LAYER" ).source.parameterName(), QString( "SOURCE_LAYER" ) );
  QGSCOMPARENEAR( variables.value( "SOURCE_LAYER_minx" ).value.toDouble(), -118.8888, 0.001 );
  QGSCOMPARENEAR( variables.value( "SOURCE_LAYER_miny" ).value.toDouble(), 22.8002, 0.001 );
  QGSCOMPARENEAR( variables.value( "SOURCE_LAYER_maxx" ).value.toDouble(), -83.3333, 0.001 );
  QGSCOMPARENEAR( variables.value( "SOURCE_LAYER_maxy" ).value.toDouble(), 46.8719, 0.001 );

  std::unique_ptr< QgsExpressionContextScope > childScope( model2.createExpressionContextScopeForChildAlgorithm( "cx1", context, modelInputs, childResults ) );
  QCOMPARE( childScope->name(), QStringLiteral( "algorithm_inputs" ) );
  QCOMPARE( childScope->variableCount(), 7 );
  QCOMPARE( childScope->variable( "DIST" ).toInt(), 271 );
  QCOMPARE( variables.value( "SOURCE_LAYER" ).source.parameterName(), QString( "SOURCE_LAYER" ) );
  QGSCOMPARENEAR( childScope->variable( "SOURCE_LAYER_minx" ).toDouble(), -118.8888, 0.001 );
  QGSCOMPARENEAR( childScope->variable( "SOURCE_LAYER_miny" ).toDouble(), 22.8002, 0.001 );
  QGSCOMPARENEAR( childScope->variable( "SOURCE_LAYER_maxx" ).toDouble(), -83.3333, 0.001 );
  QGSCOMPARENEAR( childScope->variable( "SOURCE_LAYER_maxy" ).toDouble(), 46.8719, 0.001 );


  QVariantMap results;
  results.insert( "OUTPUT", QStringLiteral( "dest.shp" ) );
  childResults.insert( "cx1", results );

  // a child who uses an output from another alg as a parameter value
  QgsProcessingModelChildAlgorithm alg2c2;
  alg2c2.setChildId( "cx2" );
  alg2c2.setAlgorithmId( "native:centroids" );
  alg2c2.addParameterSources( "INPUT", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromChildOutput( "cx1", "OUTPUT" ) );
  model2.addChildAlgorithm( alg2c2 );
  params = model2.parametersForChildAlgorithm( model2.childAlgorithm( "cx2" ), modelInputs, childResults, expContext );
  QCOMPARE( params.value( "INPUT" ).toString(), QStringLiteral( "dest.shp" ) );
  QCOMPARE( params.value( "OUTPUT" ).toString(), QStringLiteral( "memory:Centroids" ) );
  QCOMPARE( params.count(), 2 );

  variables = model2.variablesForChildAlgorithm( "cx2", context );
  QCOMPARE( variables.count(), 12 );
  QCOMPARE( variables.value( "DIST" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER_minx" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER_miny" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER_maxx" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER_maxy" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "cx1_OUTPUT" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx1_OUTPUT" ).source.outputChildId(), QStringLiteral( "cx1" ) );
  QCOMPARE( variables.value( "cx1_OUTPUT_minx" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx1_OUTPUT_minx" ).source.outputChildId(), QStringLiteral( "cx1" ) );
  QCOMPARE( variables.value( "cx1_OUTPUT_miny" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx1_OUTPUT_miny" ).source.outputChildId(), QStringLiteral( "cx1" ) );
  QCOMPARE( variables.value( "cx1_OUTPUT_maxx" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx1_OUTPUT_maxx" ).source.outputChildId(), QStringLiteral( "cx1" ) );
  QCOMPARE( variables.value( "cx1_OUTPUT_maxy" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx1_OUTPUT_maxy" ).source.outputChildId(), QStringLiteral( "cx1" ) );

  // with values
  variables = model2.variablesForChildAlgorithm( "cx2", context, modelInputs, childResults );
  QCOMPARE( variables.count(), 12 );
  QCOMPARE( variables.value( "DIST" ).value.toInt(), 271 );
  QCOMPARE( variables.value( "SOURCE_LAYER" ).source.parameterName(), QString( "SOURCE_LAYER" ) );
  QCOMPARE( variables.value( "cx1_OUTPUT" ).source.outputChildId(), QString( "cx1" ) );
  QCOMPARE( variables.value( "cx1_OUTPUT" ).source.parameterName(), QString( "" ) );
  QGSCOMPARENEAR( variables.value( "SOURCE_LAYER_minx" ).value.toDouble(), -118.8888, 0.001 );
  QGSCOMPARENEAR( variables.value( "SOURCE_LAYER_miny" ).value.toDouble(), 22.8002, 0.001 );
  QGSCOMPARENEAR( variables.value( "SOURCE_LAYER_maxx" ).value.toDouble(), -83.3333, 0.001 );
  QGSCOMPARENEAR( variables.value( "SOURCE_LAYER_maxy" ).value.toDouble(), 46.8719, 0.001 );

  // a child with an optional output
  QgsProcessingModelChildAlgorithm alg2c3;
  alg2c3.setChildId( "cx3" );
  alg2c3.setAlgorithmId( "native:extractbyexpression" );
  alg2c3.addParameterSources( "INPUT", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromChildOutput( "cx1", "OUTPUT" ) );
  alg2c3.addParameterSources( "EXPRESSION", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromStaticValue( "true" ) );
  alg2c3.addParameterSources( "OUTPUT", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromModelParameter( "MY_OUT" ) );
  alg2c3.setDependencies( QList< QgsProcessingModelChildDependency >() << QgsProcessingModelChildDependency( "cx2" ) );
  QMap<QString, QgsProcessingModelOutput> outputs3;
  QgsProcessingModelOutput out2( "MY_OUT" );
  out2.setChildOutputName( "OUTPUT" );
  outputs3.insert( QStringLiteral( "MY_OUT" ), out2 );
  alg2c3.setModelOutputs( outputs3 );

  model2.addChildAlgorithm( alg2c3 );
  params = model2.parametersForChildAlgorithm( model2.childAlgorithm( "cx3" ), modelInputs, childResults, expContext );
  QCOMPARE( params.value( "INPUT" ).toString(), QStringLiteral( "dest.shp" ) );
  QCOMPARE( params.value( "EXPRESSION" ).toString(), QStringLiteral( "true" ) );
  QVERIFY( params.value( "OUTPUT" ).canConvert<QgsProcessingOutputLayerDefinition>() );
  QgsProcessingOutputLayerDefinition outDef = qvariant_cast<QgsProcessingOutputLayerDefinition>( params.value( "OUTPUT" ) );
  QCOMPARE( outDef.destinationName, QStringLiteral( "MY_OUT" ) );
  QCOMPARE( outDef.sink.staticValue().toString(), QStringLiteral( "memory:" ) );
  QCOMPARE( params.count(), 3 ); // don't want FAIL_OUTPUT set!

  // a child with an static output value
  QgsProcessingModelChildAlgorithm alg2c4;
  alg2c4.setChildId( "cx4" );
  alg2c4.setAlgorithmId( "native:extractbyexpression" );
  alg2c4.addParameterSources( "OUTPUT", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromStaticValue( "STATIC" ) );
  model2.addChildAlgorithm( alg2c4 );
  params = model2.parametersForChildAlgorithm( model2.childAlgorithm( "cx4" ), modelInputs, childResults, expContext );
  QCOMPARE( params.value( "OUTPUT" ).toString(), QStringLiteral( "STATIC" ) );
  model2.removeChildAlgorithm( "cx4" );
  // expression based output value
  alg2c4.addParameterSources( "OUTPUT", QgsProcessingModelChildParameterSources() << QgsProcessingModelChildParameterSource::fromExpression( "'A' || 'B'" ) );
  model2.addChildAlgorithm( alg2c4 );
  params = model2.parametersForChildAlgorithm( model2.childAlgorithm( "cx4" ), modelInputs, childResults, expContext );
  QCOMPARE( params.value( "OUTPUT" ).toString(), QStringLiteral( "AB" ) );
  model2.removeChildAlgorithm( "cx4" );


  variables = model2.variablesForChildAlgorithm( "cx3", context );
  QCOMPARE( variables.count(), 17 );
  QCOMPARE( variables.value( "DIST" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER_minx" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER_miny" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER_maxx" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "SOURCE_LAYER_maxy" ).source.source(), QgsProcessingModelChildParameterSource::ModelParameter );
  QCOMPARE( variables.value( "cx1_OUTPUT" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx1_OUTPUT" ).source.outputChildId(), QStringLiteral( "cx1" ) );
  QCOMPARE( variables.value( "cx1_OUTPUT_minx" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx1_OUTPUT_minx" ).source.outputChildId(), QStringLiteral( "cx1" ) );
  QCOMPARE( variables.value( "cx1_OUTPUT_miny" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx1_OUTPUT_miny" ).source.outputChildId(), QStringLiteral( "cx1" ) );
  QCOMPARE( variables.value( "cx1_OUTPUT_maxx" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx1_OUTPUT_maxx" ).source.outputChildId(), QStringLiteral( "cx1" ) );
  QCOMPARE( variables.value( "cx1_OUTPUT_maxy" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx1_OUTPUT_maxy" ).source.outputChildId(), QStringLiteral( "cx1" ) );
  QCOMPARE( variables.value( "cx2_OUTPUT" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx2_OUTPUT" ).source.outputChildId(), QStringLiteral( "cx2" ) );
  QCOMPARE( variables.value( "cx2_OUTPUT_minx" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx2_OUTPUT_minx" ).source.outputChildId(), QStringLiteral( "cx2" ) );
  QCOMPARE( variables.value( "cx2_OUTPUT_miny" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx2_OUTPUT_miny" ).source.outputChildId(), QStringLiteral( "cx2" ) );
  QCOMPARE( variables.value( "cx2_OUTPUT_maxx" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx2_OUTPUT_maxx" ).source.outputChildId(), QStringLiteral( "cx2" ) );
  QCOMPARE( variables.value( "cx2_OUTPUT_maxy" ).source.source(), QgsProcessingModelChildParameterSource::ChildOutput );
  QCOMPARE( variables.value( "cx2_OUTPUT_maxy" ).source.outputChildId(), QStringLiteral( "cx2" ) );
  // with values
  variables = model2.variablesForChildAlgorithm( "cx3", context, modelInputs, childResults );
  QCOMPARE( variables.count(), 17 );
  QCOMPARE( variables.value( "DIST" ).value.toInt(), 271 );
  QCOMPARE( variables.value( "SOURCE_LAYER" ).source.parameterName(), QString( "SOURCE_LAYER" ) );
  QCOMPARE( variables.value( "cx1_OUTPUT" ).source.outputChildId(), QString( "cx1" ) );
  QCOMPARE( variables.value( "cx1_OUTPUT" ).source.parameterName(), QString( "" ) );
  QCOMPARE( variables.value( "cx2_OUTPUT" ).source.outputChildId(), QString( "cx2" ) );
  QCOMPARE( variables.value( "cx2_OUTPUT" ).source.parameterName(), QString( "" ) );
  QGSCOMPARENEAR( variables.value( "SOURCE_LAYER_minx" ).value.toDouble(), -118.8888, 0.001 );
  QGSCOMPARENEAR( variables.value( "SOURCE_LAYER_miny" ).value.toDouble(), 22.8002, 0.001 );
  QGSCOMPARENEAR( variables.value( "SOURCE_LAYER_maxx" ).value.toDouble(), -83.3333, 0.001 );
  QGSCOMPARENEAR( variables.value( "SOURCE_LAYER_maxy" ).value.toDouble(), 46.8719, 0.001 );

  // test safe name of the child alg parameter as source to another algorithm
  // parameter name should have [\s ' ( ) : .] chars changed to "_" (regexp [\\s'\"\\(\\):\.])
  // this case is esecially important in case of grass algs where name algorithm contains "."
  // name of the variable is get from childDescription or childId. Refs https://github.com/qgis/QGIS/issues/36377
  QgsProcessingModelChildAlgorithm &cx1 = model2.childAlgorithm( "cx1" );
  QString oldDescription = cx1.description();
  cx1.setDescription( "cx '():.1" );
  variables = model2.variablesForChildAlgorithm( "cx3", context );
  QVERIFY( !variables.contains( "cx1_OUTPUT" ) );
  QVERIFY( !variables.contains( "cx '():.1_OUTPUT" ) );
  QVERIFY( variables.contains( "cx______1_OUTPUT" ) );
  cx1.setDescription( oldDescription ); // set descrin back to avoid fail of following tests

  // test model to python conversion
  model2.setName( QStringLiteral( "2my model" ) );
  model2.childAlgorithm( "cx1" ).modelOutput( QStringLiteral( "MODEL_OUT_LAYER" ) ).setDescription( "my model output" );
  model2.updateDestinationParameters();
  model2.childAlgorithm( "cx1" ).setDescription( "first step in my model" );
  QStringList actualParts = model2.asPythonCode( QgsProcessing::PythonQgsProcessingAlgorithmSubclass, 2 );
  QgsDebugMsg( actualParts.join( '\n' ) );
  QStringList expectedParts = QStringLiteral( "\"\"\"\n"
                              "Model exported as python.\n"
                              "Name : 2my model\n"
                              "Group : \n"
                              "With QGIS : %1\n"
                              "\"\"\"\n\n"
                              "from qgis.core import QgsProcessing\n"
                              "from qgis.core import QgsProcessingAlgorithm\n"
                              "from qgis.core import QgsProcessingMultiStepFeedback\n"
                              "from qgis.core import QgsProcessingParameterFeatureSource\n"
                              "from qgis.core import QgsProcessingParameterNumber\n"
                              "from qgis.core import QgsProcessingParameterCrs\n"
                              "from qgis.core import QgsProcessingParameterFeatureSink\n"
                              "from qgis.core import QgsProcessingParameterDefinition\n"
                              "from qgis.core import QgsCoordinateReferenceSystem\n"
                              "from qgis.core import QgsExpression\n"
                              "import processing\n"
                              "\n"
                              "\n"
                              "class MyModel(QgsProcessingAlgorithm):\n"
                              "\n"
                              "  def initAlgorithm(self, config=None):\n"
                              "    # an input\n"
                              "    self.addParameter(QgsProcessingParameterFeatureSource('SOURCE_LAYER', '', defaultValue=None))\n"
                              "    self.addParameter(QgsProcessingParameterNumber('DIST', '', type=QgsProcessingParameterNumber.Double, defaultValue=None))\n"
                              "    param = QgsProcessingParameterCrs('CRS', '', defaultValue=QgsCoordinateReferenceSystem('EPSG:28355'))\n"
                              "    param.setFlags(param.flags() | QgsProcessingParameterDefinition.FlagAdvanced)\n"
                              "    self.addParameter(param)\n"
                              "    self.addParameter(QgsProcessingParameterFeatureSink('MyModelOutput', 'my model output', type=QgsProcessing.TypeVectorPolygon, createByDefault=True, supportsAppend=True, defaultValue=None))\n"
                              "    self.addParameter(QgsProcessingParameterFeatureSink('cx3:MY_OUT', '', type=QgsProcessing.TypeVectorAnyGeometry, createByDefault=True, defaultValue=None))\n"
                              "\n"
                              "  def processAlgorithm(self, parameters, context, model_feedback):\n"
                              "    # Use a multi-step feedback, so that individual child algorithm progress reports are adjusted for the\n"
                              "    # overall progress through the model\n"
                              "    feedback = QgsProcessingMultiStepFeedback(3, model_feedback)\n"
                              "    results = {}\n"
                              "    outputs = {}\n"
                              "\n"
                              "    # first step in my model\n"
                              "    alg_params = {\n"
                              "      'DISSOLVE': False,\n"
                              "      'DISTANCE': parameters['DIST'],\n"
                              "      'END_CAP_STYLE': 1,\n"
                              "      'INPUT': parameters['SOURCE_LAYER'],\n"
                              "      'JOIN_STYLE': 2,\n"
                              "      'SEGMENTS': QgsExpression('@myvar*2').evaluate(),\n"
                              "      'OUTPUT': parameters['MyModelOutput']\n"
                              "    }\n"
                              "    outputs['FirstStepInMyModel'] = processing.run('native:buffer', alg_params, context=context, feedback=feedback, is_child_algorithm=True)\n"
                              "    results['MyModelOutput'] = outputs['FirstStepInMyModel']['OUTPUT']\n"
                              "\n"
                              "    feedback.setCurrentStep(1)\n"
                              "    if feedback.isCanceled():\n"
                              "      return {}\n"
                              "\n"
                              "    alg_params = {\n"
                              "      'INPUT': outputs['FirstStepInMyModel']['OUTPUT'],\n"
                              "      'OUTPUT': QgsProcessing.TEMPORARY_OUTPUT\n"
                              "    }\n"
                              "    outputs['cx2'] = processing.run('native:centroids', alg_params, context=context, feedback=feedback, is_child_algorithm=True)\n"
                              "\n"
                              "    feedback.setCurrentStep(2)\n"
                              "    if feedback.isCanceled():\n"
                              "      return {}\n"
                              "\n"
                              "    alg_params = {\n"
                              "      'EXPRESSION': 'true',\n"
                              "      'INPUT': outputs['FirstStepInMyModel']['OUTPUT'],\n"
                              "      'OUTPUT': parameters['MY_OUT'],\n"
                              "      'OUTPUT': parameters['cx3:MY_OUT']\n"
                              "    }\n"
                              "    outputs['cx3'] = processing.run('native:extractbyexpression', alg_params, context=context, feedback=feedback, is_child_algorithm=True)\n"
                              "    results['cx3:MY_OUT'] = outputs['cx3']['OUTPUT']\n"
                              "    return results\n"
                              "\n"
                              "  def name(self):\n"
                              "    return '2my model'\n"
                              "\n"
                              "  def displayName(self):\n"
                              "    return '2my model'\n"
                              "\n"
                              "  def group(self):\n"
                              "    return ''\n"
                              "\n"
                              "  def groupId(self):\n"
                              "    return ''\n"
                              "\n"
                              "  def createInstance(self):\n"
                              "    return MyModel()\n" ).arg( Qgis::versionInt() ).split( '\n' );
  QCOMPARE( actualParts, expectedParts );
}

void TestQgsProcessing::modelBranchPruning()
{
  QgsVectorLayer *layer3111 = new QgsVectorLayer( "Point?crs=epsg:3111", "v1", "memory" );
  QgsProject p;
  p.addMapLayer( layer3111 );

  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString raster1 = testDataDir + "landsat_4326.tif";
  QFileInfo fi1( raster1 );
  QgsRasterLayer *r1 = new QgsRasterLayer( fi1.filePath(), "R1" );
  QVERIFY( r1->isValid() );
  p.addMapLayer( r1 );

  QgsProcessingContext context;
  context.setProject( &p );

  // test that model branches are trimmed for algorithms which return the FlagPruneModelBranchesBasedOnAlgorithmResults flag
  QgsProcessingModelAlgorithm model1;

  // first add the filter by layer type alg
  QgsProcessingModelChildAlgorithm algc1;
  algc1.setChildId( "filter" );
  algc1.setAlgorithmId( "native:filterlayersbytype" );
  QgsProcessingModelParameter param;
  param.setParameterName( QStringLiteral( "LAYER" ) );
  model1.addModelParameter( new QgsProcessingParameterMapLayer( QStringLiteral( "LAYER" ) ), param );
  algc1.addParameterSources( QStringLiteral( "INPUT" ), QList< QgsProcessingModelChildParameterSource >() << QgsProcessingModelChildParameterSource::fromModelParameter( QStringLiteral( "LAYER" ) ) );
  model1.addChildAlgorithm( algc1 );

  //then create some branches which come off this, depending on the layer type
  QgsProcessingModelChildAlgorithm algc2;
  algc2.setChildId( "buffer" );
  algc2.setAlgorithmId( "native:buffer" );
  algc2.addParameterSources( QStringLiteral( "INPUT" ), QList< QgsProcessingModelChildParameterSource >() << QgsProcessingModelChildParameterSource::fromChildOutput( QStringLiteral( "filter" ), QStringLiteral( "VECTOR" ) ) );
  QMap<QString, QgsProcessingModelOutput> outputsc2;
  QgsProcessingModelOutput outc2( "BUFFER_OUTPUT" );
  outc2.setChildOutputName( "OUTPUT" );
  outputsc2.insert( QStringLiteral( "BUFFER_OUTPUT" ), outc2 );
  algc2.setModelOutputs( outputsc2 );
  model1.addChildAlgorithm( algc2 );
  // ...we want a complex branch, so add some more bits to the branch
  QgsProcessingModelChildAlgorithm algc3;
  algc3.setChildId( "buffer2" );
  algc3.setAlgorithmId( "native:buffer" );
  algc3.addParameterSources( QStringLiteral( "INPUT" ), QList< QgsProcessingModelChildParameterSource >() << QgsProcessingModelChildParameterSource::fromChildOutput( QStringLiteral( "buffer" ), QStringLiteral( "OUTPUT" ) ) );
  QMap<QString, QgsProcessingModelOutput> outputsc3;
  QgsProcessingModelOutput outc3( "BUFFER2_OUTPUT" );
  outc3.setChildOutputName( "OUTPUT" );
  outputsc3.insert( QStringLiteral( "BUFFER2_OUTPUT" ), outc3 );
  algc3.setModelOutputs( outputsc3 );
  model1.addChildAlgorithm( algc3 );
  QgsProcessingModelChildAlgorithm algc4;
  algc4.setChildId( "buffer3" );
  algc4.setAlgorithmId( "native:buffer" );
  algc4.addParameterSources( QStringLiteral( "INPUT" ), QList< QgsProcessingModelChildParameterSource >() << QgsProcessingModelChildParameterSource::fromChildOutput( QStringLiteral( "buffer" ), QStringLiteral( "OUTPUT" ) ) );
  QMap<QString, QgsProcessingModelOutput> outputsc4;
  QgsProcessingModelOutput outc4( "BUFFER3_OUTPUT" );
  outc4.setChildOutputName( "OUTPUT" );
  outputsc4.insert( QStringLiteral( "BUFFER3_OUTPUT" ), outc4 );
  algc4.setModelOutputs( outputsc4 );
  model1.addChildAlgorithm( algc4 );

  // now add some bits to the raster branch
  QgsProcessingModelChildAlgorithm algr2;
  algr2.setChildId( "fill2" );
  algr2.setAlgorithmId( "native:fillnodata" );
  algr2.addParameterSources( QStringLiteral( "INPUT" ), QList< QgsProcessingModelChildParameterSource >() << QgsProcessingModelChildParameterSource::fromChildOutput( QStringLiteral( "filter" ), QStringLiteral( "RASTER" ) ) );
  QMap<QString, QgsProcessingModelOutput> outputsr2;
  QgsProcessingModelOutput outr2( "RASTER_OUTPUT" );
  outr2.setChildOutputName( "OUTPUT" );
  outputsr2.insert( QStringLiteral( "RASTER_OUTPUT" ), outr2 );
  algr2.setModelOutputs( outputsr2 );
  model1.addChildAlgorithm( algr2 );

  // some more bits on the raster branch
  QgsProcessingModelChildAlgorithm algr3;
  algr3.setChildId( "fill3" );
  algr3.setAlgorithmId( "native:fillnodata" );
  algr3.addParameterSources( QStringLiteral( "INPUT" ), QList< QgsProcessingModelChildParameterSource >() << QgsProcessingModelChildParameterSource::fromChildOutput( QStringLiteral( "fill2" ), QStringLiteral( "OUTPUT" ) ) );
  QMap<QString, QgsProcessingModelOutput> outputsr3;
  QgsProcessingModelOutput outr3( "RASTER_OUTPUT2" );
  outr3.setChildOutputName( "OUTPUT" );
  outputsr3.insert( QStringLiteral( "RASTER_OUTPUT2" ), outr3 );
  algr3.setModelOutputs( outputsr3 );
  model1.addChildAlgorithm( algr3 );

  QgsProcessingModelChildAlgorithm algr4;
  algr4.setChildId( "fill4" );
  algr4.setAlgorithmId( "native:fillnodata" );
  algr4.addParameterSources( QStringLiteral( "INPUT" ), QList< QgsProcessingModelChildParameterSource >() << QgsProcessingModelChildParameterSource::fromChildOutput( QStringLiteral( "fill2" ), QStringLiteral( "OUTPUT" ) ) );
  QMap<QString, QgsProcessingModelOutput> outputsr4;
  QgsProcessingModelOutput outr4( "RASTER_OUTPUT3" );
  outr4.setChildOutputName( "OUTPUT" );
  outputsr4.insert( QStringLiteral( "RASTER_OUTPUT3" ), outr4 );
  algr4.setModelOutputs( outputsr4 );
  model1.addChildAlgorithm( algr4 );

  QgsProcessingFeedback feedback;
  QVariantMap params;
  // vector input
  params.insert( QStringLiteral( "LAYER" ), QStringLiteral( "v1" ) );
  params.insert( QStringLiteral( "buffer:BUFFER_OUTPUT" ), QgsProcessing::TEMPORARY_OUTPUT );
  params.insert( QStringLiteral( "buffer2:BUFFER2_OUTPUT" ), QgsProcessing::TEMPORARY_OUTPUT );
  params.insert( QStringLiteral( "buffer3:BUFFER3_OUTPUT" ), QgsProcessing::TEMPORARY_OUTPUT );
  params.insert( QStringLiteral( "fill2:RASTER_OUTPUT" ), QgsProcessing::TEMPORARY_OUTPUT );
  params.insert( QStringLiteral( "fill3:RASTER_OUTPUT2" ), QgsProcessing::TEMPORARY_OUTPUT );
  params.insert( QStringLiteral( "fill4:RASTER_OUTPUT3" ), QgsProcessing::TEMPORARY_OUTPUT );
  QVariantMap results = model1.run( params, context, &feedback );
  // we should get the vector branch outputs only
  QVERIFY( !results.value( QStringLiteral( "buffer:BUFFER_OUTPUT" ) ).toString().isEmpty() );
  QVERIFY( !results.value( QStringLiteral( "buffer2:BUFFER2_OUTPUT" ) ).toString().isEmpty() );
  QVERIFY( !results.value( QStringLiteral( "buffer3:BUFFER3_OUTPUT" ) ).toString().isEmpty() );
  QVERIFY( !results.contains( QStringLiteral( "fill2:RASTER_OUTPUT" ) ) );
  QVERIFY( !results.contains( QStringLiteral( "fill3:RASTER_OUTPUT2" ) ) );
  QVERIFY( !results.contains( QStringLiteral( "fill4:RASTER_OUTPUT3" ) ) );

  // raster input
  params.insert( QStringLiteral( "LAYER" ), QStringLiteral( "R1" ) );
  results = model1.run( params, context, &feedback );
  // we should get the raster branch outputs only
  QVERIFY( !results.value( QStringLiteral( "fill2:RASTER_OUTPUT" ) ).toString().isEmpty() );
  QVERIFY( !results.value( QStringLiteral( "fill3:RASTER_OUTPUT2" ) ).toString().isEmpty() );
  QVERIFY( !results.value( QStringLiteral( "fill4:RASTER_OUTPUT3" ) ).toString().isEmpty() );
  QVERIFY( !results.contains( QStringLiteral( "buffer:BUFFER_OUTPUT" ) ) );
  QVERIFY( !results.contains( QStringLiteral( "buffer2:BUFFER2_OUTPUT" ) ) );
  QVERIFY( !results.contains( QStringLiteral( "buffer3:BUFFER3_OUTPUT" ) ) );
}

void TestQgsProcessing::modelBranchPruningConditional()
{
  QgsProcessingContext context;

  context.expressionContext().appendScope( new QgsExpressionContextScope() );
  context.expressionContext().scope( 0 )->setVariable( QStringLiteral( "var1" ), 1 );
  context.expressionContext().scope( 0 )->setVariable( QStringLiteral( "var2" ), 0 );

  // test that model branches are trimmed for algorithms which depend on conditional branches
  QgsProcessingModelAlgorithm model1;

  // first add the filter by layer type alg
  QgsProcessingModelChildAlgorithm algc1;
  algc1.setChildId( "branch" );
  algc1.setAlgorithmId( "native:condition" );
  QVariantMap config;
  QVariantList conditions;
  QVariantMap cond1;
  cond1.insert( QStringLiteral( "name" ), QStringLiteral( "name1" ) );
  cond1.insert( QStringLiteral( "expression" ), QStringLiteral( "@var1" ) );
  conditions << cond1;
  QVariantMap cond2;
  cond2.insert( QStringLiteral( "name" ), QStringLiteral( "name2" ) );
  cond2.insert( QStringLiteral( "expression" ), QStringLiteral( "@var2" ) );
  conditions << cond2;
  config.insert( QStringLiteral( "conditions" ), conditions );
  algc1.setConfiguration( config );
  model1.addChildAlgorithm( algc1 );

  //then create some branches which come off this
  QgsProcessingModelChildAlgorithm algc2;
  algc2.setChildId( "exception" );
  algc2.setAlgorithmId( "native:raiseexception" );
  algc2.setDependencies( QList< QgsProcessingModelChildDependency >() << QgsProcessingModelChildDependency( QStringLiteral( "branch" ), QStringLiteral( "name1" ) ) );
  model1.addChildAlgorithm( algc2 );

  QgsProcessingModelChildAlgorithm algc3;
  algc2.setChildId( "exception" );
  algc3.setAlgorithmId( "native:raisewarning" );
  algc3.setDependencies( QList< QgsProcessingModelChildDependency >() << QgsProcessingModelChildDependency( QStringLiteral( "branch" ), QStringLiteral( "name2" ) ) );
  model1.addChildAlgorithm( algc3 );

  QgsProcessingFeedback feedback;
  QVariantMap params;
  bool ok = false;
  QVariantMap results = model1.run( params, context, &feedback, &ok );
  QVERIFY( !ok ); // the branch with the exception should be hit

  // flip the condition results
  context.expressionContext().scope( 0 )->setVariable( QStringLiteral( "var1" ), 0 );
  context.expressionContext().scope( 0 )->setVariable( QStringLiteral( "var2" ), 1 );

  results = model1.run( params, context, &feedback, &ok );
  QVERIFY( ok ); // the branch with the exception should NOT be hit
}

void TestQgsProcessing::modelWithProviderWithLimitedTypes()
{
  QgsApplication::processingRegistry()->addProvider( new DummyProvider4() );

  QgsProcessingModelAlgorithm alg( "test", "testGroup" );
  QgsProcessingModelChildAlgorithm algc1;
  algc1.setChildId( "cx1" );
  algc1.setAlgorithmId( "dummy4:alg1" );
  QMap<QString, QgsProcessingModelOutput> algc1outputs;
  QgsProcessingModelOutput algc1out1( QStringLiteral( "my_vector_output" ) );
  algc1out1.setChildId( "cx1" );
  algc1out1.setChildOutputName( "vector_dest" );
  algc1out1.setDescription( QStringLiteral( "my output" ) );
  algc1outputs.insert( QStringLiteral( "my_vector_output" ), algc1out1 );
  QgsProcessingModelOutput algc1out2( QStringLiteral( "my_raster_output" ) );
  algc1out2.setChildId( "cx1" );
  algc1out2.setChildOutputName( "raster_dest" );
  algc1out2.setDescription( QStringLiteral( "my output" ) );
  algc1outputs.insert( QStringLiteral( "my_raster_output" ), algc1out2 );
  QgsProcessingModelOutput algc1out3( QStringLiteral( "my_sink_output" ) );
  algc1out3.setChildId( "cx1" );
  algc1out3.setChildOutputName( "sink" );
  algc1out3.setDescription( QStringLiteral( "my output" ) );
  algc1outputs.insert( QStringLiteral( "my_sink_output" ), algc1out3 );
  algc1.setModelOutputs( algc1outputs );
  alg.addChildAlgorithm( algc1 );
  // verify that model has destination parameter created
  QCOMPARE( alg.destinationParameterDefinitions().count(), 3 );
  QCOMPARE( alg.destinationParameterDefinitions().at( 2 )->name(), QStringLiteral( "cx1:my_vector_output" ) );
  QCOMPARE( alg.destinationParameterDefinitions().at( 2 )->description(), QStringLiteral( "my output" ) );
  QCOMPARE( static_cast< const QgsProcessingDestinationParameter * >( alg.destinationParameterDefinitions().at( 2 ) )->originalProvider()->id(), QStringLiteral( "dummy4" ) );
  QCOMPARE( static_cast< const QgsProcessingParameterVectorDestination * >( alg.destinationParameterDefinitions().at( 2 ) )->supportedOutputVectorLayerExtensions(), QStringList() << QStringLiteral( "mif" ) );
  QCOMPARE( static_cast< const QgsProcessingParameterVectorDestination * >( alg.destinationParameterDefinitions().at( 2 ) )->defaultFileExtension(), QStringLiteral( "mif" ) );
  QVERIFY( static_cast< const QgsProcessingParameterVectorDestination * >( alg.destinationParameterDefinitions().at( 2 ) )->generateTemporaryDestination().endsWith( QLatin1String( ".mif" ) ) );
  QVERIFY( !static_cast< const QgsProcessingDestinationParameter * >( alg.destinationParameterDefinitions().at( 2 ) )->supportsNonFileBasedOutput() );

  QCOMPARE( alg.destinationParameterDefinitions().at( 0 )->name(), QStringLiteral( "cx1:my_raster_output" ) );
  QCOMPARE( alg.destinationParameterDefinitions().at( 0 )->description(), QStringLiteral( "my output" ) );
  QCOMPARE( static_cast< const QgsProcessingDestinationParameter * >( alg.destinationParameterDefinitions().at( 0 ) )->originalProvider()->id(), QStringLiteral( "dummy4" ) );
  QCOMPARE( static_cast< const QgsProcessingParameterRasterDestination * >( alg.destinationParameterDefinitions().at( 0 ) )->supportedOutputRasterLayerExtensions(), QStringList() << QStringLiteral( "mig" ) );
  QCOMPARE( static_cast< const QgsProcessingParameterRasterDestination * >( alg.destinationParameterDefinitions().at( 0 ) )->defaultFileExtension(), QStringLiteral( "mig" ) );
  QVERIFY( static_cast< const QgsProcessingParameterRasterDestination * >( alg.destinationParameterDefinitions().at( 0 ) )->generateTemporaryDestination().endsWith( QLatin1String( ".mig" ) ) );
  QVERIFY( !static_cast< const QgsProcessingDestinationParameter * >( alg.destinationParameterDefinitions().at( 0 ) )->supportsNonFileBasedOutput() );

  QCOMPARE( alg.destinationParameterDefinitions().at( 1 )->name(), QStringLiteral( "cx1:my_sink_output" ) );
  QCOMPARE( alg.destinationParameterDefinitions().at( 1 )->description(), QStringLiteral( "my output" ) );
  QCOMPARE( static_cast< const QgsProcessingDestinationParameter * >( alg.destinationParameterDefinitions().at( 1 ) )->originalProvider()->id(), QStringLiteral( "dummy4" ) );
  QCOMPARE( static_cast< const QgsProcessingParameterFeatureSink * >( alg.destinationParameterDefinitions().at( 1 ) )->supportedOutputVectorLayerExtensions(), QStringList() << QStringLiteral( "mif" ) );
  QCOMPARE( static_cast< const QgsProcessingParameterFeatureSink * >( alg.destinationParameterDefinitions().at( 1 ) )->defaultFileExtension(), QStringLiteral( "mif" ) );
  QVERIFY( static_cast< const QgsProcessingParameterFeatureSink * >( alg.destinationParameterDefinitions().at( 1 ) )->generateTemporaryDestination().endsWith( QLatin1String( ".mif" ) ) );
  QVERIFY( !static_cast< const QgsProcessingDestinationParameter * >( alg.destinationParameterDefinitions().at( 1 ) )->supportsNonFileBasedOutput() );
}

void TestQgsProcessing::modelVectorOutputIsCompatibleType()
{
  // IMPORTANT: This method is intended to be "permissive" rather than "restrictive".
  // I.e. we only reject outputs which we know can NEVER be acceptable, but
  // if there's doubt then we default to returning true.

  // empty acceptable type list = all are compatible
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( QList<int>(), QgsProcessing::TypeVector ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( QList<int>(), QgsProcessing::TypeVectorAnyGeometry ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( QList<int>(), QgsProcessing::TypeVectorPoint ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( QList<int>(), QgsProcessing::TypeVectorLine ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( QList<int>(), QgsProcessing::TypeVectorPolygon ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( QList<int>(), QgsProcessing::TypeMapLayer ) );

  // accept any vector
  QList< int > dataTypes;
  dataTypes << QgsProcessing::TypeVector;
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVector ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorAnyGeometry ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorPoint ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorLine ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorPolygon ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeMapLayer ) );

  // accept any vector with geometry
  dataTypes.clear();
  dataTypes << QgsProcessing::TypeVectorAnyGeometry;
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVector ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorAnyGeometry ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorPoint ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorLine ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorPolygon ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeMapLayer ) );

  // accept any point vector
  dataTypes.clear();
  dataTypes << QgsProcessing::TypeVectorPoint;
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVector ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorAnyGeometry ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorPoint ) );
  QVERIFY( !QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorLine ) );
  QVERIFY( !QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorPolygon ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeMapLayer ) );

  // accept any line vector
  dataTypes.clear();
  dataTypes << QgsProcessing::TypeVectorLine;
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVector ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorAnyGeometry ) );
  QVERIFY( !QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorPoint ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorLine ) );
  QVERIFY( !QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorPolygon ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeMapLayer ) );

  // accept any polygon vector
  dataTypes.clear();
  dataTypes << QgsProcessing::TypeVectorPolygon;
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVector ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorAnyGeometry ) );
  QVERIFY( !QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorPoint ) );
  QVERIFY( !QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorLine ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorPolygon ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeMapLayer ) );

  // accept any map layer
  dataTypes.clear();
  dataTypes << QgsProcessing::TypeMapLayer;
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVector ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorAnyGeometry ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorPoint ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorLine ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeVectorPolygon ) );
  QVERIFY( QgsProcessingModelAlgorithm::vectorOutputIsCompatibleType( dataTypes, QgsProcessing::TypeMapLayer ) );
}

void TestQgsProcessing::modelAcceptableValues()
{
  QgsProcessingModelAlgorithm m;
  QgsProcessingModelParameter stringParam1( "string" );
  m.addModelParameter( new QgsProcessingParameterString( "string" ), stringParam1 );

  QgsProcessingModelParameter stringParam2( "string2" );
  m.addModelParameter( new QgsProcessingParameterString( "string2" ), stringParam2 );

  QgsProcessingModelParameter numParam( "number" );
  m.addModelParameter( new QgsProcessingParameterNumber( "number" ), numParam );

  QgsProcessingModelParameter tableFieldParam( "field" );
  m.addModelParameter( new QgsProcessingParameterField( "field" ), tableFieldParam );

  QgsProcessingModelParameter fileParam( "file" );
  m.addModelParameter( new QgsProcessingParameterFile( "file" ), fileParam );

  // test single types
  QgsProcessingModelChildParameterSources sources = m.availableSourcesForChild( QString(), QStringList() << "number" );
  QCOMPARE( sources.count(), 1 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "number" ) );
  sources = m.availableSourcesForChild( QString(), QStringList() << "field" );
  QCOMPARE( sources.count(), 1 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "field" ) );
  sources = m.availableSourcesForChild( QString(), QStringList() << "file" );
  QCOMPARE( sources.count(), 1 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "file" ) );

  // test multiple types
  sources = m.availableSourcesForChild( QString(), QStringList() << "string" << "number" << "file" );
  QCOMPARE( sources.count(), 4 );
  QSet< QString > res;
  res << sources.at( 0 ).parameterName();
  res << sources.at( 1 ).parameterName();
  res << sources.at( 2 ).parameterName();
  res << sources.at( 3 ).parameterName();

  QCOMPARE( res, QSet< QString >() << QStringLiteral( "string" )
            << QStringLiteral( "string2" )
            << QStringLiteral( "number" )
            << QStringLiteral( "file" ) );

  // check outputs
  QgsProcessingModelChildAlgorithm alg2c1;
  alg2c1.setChildId( "cx1" );
  alg2c1.setAlgorithmId( "native:centroids" );
  m.addChildAlgorithm( alg2c1 );

  sources = m.availableSourcesForChild( QString(), QStringList(), QStringList() << "string" << "outputVector" );
  QCOMPARE( sources.count(), 1 );
  res.clear();
  res << sources.at( 0 ).outputChildId() + ':' + sources.at( 0 ).outputName();
  QCOMPARE( res, QSet< QString >() << "cx1:OUTPUT" );

  // with dependencies between child algs
  QgsProcessingModelChildAlgorithm alg2c2;
  alg2c2.setChildId( "cx2" );
  alg2c2.setAlgorithmId( "native:centroids" );
  alg2c2.setDependencies( QList< QgsProcessingModelChildDependency >() << QgsProcessingModelChildDependency( "cx1" ) );
  m.addChildAlgorithm( alg2c2 );
  sources = m.availableSourcesForChild( QString(), QStringList(), QStringList() << "string" << "outputVector" );
  QCOMPARE( sources.count(), 2 );
  res.clear();
  res << sources.at( 0 ).outputChildId() + ':' + sources.at( 0 ).outputName();
  res << sources.at( 1 ).outputChildId() + ':' + sources.at( 1 ).outputName();
  QCOMPARE( res, QSet< QString >() << "cx1:OUTPUT" << "cx2:OUTPUT" );

  sources = m.availableSourcesForChild( QStringLiteral( "cx1" ), QStringList(), QStringList() << "string" << "outputVector" );
  QCOMPARE( sources.count(), 0 );

  sources = m.availableSourcesForChild( QString( "cx2" ), QStringList(), QStringList() << "string" << "outputVector" );
  QCOMPARE( sources.count(), 1 );
  res.clear();
  res << sources.at( 0 ).outputChildId() + ':' + sources.at( 0 ).outputName();
  QCOMPARE( res, QSet< QString >() << "cx1:OUTPUT" );

  // test limiting by data types
  QgsProcessingModelAlgorithm m2;
  QgsProcessingModelParameter vlInput( "vl" );
  // with no limit on data types
  m2.addModelParameter( new QgsProcessingParameterVectorLayer( "vl" ), vlInput );
  QgsProcessingModelParameter fsInput( "fs" );
  m2.addModelParameter( new QgsProcessingParameterFeatureSource( "fs" ), fsInput );

  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source" );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVectorPoint );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVector );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVectorAnyGeometry );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeMapLayer );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );

  // inputs are limited to vector layers
  m2.removeModelParameter( vlInput.parameterName() );
  m2.removeModelParameter( fsInput.parameterName() );
  m2.addModelParameter( new QgsProcessingParameterVectorLayer( "vl", QString(), QList<int>() << QgsProcessing::TypeVector ), vlInput );
  m2.addModelParameter( new QgsProcessingParameterFeatureSource( "fs", QString(), QList<int>() << QgsProcessing::TypeVector ), fsInput );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source" );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVectorPoint );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVector );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVectorAnyGeometry );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeMapLayer );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );

  // inputs are limited to vector layers with geometries
  m2.removeModelParameter( vlInput.parameterName() );
  m2.removeModelParameter( fsInput.parameterName() );
  m2.addModelParameter( new QgsProcessingParameterVectorLayer( "vl", QString(), QList<int>() << QgsProcessing::TypeVectorAnyGeometry ), vlInput );
  m2.addModelParameter( new QgsProcessingParameterFeatureSource( "fs", QString(), QList<int>() << QgsProcessing::TypeVectorAnyGeometry ), fsInput );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source" );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVectorPoint );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVector );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVectorAnyGeometry );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeMapLayer );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );

  // inputs are limited to vector layers with lines
  m2.removeModelParameter( vlInput.parameterName() );
  m2.removeModelParameter( fsInput.parameterName() );
  m2.addModelParameter( new QgsProcessingParameterVectorLayer( "vl", QString(), QList<int>() << QgsProcessing::TypeVectorLine ), vlInput );
  m2.addModelParameter( new QgsProcessingParameterFeatureSource( "fs", QString(), QList<int>() << QgsProcessing::TypeVectorLine ), fsInput );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source" );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVectorPoint );
  QCOMPARE( sources.count(), 0 );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVectorPolygon );
  QCOMPARE( sources.count(), 0 );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVectorLine );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVector );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeVectorAnyGeometry );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
  sources = m2.availableSourcesForChild( QString(), QStringList() << "vector" << "source", QStringList(), QList<int>() << QgsProcessing::TypeMapLayer );
  QCOMPARE( sources.count(), 2 );
  QCOMPARE( sources.at( 0 ).parameterName(), QStringLiteral( "fs" ) );
  QCOMPARE( sources.at( 1 ).parameterName(), QStringLiteral( "vl" ) );
}

void TestQgsProcessing::modelValidate()
{
  QgsProcessingModelAlgorithm m;
  QStringList errors;
  QVERIFY( !m.validate( errors ) );
  QCOMPARE( errors.size(), 1 );
  QCOMPARE( errors.at( 0 ), QStringLiteral( "Model does not contain any algorithms" ) );

  QgsProcessingModelParameter stringParam1( "string" );
  m.addModelParameter( new QgsProcessingParameterString( "string" ), stringParam1 );
  QgsProcessingModelChildAlgorithm alg2c1;
  alg2c1.setChildId( "cx1" );
  alg2c1.setAlgorithmId( "native:centroids" );
  alg2c1.setDescription( QStringLiteral( "centroids" ) );
  m.addChildAlgorithm( alg2c1 );

  QVERIFY( !m.validateChildAlgorithm( QStringLiteral( "cx1" ), errors ) );
  QCOMPARE( errors.size(), 2 );
  QCOMPARE( errors.at( 0 ), QStringLiteral( "Parameter <i>INPUT</i> is mandatory" ) );
  QCOMPARE( errors.at( 1 ), QStringLiteral( "Parameter <i>ALL_PARTS</i> is mandatory" ) );

  QVERIFY( !m.validate( errors ) );
  QCOMPARE( errors.size(), 2 );
  QCOMPARE( errors.at( 0 ), QStringLiteral( "<b>centroids</b>: Parameter <i>INPUT</i> is mandatory" ) );
  QCOMPARE( errors.at( 1 ), QStringLiteral( "<b>centroids</b>: Parameter <i>ALL_PARTS</i> is mandatory" ) );

  QgsProcessingModelChildParameterSource badSource;
  badSource.setSource( QgsProcessingModelChildParameterSource::StaticValue );
  badSource.setStaticValue( 56 );
  m.childAlgorithm( QStringLiteral( "cx1" ) ).addParameterSources( QStringLiteral( "INPUT" ), QList< QgsProcessingModelChildParameterSource >() << badSource );

  QVERIFY( !m.validateChildAlgorithm( QStringLiteral( "cx1" ), errors ) );
  QCOMPARE( errors.size(), 2 );
  QCOMPARE( errors.at( 0 ), QStringLiteral( "Value for <i>INPUT</i> is not acceptable for this parameter" ) );
  QCOMPARE( errors.at( 1 ), QStringLiteral( "Parameter <i>ALL_PARTS</i> is mandatory" ) );

  QgsProcessingModelChildParameterSource goodSource;
  goodSource.setSource( QgsProcessingModelChildParameterSource::Expression );
  m.childAlgorithm( QStringLiteral( "cx1" ) ).addParameterSources( QStringLiteral( "ALL_PARTS" ), QList< QgsProcessingModelChildParameterSource >() << goodSource );

  QVERIFY( !m.validateChildAlgorithm( QStringLiteral( "cx1" ), errors ) );
  QCOMPARE( errors.size(), 1 );
  QCOMPARE( errors.at( 0 ), QStringLiteral( "Value for <i>INPUT</i> is not acceptable for this parameter" ) );

  badSource.setSource( QgsProcessingModelChildParameterSource::ChildOutput );
  badSource.setOutputChildId( QStringLiteral( "cc" ) );
  m.childAlgorithm( QStringLiteral( "cx1" ) ).addParameterSources( QStringLiteral( "INPUT" ), QList< QgsProcessingModelChildParameterSource >() << badSource );

  QVERIFY( !m.validateChildAlgorithm( QStringLiteral( "cx1" ), errors ) );
  QCOMPARE( errors.size(), 1 );
  QCOMPARE( errors.at( 0 ), QStringLiteral( "Child algorithm <i>cc</i> used for parameter <i>INPUT</i> does not exist" ) );

  badSource.setSource( QgsProcessingModelChildParameterSource::ModelParameter );
  badSource.setParameterName( QStringLiteral( "cc" ) );
  m.childAlgorithm( QStringLiteral( "cx1" ) ).addParameterSources( QStringLiteral( "INPUT" ), QList< QgsProcessingModelChildParameterSource >() << badSource );

  QVERIFY( !m.validateChildAlgorithm( QStringLiteral( "cx1" ), errors ) );
  QCOMPARE( errors.size(), 1 );
  QCOMPARE( errors.at( 0 ), QStringLiteral( "Model input <i>cc</i> used for parameter <i>INPUT</i> does not exist" ) );

  goodSource.setSource( QgsProcessingModelChildParameterSource::StaticValue );
  goodSource.setStaticValue( QString( QStringLiteral( TEST_DATA_DIR ) + "/polys.shp" ) );
  m.childAlgorithm( QStringLiteral( "cx1" ) ).addParameterSources( QStringLiteral( "INPUT" ), QList< QgsProcessingModelChildParameterSource >() << goodSource );

  QVERIFY( m.validateChildAlgorithm( QStringLiteral( "cx1" ), errors ) );
  QCOMPARE( errors.size(), 0 );

  QVERIFY( m.validate( errors ) );
  QCOMPARE( errors.size(), 0 );
}

void TestQgsProcessing::modelInputs()
{
  QgsProcessingModelAlgorithm m;

  // add a bunch of inputs
  QgsProcessingModelParameter stringParam1( "string" );
  m.addModelParameter( new QgsProcessingParameterString( "string" ), stringParam1 );

  QgsProcessingModelParameter stringParam2( "a string" );
  m.addModelParameter( new QgsProcessingParameterString( "a string" ), stringParam2 );

  QgsProcessingModelParameter stringParam3( "cc string" );
  m.addModelParameter( new QgsProcessingParameterString( "cc string" ), stringParam3 );

  // set specific input order for parameters
  m.setParameterOrder( QStringList() << "cc string" << "a string" );

  QgsProcessingModelAlgorithm m2;
  m2.loadVariant( m.toVariant() );
  QCOMPARE( m2.orderedParameters().count(), 3 );
  QCOMPARE( m2.orderedParameters().at( 0 ).parameterName(), QStringLiteral( "cc string" ) );
  QCOMPARE( m2.orderedParameters().at( 1 ).parameterName(), QStringLiteral( "a string" ) );
  QCOMPARE( m2.orderedParameters().at( 2 ).parameterName(), QStringLiteral( "string" ) );

  QCOMPARE( m2.parameterDefinitions().at( 0 )->name(), QStringLiteral( "cc string" ) );
  QCOMPARE( m2.parameterDefinitions().at( 1 )->name(), QStringLiteral( "a string" ) );
  QCOMPARE( m2.parameterDefinitions().at( 2 )->name(), QStringLiteral( "string" ) );
}

void TestQgsProcessing::modelDependencies()
{
  QgsProcessingModelChildDependency dep( QStringLiteral( "childId" ), QStringLiteral( "branch" ) );

  QCOMPARE( dep.childId, QStringLiteral( "childId" ) );
  QCOMPARE( dep.conditionalBranch, QStringLiteral( "branch" ) );

  QVariant v = dep.toVariant();
  QgsProcessingModelChildDependency dep2;
  QVERIFY( dep2.loadVariant( v.toMap() ) );

  QCOMPARE( dep2.childId, QStringLiteral( "childId" ) );
  QCOMPARE( dep2.conditionalBranch, QStringLiteral( "branch" ) );

  QVERIFY( dep == dep2 );
  QVERIFY( !( dep != dep2 ) );
  dep2.conditionalBranch = QStringLiteral( "b" );

  QVERIFY( !( dep == dep2 ) );
  QVERIFY( dep != dep2 );
  dep2.conditionalBranch = QStringLiteral( "branch" );
  dep2.childId = QStringLiteral( "c" );
  QVERIFY( !( dep == dep2 ) );
  QVERIFY( dep != dep2 );
  dep2.childId = QStringLiteral( "childId" );
  QVERIFY( dep == dep2 );
  QVERIFY( !( dep != dep2 ) );
}

void TestQgsProcessing::tempUtils()
{
  QString tempFolder = QgsProcessingUtils::tempFolder();
  // tempFolder should remain constant for session
  QCOMPARE( QgsProcessingUtils::tempFolder(), tempFolder );

  QString tempFile1 = QgsProcessingUtils::generateTempFilename( "test.txt" );
  QVERIFY( tempFile1.endsWith( "test.txt" ) );
  QVERIFY( tempFile1.startsWith( tempFolder ) );

  // expect a different file
  QString tempFile2 = QgsProcessingUtils::generateTempFilename( "test.txt" );
  QVERIFY( tempFile1 != tempFile2 );
  QVERIFY( tempFile2.endsWith( "test.txt" ) );
  QVERIFY( tempFile2.startsWith( tempFolder ) );

  // invalid characters
  QString tempFile3 = QgsProcessingUtils::generateTempFilename( "mybad:file.txt" );
  QVERIFY( tempFile3.endsWith( "mybad_file.txt" ) );
  QVERIFY( tempFile3.startsWith( tempFolder ) );

  // change temp folder in the settings
  std::unique_ptr< QTemporaryDir > dir = qgis::make_unique< QTemporaryDir >();
  const QString tempDirPath = dir->path();
  dir.reset();

  QgsSettings settings;
  QString alternative_tempFolder1 = tempDirPath + QStringLiteral( "/alternative_temp_test_one" );
  settings.setValue( QStringLiteral( "Processing/Configuration/TEMP_PATH2" ), alternative_tempFolder1 );
  // check folder and if it's constant with alternative temp folder 1
  tempFolder = QgsProcessingUtils::tempFolder();
  QCOMPARE( tempFolder.left( alternative_tempFolder1.length() ), alternative_tempFolder1 );
  QCOMPARE( QgsProcessingUtils::tempFolder(), tempFolder );
  // create file
  QString alternativeTempFile1 = QgsProcessingUtils::generateTempFilename( "alternative_temptest.txt" );
  QVERIFY( alternativeTempFile1.endsWith( "alternative_temptest.txt" ) );
  QVERIFY( alternativeTempFile1.startsWith( tempFolder ) );
  QVERIFY( alternativeTempFile1.startsWith( alternative_tempFolder1 ) );
  // change temp folder in the settings again
  QString alternative_tempFolder2 =  tempDirPath + QStringLiteral( "/alternative_temp_test_two" );
  settings.setValue( QStringLiteral( "Processing/Configuration/TEMP_PATH2" ), alternative_tempFolder2 );
  // check folder and if it's constant constant with alternative temp folder 2
  tempFolder = QgsProcessingUtils::tempFolder();
  QCOMPARE( tempFolder.left( alternative_tempFolder2.length() ), alternative_tempFolder2 );
  QCOMPARE( QgsProcessingUtils::tempFolder(), tempFolder );
  // create file
  QString alternativeTempFile2 = QgsProcessingUtils::generateTempFilename( "alternative_temptest.txt" );
  QVERIFY( alternativeTempFile2.endsWith( "alternative_temptest.txt" ) );
  QVERIFY( alternativeTempFile2.startsWith( tempFolder ) );
  QVERIFY( alternativeTempFile2.startsWith( alternative_tempFolder2 ) );
  settings.setValue( QStringLiteral( "Processing/Configuration/TEMP_PATH2" ), QString() );

}

void TestQgsProcessing::convertCompatible()
{
  // start with a compatible shapefile
  QString testDataDir = QStringLiteral( TEST_DATA_DIR ) + '/'; //defined in CmakeLists.txt
  QString vector = testDataDir + "points.shp";
  QgsVectorLayer *layer = new QgsVectorLayer( vector, "vl" );
  QgsProject p;
  p.addMapLayer( layer );

  QgsProcessingContext context;
  context.setProject( &p );
  QgsProcessingFeedback feedback;
  QString out = QgsProcessingUtils::convertToCompatibleFormat( layer, false, QStringLiteral( "test" ), QStringList() << "shp", QString( "shp" ), context, &feedback );
  // layer should be returned unchanged - underlying source is compatible
  QCOMPARE( out, layer->source() );

  QString layerName;
  out = QgsProcessingUtils::convertToCompatibleFormatAndLayerName( layer, false, QStringLiteral( "test" ), QStringList() << "shp", QString( "shp" ), context, &feedback, layerName );
  // layer should be returned unchanged - underlying source is compatible
  QCOMPARE( out, layer->source() );
  QCOMPARE( layerName, QString() );

  // path with layer suffix
  QString vectorWithLayer = testDataDir + "points.shp|layername=points";
  QgsVectorLayer *layer2 = new QgsVectorLayer( vectorWithLayer, "vl" );
  p.addMapLayer( layer2 );
  out = QgsProcessingUtils::convertToCompatibleFormat( layer2, false, QStringLiteral( "test" ), QStringList() << "shp", QString( "shp" ), context, &feedback );
  // layer should be returned unchanged - underlying source is compatible
  QCOMPARE( out, vector );
  out = QgsProcessingUtils::convertToCompatibleFormatAndLayerName( layer2, false, QStringLiteral( "test" ), QStringList() << "shp", QString( "shp" ), context, &feedback, layerName );
  QCOMPARE( out, vector );
  QCOMPARE( layerName, QStringLiteral( "points" ) );

  // don't include shp as compatible type
  out = QgsProcessingUtils::convertToCompatibleFormat( layer, false, QStringLiteral( "test" ), QStringList() << "tab", QString( "tab" ), context, &feedback );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".tab" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );

  // make sure all features are copied
  std::unique_ptr< QgsVectorLayer > t = qgis::make_unique< QgsVectorLayer >( out, "vl2" );
  QCOMPARE( layer->featureCount(), t->featureCount() );
  QCOMPARE( layer->crs().authid(), QStringLiteral( "EPSG:4326" ) );

  out = QgsProcessingUtils::convertToCompatibleFormatAndLayerName( layer, false, QStringLiteral( "test2" ), QStringList() << "tab", QString( "tab" ), context, &feedback, layerName );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".tab" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  QCOMPARE( layerName, QString() );

  // use a selection - this will require translation
  QgsFeatureIds ids;
  QgsFeature f;
  QgsFeatureIterator it = layer->getFeatures();
  it.nextFeature( f );
  ids.insert( f.id() );
  it.nextFeature( f );
  ids.insert( f.id() );

  layer->selectByIds( ids );
  out = QgsProcessingUtils::convertToCompatibleFormat( layer, true, QStringLiteral( "test" ), QStringList() << "tab", QString( "tab" ), context, &feedback );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".tab" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  t = qgis::make_unique< QgsVectorLayer >( out, "vl2" );
  QCOMPARE( t->featureCount(), static_cast< long >( ids.count() ) );

  out = QgsProcessingUtils::convertToCompatibleFormatAndLayerName( layer, true, QStringLiteral( "test" ), QStringList() << "tab", QString( "tab" ), context, &feedback, layerName );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".tab" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  t = qgis::make_unique< QgsVectorLayer >( out, "vl2" );
  QCOMPARE( t->featureCount(), static_cast< long >( ids.count() ) );
  QCOMPARE( layerName, QString() );

  // using a selection but existing format - will still require translation
  out = QgsProcessingUtils::convertToCompatibleFormat( layer, true, QStringLiteral( "test" ), QStringList() << "shp", QString( "shp" ), context, &feedback );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  t = qgis::make_unique< QgsVectorLayer >( out, "vl2" );
  QCOMPARE( t->featureCount(), static_cast< long >( ids.count() ) );

  out = QgsProcessingUtils::convertToCompatibleFormatAndLayerName( layer, true, QStringLiteral( "test" ), QStringList() << "shp", QString( "shp" ), context, &feedback, layerName );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  t = qgis::make_unique< QgsVectorLayer >( out, "vl2" );
  QCOMPARE( t->featureCount(), static_cast< long >( ids.count() ) );
  QCOMPARE( layerName, QString() );

  // using a feature filter -- this will require translation
  layer->setSubsetString( "1 or 2" );
  out = QgsProcessingUtils::convertToCompatibleFormat( layer, false, QStringLiteral( "test" ), QStringList() << "shp", QString( "shp" ), context, &feedback );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  t = qgis::make_unique< QgsVectorLayer >( out, "vl2" );
  QCOMPARE( t->featureCount(), layer->featureCount() );

  out = QgsProcessingUtils::convertToCompatibleFormatAndLayerName( layer, false, QStringLiteral( "test" ), QStringList() << "shp", QString( "shp" ), context, &feedback, layerName );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  t = qgis::make_unique< QgsVectorLayer >( out, "vl2" );
  QCOMPARE( t->featureCount(), layer->featureCount() );
  QCOMPARE( layerName, QString() );
  layer->setSubsetString( QString() );

  // using GDAL's virtual I/O (/vsizip/, etc.)
  QString vsiPath = "/vsizip/" + testDataDir + "zip/points2.zip/points.shp";
  QgsVectorLayer *vsiLayer = new QgsVectorLayer( vsiPath, "vl" );
  p.addMapLayer( vsiLayer );
  out = QgsProcessingUtils::convertToCompatibleFormat( vsiLayer, false, QStringLiteral( "test" ), QStringList() << "shp", QString( "shp" ), context, &feedback );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( !out.contains( "/vsizip" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  t = qgis::make_unique< QgsVectorLayer >( out, "vl2" );
  QCOMPARE( t->featureCount(), layer->featureCount() );

  out = QgsProcessingUtils::convertToCompatibleFormatAndLayerName( vsiLayer, false, QStringLiteral( "test" ), QStringList() << "shp", QString( "shp" ), context, &feedback, layerName );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( !out.contains( "/vsizip" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  t = qgis::make_unique< QgsVectorLayer >( out, "vl2" );
  QCOMPARE( t->featureCount(), layer->featureCount() );
  QCOMPARE( layerName, QString() );

  // non-OGR source -- must be translated, regardless of extension. (e.g. delimited text provider handles CSV very different to OGR!)
  std::unique_ptr< QgsVectorLayer > memLayer = qgis::make_unique< QgsVectorLayer> ( "Point", "v1", "memory" );
  for ( int i = 1; i < 6; ++i )
  {
    QgsFeature f( i );
    f.setGeometry( QgsGeometry( new QgsPoint( 1, 2 ) ) );
    memLayer->dataProvider()->addFeatures( QgsFeatureList() << f );
  }
  out = QgsProcessingUtils::convertToCompatibleFormat( memLayer.get(), false, QStringLiteral( "test" ), QStringList() << "shp", QString( "shp" ), context, &feedback );
  QVERIFY( out != memLayer->source() );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  t = qgis::make_unique< QgsVectorLayer >( out, "vl2" );
  QCOMPARE( t->featureCount(), memLayer->featureCount() );

  out = QgsProcessingUtils::convertToCompatibleFormatAndLayerName( memLayer.get(), false, QStringLiteral( "test" ), QStringList() << "shp", QString( "shp" ), context, &feedback, layerName );
  QVERIFY( out != memLayer->source() );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  t = qgis::make_unique< QgsVectorLayer >( out, "vl2" );
  QCOMPARE( t->featureCount(), memLayer->featureCount() );
  QCOMPARE( layerName, QString() );

  //delimited text -- must be translated, regardless of extension. (delimited text provider handles CSV very different to OGR!)
  QString csvPath = "file://" + testDataDir + "delimitedtext/testpt.csv?type=csv&useHeader=No&detectTypes=yes&xyDms=yes&geomType=none&subsetIndex=no&watchFile=no";
  std::unique_ptr< QgsVectorLayer > csvLayer = qgis::make_unique< QgsVectorLayer >( csvPath, "vl", "delimitedtext" );
  QVERIFY( csvLayer->isValid() );
  out = QgsProcessingUtils::convertToCompatibleFormat( csvLayer.get(), false, QStringLiteral( "test" ), QStringList() << "csv", QString( "csv" ), context, &feedback );
  QVERIFY( out != csvLayer->source() );
  QVERIFY( out.endsWith( ".csv" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  t = qgis::make_unique< QgsVectorLayer >( out, "vl2" );
  QCOMPARE( t->featureCount(), csvLayer->featureCount() );

  out = QgsProcessingUtils::convertToCompatibleFormatAndLayerName( csvLayer.get(), false, QStringLiteral( "test" ), QStringList() << "csv", QString( "csv" ), context, &feedback, layerName );
  QVERIFY( out != csvLayer->source() );
  QVERIFY( out.endsWith( ".csv" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  t = qgis::make_unique< QgsVectorLayer >( out, "vl2" );
  QCOMPARE( t->featureCount(), csvLayer->featureCount() );
  QCOMPARE( layerName, QString() );

  // geopackage with layer
  QString gpkgPath = testDataDir + "points_gpkg.gpkg|layername=points_gpkg";
  std::unique_ptr< QgsVectorLayer > gpkgLayer = qgis::make_unique< QgsVectorLayer >( gpkgPath, "vl" );
  QVERIFY( gpkgLayer->isValid() );
  out = QgsProcessingUtils::convertToCompatibleFormat( gpkgLayer.get(), false, QStringLiteral( "test" ), QStringList() << "gpkg" << "shp", QString( "shp" ), context, &feedback );
  // layer must be translated -- we do not know if external tool can handle picking the correct layer automatically
  QCOMPARE( out, QString( testDataDir + QStringLiteral( "points_gpkg.gpkg" ) ) );
  gpkgPath = testDataDir + "points_gpkg.gpkg|layername=points_small";
  gpkgLayer = qgis::make_unique< QgsVectorLayer >( gpkgPath, "vl" );
  QVERIFY( gpkgLayer->isValid() );
  out = QgsProcessingUtils::convertToCompatibleFormat( gpkgLayer.get(), false, QStringLiteral( "test" ), QStringList() << "gpkg" << "shp", QString( "shp" ), context, &feedback );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );

  gpkgPath = testDataDir + "points_gpkg.gpkg|layername=points_gpkg";
  gpkgLayer = qgis::make_unique< QgsVectorLayer >( gpkgPath, "vl" );
  QVERIFY( gpkgLayer->isValid() );
  out = QgsProcessingUtils::convertToCompatibleFormatAndLayerName( gpkgLayer.get(), false, QStringLiteral( "test" ), QStringList() << "gpkg" << "shp", QString( "shp" ), context, &feedback, layerName );
  // layer SHOULD NOT be translated -- in this case we know that the external tool can handle specifying the correct layer
  QCOMPARE( out, QString( testDataDir + QStringLiteral( "points_gpkg.gpkg" ) ) );
  QCOMPARE( layerName, QStringLiteral( "points_gpkg" ) );
  gpkgPath = testDataDir + "points_gpkg.gpkg|layername=points_small";
  gpkgLayer = qgis::make_unique< QgsVectorLayer >( gpkgPath, "vl" );
  QVERIFY( gpkgLayer->isValid() );
  out = QgsProcessingUtils::convertToCompatibleFormatAndLayerName( gpkgLayer.get(), false, QStringLiteral( "test" ), QStringList() << "gpkg" << "shp", QString( "shp" ), context, &feedback, layerName );
  QCOMPARE( out, QString( testDataDir + QStringLiteral( "points_gpkg.gpkg" ) ) );
  QCOMPARE( layerName, QStringLiteral( "points_small" ) );

  // also test evaluating parameter to compatible format
  std::unique_ptr< QgsProcessingParameterDefinition > def( new QgsProcessingParameterFeatureSource( QStringLiteral( "source" ) ) );
  QVariantMap params;
  params.insert( QStringLiteral( "source" ), QgsProcessingFeatureSourceDefinition( layer->id(), false ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPath( def.get(), params, context, QStringList() << "shp", QString( "shp" ), &feedback );
  QCOMPARE( out, QString( testDataDir + "points.shp" ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPathAndLayerName( def.get(), params, context, QStringList() << "shp", QString( "shp" ), &feedback, &layerName );
  QCOMPARE( out, QString( testDataDir + "points.shp" ) );
  QCOMPARE( layerName, QString() );

  // incompatible format, will be converted
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPath( def.get(), params, context, QStringList() << "tab", QString( "tab" ), &feedback );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".tab" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPathAndLayerName( def.get(), params, context, QStringList() << "tab", QString( "tab" ), &feedback, &layerName );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".tab" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  QCOMPARE( layerName, QString() );

  // layer as input
  params.insert( QStringLiteral( "source" ), QVariant::fromValue( layer ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPath( def.get(), params, context, QStringList() << "shp", QString( "shp" ), &feedback );
  QCOMPARE( out, QString( testDataDir + "points.shp" ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPathAndLayerName( def.get(), params, context, QStringList() << "shp", QString( "shp" ), &feedback, &layerName );
  QCOMPARE( out, QString( testDataDir + "points.shp" ) );
  QCOMPARE( layerName, QString() );

  // incompatible format, will be converted
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPath( def.get(), params, context, QStringList() << "tab", QString( "tab" ), &feedback );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".tab" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPathAndLayerName( def.get(), params, context, QStringList() << "tab", QString( "tab" ), &feedback, &layerName );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".tab" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  QCOMPARE( layerName, QString() );

  // selected only, will force export
  params.insert( QStringLiteral( "source" ), QgsProcessingFeatureSourceDefinition( layer->id(), true ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPath( def.get(), params, context, QStringList() << "shp", QString( "shp" ), &feedback );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPathAndLayerName( def.get(), params, context, QStringList() << "shp", QString( "shp" ), &feedback, &layerName );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  QCOMPARE( layerName, QString() );

  // feature limit, will force export
  params.insert( QStringLiteral( "source" ), QgsProcessingFeatureSourceDefinition( layer->id(), false, 2 ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPath( def.get(), params, context, QStringList() << "shp", QString( "shp" ), &feedback );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  QgsVectorLayer *subset = new QgsVectorLayer( out );
  QVERIFY( subset->isValid() );
  QCOMPARE( subset->featureCount(), 2L );
  delete subset;

  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPathAndLayerName( def.get(), params, context, QStringList() << "shp", QString( "shp" ), &feedback, &layerName );
  QVERIFY( out != layer->source() );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );
  QCOMPARE( layerName, QString() );
  subset = new QgsVectorLayer( out );
  QVERIFY( subset->isValid() );
  QCOMPARE( subset->featureCount(), 2L );
  delete subset;

  // vector layer as default
  def.reset( new QgsProcessingParameterFeatureSource( QStringLiteral( "source" ), QString(), QList<int>(), QVariant::fromValue( layer ) ) );
  params.remove( QStringLiteral( "source" ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPath( def.get(), params, context, QStringList() << "shp", QString( "shp" ), &feedback );
  QCOMPARE( out, QString( testDataDir + "points.shp" ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPathAndLayerName( def.get(), params, context, QStringList() << "shp", QString( "shp" ), &feedback, &layerName );
  QCOMPARE( out, QString( testDataDir + "points.shp" ) );
  QCOMPARE( layerName, QString() );

  // geopackage with layer
  gpkgPath = testDataDir + "points_gpkg.gpkg|layername=points_gpkg";
  gpkgLayer = qgis::make_unique< QgsVectorLayer >( gpkgPath, "vl" );
  QVERIFY( gpkgLayer->isValid() );
  params.insert( QStringLiteral( "source" ), QVariant::fromValue( gpkgLayer.get() ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPath( def.get(), params, context, QStringList() << "gpkg" << "shp", QString( "shp" ), &feedback );
  // layer must be translated -- we do not know if external tool can handle picking the correct layer automatically
  QCOMPARE( out, QString( testDataDir + QStringLiteral( "points_gpkg.gpkg" ) ) );
  gpkgPath = testDataDir + "points_gpkg.gpkg|layername=points_small";
  gpkgLayer = qgis::make_unique< QgsVectorLayer >( gpkgPath, "vl" );
  QVERIFY( gpkgLayer->isValid() );
  params.insert( QStringLiteral( "source" ), QVariant::fromValue( gpkgLayer.get() ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPath( def.get(), params, context, QStringList() << "gpkg" << "shp", QString( "shp" ), &feedback );
  QVERIFY( out.endsWith( ".shp" ) );
  QVERIFY( out.startsWith( QgsProcessingUtils::tempFolder() ) );

  gpkgPath = testDataDir + "points_gpkg.gpkg|layername=points_gpkg";
  gpkgLayer = qgis::make_unique< QgsVectorLayer >( gpkgPath, "vl" );
  QVERIFY( gpkgLayer->isValid() );
  params.insert( QStringLiteral( "source" ), QVariant::fromValue( gpkgLayer.get() ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPathAndLayerName( def.get(), params, context, QStringList() << "gpkg" << "shp", QString( "shp" ), &feedback, &layerName );
  // layer SHOULD NOT be translated -- in this case we know that the external tool can handle specifying the correct layer
  QCOMPARE( out, QString( testDataDir + QStringLiteral( "points_gpkg.gpkg" ) ) );
  QCOMPARE( layerName, QStringLiteral( "points_gpkg" ) );
  gpkgPath = testDataDir + "points_gpkg.gpkg|layername=points_small";
  gpkgLayer = qgis::make_unique< QgsVectorLayer >( gpkgPath, "vl" );
  QVERIFY( gpkgLayer->isValid() );
  params.insert( QStringLiteral( "source" ), QVariant::fromValue( gpkgLayer.get() ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPathAndLayerName( def.get(), params, context, QStringList() << "gpkg" << "shp", QString( "shp" ), &feedback, &layerName );
  QCOMPARE( out, QString( testDataDir + QStringLiteral( "points_gpkg.gpkg" ) ) );
  QCOMPARE( layerName, QStringLiteral( "points_small" ) );

  // output layer as input - e.g. from a previous model child
  params.insert( QStringLiteral( "source" ), QgsProcessingOutputLayerDefinition( layer->id() ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPath( def.get(), params, context, QStringList() << "shp", QString( "shp" ), &feedback );
  QCOMPARE( out, QString( testDataDir + "points.shp" ) );
  out = QgsProcessingParameters::parameterAsCompatibleSourceLayerPathAndLayerName( def.get(), params, context, QStringList() << "shp", QString( "shp" ), &feedback, &layerName );
  QCOMPARE( out, QString( testDataDir + "points.shp" ) );
  QCOMPARE( layerName, QString() );
}

void TestQgsProcessing::create()
{
  DummyAlgorithm alg( QStringLiteral( "test" ) );
  DummyProvider p( QStringLiteral( "test_provider" ) );
  alg.setProvider( &p );

  std::unique_ptr< QgsProcessingAlgorithm > newInstance( alg.create() );
  QVERIFY( newInstance.get() );
  QCOMPARE( newInstance->provider(), &p );
}

void TestQgsProcessing::combineFields()
{
  QgsFields a;
  QgsFields b;
  // combine empty fields
  QgsFields res = QgsProcessingUtils::combineFields( a, b );
  QVERIFY( res.isEmpty() );

  // fields in a
  a.append( QgsField( "name" ) );
  res = QgsProcessingUtils::combineFields( a, b );
  QCOMPARE( res.count(), 1 );
  QCOMPARE( res.at( 0 ).name(), QStringLiteral( "name" ) );
  b.append( QgsField( "name" ) );
  res = QgsProcessingUtils::combineFields( a, b );
  QCOMPARE( res.count(), 2 );
  QCOMPARE( res.at( 0 ).name(), QStringLiteral( "name" ) );
  QCOMPARE( res.at( 1 ).name(), QStringLiteral( "name_2" ) );

  a.append( QgsField( "NEW" ) );
  res = QgsProcessingUtils::combineFields( a, b );
  QCOMPARE( res.count(), 3 );
  QCOMPARE( res.at( 0 ).name(), QStringLiteral( "name" ) );
  QCOMPARE( res.at( 1 ).name(), QStringLiteral( "NEW" ) );
  QCOMPARE( res.at( 2 ).name(), QStringLiteral( "name_2" ) );

  b.append( QgsField( "new" ) );
  res = QgsProcessingUtils::combineFields( a, b );
  QCOMPARE( res.count(), 4 );
  QCOMPARE( res.at( 0 ).name(), QStringLiteral( "name" ) );
  QCOMPARE( res.at( 1 ).name(), QStringLiteral( "NEW" ) );
  QCOMPARE( res.at( 2 ).name(), QStringLiteral( "name_2" ) );
  QCOMPARE( res.at( 3 ).name(), QStringLiteral( "new_2" ) );
}

void TestQgsProcessing::fieldNamesToIndices()
{
  QgsFields fields;
  fields.append( QgsField( "name" ) );
  fields.append( QgsField( "address" ) );
  fields.append( QgsField( "age" ) );

  QList<int> indices1 = QgsProcessingUtils::fieldNamesToIndices( QStringList(), fields );
  QCOMPARE( indices1, QList<int>() << 0 << 1 << 2 );

  QList<int> indices2 = QgsProcessingUtils::fieldNamesToIndices( QStringList() << "address" << "age", fields );
  QCOMPARE( indices2, QList<int>() << 1 << 2 );

  QList<int> indices3 = QgsProcessingUtils::fieldNamesToIndices( QStringList() << "address" << "agegege", fields );
  QCOMPARE( indices3, QList<int>() << 1 );
}

void TestQgsProcessing::indicesToFields()
{
  QgsFields fields;
  fields.append( QgsField( "name" ) );
  fields.append( QgsField( "address" ) );
  fields.append( QgsField( "age" ) );

  QList<int> indices1 = QList<int>() << 0 << 1 << 2;
  QgsFields fields1 = QgsProcessingUtils::indicesToFields( indices1, fields );
  QCOMPARE( fields1, fields );

  QList<int> indices2 = QList<int>() << 1;
  QgsFields fields2expected;
  fields2expected.append( QgsField( "address" ) );
  QgsFields fields2 = QgsProcessingUtils::indicesToFields( indices2, fields );
  QCOMPARE( fields2, fields2expected );

  QList<int> indices3;
  QgsFields fields3 = QgsProcessingUtils::indicesToFields( indices3, fields );
  QCOMPARE( fields3, QgsFields() );
}

void TestQgsProcessing::variantToPythonLiteral()
{
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QVariant() ), QStringLiteral( "None" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QVariant::fromValue( QgsProperty::fromExpression( QStringLiteral( "1+2" ) ) ) ), QStringLiteral( "QgsProperty.fromExpression('1+2')" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QVariant::fromValue( QgsCoordinateReferenceSystem() ) ), QStringLiteral( "QgsCoordinateReferenceSystem()" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QVariant::fromValue( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:3111" ) ) ) ), QStringLiteral( "QgsCoordinateReferenceSystem('EPSG:3111')" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QVariant::fromValue( QgsRectangle( 1, 2, 3, 4 ) ) ), QStringLiteral( "'1, 3, 2, 4'" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QVariant::fromValue( QgsReferencedRectangle( QgsRectangle( 1, 2, 3, 4 ), QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:28356" ) ) ) ) ), QStringLiteral( "'1, 3, 2, 4 [EPSG:28356]'" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QVariant::fromValue( QgsPointXY( 1, 2 ) ) ), QStringLiteral( "'1,2'" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QVariant::fromValue( QgsReferencedPointXY( QgsPointXY( 1, 2 ), QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:28356" ) ) ) ) ), QStringLiteral( "'1,2 [EPSG:28356]'" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( true ), QStringLiteral( "True" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( false ), QStringLiteral( "False" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( 5 ), QStringLiteral( "5" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( 5.5 ), QStringLiteral( "5.5" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( 5LL ), QStringLiteral( "5" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QVariantList() << true << QVariant() << QStringLiteral( "a" ) ), QStringLiteral( "[True,None,'a']" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QStringLiteral( "a" ) ), QStringLiteral( "'a'" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QString() ), QStringLiteral( "''" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QStringLiteral( "a 'string'" ) ), QStringLiteral( "'a \\'string\\''" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QStringLiteral( "a \"string\"" ) ), QStringLiteral( "'a \\\"string\\\"'" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QStringLiteral( "a \n str\tin\\g" ) ), QStringLiteral( "'a \\n str\\tin\\\\g'" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( QDateTime( QDate( 2345, 1, 2 ), QTime( 6, 57, 58 ) ) ), QStringLiteral( "QDateTime(QDate(2345, 1, 2), QTime(6, 57, 58))" ) );
  QVariantMap map;
  map.insert( QStringLiteral( "list" ), QVariantList() << 1 << 2 << "a" );
  map.insert( QStringLiteral( "another" ), 4 );
  map.insert( QStringLiteral( "another2" ), QStringLiteral( "test" ) );
  QCOMPARE( QgsProcessingUtils::variantToPythonLiteral( map ), QStringLiteral( "{'another': 4,'another2': 'test','list': [1,2,'a']}" ) );
}

void TestQgsProcessing::stringToPythonLiteral()
{
  QCOMPARE( QgsProcessingUtils::stringToPythonLiteral( QStringLiteral( "a" ) ), QStringLiteral( "'a'" ) );
  QCOMPARE( QgsProcessingUtils::stringToPythonLiteral( QString() ), QStringLiteral( "''" ) );
  QCOMPARE( QgsProcessingUtils::stringToPythonLiteral( QStringLiteral( "a 'string'" ) ), QStringLiteral( "'a \\'string\\''" ) );
  QCOMPARE( QgsProcessingUtils::stringToPythonLiteral( QStringLiteral( "a \"string\"" ) ), QStringLiteral( "'a \\\"string\\\"'" ) );
  QCOMPARE( QgsProcessingUtils::stringToPythonLiteral( QStringLiteral( "a \n str\tin\\g" ) ), QStringLiteral( "'a \\n str\\tin\\\\g'" ) );
}



void TestQgsProcessing::defaultExtensionsForProvider()
{
  DummyProvider3 provider;
  // default implementation should return first supported format for provider
  QCOMPARE( provider.defaultVectorFileExtension( true ), QStringLiteral( "mif" ) );
  QCOMPARE( provider.defaultRasterFileExtension(), QStringLiteral( "mig" ) );

  // a default context should use reasonable defaults
  QgsProcessingContext context;
  QCOMPARE( context.preferredVectorFormat(), QStringLiteral( "gpkg" ) );
  QCOMPARE( context.preferredRasterFormat(), QStringLiteral( "tif" ) );

  // unless the user has set a default format, which IS supported by that provider
  QgsSettings settings;

  settings.setValue( QStringLiteral( "Processing/Configuration/DefaultOutputVectorLayerExt" ), QgsVectorFileWriter::supportedFormatExtensions().indexOf( QLatin1String( "tab" ) ) );
  settings.setValue( QStringLiteral( "Processing/Configuration/DefaultOutputRasterLayerExt" ), QgsRasterFileWriter::supportedFormatExtensions().indexOf( QLatin1String( "sdat" ) ) );

  QCOMPARE( provider.defaultVectorFileExtension( true ), QStringLiteral( "tab" ) );
  QCOMPARE( provider.defaultRasterFileExtension(), QStringLiteral( "sdat" ) );

  // context should respect these as preferred formats
  QgsProcessingContext context2;
  QCOMPARE( context2.preferredVectorFormat(), QStringLiteral( "tab" ) );
  QCOMPARE( context2.preferredRasterFormat(), QStringLiteral( "sdat" ) );

  // but if default is not supported by provider, we use a supported format
  settings.setValue( QStringLiteral( "Processing/Configuration/DefaultOutputVectorLayerExt" ), QgsVectorFileWriter::supportedFormatExtensions().indexOf( QLatin1String( "gpkg" ) ) );
  settings.setValue( QStringLiteral( "Processing/Configuration/DefaultOutputRasterLayerExt" ), QgsRasterFileWriter::supportedFormatExtensions().indexOf( QLatin1String( "ecw" ) ) );
  QCOMPARE( provider.defaultVectorFileExtension( true ), QStringLiteral( "mif" ) );
  QCOMPARE( provider.defaultRasterFileExtension(), QStringLiteral( "mig" ) );
}

void TestQgsProcessing::supportedExtensions()
{
  DummyProvider4 provider;
  QCOMPARE( provider.supportedOutputVectorLayerExtensions().count(), 1 );
  QCOMPARE( provider.supportedOutputVectorLayerExtensions().at( 0 ), QStringLiteral( "mif" ) );

  // if supportedOutputTableExtensions is not implemented, supportedOutputVectorLayerExtensions should be used instead
  QCOMPARE( provider.supportedOutputTableExtensions().count(), 1 );
  QCOMPARE( provider.supportedOutputTableExtensions().at( 0 ), QStringLiteral( "mif" ) );
}

void TestQgsProcessing::supportsNonFileBasedOutput()
{
  DummyAlgorithm alg( QStringLiteral( "test" ) );
  DummyProvider p( QStringLiteral( "test_provider" ) );
  alg.addDestParams();
  // provider has no support for file based outputs, so both output parameters should deny support
  alg.setProvider( &p );
  QVERIFY( !static_cast< const QgsProcessingDestinationParameter * >( alg.destinationParameterDefinitions().at( 0 ) )->supportsNonFileBasedOutput() );
  QVERIFY( !static_cast< const QgsProcessingDestinationParameter * >( alg.destinationParameterDefinitions().at( 1 ) )->supportsNonFileBasedOutput() );

  DummyAlgorithm alg2( QStringLiteral( "test" ) );
  DummyProvider p2( QStringLiteral( "test_provider" ) );
  p2.supportsNonFileOutputs = true;
  alg2.addDestParams();
  // provider has support for file based outputs, but only first output parameter should indicate support (since the second has support explicitly denied)
  alg2.setProvider( &p2 );
  QVERIFY( static_cast< const QgsProcessingDestinationParameter * >( alg2.destinationParameterDefinitions().at( 0 ) )->supportsNonFileBasedOutput() );
  QVERIFY( !static_cast< const QgsProcessingDestinationParameter * >( alg2.destinationParameterDefinitions().at( 1 ) )->supportsNonFileBasedOutput() );
}

void TestQgsProcessing::addParameterType()
{
  QgsProcessingRegistry reg;
  QSignalSpy spy( &reg, &QgsProcessingRegistry::parameterTypeAdded );
  DummyParameterType *dpt = new DummyParameterType();
  QVERIFY( reg.addParameterType( dpt ) );
  QCOMPARE( spy.count(), 1 );
  QVERIFY( !reg.addParameterType( dpt ) );
  QCOMPARE( spy.count(), 1 );
  QVERIFY( !reg.addParameterType( new DummyParameterType() ) );
  QCOMPARE( spy.count(), 1 );
}

void TestQgsProcessing::removeParameterType()
{
  QgsProcessingRegistry reg;

  auto paramType = new DummyParameterType();

  reg.addParameterType( paramType );
  QSignalSpy spy( &reg, &QgsProcessingRegistry::parameterTypeRemoved );
  reg.removeParameterType( paramType );
  QCOMPARE( spy.count(), 1 );
}

void TestQgsProcessing::parameterTypes()
{
  QgsProcessingRegistry reg;
  int coreParamCount = reg.parameterTypes().count();
  QVERIFY( coreParamCount > 5 );

  auto paramType = new DummyParameterType();

  reg.addParameterType( paramType );
  QCOMPARE( reg.parameterTypes().count(), coreParamCount + 1 );
  QVERIFY( reg.parameterTypes().contains( paramType ) );
}

void TestQgsProcessing::parameterType()
{
  QgsProcessingRegistry reg;

  QVERIFY( reg.parameterType( QStringLiteral( "string" ) ) );
  QVERIFY( !reg.parameterType( QStringLiteral( "borken" ) ) );  //#spellok

  auto paramType = new DummyParameterType();

  reg.addParameterType( paramType );
  QCOMPARE( reg.parameterType( QStringLiteral( "paramType" ) ), paramType );
}

void TestQgsProcessing::sourceTypeToString_data()
{
  QTest::addColumn<int>( "type" );
  QTest::addColumn<QString>( "expected" );

  // IMPORTANT -- these must match the original enum values!
  QTest::newRow( "map layer" ) << static_cast< int >( QgsProcessing::TypeMapLayer ) << QStringLiteral( "TypeMapLayer" );
  QTest::newRow( "map layer" ) << static_cast< int >( QgsProcessing::TypeVectorAnyGeometry ) << QStringLiteral( "TypeVectorAnyGeometry" );
  QTest::newRow( "map layer" ) << static_cast< int >( QgsProcessing::TypeVectorPoint ) << QStringLiteral( "TypeVectorPoint" );
  QTest::newRow( "map layer" ) << static_cast< int >( QgsProcessing::TypeVectorLine ) << QStringLiteral( "TypeVectorLine" );
  QTest::newRow( "map layer" ) << static_cast< int >( QgsProcessing::TypeVectorPolygon ) << QStringLiteral( "TypeVectorPolygon" );
  QTest::newRow( "map layer" ) << static_cast< int >( QgsProcessing::TypeRaster ) << QStringLiteral( "TypeRaster" );
  QTest::newRow( "map layer" ) << static_cast< int >( QgsProcessing::TypeFile ) << QStringLiteral( "TypeFile" );
  QTest::newRow( "map layer" ) << static_cast< int >( QgsProcessing::TypeMesh ) << QStringLiteral( "TypeMesh" );
}

void TestQgsProcessing::sourceTypeToString()
{
  QFETCH( int, type );
  QFETCH( QString, expected );

  const QgsProcessing::SourceType sourceType = static_cast< QgsProcessing::SourceType >( type );
  QCOMPARE( QgsProcessing::sourceTypeToString( sourceType ), expected );
}

void TestQgsProcessing::modelSource()
{
  QgsProcessingModelChildParameterSource source;
  source.setExpression( QStringLiteral( "expression" ) );
  source.setExpressionText( QStringLiteral( "expression string" ) );
  source.setOutputName( QStringLiteral( "output name " ) );
  source.setStaticValue( QString( "value" ) );
  source.setOutputChildId( QStringLiteral( "output child id" ) );
  source.setParameterName( QStringLiteral( "parameter name" ) );
  source.setSource( QgsProcessingModelChildParameterSource::ChildOutput );

  QByteArray ba;
  QDataStream ds( &ba, QIODevice::ReadWrite );
  ds << source;

  ds.device()->seek( 0 );

  QgsProcessingModelChildParameterSource res;
  ds >> res;

  QCOMPARE( res.expression(), QStringLiteral( "expression" ) );
  QCOMPARE( res.expressionText(), QStringLiteral( "expression string" ) );
  QCOMPARE( res.outputName(), QStringLiteral( "output name " ) );
  QCOMPARE( res.staticValue().toString(), QString( "value" ) );
  QCOMPARE( res.outputChildId(), QStringLiteral( "output child id" ) );
  QCOMPARE( res.parameterName(), QStringLiteral( "parameter name" ) );
  QCOMPARE( res.source(), QgsProcessingModelChildParameterSource::ChildOutput );
}

QGSTEST_MAIN( TestQgsProcessing )
#include "testqgsprocessing.moc"
