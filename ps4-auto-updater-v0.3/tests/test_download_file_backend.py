"""Host-only POSIX backend tests for a future PS4 storage adapter.

No native PS4 path is enabled by these tests. All files are created inside
fresh private temporary directories and removed in the same test.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ps4" / "src"
FIXTURE = ROOT / "tests" / "ui_mocks" / "file_backend_smoke.c"


class DownloadFileBackendHostTests(unittest.TestCase):
    def test_private_dirfd_and_exclusive_publish_fail_closed(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("No C compiler")
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "file-backend-smoke"
            build = subprocess.run(
                [cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                 str(SRC / "download_file_backend.c"),
                 str(SRC / "download_storage.c"),
                 str(SRC / "download_sha256.c"),
                 str(FIXTURE), "-o", str(binary)],
                text=True, capture_output=True, timeout=45,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(binary)], text=True, capture_output=True, timeout=30,
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("DOWNLOAD_FILE_BACKEND_HOST_OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
