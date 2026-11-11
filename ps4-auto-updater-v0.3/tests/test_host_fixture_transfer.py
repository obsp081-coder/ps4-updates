"""Real local HTTP transfer tests; no remote sites or real game packages."""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import tempfile
import threading
import unittest

from host_fixture_transfer import download_fixture, FixtureTransferError

DATA = (b"OPENORBiS local authorized FIXTURE bytes\x00\x10" * 1800)
SHA256 = hashlib.sha256(DATA).hexdigest()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/ok")
            self.end_headers()
            return
        if self.path == "/missing":
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if self.path == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            self.wfile.write(b"0\r\n\r\n")
            return
        if self.path == "/compressed":
            self.send_response(200)
            self.send_header("Content-Length", str(len(DATA)))
            self.send_header("Content-Encoding", "gzip")
            self.end_headers()
            self.wfile.write(DATA)
            return
        if self.path == "/length-wrong":
            self.send_response(200)
            self.send_header("Content-Length", str(len(DATA) + 2))
            self.end_headers()
            self.wfile.write(DATA)
            return
        if self.path == "/truncated":
            self.send_response(200)
            self.send_header("Content-Length", str(len(DATA)))
            self.end_headers()
            self.wfile.write(DATA[:100])
            self.close_connection = True
            return
        if self.path in ("/ok", "/mutated"):
            self.send_response(200)
            self.send_header("Content-Length", str(len(DATA)))
            self.end_headers()
            self.wfile.write(DATA if self.path == "/ok" else b"X" + DATA[1:])
            return
        self.send_response(404)
        self.end_headers()


class RealLocalTransferTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(3)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"

    def tearDown(self):
        self.temp.cleanup()

    def get(self, endpoint="/ok", **kwargs):
        return download_fixture(self.url + endpoint, self.root,
                                "fixture-test.bin", len(DATA), SHA256, **kwargs)

    def test_actual_transfer_and_hash_then_exclusive_publish(self):
        output = self.get()
        self.assertEqual(output.read_bytes(), DATA)
        self.assertEqual(hashlib.sha256(output.read_bytes()).hexdigest(), SHA256)
        self.assertFalse((self.root / "fixture-test.bin.part").exists())
        with self.assertRaises(FixtureTransferError):
            self.get()
        self.assertEqual(output.read_bytes(), DATA)

    def test_content_length_mismatch_rejected_without_file(self):
        with self.assertRaises(FixtureTransferError):
            self.get("/length-wrong")
        self.assertEqual(list(self.root.iterdir()), [])

    def test_truncated_body_rejected(self):
        with self.assertRaises(FixtureTransferError):
            self.get("/truncated")
        self.assertEqual(list(self.root.iterdir()), [])

    def test_sha_mismatch_rejected(self):
        with self.assertRaises(FixtureTransferError):
            self.get("/mutated")
        self.assertEqual(list(self.root.iterdir()), [])

    def test_redirect_and_missing_are_rejected(self):
        for endpoint in ("/redirect", "/missing", "/chunked", "/compressed"):
            with self.subTest(endpoint=endpoint), self.assertRaises(FixtureTransferError):
                self.get(endpoint)
            self.assertEqual(list(self.root.iterdir()), [])

    def test_cancel_cleanup_no_fake_completion(self):
        with self.assertRaisesRegex(FixtureTransferError, "cancelled"):
            self.get(cancel_after_bytes=1)
        self.assertEqual(list(self.root.iterdir()), [])

    def test_unsafe_external_host_and_filename_rejected_before_transfer(self):
        bad = ("http://example.com:80/ok", "http://localhost:1000/ok",
               "https://127.0.0.1:443/ok", "http://127.0.0.1:80@evil.com/ok")
        for url in bad:
            with self.subTest(url=url), self.assertRaises(FixtureTransferError):
                download_fixture(url, self.root, "fixture-test.bin", len(DATA), SHA256)
        with self.assertRaises(FixtureTransferError):
            download_fixture(self.url + "/ok", self.root,
                             "../../escape.bin", len(DATA), SHA256)
        self.assertEqual(list(self.root.iterdir()), [])

    def test_partial_already_exists_and_not_deleted(self):
        part = self.root / "fixture-test.bin.part"
        part.write_bytes(b"valuable saved partial")
        with self.assertRaises(FixtureTransferError):
            self.get()
        self.assertEqual(part.read_bytes(), b"valuable saved partial")

    def test_preflight_space_and_digest_validation(self):
        with self.assertRaisesRegex(FixtureTransferError, "insufficient free space"):
            self.get(min_free_reserve=10**20)
        with self.assertRaises(FixtureTransferError):
            download_fixture(self.url + "/ok", self.root,
                             "fixture-test.bin", len(DATA), "bad")
        self.assertEqual(list(self.root.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
