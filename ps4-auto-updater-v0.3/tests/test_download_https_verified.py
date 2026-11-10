"""HOST ONLY: verified HTTPS -> real headers -> native C transport policy.

All bytes and certificates are locally generated test fixtures. Network I/O
is confined to 127.0.0.1. The hostname updates.example is used for TLS SNI
and certificate verification, NEVER resolved on the public network.
This proves Python's verified TLS adapter can feed the existing C policy;
it does NOT prove SceSsl, SceHttp, PKG sources, FW14 or PS4 download rights.
"""
from __future__ import annotations

import ctypes
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import unittest
from urllib.parse import urlsplit

SRC = Path(__file__).resolve().parents[1] / "ps4" / "src"
DATA = (b"authorized-local-https-test-data\x00\xff" * 700)
ETAG = '"test-authorized-fixture-v1"'
HOST = "updates.example"

DT_OK = 0
DT_URL_REJECTED = -3
DT_CONNECT_FAILED = -4
DT_TIMED_OUT = -12
PORT_TIMED_OUT = -3
DT_TLS_UNVERIFIED = -5
PORT_TLS_VERIFY_FAILED = -2
DT_POLICY_REJECTED = -7
DT_INCOMPLETE = -9


class CRequest(ctypes.Structure):
    _fields_ = [
        ("url", ctypes.c_char_p),
        ("expected_total", ctypes.c_uint64),
        ("resume_offset", ctypes.c_uint64),
        ("strong_etag", ctypes.c_char_p),
    ]


class CPeerFacts(ctypes.Structure):
    _fields_ = [
        ("certificate_chain_verified", ctypes.c_uint),
        ("hostname_verified", ctypes.c_uint),
        ("range_sent", ctypes.c_uint),
        ("if_range_sent", ctypes.c_uint),
    ]


OPEN = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(CRequest),
    ctypes.c_void_p, ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(CPeerFacts)
)
READ = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)
)
CLOSE = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)


class CPort(ctypes.Structure):
    _fields_ = [
        ("context", ctypes.c_void_p),
        ("open_headers", OPEN),
        ("read_body", READ),
        ("abort_request", CLOSE),
        ("close_request", CLOSE),
    ]


class CSession(ctypes.Structure):
    _fields_ = [
        ("port", CPort),
        ("remaining", ctypes.c_uint64),
        ("delivered", ctypes.c_uint64),
        ("active", ctypes.c_uint),
        ("stopped", ctypes.c_uint),
    ]


class LocalHttpsServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def handle_error(self, request, client_address):
        # Failed TLS handshakes are intentional negative tests.
        pass


class FixtureHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_GET(self):
        source_range = self.headers.get("Range")
        if_range = self.headers.get("If-Range")
        self.server.observed.append((self.path, source_range, if_range))
        if self.path == "/slow-headers":
            time.sleep(0.75)
        offset = 0
        if source_range and source_range.startswith("bytes="):
            try:
                offset = int(source_range[6:].removesuffix("-"))
            except ValueError:
                offset = 0

        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "https://other.example/fixture")
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            return

        if source_range and self.path == "/ignore-range":
            offset = 0
            status = 200
        else:
            status = 206 if source_range else 200

        payload = DATA[offset:]
        self.send_response(status)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("ETag",
                         '"different-fixture"' if self.path == "/bad-etag"
                         else ETAG)
        if status == 206:
            start = offset - 1 if self.path == "/bad-range" else offset
            self.send_header("Content-Range",
                             f"bytes {start}-{len(DATA)-1}/{len(DATA)}")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        if self.path == "/truncated":
            payload = payload[:len(payload) // 2]
        if self.path == "/slow-body":
            time.sleep(0.75)
        try:
            self.wfile.write(payload)
            self.wfile.flush()
        except (ConnectionResetError, BrokenPipeError, ssl.SSLError):
            pass


def openssl(*args):
    proc = subprocess.run(
        ["openssl", *map(str, args)],
        text=True, capture_output=True, timeout=25
    )
    if proc.returncode:
        raise AssertionError(proc.stdout + proc.stderr)


def make_ca_and_server(folder):
    """Ephemeral local CA signing an ordinary DNS SAN server certificate."""
    root = Path(folder)
    ca_key, ca_cert = root / "ca.key", root / "ca.pem"
    key, csr, cert = root / "srv.key", root / "srv.csr", root / "srv.pem"
    openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-sha256", "-days", "2", "-keyout", ca_key,
            "-out", ca_cert, "-subj", "/CN=Fixture Test CA",
            "-addext", "basicConstraints=critical,CA:TRUE",
            "-addext", "keyUsage=critical,keyCertSign,cRLSign")
    openssl("req", "-new", "-newkey", "rsa:2048", "-nodes",
            "-sha256", "-keyout", key, "-out", csr,
            "-subj", f"/CN={HOST}")
    extensions = root / "server-ext.cnf"
    extensions.write_text(
        f"subjectAltName=DNS:{HOST}\n"
        "basicConstraints=critical,CA:FALSE\n"
        "extendedKeyUsage=serverAuth\n"
        "keyUsage=digitalSignature,keyEncipherment\n",
        encoding="ascii"
    )
    openssl("x509", "-req", "-in", csr, "-CA", ca_cert,
            "-CAkey", ca_key, "-CAcreateserial", "-out", cert,
            "-days", "2", "-sha256", "-extfile", extensions)
    return ca_cert, cert, key


