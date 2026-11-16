"""Stage-5 queue/preflight Host test, no network or PS4 hardware."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ps4" / "src" / "download_queue.c"
FIXTURE = ROOT / "tests" / "ui_mocks" / "download_queue_smoke.c"


class DownloadQueueTests(unittest.TestCase):
    def test_queue_state_machine_and_space_math(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("No C compiler")
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "download-queue-smoke"
            build = subprocess.run(
                [cc, "-std=c99", "-O2", "-Wall", "-Wextra",
                 "-Werror", str(SRC), str(FIXTURE), "-o", str(binary)],
                text=True, capture_output=True, timeout=45,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(binary)], text=True, capture_output=True, timeout=20,
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("DOWNLOAD_QUEUE_HOST_OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
