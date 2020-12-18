/***************************************************************************
  qgspointcloudlayerchunkloader_p.h
  --------------------------------------
  Date                 : October 2020
  Copyright            : (C) 2020 by Peter Petrik
  Email                : zilolv dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSPOINTCLOUDLAYERCHUNKLOADER_P_H
#define QGSPOINTCLOUDLAYERCHUNKLOADER_P_H

///@cond PRIVATE

//
//  W A R N I N G
//  -------------
//
// This file is not part of the QGIS API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//

#include "qgschunkloader_p.h"
#include "qgsfeature3dhandler_p.h"
#include "qgschunkedentity_p.h"
#include "qgspointcloud3dsymbol.h"
#include "qgspointcloud3dsymbol_p.h"
#include "qgspointcloudlayer3drenderer.h"

#include <memory>

#include <QFutureWatcher>
#include <Qt3DRender/QGeometry>
#include <Qt3DRender/QBuffer>
#include <Qt3DRender/QMaterial>
#include <QVector3D>

#define SIP_NO_FILE

/**
 * \ingroup 3d
 * This loader factory is responsible for creation of loaders for individual tiles
 * of QgsQgsPointCloudLayerChunkedEntity whenever a new tile is requested by the entity.
 *
 * \since QGIS 3.18
 */
class QgsPointCloudLayerChunkLoaderFactory : public QgsChunkLoaderFactory
{
  public:

    /**
     * Constructs the factory
     * The factory takes ownership over the passed \a symbol
     */
    QgsPointCloudLayerChunkLoaderFactory( const Qgs3DMapSettings &map, QgsPointCloudIndex *pc, QgsPointCloud3DSymbol *symbol,
                                          double zValueScale, double zValueOffset );

    //! Creates loader for the given chunk node. Ownership of the returned is passed to the caller.
    virtual QgsChunkLoader *createChunkLoader( QgsChunkNode *node ) const override;
    virtual QgsChunkNode *createRootNode() const override;
    virtual QVector<QgsChunkNode *> createChildren( QgsChunkNode *node ) const override;
    const Qgs3DMapSettings &mMap;
    QgsPointCloudIndex *mPointCloudIndex;
    std::unique_ptr< QgsPointCloud3DSymbol > mSymbol;
    double mZValueScale = 1.0;
    double mZValueOffset = 0;
};


/**
 * \ingroup 3d
 * This loader class is responsible for async loading of data for a single tile
 * of QgsPointCloudLayerChunkedEntity and creation of final 3D entity from the data
 * previously prepared in a worker thread.
 *
 * \since QGIS 3.18
 */
class QgsPointCloudLayerChunkLoader : public QgsChunkLoader
{
  public:

    /**
     * Constructs the loader
     * QgsPointCloudLayerChunkLoader takes ownership over symbol
     */
    QgsPointCloudLayerChunkLoader( const QgsPointCloudLayerChunkLoaderFactory *factory, QgsChunkNode *node, std::unique_ptr< QgsPointCloud3DSymbol > symbol, double zValueScale, double zValueOffset );
    ~QgsPointCloudLayerChunkLoader() override;

    virtual void cancel() override;
    virtual Qt3DCore::QEntity *createEntity( Qt3DCore::QEntity *parent ) override;

  private:
    const QgsPointCloudLayerChunkLoaderFactory *mFactory;
    std::unique_ptr<QgsPointCloud3DSymbolHandler> mHandler;
    QgsPointCloud3DRenderContext mContext;
    bool mCanceled = false;
    QFutureWatcher<void> *mFutureWatcher = nullptr;
};


/**
 * \ingroup 3d
 * 3D entity used for rendering of point cloud layers with a single 3D symbol for all points.
 *
 * It is implemented using tiling approach with QgsChunkedEntity. Internally it uses
 * QgsPointCloudLayerChunkLoaderFactory and QgsPointCloudLayerChunkLoader to do the actual work
 * of loading and creating 3D sub-entities for each tile.
 *
 * \since QGIS 3.18
 */
class QgsPointCloudLayerChunkedEntity : public QgsChunkedEntity
{
    Q_OBJECT
  public:
    explicit QgsPointCloudLayerChunkedEntity( QgsPointCloudIndex *pc, const Qgs3DMapSettings &map, QgsPointCloud3DSymbol *symbol, float maxScreenError, bool showBoundingBoxes,
        double zValueScale, double zValueOffset );

    ~QgsPointCloudLayerChunkedEntity();
};

/// @endcond

#endif // QGSPOINTCLOUDLAYERCHUNKLOADER_P_H
