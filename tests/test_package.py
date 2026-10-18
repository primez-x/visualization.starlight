"""The staged package must load as a CoreELEC 32-bit hard-float Kodi visualisation."""
from pathlib import Path
import struct
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


class PackageTests(unittest.TestCase):
    def test_module_matches_manifest_and_target(self):
        addon = ET.parse(ROOT / 'addon.xml').getroot()
        self.assertEqual(addon.get('id'), 'visualization.starlight')
        library = addon.find("extension[@point='xbmc.player.musicviz']").get('library_linux')
        data = (ROOT / library).read_bytes()
        self.assertEqual(data[:4], b'\x7fELF')
        self.assertEqual(data[4], 1, 'ELF class must be 32-bit')
        self.assertEqual(data[5], 1, 'ELF data must be little-endian')
        machine, = struct.unpack_from('<H', data, 18)
        flags, = struct.unpack_from('<I', data, 36)
        self.assertEqual(machine, 40, 'ELF machine must be ARM')
        self.assertTrue(flags & 0x400, 'ELF must use the hard-float ABI')
        for symbol in (b'ADDON_Create', b'ADDON_GetTypeVersion', b'ADDON_GetTypeMinVersion'):
            self.assertIn(symbol, data)

    def test_assets_resolve(self):
        metadata = ET.parse(ROOT / 'addon.xml').getroot().find("extension[@point='xbmc.addon.metadata']")
        for asset in metadata.findall('assets/*'):
            self.assertTrue((ROOT / asset.text).is_file(), asset.text)


if __name__ == '__main__':
    unittest.main()
