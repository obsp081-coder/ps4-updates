"""Stage 5 C integration: actual localhost HTTP bytes -> C parser -> queue ->
host-owned storage -> SHA-256 -> exclusive final file.

No public network, no TLS, no PS4 writes, no games or packages. The Host
fixture's synthetic 'tls-test' flag is NOT real certificate authentication.
"""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ps4" / "src"
CLIENT = ROOT / "tests" / "ui_mocks" / "pipeline_loopback_client.c"
CID = "IV0000-CUSA02343_00-SCANNER000000000"
DATA = (b"HOST-C-PIPELINE-REAL-TRANSFER-2026\x00\xff" * 6400)
DIGEST = hashlib.sha256(DATA).hexdigest()


class FixtureServer(BaseHTTPRequestHandler):
    def log_message(self, *unused):
        pass

    def do_GET(self):
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/ok")
            self.end_headers()
            return
        if self.path == "/bad-range":
            self.send_response(206)
            self.send_header("Content-Length", str(len(DATA)))
            self.send_header("Content-Range", f"bytes 0-{len(DATA)-1}/{len(DATA)}")
            self.send_header("ETag", '"fixture-v1"')
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(DATA)))
        self.send_header("ETag", '"fixture-v1"')
        if self.path == "/duplicate":
            self.send_header("Content-Length", str(len(DATA)))
        self.end_headers()
        if self.path in ("/redirect", "/bad-range"):
            return
        if self.path == "/truncated":
            body = DATA[:len(DATA) // 2]
        elif self.path == "/mutated":
            body = b"Z" + DATA[1:]
        elif self.path == "/overflow":
            body = DATA + b"extra!"
        else:
            body = DATA
        try:
            # Chunked Python writes are still ordinary HTTP Content-Length body
            # bytes. This also exercises multiple recv() calls in native C.
            for offset in range(0, len(body), 2048):
                self.wfile.write(body[offset:offset + 2048])
        except (BrokenPipeError, ConnectionResetError):
            pass


class DownloadPipelineLoopbackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            raise unittest.SkipTest("C compiler unavailable")
        cls.compiled = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.compiled.name) / "pipeline-client"
        sources = [
            SRC / "download_pipeline.c",
            SRC / "download_queue.c",
            SRC / "download_http_headers.c",
            SRC / "download_http_policy.c",
            SRC / "download_storage.c",
            SRC / "download_sha256.c",
            SRC / "download_file_backend.c",
            CLIENT,
        ]
        result = subprocess.run(
            [cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
             *map(str, sources), "-o", str(cls.binary)],
            capture_output=True, text=True, timeout=50,
        )
        if result.returncode:
            cls.compiled.cleanup()
            raise AssertionError("C pipeline build failed: " +
                                 result.stdout + result.stderr)
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), FixtureServer)
        cls.thread = threading.Thread(
            target=cls.server.serve_forever, daemon=True
        )
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "server"):
            cls.server.shutdown()
            cls.server.server_close()
            cls.thread.join(timeout=3)
        cls.compiled.cleanup()

    def run_case(self, endpoint, mode="run", trust="tls-test", success=False):
        with tempfile.TemporaryDirectory() as path:
            result = subprocess.run(
                [str(self.binary), str(self.server.server_address[1]),
                 path, endpoint, DIGEST, str(len(DATA)), mode, trust],
                capture_output=True, text=True, timeout=25,
            )
            files = list(Path(path).iterdir())
            if success:
                self.assertEqual(result.returncode, 0,
                                 result.stdout + result.stderr)
                self.assertIn(f"bytes={len(DATA)}", result.stdout)
                self.assertEqual(len(files), 1)
                self.assertEqual(files[0].name, f"{CID}-01.09.bin")
                self.assertEqual(files[0].read_bytes(), DATA)
                self.assertEqual(hashlib.sha256(files[0].read_bytes()).hexdigest(),
                                 DIGEST)
            else:
                self.assertNotEqual(result.returncode, 0,
                                    result.stdout + result.stderr)
                self.assertEqual(files, [], result.stdout + result.stderr)
            return result

    def test_real_loopback_bytes_to_native_queue_and_stored_sha(self):
        self.run_case("/ok", success=True)

    def test_malformed_or_untrusted_headers_cannot_create_final_file(self):
        for endpoint, trust in [
            ("/duplicate", "tls-test"),
            ("/redirect", "tls-test"),
            ("/bad-range", "tls-test"),
            ("/ok", "untrusted"),
        ]:
            with self.subTest(endpoint=endpoint, trust=trust):
                self.run_case(endpoint, trust=trust)

    def test_truncated_mutated_or_extra_bytes_never_complete(self):
        for endpoint in ("/truncated", "/mutated", "/overflow"):
            with self.subTest(endpoint=endpoint):
                self.run_case(endpoint)

    def test_cancel_after_real_bytes_removes_only_owned_partial(self):
        result = self.run_case("/ok", mode="cancel")
        self.assertIn("PIPELINE_LOOPBACK_CANCELLED", result.stdout)
        self.assertNotIn("bytes=0", result.stdout)


if __name__ == "__main__":
    unittest.main()
