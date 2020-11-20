/***************************************************************************
                              qgsLandingPagehandlers.cpp
                              -------------------------
  begin                : May 3, 2019
  copyright            : (C) 2019 by Alessandro Pasotti
  email                : elpaso at itopen dot it
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgslandingpagehandlers.h"
#include "qgslandingpageutils.h"
#include "qgsserverinterface.h"
#include "qgsserverresponse.h"
#include "qgsserverprojectutils.h"
#include "qgsvectorlayer.h"
#include "qgslayertreenode.h"
#include "qgslayertree.h"

#include <QDir>
#include <QCryptographicHash>

QgsLandingPageHandler::QgsLandingPageHandler( const QgsServerSettings *settings )
  : mSettings( settings )
{
  setContentTypes( { QgsServerOgcApi::ContentType::JSON, QgsServerOgcApi::ContentType::HTML } );
}

void QgsLandingPageHandler::handleRequest( const QgsServerApiContext &context ) const
{
  if ( context.request()->url().path( ) == '/' || context.request()->url().path( ).isEmpty() )
  {
    QUrl url { context.request()->url() };
    url.setPath( QStringLiteral( "/index.%1" ).arg( QgsServerOgcApi::contentTypeToExtension( contentTypeFromRequest( context.request() ) ) ) );
    context.response()->setStatusCode( 302 );
    context.response()->setHeader( QStringLiteral( "Location" ), url.toString() );
  }
  else
  {
    const json projects = projectsData( ) ;
    json data
    {
      { "links", links( context ) },
      { "projects", projects },
      { "projects_count", projects.size() }
    };
    write( data, context, {{ "pageTitle", linkTitle() }, { "navigation", json::array() }} );
  }
}

const QString QgsLandingPageHandler::templatePath( const QgsServerApiContext &context ) const
{
  QString path { context.serverInterface()->serverSettings()->apiResourcesDirectory() };
  path += QLatin1String( "/ogc/static/landingpage/index.html" );
  return path;
}

json QgsLandingPageHandler::projectsData() const
{
  json j = json::array();
  const auto availableProjects { QgsLandingPageUtils::projects( *mSettings ) };
  const auto constProjectKeys { availableProjects.keys() };
  for ( const auto &p : constProjectKeys )
  {
    j.push_back( QgsLandingPageUtils::projectInfo( availableProjects[ p ] ) );
  }
  return j;
}


QgsLandingPageMapHandler::QgsLandingPageMapHandler( const QgsServerSettings *settings )
  : mSettings( settings )
{
  setContentTypes( { QgsServerOgcApi::ContentType::JSON } );
}

void QgsLandingPageMapHandler::handleRequest( const QgsServerApiContext &context ) const
{
  json data;
  data[ "links" ] = json::array();
  const QString projectPath { QgsLandingPageUtils::projectUriFromUrl( context.request()->url().path(), *mSettings ) };
  if ( projectPath.isEmpty() )
  {
    throw QgsServerApiNotFoundError( QStringLiteral( "Requested project hash not found!" ) );
  }
  data[ "project" ] = QgsLandingPageUtils::projectInfo( projectPath, mSettings );
  write( data, context, {{ "pageTitle", linkTitle() }, { "navigation", json::array() }} );
}

