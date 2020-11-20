/***************************************************************************
                         testqgstemporalnavigationobject.cpp
                         ---------------
    begin                : April 2020
    copyright            : (C) 2020 by Samweli Mwakisambwe
    email                : samweli at kartoza dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgstest.h"
#include <QObject>

//qgis includes...
#include <qgstemporalnavigationobject.h>

/**
 * \ingroup UnitTests
 * This is a unit test for the QgsTemporalNavigationObject class.
 */
class TestQgsTemporalNavigationObject : public QObject
{
    Q_OBJECT

  public:
    TestQgsTemporalNavigationObject() = default;

  private slots:
    void initTestCase();// will be called before the first testfunction is executed.
    void cleanupTestCase();// will be called after the last testfunction was executed.
    void init(); // will be called before each testfunction is executed.
    void cleanup(); // will be called after every testfunction.

    void animationState();
    void temporalExtents();
    void frameSettings();
    void navigationMode();
    void expressionContext();

  private:
    QgsTemporalNavigationObject *navigationObject = nullptr;
};

void TestQgsTemporalNavigationObject::initTestCase()
{
  //
  // Runs once before any tests are run
  //
  // init QGIS's paths - true means that all path will be inited from prefix
  QgsApplication::init();
  QgsApplication::initQgis();
  QgsApplication::showSettings();

}

void TestQgsTemporalNavigationObject::init()
{
  //create some objects that will be used in all tests...
  //create a temporal object that will be used in all tests...

  navigationObject = new QgsTemporalNavigationObject();
  navigationObject->setNavigationMode( QgsTemporalNavigationObject::Animated );
}

void TestQgsTemporalNavigationObject::cleanup()
{
}

void TestQgsTemporalNavigationObject::cleanupTestCase()
{
  QgsApplication::exitQgis();
}

void TestQgsTemporalNavigationObject::animationState()
{
  QgsDateTimeRange range = QgsDateTimeRange(
                             QDateTime( QDate( 2020, 1, 1 ), QTime( 8, 0, 0 ) ),
                             QDateTime( QDate( 2020, 10, 1 ), QTime( 8, 0, 0 ) )
                           );
  navigationObject->setTemporalExtents( range );

  navigationObject->setFrameDuration( QgsInterval( 1, QgsUnitTypes::TemporalMonths ) );

  qRegisterMetaType<QgsTemporalNavigationObject::AnimationState>( "AnimationState" );
  QSignalSpy stateSignal( navigationObject, &QgsTemporalNavigationObject::stateChanged );

  QCOMPARE( navigationObject->animationState(), QgsTemporalNavigationObject::Idle );

  navigationObject->setAnimationState( QgsTemporalNavigationObject::Forward );
  QCOMPARE( navigationObject->animationState(), QgsTemporalNavigationObject::Forward );
  QCOMPARE( stateSignal.count(), 1 );

  navigationObject->playBackward();
  QCOMPARE( navigationObject->animationState(), QgsTemporalNavigationObject::Reverse );
  QCOMPARE( stateSignal.count(), 2 );

  navigationObject->playForward();
  QCOMPARE( navigationObject->animationState(), QgsTemporalNavigationObject::Forward );
  QCOMPARE( stateSignal.count(), 3 );

  navigationObject->pause();
  QCOMPARE( navigationObject->animationState(), QgsTemporalNavigationObject::Idle );
  QCOMPARE( stateSignal.count(), 4 );

  navigationObject->next();
  QCOMPARE( navigationObject->currentFrameNumber(), 1 );

  navigationObject->previous();
  QCOMPARE( navigationObject->currentFrameNumber(), 0 );

  navigationObject->skipToEnd();
  QCOMPARE( navigationObject->currentFrameNumber(), 9 );

  navigationObject->rewindToStart();
  QCOMPARE( navigationObject->currentFrameNumber(), 0 );

  QCOMPARE( navigationObject->isLooping(), false );
  navigationObject->setLooping( true );
  QCOMPARE( navigationObject->isLooping(), true );

}

void TestQgsTemporalNavigationObject::temporalExtents()
{
  QgsDateTimeRange range = QgsDateTimeRange(
                             QDateTime( QDate( 2020, 1, 1 ), QTime( 8, 0, 0 ) ),
                             QDateTime( QDate( 2020, 12, 1 ), QTime( 8, 0, 0 ) )
                           );
  navigationObject->setTemporalExtents( range );
  QCOMPARE( navigationObject->temporalExtents(), range );

  navigationObject->setTemporalExtents( QgsDateTimeRange() );
  QCOMPARE( navigationObject->temporalExtents(), QgsDateTimeRange() );
}

void TestQgsTemporalNavigationObject::navigationMode()
{
  QgsDateTimeRange range = QgsDateTimeRange(
                             QDateTime( QDate( 2010, 1, 1 ), QTime( 0, 0, 0 ) ),
                             QDateTime( QDate( 2020, 1, 1 ), QTime( 0, 0, 0 ) ) );

  QgsDateTimeRange range2 = QgsDateTimeRange(
                              QDateTime( QDate( 2015, 1, 1 ), QTime( 0, 0, 0 ) ),
                              QDateTime( QDate( 2020, 1, 1 ), QTime( 0, 0, 0 ) ) );

  QgsDateTimeRange check;
  auto checkUpdateTemporalRange = [&check]( const QgsDateTimeRange range )
  {
    QCOMPARE( range, check );
  };
  QObject *context = new QObject( this );
  connect( navigationObject, &QgsTemporalNavigationObject::updateTemporalRange, context, checkUpdateTemporalRange );

  // Changing navigation mode emits an updateTemporalRange, in this case it should be an empty range
  navigationObject->setNavigationMode( QgsTemporalNavigationObject::NavigationOff );
  // Setting temporal extents also triggers an updateTemporalRange with an empty range
  navigationObject->setTemporalExtents( range );

  // Changing navigation mode emits an updateTemporalRange, in this case it should be the last range
  // we used in setTemporalExtents.
  check = range;
  navigationObject->setNavigationMode( QgsTemporalNavigationObject::FixedRange );
  check = range2;
  navigationObject->setTemporalExtents( range2 );

  // Delete context to disconnect the signal to the lambda function
  delete context;
  navigationObject->setNavigationMode( QgsTemporalNavigationObject::Animated );
}

