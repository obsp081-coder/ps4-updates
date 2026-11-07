"""Host-only fixtures; do not require or access a real PS4 console."""
import json
import shutil
import sqlite3
import subprocess
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def make_sfo(entries):
    keys, vals, records = b'', b'', []
    for name, value in entries.items():
        keyoff=len(keys); keys+=name.encode()+b'\0'
        val=(value+'\0').encode()
        records.append((keyoff, 0x0204, len(val), len(val), len(vals)))
        vals+=val
    koff=20+16*len(records)
    voff=koff+len(keys)
    return struct.pack('<IIIII',0x46535000,0x101,koff,voff,len(records)) + b''.join(struct.pack('<HHIII',*r) for r in records) + keys + vals

@unittest.skipUnless(shutil.which('cc'), 'C compiler unavailable')
class AppDBTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory()
        cls.exe=Path(cls.tmp.name)/'scanner'
        subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-O2','-o',str(cls.exe),str(ROOT/'native'/'scan_appdb.c'),'-lsqlite3'],check=True,capture_output=True)
    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()
    def test_reads_actual_installed_games_and_ignores_dlc_and_duplicates(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            db=root/'app.db'; meta=root/'appmeta'
            con=sqlite3.connect(db)
            con.execute('CREATE TABLE tbl_appbrowse_0100 (titleId TEXT, titleName TEXT, category TEXT)')
            con.execute('CREATE TABLE tbl_appbrowse_0200 (titleId TEXT, titleName TEXT, category TEXT)')
            con.executemany('INSERT INTO tbl_appbrowse_0100 VALUES(?,?,?)',[
                ('CUSA02343','Uncharted: Nathan Drake Collection','gd'),
                ('CUSA02344','Not installed metadata','gd'),
                ('CUSA09999','Some DLC','ac'),
                ('NOTAGAME','Bad ID','gd')])
            con.execute('INSERT INTO tbl_appbrowse_0200 VALUES(?,?,?)',('CUSA02343','Uncharted: Nathan Drake Collection','gd'))
            con.commit();con.close()
            game=(meta/'CUSA02343');game.mkdir(parents=True)
            (game/'param.sfo').write_bytes(make_sfo({'TITLE_ID':'CUSA02343','TITLE':'Uncharted Test','APP_VER':'01.00','CONTENT_ID':'EXAMPLE-ID','CATEGORY':'gd'}))
            out=root/'inventory.json'
            subprocess.run([str(self.exe),'--appdb',str(db),'--appmeta',str(meta),'--out',str(out)],check=True,capture_output=True)
            data=json.loads(out.read_text())
            self.assertEqual(data['schema'],2)
            self.assertFalse(data['metadata_only'])
            self.assertEqual([g['cusa'] for g in data['games']],['CUSA02343','CUSA02344'])
            self.assertEqual(data['games'][0]['app_ver'],'01.00')
            self.assertTrue(data['games'][0]['installed_confirmed'])
            self.assertFalse(data['games'][1]['installed_confirmed'])
    def test_external_meta_and_invalid_sfo_id_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);db=p/'app.db';meta=p/'meta'/'external'/'CUSA02343';meta.mkdir(parents=True)
            con=sqlite3.connect(db)
            con.execute('CREATE TABLE tbl_appbrowse_0100 (titleId TEXT, titleName TEXT, category TEXT)')
            con.execute('INSERT INTO tbl_appbrowse_0100 VALUES (?,?,?)',('CUSA02343','Uncharted','gd'))
            con.commit();con.close()
            (meta/'param.sfo').write_bytes(make_sfo({'TITLE_ID':'CUSA00000','APP_VER':'01.00'}))
            out=p/'out.json'
            subprocess.run([str(self.exe),'--appdb',str(db),'--appmeta',str(p/'meta'),'--out',str(out)],check=True,capture_output=True)
            g=json.loads(out.read_text())['games'][0]
            self.assertFalse(g['installed_confirmed'])
            (meta/'param.sfo').write_bytes(make_sfo({'TITLE_ID':'CUSA02343','APP_VER':'01.00'}))
            subprocess.run([str(self.exe),'--appdb',str(db),'--appmeta',str(p/'meta'),'--out',str(out)],check=True,capture_output=True)
            g=json.loads(out.read_text())['games'][0]
            self.assertTrue(g['installed_confirmed'])
            self.assertEqual(g['metadata_location'],'external')
    def test_missing_db_errors_without_creating_new_db(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);db=root/'not-there.db';out=root/'out.json'
            result=subprocess.run([str(self.exe),'--appdb',str(db),'--out',str(out)],capture_output=True)
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(db.exists())
            self.assertFalse(out.exists())

if __name__=='__main__':unittest.main()
