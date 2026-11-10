"""Native stage-3 source boundary; host C, no networks or PKG files."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"ps4"/"src"
FIX=ROOT/"tests"/"ui_mocks"/"source_bridge_smoke.c"

class SourceBridgeTests(unittest.TestCase):
    def test_source_boundary_remains_read_only_and_transactional(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("No Host C compiler")
        with tempfile.TemporaryDirectory() as folder:
            executable=Path(folder)/"stage3"
            b=subprocess.run([cc,"-std=c99","-Wall","-Wextra","-Werror",
                              "-O2",str(SRC/"ui_models.c"),
                              str(SRC/"ui_compatibility.c"),
                              str(SRC/"ui_source_bridge.c"),str(FIX),
                              "-o",str(executable)],
                             capture_output=True,text=True,timeout=45)
            self.assertEqual(b.returncode,0,b.stdout+b.stderr)
            p=subprocess.run([str(executable)],
                             capture_output=True,text=True,timeout=20)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            self.assertIn("UI_SOURCE_BRIDGE_HOST_OK",p.stdout)
if __name__=="__main__":unittest.main()