void TestQgsTemporalNavigationObject::frameSettings()
{
  qRegisterMetaType<QgsDateTimeRange>( "QgsDateTimeRange" );
  QSignalSpy temporalRangeSignal( navigationObject, &QgsTemporalNavigationObject::updateTemporalRange );

  QgsDateTimeRange range = QgsDateTimeRange(
                             QDateTime( QDate( 2020, 1, 1 ), QTime( 8, 0, 0 ) ),
                             QDateTime( QDate( 2020, 1, 1 ), QTime( 12, 0, 0 ) ),
                             true,
                             false
                           );
  QgsDateTimeRange lastRange = QgsDateTimeRange(
                                 QDateTime( QDate( 2020, 1, 1 ), QTime( 12, 0, 0 ) ),
                                 QDateTime( QDate( 2020, 1, 1 ), QTime( 12, 0, 0 ) ),
                                 true,
                                 false
                               );
  navigationObject->setTemporalExtents( range );
  QCOMPARE( temporalRangeSignal.count(), 1 );

  navigationObject->setFrameDuration( QgsInterval( 1, QgsUnitTypes::TemporalHours ) );
  QCOMPARE( navigationObject->frameDuration(), QgsInterval( 1, QgsUnitTypes::TemporalHours ) );
  QCOMPARE( temporalRangeSignal.count(), 2 );

  QCOMPARE( navigationObject->currentFrameNumber(), 0 );
  QCOMPARE( navigationObject->totalFrameCount(), 5 );

  navigationObject->setCurrentFrameNumber( 1 );
  QCOMPARE( navigationObject->currentFrameNumber(), 1 );
  QCOMPARE( temporalRangeSignal.count(), 3 );

  // Test Overflow
  navigationObject->setCurrentFrameNumber( 100 );
  QCOMPARE( navigationObject->currentFrameNumber(), navigationObject->totalFrameCount() - 1 );
  QCOMPARE( temporalRangeSignal.count(), 4 );

  // Test Underflow
  navigationObject->setCurrentFrameNumber( -100 );
  QCOMPARE( navigationObject->currentFrameNumber(), 0 );
  QCOMPARE( temporalRangeSignal.count(), 5 );

  navigationObject->setFramesPerSecond( 1 );
  QCOMPARE( navigationObject->framesPerSecond(), 1.0 );

  QCOMPARE( navigationObject->dateTimeRangeForFrameNumber( 4 ), lastRange );

  // Test if changing the frame duration 'keeps' the current frameNumber
  navigationObject->setCurrentFrameNumber( 4 ); // 12:00-...
  QCOMPARE( navigationObject->currentFrameNumber(), 4 );
  navigationObject->setFrameDuration( QgsInterval( 2, QgsUnitTypes::TemporalHours ) );
  QCOMPARE( navigationObject->currentFrameNumber(), 2 ); // going from 1 hour to 2 hour frames, but stay on 12:00-...
  QCOMPARE( temporalRangeSignal.count(), 7 );

  // Test if, when changing to Cumulative mode, the dateTimeRange for frame 4 (with 2 hours frames) is indeed the full range
  navigationObject->setTemporalRangeCumulative( true );
  QCOMPARE( navigationObject->dateTimeRangeForFrameNumber( 4 ), range );
  QCOMPARE( temporalRangeSignal.count(), 7 );
}

void TestQgsTemporalNavigationObject::expressionContext()
{
  QgsTemporalNavigationObject object;
  QgsDateTimeRange range = QgsDateTimeRange(
                             QDateTime( QDate( 2020, 1, 1 ), QTime( 8, 0, 0 ) ),
                             QDateTime( QDate( 2020, 1, 1 ), QTime( 12, 0, 0 ) )
                           );
  object.setTemporalExtents( range );
  object.setFrameDuration( QgsInterval( 1, QgsUnitTypes::TemporalHours ) );
  object.setCurrentFrameNumber( 1 );
  object.setFramesPerSecond( 30 );

  std::unique_ptr< QgsExpressionContextScope > scope( object.createExpressionContextScope() );
  QCOMPARE( scope->variable( QStringLiteral( "frame_rate" ) ).toDouble(), 30.0 );
  QCOMPARE( scope->variable( QStringLiteral( "frame_duration" ) ).value< QgsInterval >().seconds(), 3600.0 );
  QCOMPARE( scope->variable( QStringLiteral( "frame_number" ) ).toInt(), 1 );
  QCOMPARE( scope->variable( QStringLiteral( "animation_start_time" ) ).toDateTime(), range.begin() );
  QCOMPARE( scope->variable( QStringLiteral( "animation_end_time" ) ).toDateTime(), range.end() );
  QCOMPARE( scope->variable( QStringLiteral( "animation_interval" ) ).value< QgsInterval >(), range.end() - range.begin() );
}

QGSTEST_MAIN( TestQgsTemporalNavigationObject )
#include "testqgstemporalnavigationobject.moc"
