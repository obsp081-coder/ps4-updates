"""PS4 shell exit path tested with mocked kernel and system service.

This tests call order and the non-returning failure path; real firmware
compatibility still requires a PS4 hardware test.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ps4" / "src" / "ps4_shell_exit.c"
MOCKS = Path(__file__).resolve().parent / "exit_mocks"


class NativeShellExitTests(unittest.TestCase):
    def test_system_service_success_and_failure_never_return_to_libc(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if cc is None:
            self.skipTest("C compiler not installed")
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "exit-smoke"
            compile_process = subprocess.run(
                [
                    cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-DPS4_EXIT_HOST_TEST",
                    "-I", str(MOCKS),
                    str(SOURCE), str(MOCKS / "exit_smoke.c"),
                    "-o", str(binary),
                ],
                capture_output=True, text=True, timeout=40
            )
            self.assertEqual(
                compile_process.returncode, 0,
                compile_process.stdout + compile_process.stderr
            )
            run = subprocess.run(
                [str(binary)], capture_output=True, text=True, timeout=12
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("PS4_SHELL_EXIT_HOST_OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
