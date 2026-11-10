"""Actual Host-only localhost HTTP Range/If-Range recovery tests.

This exercises a private disk checkpoint, partial-file checksum, independent
full checksum, malformed 206 responses and exclusive publication.
No real HTTPS, no FPKG, no PlayStation, no background service.
"""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading
import unittest

from host_fixture_resume import (
    ResumeFixtureError, start_interrupted_fixture, resume_fixture,
)

DATA = b"ONLY-TEST-DATA-PS4-HOMEBREW-RESUME\x00\xff" * 4000
DIGEST = hashlib.sha256(DATA).hexdigest()
FILE = "fixture-resume.bin"
ETAG = '"fixture-version-1"'


class RangeFixture(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_GET(self):
        ranged = self.headers.get("Range")
        self.server.observed.append((self.path, ranged, self.headers.get("If-Range")))
        if ranged and self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/ok")
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            return
        if not ranged and self.path == "/initial-redirect":
            self.send_response(302)
            self.send_header("Location", "/ok")
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            return

        expected_offset = int(ranged.removeprefix("bytes=").removesuffix("-")) if ranged else 0
        if ranged and self.path == "/ignore-range":
            status, offset = 200, 0
        else:
            status, offset = (206, expected_offset) if ranged else (200, 0)
        remaining = DATA[offset:]
        if ranged and self.path == "/mutated":
            remaining = b"X" + remaining[1:]
        if ranged and self.path == "/truncated":
            emitted = remaining[:len(remaining) // 2]
        else:
            emitted = remaining

        self.send_response(status)
        count = len(remaining)
        if ranged and self.path == "/wrong-length":
            count += 1
        self.send_header("Content-Length", str(count))
        self.send_header("ETag", '"changed-version"' if ranged and
                         self.path == "/etag-changed" else ETAG)
        if ranged and status == 206:
            start = offset - 1 if self.path == "/wrong-start" else offset
            suffix = f"bytes {start}-{len(DATA) - 1}/{len(DATA)}"
            self.send_header("Content-Range", suffix)
            if self.path == "/duplicate-range":
                self.send_header("Content-Range", suffix)
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        try:
            for i in range(0, len(emitted), 8192):
                self.wfile.write(emitted[i:i+8192])
        except (ConnectionResetError, BrokenPipeError):
            pass


class HostFixtureResumeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), RangeFixture)
        cls.server.observed = []
        cls.thread = threading.Thread(target=cls.server.serve_forever,
                                      daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=3)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.server.observed.clear()

    def tearDown(self):
        self.temp.cleanup()

    def url(self, path):
        return f"http://127.0.0.1:{self.server.server_address[1]}{path}"

    def interrupt(self, endpoint="/ok", size=60001):
        url = self.url(endpoint)
        count = start_interrupted_fixture(url, self.root, FILE,
                                          len(DATA), DIGEST, size)
        self.assertEqual(count, size)
        return url

    def test_real_http_206_resume_and_final_hash(self):
        url = self.interrupt(size=60237)
        part = self.root / (FILE + ".part")
        meta = self.root / (FILE + ".resume.json")
        self.assertEqual(part.stat().st_size, 60237)
        self.assertTrue(meta.exists())
        result = resume_fixture(url, self.root, FILE, len(DATA), DIGEST)
        self.assertEqual(result.read_bytes(), DATA)
        self.assertEqual(hashlib.sha256(result.read_bytes()).hexdigest(), DIGEST)
        self.assertFalse(part.exists())
        self.assertFalse(meta.exists())
        self.assertEqual(len(self.server.observed), 2)
        self.assertEqual(self.server.observed[1],
                         ("/ok", "bytes=60237-", ETAG))

    def test_corrupted_part_cannot_resume_even_with_unmodified_checkpoint(self):
        url = self.interrupt()
        part = self.root / (FILE + ".part")
        with part.open("r+b") as f:
            f.seek(3)
            f.write(b"Z")
        with self.assertRaisesRegex(ResumeFixtureError, "fingerprint"):
            resume_fixture(url, self.root, FILE, len(DATA), DIGEST)
        self.assertTrue(part.exists())
        self.assertTrue((self.root / (FILE + ".resume.json")).exists())
        self.assertFalse((self.root / FILE).exists())
        self.assertEqual(len(self.server.observed), 1)

    def test_tampered_or_stale_manifest_checkpoint_is_rejected(self):
        url = self.interrupt()
        meta = self.root / (FILE + ".resume.json")
        payload = json.loads(meta.read_text())
        payload["full_sha256"] = "0" * 64
        meta.write_text(json.dumps(payload))
        with self.assertRaisesRegex(ResumeFixtureError, "manifest"):
            resume_fixture(url, self.root, FILE, len(DATA), DIGEST)
        self.assertFalse((self.root / FILE).exists())
        self.assertEqual(len(self.server.observed), 1)

    def test_source_identity_requires_exact_original_url(self):
        self.interrupt()
        with self.assertRaisesRegex(ResumeFixtureError, "manifest"):
            resume_fixture(self.url("/redirect"), self.root,
                           FILE, len(DATA), DIGEST)
        self.assertEqual(len(self.server.observed), 1)

    def test_changed_etag_or_downgraded_status_refuses_append(self):
        for endpoint in ("/etag-changed", "/ignore-range", "/wrong-start",
                         "/wrong-length", "/duplicate-range", "/redirect"):
            with self.subTest(endpoint=endpoint):
                with tempfile.TemporaryDirectory() as temp:
                    root = Path(temp)
                    url = self.url(endpoint)
                    start_interrupted_fixture(url, root, FILE,
                                              len(DATA), DIGEST, 40001)
                    before = (root / (FILE + ".part")).read_bytes()
                    with self.assertRaises(ResumeFixtureError):
                        resume_fixture(url, root, FILE, len(DATA), DIGEST)
                    self.assertEqual((root / (FILE + ".part")).read_bytes(), before)
                    self.assertFalse((root / FILE).exists())

    def test_mutated_206_body_fails_final_digest_and_never_publishes(self):
        url = self.interrupt("/mutated")
        with self.assertRaisesRegex(ResumeFixtureError, "final SHA-256"):
            resume_fixture(url, self.root, FILE, len(DATA), DIGEST)
        self.assertFalse((self.root / FILE).exists())
        self.assertTrue((self.root / (FILE + ".resume.json")).exists())

    def test_mid_resume_disconnect_keeps_untrusted_partial_not_final(self):
        url = self.interrupt("/truncated")
        with self.assertRaises(ResumeFixtureError):
            resume_fixture(url, self.root, FILE, len(DATA), DIGEST)
        self.assertFalse((self.root / FILE).exists())
        self.assertTrue((self.root / (FILE + ".resume.json")).exists())
        # After a partial append, checkpoint offset no longer matches file;
        # repeated attempts are denied until explicit recovery/reset.
        with self.assertRaises(ResumeFixtureError):
            resume_fixture(url, self.root, FILE, len(DATA), DIGEST)

    def test_existing_final_or_partial_is_never_overwritten(self):
        self.interrupt()
        final = self.root / FILE
        final.write_bytes(b"previous user file")
        with self.assertRaisesRegex(ResumeFixtureError, "destination exists"):
            resume_fixture(self.url("/ok"), self.root,
                           FILE, len(DATA), DIGEST)
        self.assertEqual(final.read_bytes(), b"previous user file")
        with self.assertRaises(ResumeFixtureError):
            start_interrupted_fixture(self.url("/ok"), self.root, FILE,
                                      len(DATA), DIGEST, 40000)
        self.assertEqual(final.read_bytes(), b"previous user file")

    def test_existing_orphan_partial_is_preserved(self):
        prior = self.root / (FILE + ".part")
        prior.write_bytes(b"keep me")
        with self.assertRaises(ResumeFixtureError):
            self.interrupt()
        self.assertEqual(prior.read_bytes(), b"keep me")
        self.assertFalse((self.root / FILE).exists())

    def test_initial_redirect_never_leaves_partial(self):
        with self.assertRaises(ResumeFixtureError):
            self.interrupt("/initial-redirect")
        self.assertEqual(list(self.root.iterdir()), [])

    def test_insufficient_or_invalid_trusted_parameters_rejected(self):
        with self.assertRaises(ResumeFixtureError):
            start_interrupted_fixture(self.url("/ok"), self.root, FILE,
                                      len(DATA), "not-a-digest", 50000)
        with self.assertRaises(ResumeFixtureError):
            start_interrupted_fixture("http://example.com/x", self.root,
                                      FILE, len(DATA), DIGEST, 50000)
        with self.assertRaises(ResumeFixtureError):
            start_interrupted_fixture(self.url("/ok"), self.root,
                                      "../unsafe.bin", len(DATA), DIGEST, 50000)
        self.assertEqual(list(self.root.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
