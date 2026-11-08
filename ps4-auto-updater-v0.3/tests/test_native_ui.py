"""Native UI host smoke tests with fake VideoOut/Pad calls.

Host tests exercise control flow and resource cleanup, not PS4 hardware.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
UI_SOURCE = ROOT / "ps4" / "src" / "scanner_ui.cpp"
MOCK_DIR = Path(__file__).resolve().parent / "ui_mocks"
MOCK_TEST = MOCK_DIR / "ui_smoke.cpp"


class NativeUiSmokeTests(unittest.TestCase):

    def test_video_pad_navigation_and_cleanup(self):
        cxx = shutil.which("g++") or shutil.which("clang++")
        if cxx is None:
            self.skipTest("No C++ compiler available")

        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "ui-smoke"
            compile_process = subprocess.run(
                [
                    cxx, "-std=c++17", "-O2", "-Wall", "-Wextra",
                    "-Werror", "-DPS4_UI_HOST_TEST", "-I", str(MOCK_DIR),
                    str(UI_SOURCE), str(MOCK_TEST), "-o", str(binary),
                ],
                capture_output=True, text=True, timeout=40
            )
            self.assertEqual(compile_process.returncode, 0,
                             compile_process.stdout + compile_process.stderr)
            run = subprocess.run(
                [str(binary)], capture_output=True, text=True, timeout=12
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("UI_HOST_SMOKE_OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
