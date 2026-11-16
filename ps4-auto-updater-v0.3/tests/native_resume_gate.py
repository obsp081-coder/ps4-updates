"""Host-only ctypes wrapper for the SAME native C resume policy object.

This glue is used by localhost integration tests. It asserts TLS success as
a SYNTHETIC TEST FACT, not a verified live TLS connection. NEVER use it to
authorize a PS4 download, public URL, or package installation.
"""
import ctypes
from pathlib import Path

from host_fixture_resume import FIXTURE_CONTENT_ID, FIXTURE_VERSION


class CCheckpoint(ctypes.Structure):
    _fields_ = [
        ("content_id", ctypes.c_char * 37),
        ("version", ctypes.c_char * 16),
        ("strong_etag", ctypes.c_char * 96),
        ("total_size", ctypes.c_uint64),
        ("offset", ctypes.c_uint64),
        ("full_sha256", ctypes.c_uint8 * 32),
        ("partial_sha256", ctypes.c_uint8 * 32),
    ]


class NativeResumeGate:
    def __init__(self, shared_object: Path):
        lib = ctypes.CDLL(str(shared_object))
        check = lib.dl_resume_review
        check.argtypes = [
            ctypes.POINTER(CCheckpoint),
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_char_p, ctypes.c_size_t,
            ctypes.c_int, ctypes.c_int, ctypes.c_int,
        ]
        check.restype = ctypes.c_int
        self._lib = lib
        self.check = check

    def __call__(self, checkpoint, observed_hash: str,
                 headers: bytes, total: int, trusted_digest: str):
        c = CCheckpoint()
        c.content_id = checkpoint["content_id"].encode("ascii")
        c.version = checkpoint["version"].encode("ascii")
        c.strong_etag = checkpoint["etag"].encode("ascii")
        c.total_size = checkpoint["total"]
        c.offset = checkpoint["offset"]
        c.full_sha256[:] = bytes.fromhex(checkpoint["full_sha256"])
        c.partial_sha256[:] = bytes.fromhex(checkpoint["partial_sha256"])
        full = (ctypes.c_uint8 * 32).from_buffer_copy(
            bytes.fromhex(trusted_digest)
        )
        observed = (ctypes.c_uint8 * 32).from_buffer_copy(
            bytes.fromhex(observed_hash)
        )
        # Test-only mock of independently authenticated transport.
        # The production PS4 transport remains explicitly DISABLED.
        return self.check(
            ctypes.byref(c),
            FIXTURE_CONTENT_ID.encode("ascii"),
            FIXTURE_VERSION.encode("ascii"),
            total, full, checkpoint["offset"], observed,
            headers, len(headers), 1, 1, 1,
        ) == 0
