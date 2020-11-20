/***************************************************************************
                         qgstemporalnavigationobject.cpp
                         ---------------
    begin                : March 2020
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

#include "qgstemporalnavigationobject.h"
#include "qgis.h"

QgsTemporalNavigationObject::QgsTemporalNavigationObject( QObject *parent )
  : QgsTemporalController( parent )
{
  mNewFrameTimer = new QTimer( this );

  connect( mNewFrameTimer, &QTimer::timeout,
           this, &QgsTemporalNavigationObject::timerTimeout );
}

void QgsTemporalNavigationObject::timerTimeout()
{
  switch ( mPlayBackMode )
  {
    case AnimationState::Forward:
      next();
      if ( mCurrentFrameNumber >= totalFrameCount() - 1 )
      {
        if ( mLoopAnimation )
          mCurrentFrameNumber = -1; // we don't jump immediately to frame 0, instead we delay that till the next timeout
        else
          pause();
      }
      break;

    case AnimationState::Reverse:
      previous();
      if ( mCurrentFrameNumber <= 0 )
      {
        if ( mLoopAnimation )
          mCurrentFrameNumber = totalFrameCount(); // we don't jump immediately to real last frame..., instead we delay that till the next timeout
        else
          pause();
      }
      break;

    case AnimationState::Idle:
      // should not happen - in an idle state the timeout won't occur
      break;
  }
}

bool QgsTemporalNavigationObject::isLooping() const
{
  return mLoopAnimation;
}

void QgsTemporalNavigationObject::setLooping( bool loopAnimation )
{
  mLoopAnimation = loopAnimation;
}

QgsExpressionContextScope *QgsTemporalNavigationObject::createExpressionContextScope() const
{
  std::unique_ptr< QgsExpressionContextScope > scope = qgis::make_unique< QgsExpressionContextScope >( QStringLiteral( "temporal" ) );
  scope->setVariable( QStringLiteral( "frame_rate" ), mFramesPerSecond, true );
  scope->setVariable( QStringLiteral( "frame_number" ), mCurrentFrameNumber, true );
  scope->setVariable( QStringLiteral( "frame_duration" ), mFrameDuration, true );
  scope->setVariable( QStringLiteral( "animation_start_time" ), mTemporalExtents.begin(), true );
  scope->setVariable( QStringLiteral( "animation_end_time" ), mTemporalExtents.end(), true );
  scope->setVariable( QStringLiteral( "animation_interval" ), mTemporalExtents.end() - mTemporalExtents.begin(), true );
  return scope.release();
}

QgsDateTimeRange QgsTemporalNavigationObject::dateTimeRangeForFrameNumber( long long frame ) const
{
  const QDateTime start = mTemporalExtents.begin();

  if ( frame < 0 )
    frame = 0;

  const long long nextFrame = frame + 1;

  const QDateTime begin = start.addSecs( frame * mFrameDuration.seconds() );
  const QDateTime end = start.addSecs( nextFrame * mFrameDuration.seconds() );

  QDateTime frameStart = begin;

  if ( mCumulativeTemporalRange )
    frameStart = start;

  if ( end <= mTemporalExtents.end() )
    return QgsDateTimeRange( frameStart, end, true, false );

  return QgsDateTimeRange( frameStart, mTemporalExtents.end(), true, false );
}

void QgsTemporalNavigationObject::setNavigationMode( const NavigationMode mode )
{
  if ( mNavigationMode == mode )
    return;

  mNavigationMode = mode;
  emit navigationModeChanged( mode );

  if ( !mBlockUpdateTemporalRangeSignal )
  {
    switch ( mNavigationMode )
    {
      case Animated:
        emit updateTemporalRange( dateTimeRangeForFrameNumber( mCurrentFrameNumber ) );
        break;
      case FixedRange:
        emit updateTemporalRange( mTemporalExtents );
        break;
      case NavigationOff:
        emit updateTemporalRange( QgsDateTimeRange() );
        break;
    }
  }
}

void QgsTemporalNavigationObject::setTemporalExtents( const QgsDateTimeRange &temporalExtents )
{
  if ( mTemporalExtents == temporalExtents )
  {
    return;
  }
  QgsDateTimeRange oldFrame = dateTimeRangeForFrameNumber( currentFrameNumber() );
  mTemporalExtents = temporalExtents;
  mCurrentFrameNumber = findBestFrameNumberForFrameStart( oldFrame.begin() );
  emit temporalExtentsChanged( mTemporalExtents );

  switch ( mNavigationMode )
  {
    case Animated:
    {
      int currentFrameNumber = mCurrentFrameNumber;

      // Force to emit signal if the current frame number doesn't change
      if ( currentFrameNumber == mCurrentFrameNumber && !mBlockUpdateTemporalRangeSignal )
        emit updateTemporalRange( dateTimeRangeForFrameNumber( mCurrentFrameNumber ) );
      break;
    }
    case FixedRange:
      if ( !mBlockUpdateTemporalRangeSignal )
        emit updateTemporalRange( mTemporalExtents );
      break;
    case NavigationOff:
      break;
  }

}

QgsDateTimeRange QgsTemporalNavigationObject::temporalExtents() const
{
  return mTemporalExtents;
}

void QgsTemporalNavigationObject::setCurrentFrameNumber( long long frameNumber )
{
  if ( mCurrentFrameNumber != frameNumber )
  {
    mCurrentFrameNumber = std::max( 0LL, std::min( frameNumber, totalFrameCount() - 1 ) );
    QgsDateTimeRange range = dateTimeRangeForFrameNumber( mCurrentFrameNumber );

    if ( !mBlockUpdateTemporalRangeSignal )
      emit updateTemporalRange( range );
  }
}

long long QgsTemporalNavigationObject::currentFrameNumber() const
{
  return mCurrentFrameNumber;
}

void QgsTemporalNavigationObject::setFrameDuration( QgsInterval frameDuration )
{
  if ( mFrameDuration == frameDuration )
  {
    return;
  }
  QgsDateTimeRange oldFrame = dateTimeRangeForFrameNumber( currentFrameNumber() );
  mFrameDuration = frameDuration;
  mCurrentFrameNumber = findBestFrameNumberForFrameStart( oldFrame.begin() );
  emit temporalFrameDurationChanged( mFrameDuration );

  // temporarily disable the updateTemporalRange signal, as we'll emit it ourselves at the end of this function...

  // forcing an update of our views
  QgsDateTimeRange range = dateTimeRangeForFrameNumber( mCurrentFrameNumber );

  if ( !mBlockUpdateTemporalRangeSignal && mNavigationMode != NavigationOff )
    emit updateTemporalRange( range );
}

QgsInterval QgsTemporalNavigationObject::frameDuration() const
{
  return mFrameDuration;
}

void QgsTemporalNavigationObject::setFramesPerSecond( double framesPerSeconds )
{
  if ( framesPerSeconds > 0 )
  {
    mFramesPerSecond = framesPerSeconds;
    mNewFrameTimer->setInterval( ( 1.0 / mFramesPerSecond ) * 1000 );
  }
}

double QgsTemporalNavigationObject::framesPerSecond() const
{
  return mFramesPerSecond;
}

void QgsTemporalNavigationObject::setTemporalRangeCumulative( bool state )
{
  mCumulativeTemporalRange = state;
}

bool QgsTemporalNavigationObject::temporalRangeCumulative() const
{
  return mCumulativeTemporalRange;
}

void QgsTemporalNavigationObject::play()
{
  mNewFrameTimer->start( ( 1.0 / mFramesPerSecond ) * 1000 );
}

void QgsTemporalNavigationObject::pause()
{
  mNewFrameTimer->stop();
  setAnimationState( AnimationState::Idle );
}

void QgsTemporalNavigationObject::playForward()
{
  if ( mPlayBackMode == Idle &&  mCurrentFrameNumber >= totalFrameCount() - 1 )
  {
    // if we are paused at the end of the video, and the user hits play, we automatically rewind and play again
    rewindToStart();
  }

  setAnimationState( AnimationState::Forward );
  play();
}

void QgsTemporalNavigationObject::playBackward()
{
  if ( mPlayBackMode == Idle &&  mCurrentFrameNumber <= 0 )
  {
    // if we are paused at the start of the video, and the user hits play, we automatically skip to end and play in reverse again
    skipToEnd();
  }

  setAnimationState( AnimationState::Reverse );
  play();
}

void QgsTemporalNavigationObject::next()
{
  setCurrentFrameNumber( mCurrentFrameNumber + 1 );
}

void QgsTemporalNavigationObject::previous()
{
  setCurrentFrameNumber( mCurrentFrameNumber - 1 );
}

void QgsTemporalNavigationObject::rewindToStart()
{
  setCurrentFrameNumber( 0 );
}

void QgsTemporalNavigationObject::skipToEnd()
{
  const long long frame = totalFrameCount() - 1;
  setCurrentFrameNumber( frame );
}

long long QgsTemporalNavigationObject::totalFrameCount() const
{
  QgsInterval totalAnimationLength = mTemporalExtents.end() - mTemporalExtents.begin();
  return std::floor( totalAnimationLength.seconds() / mFrameDuration.seconds() ) + 1;
}

void QgsTemporalNavigationObject::setAnimationState( AnimationState mode )
{
  if ( mode != mPlayBackMode )
  {
    mPlayBackMode = mode;
    emit stateChanged( mPlayBackMode );
  }
}

QgsTemporalNavigationObject::AnimationState QgsTemporalNavigationObject::animationState() const
{
  return mPlayBackMode;
}

long QgsTemporalNavigationObject::findBestFrameNumberForFrameStart( const QDateTime &frameStart ) const
{
  long bestFrame = 0;
  QgsDateTimeRange testFrame = QgsDateTimeRange( frameStart, frameStart ); // creatng an 'instant' Range here
  for ( long i = 0; i < totalFrameCount(); ++i )
  {
    QgsDateTimeRange range = dateTimeRangeForFrameNumber( i );
    if ( range.overlaps( testFrame ) )
    {
      bestFrame = i;
      break;
    }
  }
  return bestFrame;
}
