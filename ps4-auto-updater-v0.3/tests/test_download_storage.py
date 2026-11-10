"""Host-only POSIX storage lifecycle tests; NOT FW14 hardware evidence."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ps4" / "src"
FIXTURE = ROOT / "tests" / "ui_mocks" / "storage_smoke.c"


class DownloadStorageHostTests(unittest.TestCase):
    def test_exclusive_partial_publish_cleanup_and_queue_integration(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "storage-smoke"
            build = subprocess.run(
                [cc, "-std=c99", "-O2", "-Wall", "-Wextra",
                 "-Werror", str(SRC / "download_storage.c"),
                 str(SRC / "download_queue.c"),
                 str(FIXTURE), "-o", str(binary)],
                text=True, capture_output=True, timeout=45,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(binary)], text=True, capture_output=True, timeout=20,
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("DOWNLOAD_STORAGE_HOST_OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
