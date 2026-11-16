"""Bounded host-only HTTP fixture download with verified, no-overwrite publish.

This module NEVER runs on PS4 and ONLY allows plain HTTP to 127.0.0.1.
It is a real-transfer integration harness, not a Sony/FPKG downloader.
"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
from urllib.parse import urlsplit
from urllib.request import build_opener, HTTPRedirectHandler, ProxyHandler

MAX_FIXTURE_BYTES = 8 * 1024 * 1024
CHUNK_BYTES = 16 * 1024
_SAFE_FILENAME = re.compile(r"fixture-[a-z0-9_-]{1,48}\.bin\Z")
_SHA256 = re.compile(r"[0-9a-f]{64}\Z")


class FixtureTransferError(Exception):
    """A host fixture was rejected or its transferred bytes were unsafe."""


class _NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def download_fixture(
    url: str, directory: Path, filename: str, expected_size: int,
    expected_sha256: str, *, cancel_after_bytes: int | None = None,
    min_free_reserve: int = 4096,
) -> Path:
    """Download only a host-created localhost fixture with independent size/hash.

    A .part is created exclusively and removed on failure/cancel. A fully
    verified file is published with an exclusive hard link: a pre-existing
    final filename cannot be silently replaced. The caller must supply known
    size and SHA-256 from the fixture definition, never from HTTP headers.
    """
    target_url = urlsplit(url)
    try:
        port = target_url.port
    except ValueError as exc:
        raise FixtureTransferError("invalid port") from exc
    if (target_url.scheme != "http" or target_url.hostname != "127.0.0.1"
            or not port or target_url.username or target_url.password
            or target_url.fragment or not target_url.path.startswith("/")
            or "\\" in url):
        raise FixtureTransferError("only loopback HTTP fixtures are allowed")
    if not isinstance(filename, str) or not _SAFE_FILENAME.fullmatch(filename):
        raise FixtureTransferError("unsafe fixture filename")
    if (type(expected_size) is not int or not 0 < expected_size <= MAX_FIXTURE_BYTES
            or not isinstance(expected_sha256, str)
            or not _SHA256.fullmatch(expected_sha256)):
        raise FixtureTransferError("invalid independent size/hash")
    if (type(min_free_reserve) is not int or min_free_reserve < 0):
        raise FixtureTransferError("invalid disk reserve")
    if (cancel_after_bytes is not None and
            (type(cancel_after_bytes) is not int or cancel_after_bytes < 0)):
        raise FixtureTransferError("invalid cancellation threshold")

    root = Path(directory)
    if not root.is_dir() or root.is_symlink():
        raise FixtureTransferError("fixture output must be an existing directory")
    available = os.statvfs(root)
    free_bytes = available.f_bavail * available.f_frsize
    if free_bytes < expected_size + min_free_reserve:
        raise FixtureTransferError("insufficient free space")

    final = root / filename
    part = root / (filename + ".part")
    # Never overwrite, follow, or discard an existing file or partial.
    if final.exists() or final.is_symlink():
        raise FixtureTransferError("destination already exists")

    fd = None
    owns_partial = False
    try:
        try:
            fd = os.open(part, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            owns_partial = True
        except FileExistsError as exc:
            raise FixtureTransferError("partial file already exists") from exc
        digest = hashlib.sha256()
        count = 0
        opener = build_opener(ProxyHandler({}), _NoRedirect())
        try:
            with opener.open(url, timeout=4) as response:
                if response.status != 200:
                    raise FixtureTransferError("unexpected HTTP status")
                if response.geturl() != url:
                    raise FixtureTransferError("redirected response")
                lengths = response.headers.get_all("Content-Length", [])
                if (len(lengths) != 1 or not lengths[0].isdigit()
                        or int(lengths[0]) != expected_size):
                    raise FixtureTransferError("Content-Length mismatch")
                if response.headers.get("Transfer-Encoding"):
                    raise FixtureTransferError("transfer encoding is unsupported")
                if response.headers.get("Content-Encoding") not in (None, "identity"):
                    raise FixtureTransferError("compressed response is unsupported")
                while True:
                    # A one-byte read beyond the expected size catches overflow.
                    block = response.read(min(CHUNK_BYTES, expected_size - count + 1))
                    if not block:
                        break
                    if len(block) > expected_size - count:
                        raise FixtureTransferError("response larger than expected")
                    if cancel_after_bytes is not None and count >= cancel_after_bytes:
                        raise FixtureTransferError("cancelled")
                    written = 0
                    while written < len(block):
                        n = os.write(fd, block[written:])
                        if n <= 0:
                            raise FixtureTransferError("write failed")
                        written += n
                    count += len(block)
                    digest.update(block)
                if count != expected_size:
                    raise FixtureTransferError("truncated response")
        except FixtureTransferError:
            raise
        except (OSError, ValueError) as exc:
            raise FixtureTransferError("network or file transfer failed") from exc
        if digest.hexdigest() != expected_sha256:
            raise FixtureTransferError("SHA-256 mismatch")
        os.fsync(fd)
        os.close(fd)
        fd = None
        # Exclusive promotion, in the same directory, prevents overwrites.
        try:
            os.link(part, final, follow_symlinks=False)
        except FileExistsError as exc:
            raise FixtureTransferError("destination appeared during transfer") from exc
        part.unlink()
        return final
    finally:
        if fd is not None:
            os.close(fd)
        # On failure remove only the .part we created, never an earlier file.
        if owns_partial and part.exists():
            part.unlink()
