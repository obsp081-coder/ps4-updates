"""SHA-256 native C host regression against independently known vectors."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ps4" / "src"
FIXTURE = ROOT / "tests" / "ui_mocks" / "sha256_smoke.c"


class Sha256NativeTests(unittest.TestCase):
    def test_fips_vectors_streaming_boundaries_and_overflow(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("No C compiler available")
        with tempfile.TemporaryDirectory() as root:
            binary = Path(root) / "sha256-smoke"
            build = subprocess.run(
                [cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                 str(SRC / "download_sha256.c"), str(FIXTURE),
                 "-o", str(binary)],
                capture_output=True, text=True, timeout=45,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(binary)], capture_output=True, text=True, timeout=30
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("NATIVE_SHA256_HOST_OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
