"""Host C test for PS4-native HTTP response policy; no real TLS/network."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ps4" / "src" / "download_http_policy.c"
FIXTURE = ROOT / "tests" / "ui_mocks" / "http_policy_smoke.c"


class DownloadHttpPolicyTests(unittest.TestCase):
    def test_fail_closed_status_length_range_etag_and_tls_facts(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "http-policy"
            build = subprocess.run(
                [cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                 str(SOURCE), str(FIXTURE), "-o", str(binary)],
                text=True, capture_output=True, timeout=45,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(binary)], text=True, capture_output=True, timeout=20,
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("DOWNLOAD_HTTP_POLICY_HOST_OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
