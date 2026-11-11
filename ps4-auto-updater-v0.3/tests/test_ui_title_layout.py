"""Host-only tests for bounded Arabic/Latin title direction runs.

This validates layout logic, not full UAX #9, glyph shaping, PS4 rendering or
physical PS4 firmware support. No network or game content is accessed.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ps4" / "src" / "ui_title_layout.c"
SMOKE = Path(__file__).resolve().parent / "ui_mocks" / "title_layout_smoke.c"


class MixedTitleLayoutTests(unittest.TestCase):
    def test_direction_runs_bounds_and_marks(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("No C compiler available")
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "title-smoke"
            command = [compiler, "-std=c99", "-O2", "-Wall", "-Wextra",
                       "-Werror", "-o", str(target), str(SOURCE), str(SMOKE)]
            built = subprocess.run(command, capture_output=True, text=True,
                                   timeout=35)
            self.assertEqual(built.returncode, 0,
                             built.stdout + built.stderr)
            ran = subprocess.run([str(target)], capture_output=True,
                                 text=True, timeout=15)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("UI_TITLE_LAYOUT_HOST_OK", ran.stdout)


if __name__ == "__main__":
    unittest.main()
