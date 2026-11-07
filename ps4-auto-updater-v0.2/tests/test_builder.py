import csv
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'catalog'))
from build_catalog import build

COLUMNS = ['game_name','cusa','base_content_id','base_app_ver','update_version','region','size_bytes','direct_pkg_url','verified_compatible','proof_note','release_date','cover_url']
INV = {"games": [{"cusa":"CUSA02343", "name":"Uncharted Collection (example)",
                   "app_ver":"01.00", "content_id":"EP9000-CUSA02343_00-FAKEEXAMPLE000001", "installed_confirmed": True}]}

class BuilderTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / 'approved.csv'
    def tearDown(self):
        self.tmp.cleanup()
    def run_rows(self, rows):
        with self.path.open('w', newline='', encoding='utf-8') as f:
            w=csv.DictWriter(f,fieldnames=COLUMNS);w.writeheader();w.writerows(rows)
        return build(INV, self.path)
    def base(self):
        return dict(game_name='Uncharted Collection',cusa='CUSA02343',base_content_id=INV['games'][0]['content_id'],
                    base_app_ver='01.00',update_version='01.02',region='EUR',size_bytes='1048576',
                    direct_pkg_url='https://example.invalid/test.pkg',verified_compatible='YES',proof_note='Tested on identical base build',release_date='',cover_url='')
    def test_approval_and_exact_match(self):
        self.assertEqual(len(self.run_rows([self.base()])['DATA']),1)
    def test_reject_unconfirmed_install(self):
        import copy
        snapshot = copy.deepcopy(INV)
        snapshot["games"][0]["installed_confirmed"] = False
        with self.path.open('w', newline='', encoding='utf-8') as f:
            w=csv.DictWriter(f,fieldnames=COLUMNS);w.writeheader();w.writerow(self.base())
        self.assertFalse(build(snapshot,self.path)["DATA"])
    def test_reject_mismatched_build(self):
        row=self.base();row['base_content_id']='DIFFERENT'
        self.assertFalse(self.run_rows([row])['DATA'])
    def test_reject_unverified(self):
        row=self.base();row['verified_compatible']='NO'
        self.assertFalse(self.run_rows([row])['DATA'])
    def test_reject_same_or_older_update(self):
        row=self.base();row['update_version']='01.00'
        self.assertFalse(self.run_rows([row])['DATA'])
    def test_reject_indirect_link(self):
        row=self.base();row['direct_pkg_url']='https://example.invalid/download'
        self.assertFalse(self.run_rows([row])['DATA'])
    def test_reject_missing_proof(self):
        row=self.base();row['proof_note']=''
        self.assertFalse(self.run_rows([row])['DATA'])

if __name__=='__main__':unittest.main()
