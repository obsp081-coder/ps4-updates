"""Host-only callback policy tests and compile-time PS4 fail-closed proof.

TLS success flags below are SYNTHETIC. They do not authenticate a server.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ps4" / "src"
SMOKE = ROOT / "tests" / "ui_mocks" / "transport_policy_smoke.c"


class TransportContractTests(unittest.TestCase):
    def compile_run(self, ps4_gate):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / "transport-contract"
            command = [cc, "-std=c99", "-O2", "-Wall", "-Wextra",
                       "-Werror"]
            if ps4_gate:
                command.append("-D__ORBIS__")
            command += [
                str(SRC / "download_transport.c"),
                str(SRC / "download_http_headers.c"),
                str(SRC / "download_http_policy.c"),
                str(SMOKE), "-o", str(exe),
            ]
            build = subprocess.run(
                command, text=True, capture_output=True, timeout=45
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(exe)], text=True, capture_output=True, timeout=20
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn(
                "DOWNLOAD_TRANSPORT_PS4_DISABLED_OK" if ps4_gate else
                "DOWNLOAD_TRANSPORT_HOST_POLICY_OK", run.stdout
            )

    def test_bounded_host_transport_contract(self):
        self.compile_run(False)

    def test_ps4_transport_refuses_all_callbacks(self):
        self.compile_run(True)


if __name__ == "__main__":
    unittest.main()
