"""Bounded LRU icon cache host tests. Uses synthetic PNG, no console read."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
MODULES=[ROOT/"ps4"/"src"/name for name in ("ui_image.c","ui_icon_cache.c")]
TEST=ROOT/"tests"/"ui_mocks"/"icon_cache_smoke.c"
class IconCacheTests(unittest.TestCase):
    def test_eight_slot_lru_and_broken_png_retains_existing_cover(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if cc is None: self.skipTest("No host C compiler")
        with tempfile.TemporaryDirectory() as td:
            out=Path(td)/"icon_cache"
            build=subprocess.run([cc,"-std=c11","-O2","-Wall","-Wextra","-Werror",
                                  *map(str,MODULES),str(TEST),"-o",str(out),"-lm"],
                                 capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(out)],capture_output=True,text=True,timeout=12)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn("UI_ICON_CACHE_HOST_OK",run.stdout)
if __name__=="__main__":
    unittest.main()
