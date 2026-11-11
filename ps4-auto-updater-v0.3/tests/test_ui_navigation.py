"""Host-only navigation state machine tests; not a PS4 hardware result."""
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"ps4"/"src"/"ui_navigation.c"
FIX=ROOT/"tests"/"ui_mocks"/"navigation_smoke.c"
class UiNavigationTests(unittest.TestCase):
    def test_bounded_navigation_pages_and_search(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("No host C compiler")
        with tempfile.TemporaryDirectory() as tmp:
            executable=Path(tmp)/"navigation"
            build=subprocess.run([cc,"-std=c99","-Wall","-Wextra",
                                  "-Werror","-O2",str(SRC),str(FIX),
                                  "-o",str(executable)],
                                 text=True,capture_output=True,timeout=45)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            test=subprocess.run([str(executable)],
                                text=True,capture_output=True,timeout=15)
            self.assertEqual(test.returncode,0,test.stdout+test.stderr)
            self.assertIn("UI_NAVIGATION_HOST_OK",test.stdout)
            # Keep the 256-record Arabic search baseline visible in CI.
            # It is Host CPU time, NOT PS4 runtime or a pass/fail threshold.
            import re
            baseline = re.search(
                r"UI_SEARCH_256_ARABIC_HOST_CPU_MS_PER_128: ([0-9]+\.[0-9]+)",
                test.stdout,
            )
            self.assertIsNotNone(
                baseline, "Host search stress test did not report CPU time"
            )
            print(
                "HOST_ONLY_SEARCH_256_TITLES_128_QUERIES_CPU_MS="
                + baseline.group(1),
                flush=True,
            )
if __name__=="__main__":
    unittest.main()
