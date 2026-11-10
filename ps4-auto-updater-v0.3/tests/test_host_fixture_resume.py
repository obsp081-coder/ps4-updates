"""Actual Host-only localhost HTTP Range/If-Range recovery tests.

This exercises a private disk checkpoint, partial-file checksum, independent
full checksum, malformed 206 responses and exclusive publication.
No real HTTPS, no FPKG, no PlayStation, no background service.
"""
import errno
import hashlib
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import unittest
from unittest.mock import patch
import host_fixture_resume as fixture
from native_resume_gate import NativeResumeGate

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
        if ranged and (self.path == "/truncated" or
                (self.path == "/truncated-once" and
                 not self.server.once_truncated)):
            emitted = remaining[:len(remaining) // 2]
            if self.path == "/truncated-once":
                self.server.once_truncated = True
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
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            raise unittest.SkipTest("C compiler required for native resume gate")
        cls.compiled = tempfile.TemporaryDirectory()
        src = Path(__file__).resolve().parents[1] / "ps4" / "src"
        shared = Path(cls.compiled.name) / "libnative-resume.so"
        build = subprocess.run(
            [cc, "-std=c99", "-fPIC", "-shared", "-O2", "-Wall",
             "-Wextra", "-Werror",
             str(src / "download_resume.c"),
             str(src / "download_http_headers.c"),
             str(src / "download_http_policy.c"),
             str(src / "download_sha256.c"),
             "-o", str(shared)],
            text=True, capture_output=True, timeout=45,
        )
        if build.returncode:
            cls.compiled.cleanup()
            raise AssertionError(build.stdout + build.stderr)
        cls.native = NativeResumeGate(shared)
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
        cls.compiled.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.server.observed.clear()
        self.server.once_truncated = False

    def tearDown(self):
        self.temp.cleanup()

    def url(self, path):
        return f"http://127.0.0.1:{self.server.server_address[1]}{path}"

    def resume(self, url, root, filename, total, digest):
        # Execute the native C checkpoint reviewer against headers returned
        # by a REAL localhost HTTP 206 response before appending bytes.
        return resume_fixture(url, root, filename, total, digest,
                              native_reviewer=self.native)

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
        result = self.resume(url, self.root, FILE, len(DATA), DIGEST)
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
            self.resume(url, self.root, FILE, len(DATA), DIGEST)
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
            self.resume(url, self.root, FILE, len(DATA), DIGEST)
        self.assertFalse((self.root / FILE).exists())
        self.assertEqual(len(self.server.observed), 1)

    def test_source_identity_requires_exact_original_url(self):
        self.interrupt()
        with self.assertRaisesRegex(ResumeFixtureError, "manifest"):
            self.resume(self.url("/redirect"), self.root,
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
                        self.resume(url, root, FILE, len(DATA), DIGEST)
                    self.assertEqual((root / (FILE + ".part")).read_bytes(), before)
                    self.assertFalse((root / FILE).exists())

    def test_mutated_206_body_fails_final_digest_and_never_publishes(self):
        url = self.interrupt("/mutated")
        with self.assertRaisesRegex(ResumeFixtureError, "final SHA-256"):
            self.resume(url, self.root, FILE, len(DATA), DIGEST)
        self.assertFalse((self.root / FILE).exists())
        self.assertTrue((self.root / (FILE + ".resume.json")).exists())

    def test_mid_resume_disconnect_keeps_untrusted_partial_not_final(self):
        url = self.interrupt("/truncated")
        with self.assertRaises(ResumeFixtureError):
            self.resume(url, self.root, FILE, len(DATA), DIGEST)
        self.assertFalse((self.root / FILE).exists())
        self.assertTrue((self.root / (FILE + ".resume.json")).exists())
        # The next attempt must rehash the checkpointed prefix and discard
        # the unverified tail; this permanently truncated source still fails.
        with self.assertRaises(ResumeFixtureError):
            self.resume(url, self.root, FILE, len(DATA), DIGEST)
        self.assertEqual(self.server.observed[-1][1], "bytes=60001-")

    def test_existing_final_or_partial_is_never_overwritten(self):
        self.interrupt()
        final = self.root / FILE
        final.write_bytes(b"previous user file")
        with self.assertRaisesRegex(ResumeFixtureError, "destination exists"):
            self.resume(self.url("/ok"), self.root,
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


    def test_two_interruptions_then_verified_recovery(self):
        url = self.interrupt("/truncated-once")
        with self.assertRaises(ResumeFixtureError):
            self.resume(url, self.root, FILE, len(DATA), DIGEST)
        part = self.root / (FILE + ".part")
        self.assertGreater(part.stat().st_size, 60001)
        final = self.resume(url, self.root, FILE, len(DATA), DIGEST)
        self.assertEqual(final.read_bytes(), DATA)
        self.assertEqual([x[1] for x in self.server.observed],
                         [None, "bytes=60001-", "bytes=60001-"])

    def test_full_verified_partial_after_crash_publishes_without_http(self):
        self.interrupt()
        part = self.root / (FILE + ".part")
        with part.open("ab") as handle:
            handle.write(DATA[60001:])
            handle.flush()
            os.fsync(handle.fileno())
        final = self.resume(self.url("/ok"), self.root, FILE, len(DATA), DIGEST)
        self.assertEqual(final.read_bytes(), DATA)
        self.assertEqual(len(self.server.observed), 1)
        self.assertFalse(part.exists())

    def test_disk_full_after_partial_write_recovers_only_verified_prefix(self):
        url = self.interrupt()
        original = fixture.write_all
        def disk_full(fd, data):
            original(fd, data[:117])
            raise OSError(errno.ENOSPC, "simulated disk full")
        with patch.object(fixture, "write_all", side_effect=disk_full):
            with self.assertRaises(ResumeFixtureError):
                self.resume(url, self.root, FILE, len(DATA), DIGEST)
        self.assertGreater((self.root / (FILE + ".part")).stat().st_size, 60001)
        result = self.resume(url, self.root, FILE, len(DATA), DIGEST)
        self.assertEqual(result.read_bytes(), DATA)

    def test_crash_after_exclusive_link_preserves_complete_final(self):
        url = self.interrupt()
        with patch.object(fixture, "disk_sync",
                          side_effect=OSError(errno.EIO, "crash timing")):
            with self.assertRaises(ResumeFixtureError):
                self.resume(url, self.root, FILE, len(DATA), DIGEST)
        final = self.root / FILE
        self.assertEqual(final.read_bytes(), DATA)
        self.assertTrue((self.root / (FILE + ".part")).exists())
        result = self.resume(url, self.root, FILE, len(DATA), DIGEST)
        self.assertEqual(result.read_bytes(), DATA)
        self.assertEqual(len(self.server.observed), 2)

    def test_replaced_partial_inode_rejected_without_truncation(self):
        url = self.interrupt()
        part = self.root / (FILE + ".part")
        replacement = self.root / "replacement"
        replacement.write_bytes(part.read_bytes() + b"untrusted")
        os.replace(replacement, part)
        before = part.read_bytes()
        with self.assertRaisesRegex(ResumeFixtureError, "fingerprint"):
            self.resume(url, self.root, FILE, len(DATA), DIGEST)
        self.assertEqual(part.read_bytes(), before)
        self.assertFalse((self.root / FILE).exists())

    def test_untrusted_tail_and_modified_prefix_remain_blocked(self):
        url = self.interrupt()
        part = self.root / (FILE + ".part")
        with part.open("r+b") as handle:
            handle.seek(0)
            handle.write(b"X")
            handle.seek(0, os.SEEK_END)
            handle.write(b"extra")
        before = part.read_bytes()
        with self.assertRaisesRegex(ResumeFixtureError, "fingerprint"):
            self.resume(url, self.root, FILE, len(DATA), DIGEST)
        self.assertEqual(part.read_bytes(), before)

    def test_checkpoint_duplicate_keys_and_partial_schema_fail_closed(self):
        url = self.interrupt()
        meta = self.root / (FILE + ".resume.json")
        raw = meta.read_text()
        meta.write_text(raw[:-1] + ',"offset":60001}')
        with self.assertRaisesRegex(ResumeFixtureError, "duplicate"):
            self.resume(url, self.root, FILE, len(DATA), DIGEST)
        meta.write_text(raw.replace('"checkpoint_version":2,', ''))
        with self.assertRaisesRegex(ResumeFixtureError, "schema"):
            self.resume(url, self.root, FILE, len(DATA), DIGEST)
        self.assertFalse((self.root / FILE).exists())

    def test_symlink_resume_lock_is_never_followed(self):
        url = self.interrupt()
        protected = self.root / "protected"
        protected.write_bytes(b"do not write")
        (self.root / (FILE + ".resume.lock")).symlink_to(protected)
        with self.assertRaises(ResumeFixtureError):
            self.resume(url, self.root, FILE, len(DATA), DIGEST)
        self.assertEqual(protected.read_bytes(), b"do not write")

    def test_failed_initial_transfer_does_not_delete_replaced_leaf(self):
        url = self.url("/ok")
        part = self.root / (FILE + ".part")
        def replaced(fd, data):
            part.unlink()
            part.write_bytes(b"foreign file")
            raise OSError(errno.EIO, "write failed")
        with patch.object(fixture, "write_all", side_effect=replaced):
            with self.assertRaises(ResumeFixtureError):
                self.interrupt()
        self.assertEqual(part.read_bytes(), b"foreign file")
        self.assertFalse((self.root / FILE).exists())


if __name__ == "__main__":
    unittest.main()
