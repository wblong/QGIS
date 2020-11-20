# -*- coding: utf-8 -*-

"""
***************************************************************************
    test_qgsarrowsymbollayer.py
    ---------------------
    Date                 : March 2016
    Copyright            : (C) 2016 by Hugo Mercier
    Email                : hugo dot mercier at oslandia dot com
***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************
"""

__author__ = 'Hugo Mercier'
__date__ = 'March 2016'
__copyright__ = '(C) 2016, Hugo Mercier'

import qgis  # NOQA

import os

from qgis.PyQt.QtCore import QSize, QDir
from qgis.PyQt.QtGui import QColor

from qgis.core import (
    QgsVectorLayer,
    QgsSingleSymbolRenderer,
    QgsLineSymbol,
    QgsFillSymbol,
    QgsProject,
    QgsRectangle,
    QgsArrowSymbolLayer,
    QgsMultiRenderChecker,
    QgsProperty,
    QgsSymbolLayer,
    QgsMapSettings,
    QgsSymbol
)

from qgis.testing import start_app, unittest
from qgis.testing.mocked import get_iface
from utilities import unitTestDataPath

# Convenience instances in case you may need them
# not used in this test
start_app()
TEST_DATA_DIR = unitTestDataPath()


class TestQgsArrowSymbolLayer(unittest.TestCase):

    def setUp(self):
        self.report = "<h1>Python QgsArrowSymbolLayer Tests</h1>\n"

        self.iface = get_iface()

        lines_shp = os.path.join(TEST_DATA_DIR, 'lines.shp')
        self.lines_layer = QgsVectorLayer(lines_shp, 'Lines', 'ogr')
        QgsProject.instance().addMapLayer(self.lines_layer)

        # Create style
        sym2 = QgsLineSymbol.createSimple({'color': '#fdbf6f'})
        self.lines_layer.setRenderer(QgsSingleSymbolRenderer(sym2))

        self.mapsettings = self.iface.mapCanvas().mapSettings()
        self.mapsettings.setOutputSize(QSize(400, 400))
        self.mapsettings.setOutputDpi(96)
        self.mapsettings.setExtent(QgsRectangle(-113, 28, -91, 40))
        self.mapsettings.setBackgroundColor(QColor("white"))

    def tearDown(self):
        QgsProject.instance().removeAllMapLayers()
        report_file_path = "%s/qgistest.html" % QDir.tempPath()
        with open(report_file_path, 'a') as report_file:
            report_file.write(self.report)

    def test_1(self):
        sym = self.lines_layer.renderer().symbol()
        sym_layer = QgsArrowSymbolLayer.create({'head_length': '6.5', 'head_thickness': '6.5'})
        dd = QgsProperty.fromExpression("(@geometry_point_num % 4) * 2")
        sym_layer.setDataDefinedProperty(QgsSymbolLayer.PropertyArrowWidth, dd)
        dd2 = QgsProperty.fromExpression("(@geometry_point_num % 4) * 2")
        sym_layer.setDataDefinedProperty(QgsSymbolLayer.PropertyArrowHeadLength, dd2)
        dd3 = QgsProperty.fromExpression("(@geometry_point_num % 4) * 2")
        sym_layer.setDataDefinedProperty(QgsSymbolLayer.PropertyArrowHeadThickness, dd3)
        fill_sym = QgsFillSymbol.createSimple({'color': '#8bcfff', 'outline_color': '#000000', 'outline_style': 'solid', 'outline_width': '1'})
        sym_layer.setSubSymbol(fill_sym)
        sym.changeSymbolLayer(0, sym_layer)

        rendered_layers = [self.lines_layer]
        self.mapsettings.setLayers(rendered_layers)

        renderchecker = QgsMultiRenderChecker()
        renderchecker.setMapSettings(self.mapsettings)
        renderchecker.setControlName('expected_arrowsymbollayer_1')
        self.assertTrue(renderchecker.runTest('arrowsymbollayer_1'))

    def test_2(self):
        sym = self.lines_layer.renderer().symbol()
        # double headed
        sym_layer = QgsArrowSymbolLayer.create({'arrow_width': '5', 'head_length': '4', 'head_thickness': '6', 'head_type': '2'})
        fill_sym = QgsFillSymbol.createSimple({'color': '#8bcfff', 'outline_color': '#000000', 'outline_style': 'solid', 'outline_width': '1'})
        sym_layer.setSubSymbol(fill_sym)
        sym.changeSymbolLayer(0, sym_layer)

        rendered_layers = [self.lines_layer]
        self.mapsettings.setLayers(rendered_layers)

        renderchecker = QgsMultiRenderChecker()
        renderchecker.setMapSettings(self.mapsettings)
        renderchecker.setControlName('expected_arrowsymbollayer_2')
        self.assertTrue(renderchecker.runTest('arrowsymbollayer_2'))

    def test_3(self):
        sym = self.lines_layer.renderer().symbol()
        # double headed
        sym_layer = QgsArrowSymbolLayer.create({'arrow_width': '7', 'head_length': '6', 'head_thickness': '8', 'head_type': '0', 'arrow_type': '1', 'is_curved': '0'})
        fill_sym = QgsFillSymbol.createSimple({'color': '#8bcfff', 'outline_color': '#000000', 'outline_style': 'solid', 'outline_width': '1'})
        sym_layer.setSubSymbol(fill_sym)
        sym.changeSymbolLayer(0, sym_layer)

        rendered_layers = [self.lines_layer]
        self.mapsettings.setLayers(rendered_layers)

        renderchecker = QgsMultiRenderChecker()
        ms = self.mapsettings
        ms.setExtent(QgsRectangle(-101, 35, -99, 37))
        renderchecker.setMapSettings(ms)
        renderchecker.setControlName('expected_arrowsymbollayer_3')
        self.assertTrue(renderchecker.runTest('arrowsymbollayer_3'))

    def test_unrepeated(self):
        sym = self.lines_layer.renderer().symbol()
        # double headed
        sym_layer = QgsArrowSymbolLayer.create({'arrow_width': '7', 'head_length': '6', 'head_thickness': '8', 'head_type': '0', 'arrow_type': '0'})
        # no repetition
        sym_layer.setIsRepeated(False)
        fill_sym = QgsFillSymbol.createSimple({'color': '#8bcfff', 'outline_color': '#000000', 'outline_style': 'solid', 'outline_width': '1'})
        sym_layer.setSubSymbol(fill_sym)
        sym.changeSymbolLayer(0, sym_layer)

        rendered_layers = [self.lines_layer]
        self.mapsettings.setLayers(rendered_layers)

        renderchecker = QgsMultiRenderChecker()
        ms = self.mapsettings
        ms.setExtent(QgsRectangle(-119, 17, -82, 50))
        renderchecker.setMapSettings(ms)
        renderchecker.setControlName('expected_arrowsymbollayer_4')
        self.assertTrue(renderchecker.runTest('arrowsymbollayer_4'))

    def testColors(self):
        """
        Test colors, need to make sure colors are passed/retrieved from subsymbol
        """
        sym_layer = QgsArrowSymbolLayer.create()
        sym_layer.setColor(QColor(150, 50, 100))
        self.assertEqual(sym_layer.color(), QColor(150, 50, 100))
        self.assertEqual(sym_layer.subSymbol().color(), QColor(150, 50, 100))
        sym_layer.subSymbol().setColor(QColor(250, 150, 200))
        self.assertEqual(sym_layer.subSymbol().color(), QColor(250, 150, 200))
        self.assertEqual(sym_layer.color(), QColor(250, 150, 200))

    def testOpacityWithDataDefinedColor(self):
        line_shp = os.path.join(TEST_DATA_DIR, 'lines.shp')
        line_layer = QgsVectorLayer(line_shp, 'Lines', 'ogr')
        self.assertTrue(line_layer.isValid())

        sym = QgsLineSymbol()
        sym_layer = QgsArrowSymbolLayer.create({'arrow_width': '7', 'head_length': '6', 'head_thickness': '8', 'head_type': '0', 'arrow_type': '0', 'is_repeated': '0', 'is_curved': '0'})
        fill_sym = QgsFillSymbol.createSimple({'color': '#8bcfff', 'outline_color': '#000000', 'outline_style': 'solid', 'outline_width': '1'})
        fill_sym.symbolLayer(0).setDataDefinedProperty(QgsSymbolLayer.PropertyFillColor, QgsProperty.fromExpression(
            "if(Name='Arterial', 'red', 'green')"))
        fill_sym.symbolLayer(0).setDataDefinedProperty(QgsSymbolLayer.PropertyStrokeColor, QgsProperty.fromExpression(
            "if(Name='Arterial', 'magenta', 'blue')"))

        sym_layer.setSubSymbol(fill_sym)
        sym.changeSymbolLayer(0, sym_layer)

        # set opacity on both the symbol and subsymbol, to test that they get combined
        fill_sym.setOpacity(0.5)
        sym.setOpacity(0.5)

        line_layer.setRenderer(QgsSingleSymbolRenderer(sym))

        ms = QgsMapSettings()
        ms.setOutputSize(QSize(400, 400))
        ms.setOutputDpi(96)
        ms.setExtent(QgsRectangle(-118.5, 19.0, -81.4, 50.4))
        ms.setLayers([line_layer])

        # Test rendering
        renderchecker = QgsMultiRenderChecker()
        renderchecker.setMapSettings(ms)
        renderchecker.setControlPathPrefix('symbol_arrow')
        renderchecker.setControlName('expected_arrow_opacityddcolor')
        res = renderchecker.runTest('expected_arrow_opacityddcolor')
        self.report += renderchecker.report()
        self.assertTrue(res)

    def testDataDefinedOpacity(self):
        line_shp = os.path.join(TEST_DATA_DIR, 'lines.shp')
        line_layer = QgsVectorLayer(line_shp, 'Lines', 'ogr')
        self.assertTrue(line_layer.isValid())

        sym = QgsLineSymbol()
        sym_layer = QgsArrowSymbolLayer.create({'arrow_width': '7', 'head_length': '6', 'head_thickness': '8', 'head_type': '0', 'arrow_type': '0', 'is_repeated': '0', 'is_curved': '0'})
        fill_sym = QgsFillSymbol.createSimple({'color': '#8bcfff', 'outline_color': '#000000', 'outline_style': 'solid', 'outline_width': '1'})
        fill_sym.symbolLayer(0).setDataDefinedProperty(QgsSymbolLayer.PropertyFillColor, QgsProperty.fromExpression(
            "if(Name='Arterial', 'red', 'green')"))
        fill_sym.symbolLayer(0).setDataDefinedProperty(QgsSymbolLayer.PropertyStrokeColor, QgsProperty.fromExpression(
            "if(Name='Arterial', 'magenta', 'blue')"))

        sym_layer.setSubSymbol(fill_sym)
        sym.changeSymbolLayer(0, sym_layer)

        sym.setDataDefinedProperty(QgsSymbol.PropertyOpacity, QgsProperty.fromExpression("if(\"Value\" = 1, 25, 50)"))

        line_layer.setRenderer(QgsSingleSymbolRenderer(sym))

        ms = QgsMapSettings()
        ms.setOutputSize(QSize(400, 400))
        ms.setOutputDpi(96)
        ms.setExtent(QgsRectangle(-118.5, 19.0, -81.4, 50.4))
        ms.setLayers([line_layer])

        # Test rendering
        renderchecker = QgsMultiRenderChecker()
        renderchecker.setMapSettings(ms)
        renderchecker.setControlPathPrefix('symbol_arrow')
        renderchecker.setControlName('expected_arrow_ddopacity')
        res = renderchecker.runTest('expected_arrow_ddopacity')
        self.report += renderchecker.report()
        self.assertTrue(res)


if __name__ == '__main__':
    unittest.main()
