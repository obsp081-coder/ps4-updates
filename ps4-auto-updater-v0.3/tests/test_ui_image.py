"""Bounded, native-independent PNG decoding tests; no PS4 runtime proof."""
from pathlib import Path
import tempfile
import shutil
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"ps4"/"src"/"ui_image.c"
FIX=ROOT/"tests"/"ui_mocks"/"icon_smoke.c"
class UiImageTests(unittest.TestCase):
    def test_png_to_argb_and_reject_corrupt_or_giant(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if cc is None:
            self.skipTest("Host C compiler not installed")
        with tempfile.TemporaryDirectory() as td:
            binary=Path(td)/"icon"
            build=subprocess.run([cc,"-std=c11","-O2","-Wall","-Wextra",
                                  "-Werror",str(SRC),str(FIX),"-o",str(binary),
                                  "-lm"],capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=15)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn("UI_IMAGE_HOST_OK",run.stdout)
if __name__=="__main__":
    unittest.main()
