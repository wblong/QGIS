/***************************************************************************
  qgspdaldataitemguiprovider.cpp
  --------------------------------------
  Date                 : November 2020
  Copyright            : (C) 2020 by Peter Petrik
  Email                : zilolv at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgspdaldataitemguiprovider.h"

#include "qgsmanageconnectionsdialog.h"
#include "qgspdaldataitems.h"
#include "qgspdalprovider.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>


void QgsPdalDataItemGuiProvider::populateContextMenu( QgsDataItem *item, QMenu *menu, const QList<QgsDataItem *> &, QgsDataItemGuiContext context )
{
  Q_UNUSED( item )
  Q_UNUSED( menu )
  Q_UNUSED( context )
}
