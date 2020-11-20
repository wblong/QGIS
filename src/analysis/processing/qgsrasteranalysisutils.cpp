/***************************************************************************
  qgsrasteranalysisutils.cpp
  ---------------------
  Date                 : June 2018
  Copyright            : (C) 2018 by Nyall Dawson
  Email                : nyall dot dawson at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsrasteranalysisutils.h"

#include "qgsfeedback.h"
#include "qgsrasterblock.h"
#include "qgsrasteriterator.h"
#include "qgsgeos.h"
#include "qgsprocessingparameters.h"
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
///@cond PRIVATE

void QgsRasterAnalysisUtils::cellInfoForBBox( const QgsRectangle &rasterBBox, const QgsRectangle &featureBBox, double cellSizeX, double cellSizeY,
    int &nCellsX, int &nCellsY, int rasterWidth, int rasterHeight, QgsRectangle &rasterBlockExtent )
{
  //get intersecting bbox
  QgsRectangle intersectBox = rasterBBox.intersect( featureBBox );
  if ( intersectBox.isEmpty() )
  {
    nCellsX = 0;
    nCellsY = 0;
    rasterBlockExtent = QgsRectangle();
    return;
  }

  //get offset in pixels in x- and y- direction
  int offsetX = static_cast< int >( std::floor( ( intersectBox.xMinimum() - rasterBBox.xMinimum() ) / cellSizeX ) );
  int offsetY = static_cast< int >( std::floor( ( rasterBBox.yMaximum() - intersectBox.yMaximum() ) / cellSizeY ) );

  int maxColumn = static_cast< int >( std::floor( ( intersectBox.xMaximum() - rasterBBox.xMinimum() ) / cellSizeX ) ) + 1;
  int maxRow = static_cast< int >( std::floor( ( rasterBBox.yMaximum() - intersectBox.yMinimum() ) / cellSizeY ) ) + 1;

  nCellsX = maxColumn - offsetX;
  nCellsY = maxRow - offsetY;

  //avoid access to cells outside of the raster (may occur because of rounding)
  nCellsX = std::min( offsetX + nCellsX, rasterWidth ) - offsetX;
  nCellsY = std::min( offsetY + nCellsY, rasterHeight ) - offsetY;

  rasterBlockExtent = QgsRectangle( rasterBBox.xMinimum() + offsetX * cellSizeX,
                                    rasterBBox.yMaximum() - offsetY * cellSizeY,
                                    rasterBBox.xMinimum() + ( nCellsX + offsetX ) * cellSizeX,
                                    rasterBBox.yMaximum() - ( nCellsY + offsetY ) * cellSizeY );
}

void QgsRasterAnalysisUtils::statisticsFromMiddlePointTest( QgsRasterInterface *rasterInterface, int rasterBand, const QgsGeometry &poly, int nCellsX, int nCellsY, double cellSizeX, double cellSizeY, const QgsRectangle &rasterBBox,  const std::function<void( double )> &addValue, bool skipNodata )
{
  std::unique_ptr< QgsGeometryEngine > polyEngine( QgsGeometry::createGeometryEngine( poly.constGet( ) ) );
  if ( !polyEngine )
  {
    return;
  }
  polyEngine->prepareGeometry();

  QgsRasterIterator iter( rasterInterface );
  iter.startRasterRead( rasterBand, nCellsX, nCellsY, rasterBBox );

  std::unique_ptr< QgsRasterBlock > block;
  int iterLeft = 0;
  int iterTop = 0;
  int iterCols = 0;
  int iterRows = 0;
  QgsRectangle blockExtent;
  bool isNoData = false;
  while ( iter.readNextRasterPart( rasterBand, iterCols, iterRows, block, iterLeft, iterTop, &blockExtent ) )
  {
    double cellCenterY = blockExtent.yMaximum() - 0.5 * cellSizeY;

    for ( int row = 0; row < iterRows; ++row )
    {
      double cellCenterX = blockExtent.xMinimum() + 0.5 * cellSizeX;
      for ( int col = 0; col < iterCols; ++col )
      {
        const double pixelValue = block->valueAndNoData( row, col, isNoData );
        if ( validPixel( pixelValue ) && ( !skipNodata || !isNoData ) )
        {
          QgsPoint cellCenter( cellCenterX, cellCenterY );
          if ( polyEngine->contains( &cellCenter ) )
          {
            addValue( pixelValue );
          }
        }
        cellCenterX += cellSizeX;
      }
      cellCenterY -= cellSizeY;
    }
  }
}

void QgsRasterAnalysisUtils::statisticsFromPreciseIntersection( QgsRasterInterface *rasterInterface, int rasterBand, const QgsGeometry &poly, int nCellsX, int nCellsY, double cellSizeX, double cellSizeY, const QgsRectangle &rasterBBox,  const std::function<void( double, double )> &addValue, bool skipNodata )
{
  QgsGeometry pixelRectGeometry;

  double hCellSizeX = cellSizeX / 2.0;
  double hCellSizeY = cellSizeY / 2.0;
  double pixelArea = cellSizeX * cellSizeY;
  double weight = 0;

  std::unique_ptr< QgsGeometryEngine > polyEngine( QgsGeometry::createGeometryEngine( poly.constGet( ) ) );
  if ( !polyEngine )
  {
    return;
  }
  polyEngine->prepareGeometry();

  QgsRasterIterator iter( rasterInterface );
  iter.startRasterRead( rasterBand, nCellsX, nCellsY, rasterBBox );

  std::unique_ptr< QgsRasterBlock > block;
  int iterLeft = 0;
  int iterTop = 0;
  int iterCols = 0;
  int iterRows = 0;
  QgsRectangle blockExtent;
  bool isNoData = false;
  while ( iter.readNextRasterPart( rasterBand, iterCols, iterRows, block, iterLeft, iterTop, &blockExtent ) )
  {
    double currentY = blockExtent.yMaximum() - 0.5 * cellSizeY;
    for ( int row = 0; row < iterRows; ++row )
    {
      double currentX = blockExtent.xMinimum() + 0.5 * cellSizeX;
      for ( int col = 0; col < iterCols; ++col )
      {
        const double pixelValue = block->valueAndNoData( row, col, isNoData );
        if ( validPixel( pixelValue ) && ( !skipNodata || !isNoData ) )
        {
          pixelRectGeometry = QgsGeometry::fromRect( QgsRectangle( currentX - hCellSizeX, currentY - hCellSizeY, currentX + hCellSizeX, currentY + hCellSizeY ) );
          // GEOS intersects tests on prepared geometry is MAGNITUDES faster than calculating the intersection itself,
          // so we first test to see if there IS an intersection before doing the actual calculation
          if ( !pixelRectGeometry.isNull() && polyEngine->intersects( pixelRectGeometry.constGet() ) )
          {
            //intersection
            QgsGeometry intersectGeometry = pixelRectGeometry.intersection( poly );
            if ( !intersectGeometry.isEmpty() )
            {
              double intersectionArea = intersectGeometry.area();
              if ( intersectionArea > 0.0 )
              {
                weight = intersectionArea / pixelArea;
                addValue( pixelValue, weight );
              }
            }
          }
        }
        currentX += cellSizeX;
      }
      currentY -= cellSizeY;
    }
  }
}

bool QgsRasterAnalysisUtils::validPixel( double value )
{
  return !std::isnan( value );
}

void QgsRasterAnalysisUtils::mapToPixel( const double x, const double y, const QgsRectangle bounds, const double unitsPerPixelX, const double unitsPerPixelY, int &px, int &py )
{
  px = trunc( ( x - bounds.xMinimum() ) / unitsPerPixelX );
  py = trunc( ( y - bounds.yMaximum() ) / -unitsPerPixelY );
}

void QgsRasterAnalysisUtils::pixelToMap( const int px, const int py, const QgsRectangle bounds, const double unitsPerPixelX, const double unitsPerPixelY, double &x, double &y )
{
  x = bounds.xMinimum() + ( px + 0.5 ) * unitsPerPixelX;
  y = bounds.yMaximum() - ( py + 0.5 ) * unitsPerPixelY;
}

static QVector< QPair< QString, Qgis::DataType > > sDataTypes;

void populateDataTypes()
{
  if ( sDataTypes.empty() )
  {
    sDataTypes.append( qMakePair( QStringLiteral( "Byte" ), Qgis::Byte ) );
    sDataTypes.append( qMakePair( QStringLiteral( "Int16" ), Qgis::Int16 ) );
    sDataTypes.append( qMakePair( QStringLiteral( "UInt16" ), Qgis::UInt16 ) );
    sDataTypes.append( qMakePair( QStringLiteral( "Int32" ), Qgis::Int32 ) );
    sDataTypes.append( qMakePair( QStringLiteral( "UInt32" ), Qgis::UInt32 ) );
    sDataTypes.append( qMakePair( QStringLiteral( "Float32" ), Qgis::Float32 ) );
    sDataTypes.append( qMakePair( QStringLiteral( "Float64" ), Qgis::Float64 ) );
    sDataTypes.append( qMakePair( QStringLiteral( "CInt16" ), Qgis::CInt16 ) );
    sDataTypes.append( qMakePair( QStringLiteral( "CInt32" ), Qgis::CInt32 ) );
    sDataTypes.append( qMakePair( QStringLiteral( "CFloat32" ), Qgis::CFloat32 ) );
    sDataTypes.append( qMakePair( QStringLiteral( "CFloat64" ), Qgis::CFloat64 ) );
  }
}

std::unique_ptr<QgsProcessingParameterDefinition> QgsRasterAnalysisUtils::createRasterTypeParameter( const QString &name, const QString &description, Qgis::DataType defaultType )
{
  populateDataTypes();

  QStringList names;
  int defaultChoice = 0;
  int i = 0;
  for ( auto it = sDataTypes.constBegin(); it != sDataTypes.constEnd(); ++it )
  {
    names.append( it->first );
    if ( it->second == defaultType )
      defaultChoice = i;
    i++;
  }

  return qgis::make_unique< QgsProcessingParameterEnum >( name, description, names, false, defaultChoice );
}

Qgis::DataType QgsRasterAnalysisUtils::rasterTypeChoiceToDataType( int choice )
{
  if ( choice < 0 || choice >= sDataTypes.count() )
    return Qgis::Float32;

  return sDataTypes.value( choice ).second;
}

void QgsRasterAnalysisUtils::applyRasterLogicOperator( const std::vector< QgsRasterAnalysisUtils::RasterLogicInput > &inputs, QgsRasterDataProvider *destinationRaster, double outputNoDataValue, const bool treatNoDataAsFalse,
    int width, int height, const QgsRectangle &extent, QgsFeedback *feedback,
    std::function<void( const std::vector< std::unique_ptr< QgsRasterBlock > > &, bool &, bool &, int, int, bool )> &applyLogicFunc,
    qgssize &noDataCount, qgssize &trueCount, qgssize &falseCount )
{
  int maxWidth = QgsRasterIterator::DEFAULT_MAXIMUM_TILE_WIDTH;
  int maxHeight = QgsRasterIterator::DEFAULT_MAXIMUM_TILE_HEIGHT;
  int nbBlocksWidth = static_cast< int>( std::ceil( 1.0 * width / maxWidth ) );
  int nbBlocksHeight = static_cast< int >( std::ceil( 1.0 * height / maxHeight ) );
  int nbBlocks = nbBlocksWidth * nbBlocksHeight;

  destinationRaster->setEditable( true );
  QgsRasterIterator outputIter( destinationRaster );
  outputIter.startRasterRead( 1, width, height, extent );

  int iterLeft = 0;
  int iterTop = 0;
  int iterCols = 0;
  int iterRows = 0;
  QgsRectangle blockExtent;
  std::unique_ptr< QgsRasterBlock > outputBlock;
  while ( outputIter.readNextRasterPart( 1, iterCols, iterRows, outputBlock, iterLeft, iterTop, &blockExtent ) )
  {
    std::vector< std::unique_ptr< QgsRasterBlock > > inputBlocks;
    for ( const QgsRasterAnalysisUtils::RasterLogicInput &i : inputs )
    {
      for ( int band : i.bands )
      {
        std::unique_ptr< QgsRasterBlock > b( i.interface->block( band, blockExtent, iterCols, iterRows ) );
        inputBlocks.emplace_back( std::move( b ) );
      }
    }

    feedback->setProgress( 100 * ( ( iterTop / maxHeight * nbBlocksWidth ) + iterLeft / maxWidth ) / nbBlocks );
    for ( int row = 0; row < iterRows; row++ )
    {
      if ( feedback->isCanceled() )
        break;

      for ( int column = 0; column < iterCols; column++ )
      {
        bool res = false;
        bool resIsNoData = false;
        applyLogicFunc( inputBlocks, res, resIsNoData, row, column, treatNoDataAsFalse );
        if ( resIsNoData )
          noDataCount++;
        else if ( res )
          trueCount++;
        else
          falseCount++;

        outputBlock->setValue( row, column, resIsNoData ? outputNoDataValue : ( res ? 1 : 0 ) );
      }
    }
    destinationRaster->writeBlock( outputBlock.get(), 1, iterLeft, iterTop );
  }
  destinationRaster->setEditable( false );
}

std::vector<double> QgsRasterAnalysisUtils::getCellValuesFromBlockStack( const std::vector< std::unique_ptr< QgsRasterBlock > > &inputBlocks, int &row, int &col, bool &noDataInStack )
{
  //get all values from inputBlocks
  std::vector<double> cellValues;
  bool hasNoData = false;
  cellValues.reserve( inputBlocks.size() );

  for ( auto &block : inputBlocks )
  {
    double value = 0;
    if ( !block || !block->isValid() )
    {
      noDataInStack = true;
      break;
    }
    else
    {
      value = block->valueAndNoData( row, col, hasNoData );
      if ( hasNoData )
      {
        noDataInStack = true;
        continue; //NoData is not included in the cell value vector
      }
      else
      {
        cellValues.push_back( value );
      }
    }
  }
  return cellValues;
}

double QgsRasterAnalysisUtils::meanFromCellValues( std::vector<double> cellValues, int stackSize )
{
  double sum = std::accumulate( cellValues.begin(), cellValues.end(), 0.0 );
  double mean = sum / static_cast<double>( stackSize );
  return mean;
}

double QgsRasterAnalysisUtils::medianFromCellValues( std::vector<double> cellValues, int stackSize )
{
  std::sort( cellValues.begin(), cellValues.end() );
  bool even = ( stackSize % 2 ) < 1;
  if ( even )
  {
    return ( cellValues[stackSize / 2 - 1] + cellValues[stackSize / 2] ) / 2.0;
  }
  else //odd
  {
    return cellValues[( stackSize + 1 ) / 2 - 1];
  }
}


double QgsRasterAnalysisUtils::stddevFromCellValues( std::vector<double> cellValues, int stackSize )
{
  double variance = varianceFromCellValues( cellValues, stackSize );
  double stddev = std::sqrt( variance );
  return stddev;
}

double QgsRasterAnalysisUtils::varianceFromCellValues( std::vector<double> cellValues, int stackSize )
{
  double mean = meanFromCellValues( cellValues, stackSize );
  double accum = 0.0;
  for ( int i = 0; i < stackSize; i++ )
  {
    accum += std::pow( ( cellValues.at( i ) - mean ), 2.0 );
  }
  double variance = accum / static_cast<double>( stackSize );
  return variance;
}

double QgsRasterAnalysisUtils::maximumFromCellValues( std::vector<double> cellValues )
{
  return *std::max_element( cellValues.begin(), cellValues.end() );
}

double QgsRasterAnalysisUtils::minimumFromCellValues( std::vector<double> cellValues )
{
  return *std::min_element( cellValues.begin(), cellValues.end() );
}

double QgsRasterAnalysisUtils::majorityFromCellValues( std::vector<double> cellValues, const double noDataValue, int stackSize )
{
  if ( stackSize == 1 )
  {
    //output will be same as input if only one layer is entered
    return cellValues[0];
  }
  else if ( stackSize == 2 )
  {
    //if only two layers are input, return NoData if values are not the same (eg. no Majority could  be found)
    return ( qgsDoubleNear( cellValues[0], cellValues[1] ) ) ?  cellValues[0] : noDataValue;
  }
  else if ( std::adjacent_find( cellValues.begin(), cellValues.end(), std::not_equal_to<double>() ) == cellValues.end() )
  {
    //check if all values in cellValues are equal
    //output will be same as input if all cellValues of the stack are the same
    return cellValues[0];
  }
  else
  {
    //search for majority using hash map [O(n)]
    std::unordered_map<double, int> map;

    for ( int i = 0; i < stackSize; i++ )
    {
      map[cellValues[i]]++;
    }

    int maxCount = 0;
    bool multipleMajorities = false;
    double result = noDataValue;
    for ( auto pair : map )
    {
      if ( maxCount < pair.second )
      {
        result = pair.first;
        maxCount = pair.second;
        multipleMajorities = false;
      }
      else if ( maxCount == pair.second )
      {
        multipleMajorities = true;
      }
    }
    return multipleMajorities ? noDataValue : result;
  }
}

double QgsRasterAnalysisUtils::minorityFromCellValues( std::vector<double> cellValues, const double noDataValue, int stackSize )
{
  if ( stackSize == 1 )
  {
    //output will be same as input if only one layer is entered
    return cellValues[0];
  }
  else if ( stackSize == 2 )
  {
    //if only two layers are input, return NoData if values are not the same (eg. no minority could  be found)
    return ( qgsDoubleNear( cellValues[0], cellValues[1] ) ) ?  cellValues[0] : noDataValue;
  }
  else if ( std::adjacent_find( cellValues.begin(), cellValues.end(), std::not_equal_to<double>() ) == cellValues.end() )
  {
    //check if all values in cellValues are equal
    //output will be same as input if all cellValues of the stack are the same
    return cellValues[0];
  }
  else
  {
    //search for minority using hash map [O(n)]
    std::unordered_map<double, int> map;

    for ( int i = 0; i < stackSize; i++ )
    {
      map[cellValues[i]]++;
    }

    int minCount = stackSize;
    bool multipleMinorities = false;
    double result = noDataValue; //result will stay NoData if no minority value exists
    for ( auto pair : map )
    {
      if ( minCount > pair.second )
      {
        result = pair.first;
        minCount = pair.second;
        multipleMinorities = false;
      }
      else if ( minCount == pair.second )
      {
        multipleMinorities = true;
      }
    }
    return multipleMinorities ? noDataValue : result;
  }
}

double QgsRasterAnalysisUtils::rangeFromCellValues( std::vector<double> cellValues )
{
  double max = *std::max_element( cellValues.begin(), cellValues.end() );
  double min = *std::min_element( cellValues.begin(), cellValues.end() );
  return max - min;
}

double QgsRasterAnalysisUtils::varietyFromCellValues( std::vector<double> cellValues )
{
  std::unordered_set<double> uniqueValues( cellValues.begin(), cellValues.end() );
  return uniqueValues.size();
}


///@endcond PRIVATE

