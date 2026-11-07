"""Host-only smoke tests for v0.3. No PS4 or personal files needed."""
import json
import pathlib
import struct
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE.parent / 'ps4' / 'src' / 'main.c'


def make_sfo(mapping):
    keys = b''
    values = b''
    entries = b''
    for key, value in mapping.items():
        key_off = len(keys)
        keys += key.encode() + b'\0'
        value_off = len(values)
        payload = value.encode('utf-8') + b'\0'
        values += payload
        entries += struct.pack('<HHIII', key_off, 0x0204, len(payload), len(payload), value_off)
    header = struct.pack('<IIIII', 0x46535000, 0x101, 20 + len(entries),
                         20 + len(entries) + len(keys), len(mapping))
    return header + entries + keys + values


class NativeScannerSmokeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.exe = pathlib.Path(cls.build.name) / 'scanner'
        subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-O2',
                        '-o', str(cls.exe), str(SRC)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.meta = self.root / 'appmeta'
        self.meta.mkdir()
        self.target = self.root / 'inventory.json'

    def tearDown(self):
        self.temp.cleanup()

    def add(self, ident, location='internal', **overrides):
        folder = self.meta if location == 'internal' else self.meta / 'external'
        folder = folder / ident
        folder.mkdir(parents=True, exist_ok=True)
        fields = {'TITLE_ID': ident, 'TITLE': 'Test Game', 'APP_VER': '01.02',
                  'CONTENT_ID': 'EP9000-CUSA02343_00-TEST000000000000', 'CATEGORY': 'gd'}
        fields.update(overrides)
        (folder / 'param.sfo').write_bytes(make_sfo(fields))

    def run_scan(self):
        cmd = [str(self.exe), '--appmeta', str(self.meta), '--out', str(self.target)]
        p = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        return json.loads(self.target.read_text())

    def test_scans_metadata(self):
        self.add('CUSA02343')
        o = self.run_scan()
        self.assertEqual(o['schema'], 3)
        self.assertTrue(o['metadata_only'])
        self.assertEqual(o['games'][0]['app_ver'], '01.02')
        self.assertEqual(o['games'][0]['cusa'], 'CUSA02343')
        self.assertFalse(o['games'][0]['installed_confirmed'])

    def test_external_metadata(self):
        self.add('CUSA02343', 'external')
        self.assertEqual(self.run_scan()['games'][0]['metadata_location'], 'external')

    def test_no_duplicate_across_drives(self):
        self.add('CUSA02343')
        self.add('CUSA02343', 'external')
        self.assertEqual(len(self.run_scan()['games']), 1)

    def test_skip_addon_category(self):
        self.add('CUSA02343', CATEGORY='ac')
        self.assertEqual(len(self.run_scan()['games']), 0)

    def test_skip_wrong_id(self):
        self.add('CUSA02343', TITLE_ID='CUSA00001')
        self.assertEqual(len(self.run_scan()['games']), 0)

    def test_corrupt_sfo(self):
        folder = self.meta / 'CUSA02343'
        folder.mkdir()
        (folder / 'param.sfo').write_bytes(b'bad data')
        self.assertEqual(len(self.run_scan()['games']), 0)

    def test_sanitizes_json(self):
        self.add('CUSA02343', TITLE='Game "\u00a3" \\ test')
        self.assertEqual(self.run_scan()['games'][0]['name'], 'Game "\u00a3" \\ test')


if __name__ == '__main__':
    unittest.main()
