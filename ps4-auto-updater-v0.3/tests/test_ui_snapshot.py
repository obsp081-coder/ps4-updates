"""Host-only snapshot file contract; no proof of PS4 filesystem behavior."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ps4" / "src" / "ui_snapshot.c"
FIXTURE = ROOT / "tests" / "ui_mocks" / "snapshot_smoke.c"


class UiSnapshotTests(unittest.TestCase):
    def test_atomic_crc_bounded_stale_metadata_snapshot(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if cc is None:
            self.skipTest("C host compiler unavailable")
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "snapshot-test"
            build = subprocess.run(
                [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                 str(SRC), str(FIXTURE), "-o", str(binary)],
                text=True, capture_output=True, timeout=45
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(binary), str(Path(tmp) / "cache.bin")],
                text=True, capture_output=True, timeout=20
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("UI_SNAPSHOT_HOST_OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
