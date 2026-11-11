"""Compile production-disabled pipeline with __ORBIS__ on Host.

This asserts a compile-time policy; it does not emulate FW14 behavior.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ps4" / "src"
TEST_C = ROOT / "tests" / "ui_mocks" / "pipeline_ps4_gate_smoke.c"


class DownloadPipelinePs4GateTests(unittest.TestCase):
    def test_ps4_runtime_remains_hard_disabled(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler unavailable")
        sources = [
            "download_pipeline.c", "download_queue.c",
            "download_http_headers.c", "download_http_policy.c",
            "download_storage.c", "download_file_backend.c",
        ]
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "disabled-pipeline"
            result = subprocess.run(
                [cc, "-D__ORBIS__", "-std=c99", "-O2", "-Wall",
                 "-Wextra", "-Werror",
                 *(str(SRC / name) for name in sources),
                 str(TEST_C), "-o", str(binary)],
                text=True, capture_output=True, timeout=45,
            )
            self.assertEqual(result.returncode, 0,
                             result.stdout + result.stderr)
            run = subprocess.run(
                [str(binary)], text=True, capture_output=True, timeout=15,
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("PIPELINE_PS4_RUNTIME_GATE_OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
