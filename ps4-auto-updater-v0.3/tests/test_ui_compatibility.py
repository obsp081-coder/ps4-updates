"""Pure C compatibility-engine test on Host; no PS4 API or PKG execution."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ps4" / "src" / "ui_compatibility.c"
FIXTURE = ROOT / "tests" / "ui_mocks" / "compatibility_smoke.c"


class UiCompatibilityHostTests(unittest.TestCase):
    def test_all_untrusted_and_incompatible_cases_fail_closed(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if cc is None:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as tmp:
            executable = Path(tmp) / "compatibility-smoke"
            build = subprocess.run(
                [cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
                 "-O2", str(SOURCE), str(FIXTURE), "-o", str(executable)],
                text=True, capture_output=True, timeout=45,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(executable)], text=True, capture_output=True, timeout=20
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("UI_COMPATIBILITY_HOST_OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
