import json
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def sfo(entries):
    keys=b''; data=b''; records=[]
    for key,val in entries.items():
        keyoffset=len(keys); keys+=key.encode()+b'\0'
        value=val.encode()+b'\0'
        records.append((keyoffset,0x0204,len(value),len(value),len(data)))
        data+=value
    key_table=20+len(records)*16
    data_table=key_table+len(keys)
    head=struct.pack('<5I',0x46535000,0x101,key_table,data_table,len(records))
    return head + b''.join(struct.pack('<HHIII',*r) for r in records)+keys+data

@unittest.skipIf(not (shutil.which('cc') or shutil.which('gcc')), 'No C compiler')
class ScannerTests(unittest.TestCase):
    def test_scans_mock_sfo(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);appmeta=p/'appmeta'; folder=appmeta/'CUSA02343';folder.mkdir(parents=True)
            (folder/'param.sfo').write_bytes(sfo({'TITLE_ID':'CUSA02343','TITLE':'Uncharted Sample','APP_VER':'01.00',
                        'CONTENT_ID':'TEST-CUSA02343','CATEGORY':'gd'}))
            binary=p/'scanner'
            subprocess.run([shutil.which('cc') or shutil.which('gcc'),'-std=c99','-Wall','-Wextra','-O2',
                            '-o',str(binary),str(ROOT/'scanner'/'scan_appmeta.c')],check=True,capture_output=True)
            result=p/'inventory.json'
            subprocess.run([str(binary),'--appmeta',str(appmeta),'--out',str(result)],check=True,capture_output=True)
            inv=json.loads(result.read_text())
            self.assertTrue(inv['metadata_only'])
            self.assertEqual(len(inv['games']),1)
            self.assertEqual(inv['games'][0]['cusa'],'CUSA02343')
            self.assertEqual(inv['games'][0]['app_ver'],'01.00')
            self.assertFalse(inv['games'][0]['installed_confirmed'])

if __name__=='__main__':unittest.main()
