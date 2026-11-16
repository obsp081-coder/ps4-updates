"""Strict native HTTP response header parser test on Host, no real TLS."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ps4" / "src"
FIXTURE = ROOT / "tests" / "ui_mocks" / "http_headers_smoke.c"


class DownloadHttpHeaderTests(unittest.TestCase):
    def test_strict_headers_and_policy_handoff_fail_closed(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as root:
            exe = Path(root) / "headers-smoke"
            command = [
                cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                str(SRC / "download_http_headers.c"),
                str(SRC / "download_http_policy.c"),
                str(FIXTURE), "-o", str(exe),
            ]
            build = subprocess.run(command, capture_output=True, text=True,
                                   timeout=45)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True,
                                 timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("DOWNLOAD_HTTP_HEADERS_HOST_OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