class VerifiedHostPort:
    """Python adapter: verified SSL handshake, then C-policy callbacks.

    Its only TCP destination is 127.0.0.1. The URL DNS name is used solely
    for certificate verification and SNI, not for socket resolution.
    """
    def __init__(self, port_number, ca_file, *, trust_ca=True,
                 timeout_seconds=3):
        self.port_number = port_number
        self.ca_file = ca_file
        self.trust_ca = trust_ca
        self.timeout_seconds = timeout_seconds
        self.connection = None
        self.response = None
        self.handshake_verified = False
        self.ssl_failure = None
        self.close_count = 0
        self._cb_open = OPEN(self.open_headers)
        self._cb_read = READ(self.read_body)
        self._cb_abort = CLOSE(self.abort_request)
        self._cb_close = CLOSE(self.close_request)
        self.port = CPort(None, self._cb_open, self._cb_read,
                          self._cb_abort, self._cb_close)

    def open_headers(self, unused, request_pointer, buffer, capacity,
                     header_size_pointer, peer_pointer):
        tcp = None
        try:
            request = request_pointer.contents
            parsed = urlsplit(request.url.decode("ascii"))
            assert parsed.scheme == "https"
            # Confined test: reject explicit ports or other URL authorities.
            assert parsed.port is None and parsed.username is None
            context = ssl.create_default_context(
                ssl.Purpose.SERVER_AUTH,
                cafile=str(self.ca_file) if self.trust_ca else None
            )
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            assert context.verify_mode == ssl.CERT_REQUIRED
            assert context.check_hostname

            tcp = socket.create_connection(
                ("127.0.0.1", self.port_number), timeout=self.timeout_seconds
            )
            self.connection = context.wrap_socket(
                tcp, server_hostname=parsed.hostname
            )
            self.connection.settimeout(self.timeout_seconds)
            self.handshake_verified = bool(self.connection.getpeercert())
            if not self.handshake_verified:
                return -1

            path = parsed.path or "/"
            if parsed.query:
                path += "?" + parsed.query
            message = [
                f"GET {path} HTTP/1.1",
                f"Host: {parsed.hostname}",
                "Accept-Encoding: identity",
                "Connection: close",
            ]
            resume = request.resume_offset > 0
            if resume:
                assert request.strong_etag
                message.append(f"Range: bytes={request.resume_offset}-")
                message.append(
                    f"If-Range: {request.strong_etag.decode('ascii')}"
                )
            self.connection.sendall(("\r\n".join(message) +
                                     "\r\n\r\n").encode("ascii"))
            self.response = http.client.HTTPResponse(self.connection)
            self.response.begin()
            if self.response.version != 11:
                return -1
            raw = (f"HTTP/1.1 {self.response.status} "
                   f"{self.response.reason}\r\n" +
                   "".join(f"{k}: {v}\r\n"
                           for k, v in self.response.headers.raw_items()) +
                   "\r\n").encode("ascii")
            if len(raw) > capacity:
                return -1
            ctypes.memmove(buffer, raw, len(raw))
            header_size_pointer[0] = len(raw)
            peer_pointer.contents.certificate_chain_verified = 1
            peer_pointer.contents.hostname_verified = 1
            peer_pointer.contents.range_sent = int(resume)
            peer_pointer.contents.if_range_sent = int(resume)
            return 0
        except ssl.SSLCertVerificationError as exc:
            self.ssl_failure = exc
            if self.connection is None and tcp is not None:
                tcp.close()
            return PORT_TLS_VERIFY_FAILED
        except TimeoutError as exc:
            self.ssl_failure = exc
            if self.connection is None and tcp is not None:
                tcp.close()
            return PORT_TIMED_OUT
        except (AssertionError, OSError, ssl.SSLError, UnicodeError,
                ValueError) as exc:
            self.ssl_failure = exc
            if self.connection is None and tcp is not None:
                tcp.close()
            return -1

    def read_body(self, unused, buffer, capacity, received):
        try:
            data = self.response.read(capacity)
            ctypes.memmove(buffer, data, len(data))
            received[0] = len(data)
            return 0
        except TimeoutError:
            return PORT_TIMED_OUT
        except (OSError, ssl.SSLError, http.client.HTTPException):
            return -1

    def abort_request(self, unused):
        return 0

    def close_request(self, unused):
        self.close_count += 1
        if self.response is not None:
            self.response.close()
            self.response = None
        if self.connection is not None:
            self.connection.close()
            self.connection = None
        return 0


class HttpsVerifiedNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("openssl"):
            raise unittest.SkipTest("OpenSSL CLI is required for TLS fixtures")
        compiler = shutil.which("cc") or shutil.which("clang")
        if not compiler:
            raise unittest.SkipTest("C compiler is required")
        cls.tmp = tempfile.TemporaryDirectory()
        try:
            ca, cert, key = make_ca_and_server(cls.tmp.name)
            cls.ca = ca
            so = Path(cls.tmp.name) / "transport.so"
            command = [
                compiler, "-std=c99", "-fPIC", "-shared",
                "-O2", "-Wall", "-Wextra", "-Werror",
                str(SRC / "download_transport.c"),
                str(SRC / "download_http_headers.c"),
                str(SRC / "download_http_policy.c"),
                "-o", str(so),
            ]
            result = subprocess.run(
                command, text=True, capture_output=True, timeout=45
            )
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            cls.native = ctypes.CDLL(str(so))
            cls.native.dt_init.argtypes = [ctypes.POINTER(CSession)]
            cls.native.dt_begin.argtypes = [
                ctypes.POINTER(CSession), ctypes.POINTER(CPort),
                ctypes.POINTER(CRequest)
            ]
            cls.native.dt_begin.restype = ctypes.c_int
            cls.native.dt_read.argtypes = [
                ctypes.POINTER(CSession), ctypes.POINTER(ctypes.c_uint8),
                ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)
            ]
            cls.native.dt_read.restype = ctypes.c_int
            cls.native.dt_finish.argtypes = [ctypes.POINTER(CSession)]
            cls.native.dt_finish.restype = ctypes.c_int
            cls.native.dt_cancel.argtypes = [ctypes.POINTER(CSession)]
            cls.native.dt_cancel.restype = ctypes.c_int

            cls.server = LocalHttpsServer(("127.0.0.1", 0), FixtureHandler)
            cls.server.observed = []
            ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ssl_context.load_cert_chain(certfile=cert, keyfile=key)
            cls.server.socket = ssl_context.wrap_socket(
                cls.server.socket, server_side=True
            )
            cls.worker = threading.Thread(
                target=cls.server.serve_forever, daemon=True
            )
            cls.worker.start()
        except Exception:
            cls.tmp.cleanup()
            raise

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.worker.join(timeout=5)
        cls.tmp.cleanup()

    def setUp(self):
        self.server.observed.clear()

    def transfer(self, path="/ok", *, host=HOST, scheme="https",
                 trusted=True, offset=0, etag=None, timeout_seconds=3):
        adapter = VerifiedHostPort(
            self.server.server_port, self.ca, trust_ca=trusted,
            timeout_seconds=timeout_seconds
        )
        session = CSession()
        self.native.dt_init(ctypes.byref(session))
        url = f"{scheme}://{host}{path}".encode("ascii")
        version = etag.encode("ascii") if etag else None
        request = CRequest(url, len(DATA), offset, version)
        state = self.native.dt_begin(
            ctypes.byref(session), ctypes.byref(adapter.port),
            ctypes.byref(request)
        )
        received = bytearray()
        if state != DT_OK:
            return state, bytes(received), adapter
        while session.delivered < session.remaining:
            out = (ctypes.c_uint8 * 4096)()
            n = ctypes.c_size_t(0)
            state = self.native.dt_read(
                ctypes.byref(session), out, 4096, ctypes.byref(n)
            )
            if state != DT_OK:
                return state, bytes(received), adapter
            received.extend(out[:n.value])
        state = self.native.dt_finish(ctypes.byref(session))
        return state, bytes(received), adapter

    def test_verified_ca_hostname_and_full_body_cross_native_policy(self):
        status, body, port = self.transfer()
        self.assertEqual(status, DT_OK)
        self.assertEqual(body, DATA)
        self.assertTrue(port.handshake_verified)
        self.assertEqual(port.close_count, 1)
        self.assertEqual(self.server.observed, [("/ok", None, None)])

    def test_verified_tls_resume_uses_range_and_if_range(self):
        status, body, port = self.transfer(offset=601, etag=ETAG)
        self.assertEqual(status, DT_OK)
        self.assertEqual(body, DATA[601:])
        self.assertTrue(port.handshake_verified)
        self.assertEqual(self.server.observed,
                         [("/ok", "bytes=601-", ETAG)])

    def test_wrong_hostname_is_rejected_by_tls_before_native_body(self):
        status, body, port = self.transfer(host="wrong.example")
        self.assertEqual(status, DT_TLS_UNVERIFIED)
        self.assertEqual(body, b"")
        self.assertFalse(port.handshake_verified)
        self.assertIsInstance(port.ssl_failure,
                              ssl.SSLCertVerificationError)

    def test_untrusted_ca_is_rejected_by_tls(self):
        status, body, port = self.transfer(trusted=False)
        self.assertEqual(status, DT_TLS_UNVERIFIED)
        self.assertEqual(body, b"")
        self.assertFalse(port.handshake_verified)
        self.assertIsInstance(port.ssl_failure,
                              ssl.SSLCertVerificationError)

    def test_http_redirect_is_blocked_after_successful_tls(self):
        status, body, port = self.transfer("/redirect")
        self.assertEqual(status, DT_POLICY_REJECTED)
        self.assertEqual(body, b"")
        self.assertTrue(port.handshake_verified)
        self.assertEqual(port.close_count, 1)
        self.assertEqual(self.server.observed, [("/redirect", None, None)])

    def test_range_etag_and_http_200_downgrade_are_rejected(self):
        for path in ("/bad-range", "/bad-etag", "/ignore-range"):
            with self.subTest(path=path):
                status, body, port = self.transfer(
                    path, offset=511, etag=ETAG
                )
                self.assertEqual(status, DT_POLICY_REJECTED)
                self.assertEqual(body, b"")
                self.assertTrue(port.handshake_verified)

    def test_truncated_verified_https_never_reports_completed(self):
        status, body, port = self.transfer("/truncated")
        self.assertEqual(status, DT_INCOMPLETE)
        self.assertGreater(len(body), 0)
        self.assertLess(len(body), len(DATA))
        self.assertEqual(port.close_count, 1)

    def test_verified_tls_slow_headers_get_bounded_timeout(self):
        status, body, adapter = self.transfer(
            "/slow-headers", timeout_seconds=0.20
        )
        self.assertEqual(status, DT_TIMED_OUT)
        self.assertEqual(body, b"")
        self.assertTrue(adapter.handshake_verified)
        self.assertIsInstance(adapter.ssl_failure, TimeoutError)
        self.assertEqual(adapter.close_count, 1)

    def test_verified_tls_slow_body_get_bounded_timeout(self):
        status, body, adapter = self.transfer(
            "/slow-body", timeout_seconds=0.20
        )
        self.assertEqual(status, DT_TIMED_OUT)
        self.assertEqual(body, b"")
        self.assertTrue(adapter.handshake_verified)
        self.assertEqual(adapter.close_count, 1)

    def test_plain_http_rejected_before_opening_any_socket(self):
        status, body, port = self.transfer(scheme="http")
        self.assertEqual(status, DT_URL_REJECTED)
        self.assertEqual(body, b"")
        self.assertFalse(port.handshake_verified)

    def test_explicit_url_port_rejected_before_open(self):
        status, body, port = self.transfer(host=HOST + ":443")
        self.assertEqual(status, DT_URL_REJECTED)
        self.assertEqual(body, b"")
        self.assertFalse(port.handshake_verified)


if __name__ == "__main__":
    unittest.main()
