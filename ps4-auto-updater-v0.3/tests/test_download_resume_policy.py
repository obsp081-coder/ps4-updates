"""Native C resume-checkpoint reviewer tested without PS4 or real TLS."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ps4" / "src"
SMOKE = ROOT / "tests" / "ui_mocks" / "resume_policy_smoke.c"


class DownloadResumeNativeTests(unittest.TestCase):
    def test_resume_requires_trusted_manifest_partial_hash_etag_range_and_tls(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / "resume-policy"
            build = subprocess.run(
                [cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                 str(SRC / "download_resume.c"),
                 str(SRC / "download_http_headers.c"),
                 str(SRC / "download_http_policy.c"),
                 str(SRC / "download_sha256.c"),
                 str(SMOKE), "-o", str(exe)],
                text=True, capture_output=True, timeout=45,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(exe)], text=True, capture_output=True, timeout=20,
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("DOWNLOAD_RESUME_POLICY_HOST_OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
