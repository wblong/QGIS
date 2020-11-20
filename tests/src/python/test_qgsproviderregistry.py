# -*- coding: utf-8 -*-
"""QGIS Unit tests for QgsProviderRegistry.

.. note:: This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""
__author__ = 'Nyall Dawson'
__date__ = '16/03/2020'
__copyright__ = 'Copyright 2020, The QGIS Project'

import qgis  # NOQA

from qgis.core import QgsProviderRegistry
from qgis.testing import start_app, unittest

# Convenience instances in case you may need them
# to find the srs.db
start_app()


class TestQgsProviderRegistry(unittest.TestCase):

    def testProviderList(self):
        """
        Test provider list
        """

        providers = QgsProviderRegistry.instance().providerList()
        self.assertIn('ogr', providers)
        self.assertIn('gdal', providers)

    def testProviderMetadata(self):
        """
        Test retrieving provider metadata
        """

        providers = QgsProviderRegistry.instance().providerList()
        for p in providers:
            self.assertTrue(QgsProviderRegistry.instance().providerMetadata(p))
            # should be case-insensitive
            self.assertTrue(QgsProviderRegistry.instance().providerMetadata(p.lower()))
            self.assertTrue(QgsProviderRegistry.instance().providerMetadata(p.upper()))

        self.assertIsNone(QgsProviderRegistry.instance().providerMetadata('asdasdasdasdasd'))

    def testCreateProvider(self):
        """
        Test creating provider instance
        """
        providers = QgsProviderRegistry.instance().providerList()
        for p in providers:
            if p == 'geonode' or p == 'vectortile':
                continue

            self.assertTrue(QgsProviderRegistry.instance().createProvider(p, ''))
            # should be case-insensitive
            self.assertTrue(QgsProviderRegistry.instance().createProvider(p.lower(), ''))
            self.assertTrue(QgsProviderRegistry.instance().createProvider(p.upper(), ''))

        self.assertIsNone(QgsProviderRegistry.instance().createProvider('asdasdasdasdasd', ''))

    @unittest.skipIf('ept' not in QgsProviderRegistry.instance().providerList(), 'EPT provider not available')
    def testShouldDeferUriForOtherProvidersEpt(self):
        self.assertTrue(QgsProviderRegistry.instance().shouldDeferUriForOtherProviders('/home/nyall/ept.json', 'ogr'))
        self.assertFalse(QgsProviderRegistry.instance().shouldDeferUriForOtherProviders('/home/nyall/ept.json', 'ept'))
        self.assertFalse(QgsProviderRegistry.instance().shouldDeferUriForOtherProviders('/home/nyall/my.json', 'ogr'))

    def testUriIsBlocklisted(self):
        self.assertFalse(QgsProviderRegistry.instance().uriIsBlocklisted('/home/nyall/me.tif'))
        self.assertFalse(QgsProviderRegistry.instance().uriIsBlocklisted('/home/nyall/me.shp'))

        # internal details only -- we should be hiding these uris!
        self.assertTrue(QgsProviderRegistry.instance().uriIsBlocklisted('/home/nyall/me.shp.xml'))
        self.assertTrue(QgsProviderRegistry.instance().uriIsBlocklisted('/home/nyall/me.aux.xml'))
        self.assertTrue(QgsProviderRegistry.instance().uriIsBlocklisted('/home/nyall/me.AUX.XML'))
        self.assertTrue(QgsProviderRegistry.instance().uriIsBlocklisted('/home/nyall/me.tif.aux.xml'))
        self.assertTrue(QgsProviderRegistry.instance().uriIsBlocklisted('/home/nyall/me.tif.AUX.XML'))
        self.assertTrue(QgsProviderRegistry.instance().uriIsBlocklisted('/home/nyall/me.png.aux.xml'))
        self.assertTrue(QgsProviderRegistry.instance().uriIsBlocklisted('/home/nyall/me.tif.xml'))

    @unittest.skipIf('ept' not in QgsProviderRegistry.instance().providerList(), 'EPT provider not available')
    def testFilePointCloudFilters(self):
        parts = QgsProviderRegistry.instance().filePointCloudFilters().split(';;')
        self.assertTrue(parts[0].startswith('All Supported Files ('))
        all_filter = parts[0][21:-1]
        self.assertIn('ept.json', all_filter.split(' '))
        self.assertIn('EPT.JSON', all_filter.split(' '))

        self.assertEqual(parts[1], 'All Files (*.*)')
        self.assertIn('Entwine Point Clouds (ept.json EPT.JSON)', parts)


if __name__ == '__main__':
    unittest.main()
