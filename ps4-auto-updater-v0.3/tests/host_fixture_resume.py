"""HOST TEST ONLY: real localhost HTTP Range/If-Range with disk checkpoint.

This is not a PS4 downloader or a TLS implementation. It is deliberately
confined to 127.0.0.1 test data (maximum 8 MiB). A checkpoint alone does NOT
authorize a resume: trusted manifest, on-disk prefix SHA-256, source identity,
response range/ETag and final SHA-256 must all match.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import stat
from urllib.parse import urlsplit
from urllib.request import build_opener, HTTPRedirectHandler, ProxyHandler, Request

CAP = 8 * 1024 * 1024
CHUNK = 16 * 1024
NAME = re.compile(r"fixture-[a-z0-9_-]{1,48}\.bin\Z")
HEX = re.compile(r"[0-9a-f]{64}\Z")
RANGE = re.compile(r"bytes ([0-9]+)-([0-9]+)/([0-9]+)\Z")
ETAG = re.compile(r'"[\x21\x23-\x7e]{1,92}"\Z')
CHECKPOINT_KEYS = {
    "source", "filename", "total", "offset", "full_sha256",
    "partial_sha256", "etag",
}


class ResumeFixtureError(Exception):
    pass


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def validate(url, directory, filename, total, full_sha):
    p = urlsplit(url)
    try:
        port = p.port
    except ValueError as exc:
        raise ResumeFixtureError("invalid port") from exc
    if (p.scheme != "http" or p.hostname != "127.0.0.1"
            or not port or p.username or p.password or p.fragment
            or not p.path.startswith("/") or "\\" in url):
        raise ResumeFixtureError("only loopback fixture URLs allowed")
    if not isinstance(filename, str) or not NAME.fullmatch(filename):
        raise ResumeFixtureError("unsafe filename")
    if type(total) is not int or not 0 < total <= CAP:
        raise ResumeFixtureError("invalid trusted length")
    if not isinstance(full_sha, str) or not HEX.fullmatch(full_sha):
        raise ResumeFixtureError("invalid trusted digest")
    directory = Path(directory)
    st = directory.lstat()
    if (not stat.S_ISDIR(st.st_mode) or st.st_uid != os.geteuid()
            or st.st_mode & 0o022):
        raise ResumeFixtureError("output directory not private")
    return directory


def names(root, name):
    return root / (name + ".part"), root / (name + ".resume.json"), root / name


def open_exclusive(path):
    return os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                   getattr(os, "O_NOFOLLOW", 0), 0o600)


def disk_sync(root):
    fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY |
                 getattr(os, "O_NOFOLLOW", 0))
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def header_one(response, name):
    values = response.headers.get_all(name, [])
    if len(values) != 1:
        raise ResumeFixtureError(f"missing or duplicate {name}")
    return values[0]


def response_identity(response, expected_status, expected_length):
    if response.status != expected_status:
        raise ResumeFixtureError("HTTP status mismatch")
    length = header_one(response, "Content-Length")
    if not length.isascii() or not length.isdecimal() or int(length) != expected_length:
        raise ResumeFixtureError("Content-Length mismatch")
    if response.headers.get("Transfer-Encoding"):
        raise ResumeFixtureError("transfer coding not supported")
    if response.headers.get("Content-Encoding") not in (None, "identity"):
        raise ResumeFixtureError("content coding not supported")
    if response.headers.get("Location"):
        raise ResumeFixtureError("redirect denied")
    tag = header_one(response, "ETag")
    if not ETAG.fullmatch(tag):
        raise ResumeFixtureError("strong ETag required")
    return tag


def hash_fd(fd, expected_length):
    st = os.fstat(fd)
    if not stat.S_ISREG(st.st_mode) or st.st_nlink != 1 or st.st_size != expected_length:
        raise ResumeFixtureError("partial file type, link count or size changed")
    os.lseek(fd, 0, os.SEEK_SET)
    digest = hashlib.sha256()
    consumed = 0
    while consumed < expected_length:
        block = os.read(fd, min(CHUNK, expected_length - consumed))
        if not block:
            raise ResumeFixtureError("stored partial unexpectedly short")
        digest.update(block)
        consumed += len(block)
    return digest.hexdigest()


def write_all(fd, data):
    offset = 0
    while offset < len(data):
        n = os.write(fd, data[offset:])
        if n <= 0:
            raise ResumeFixtureError("write failed")
        offset += n


def start_interrupted_fixture(url, directory, filename, total, full_sha,
                               stop_after):
    """Persist a verified prefix and an exclusive checkpoint after fsync.

    Crash before checkpoint creation leaves an unclaimed .part that cannot be
    resumed automatically; it must not be silently overwritten or deleted.
    """
    root = validate(url, directory, filename, total, full_sha)
    if type(stop_after) is not int or not 0 < stop_after < total:
        raise ResumeFixtureError("invalid interruption point")
    part, meta, final = names(root, filename)
    if final.exists() or final.is_symlink():
        raise ResumeFixtureError("destination exists")
    own_part = own_meta = False
    completed_checkpoint = False
    fd = None
    try:
        fd = open_exclusive(part)
        own_part = True
        opener = build_opener(ProxyHandler({}), NoRedirect())
        request = Request(url, headers={"Accept-Encoding": "identity"})
        with opener.open(request, timeout=4) as response:
            etag = response_identity(response, 200, total)
            if response.headers.get_all("Content-Range", []):
                raise ResumeFixtureError("unsolicited Content-Range")
            received = 0
            digest = hashlib.sha256()
            while received < stop_after:
                data = response.read(min(CHUNK, stop_after - received))
                if not data:
                    raise ResumeFixtureError("connection lost before checkpoint")
                write_all(fd, data)
                digest.update(data)
                received += len(data)
        os.fsync(fd)
        os.close(fd)
        fd = None
        payload = {
            "source": url, "filename": filename, "total": total,
            "offset": received, "full_sha256": full_sha,
            "partial_sha256": digest.hexdigest(), "etag": etag,
        }
        meta_fd = open_exclusive(meta)
        own_meta = True
        with os.fdopen(meta_fd, "w", encoding="utf-8") as dest:
            json.dump(payload, dest, sort_keys=True, separators=(",", ":"))
            dest.flush()
            os.fsync(dest.fileno())
        disk_sync(root)
        completed_checkpoint = True
        return received
    except ResumeFixtureError:
        raise
    except (OSError, ValueError) as exc:
        raise ResumeFixtureError("initial transfer or checkpoint failed") from exc
    finally:
        if fd is not None:
            os.close(fd)
        if not completed_checkpoint:
            if own_meta:
                meta.unlink(missing_ok=True)
            if own_part:
                part.unlink(missing_ok=True)


def load_checkpoint(meta):
    fd = os.open(meta, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_nlink != 1 or st.st_size > 2048:
            raise ResumeFixtureError("checkpoint file rejected")
        raw = os.read(fd, 2049)
        if len(raw) > 2048:
            raise ResumeFixtureError("checkpoint too large")
    finally:
        os.close(fd)
    try:
        payload = json.loads(raw)
    except (UnicodeDecodeError, ValueError) as exc:
        raise ResumeFixtureError("malformed checkpoint") from exc
    if type(payload) is not dict or set(payload) != CHECKPOINT_KEYS:
        raise ResumeFixtureError("checkpoint schema changed")
    return payload


def resume_fixture(url, directory, filename, total, full_sha):
    """One safe resume attempt; failed appends require a fresh explicit reset.

    On validation failure keep existing partial/checkpoint untouched. After
    partial write failure, the checkpoint deliberately stays stale, so no
    second resume can trust the changed file automatically.
    """
    root = validate(url, directory, filename, total, full_sha)
    part, meta, final = names(root, filename)
    if final.exists() or final.is_symlink():
        raise ResumeFixtureError("destination exists")
    cp = load_checkpoint(meta)
    if (cp["source"] != url or cp["filename"] != filename or
            type(cp["total"]) is not int or cp["total"] != total or
            cp["full_sha256"] != full_sha or
            type(cp["offset"]) is not int or not 0 < cp["offset"] < total or
            not isinstance(cp["partial_sha256"], str) or
            not HEX.fullmatch(cp["partial_sha256"]) or
            not isinstance(cp["etag"], str) or not ETAG.fullmatch(cp["etag"])):
        raise ResumeFixtureError("checkpoint does not match trusted manifest")
    offset = cp["offset"]
    fd = os.open(part, os.O_RDWR | getattr(os, "O_NOFOLLOW", 0))
    try:
        initial = os.fstat(fd)
        if (initial.st_uid != os.geteuid() or initial.st_nlink != 1
                or hash_fd(fd, offset) != cp["partial_sha256"]):
            raise ResumeFixtureError("partial prefix fingerprint changed")
        opener = build_opener(ProxyHandler({}), NoRedirect())
        request = Request(url, headers={
            "Accept-Encoding": "identity", "Range": f"bytes={offset}-",
            "If-Range": cp["etag"],
        })
        with opener.open(request, timeout=4) as response:
            current = response_identity(response, 206, total - offset)
            if current != cp["etag"]:
                raise ResumeFixtureError("ETag changed")
            parts = RANGE.fullmatch(header_one(response, "Content-Range"))
            if (not parts or tuple(map(int, parts.groups())) !=
                    (offset, total - 1, total)):
                raise ResumeFixtureError("Content-Range changed")
            if hash_fd(fd, offset) != cp["partial_sha256"]:
                raise ResumeFixtureError("partial changed during request")
            os.lseek(fd, offset, os.SEEK_SET)
            received = offset
            while received < total:
                block = response.read(min(CHUNK, total - received + 1))
                if not block:
                    raise ResumeFixtureError("resumed response truncated")
                if len(block) > total - received:
                    raise ResumeFixtureError("resumed response exceeds total")
                write_all(fd, block)
                received += len(block)
            if response.read(1):
                raise ResumeFixtureError("unexpected extra bytes")
        os.fsync(fd)
        if hash_fd(fd, total) != full_sha:
            raise ResumeFixtureError("final SHA-256 mismatch")
        # All writes and verified hash refer to the same inode, kept open.
        current_stat = part.lstat()
        if ((current_stat.st_dev, current_stat.st_ino) !=
                (initial.st_dev, initial.st_ino)):
            raise ResumeFixtureError("partial inode replaced")
        os.link(part, final, follow_symlinks=False)
        disk_sync(root)
        part.unlink()
        meta.unlink()
        disk_sync(root)
        return final
    except ResumeFixtureError:
        raise
    except (OSError, ValueError) as exc:
        raise ResumeFixtureError("resume transfer or publication failed") from exc
    finally:
        os.close(fd)
