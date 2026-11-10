"""Offline, non-installing PS4 patch identity and SHA-256 evidence gate.

Never grants FPKG authenticity, publisher rights, PS4 install compatibility,
download/install permissions or proof of installed game. Inputs are an
untrusted local manifest, one already-owned PKG file and an inventory snapshot.
Even METADATA_HASH_MATCH_REVIEW_ONLY is not a trusted update verdict.

Uses one no-follow regular-file descriptor for PKG SFO reads and hashing,
and bounded-memory streaming. No network or PS4 API. Real hardware tests needed.
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
import re
import stat
from pathlib import Path

from catalog_ingest import CONTENT_ID, CUSA, SHA256, VERSION
from pkg_local_inspector import numeric_version, read_pkg_metadata_stream

MAX_PKG_BYTES = 1 << 42   # Review bound, not a promise of PS4 storage support.
HASH_CHUNK = 1024 * 1024
MANIFEST_FIELDS = frozenset((
    "schema", "review_mode", "declared_package_kind",
    "cusa", "content_id", "patch_version",
    "required_base_version", "minimum_fw", "size_bytes", "sha256",
))


def validate_manifest(manifest):
    """Validate shape, not provenance. Reject any action-bearing extensions."""
    if not isinstance(manifest, dict) or set(manifest) != MANIFEST_FIELDS:
        raise ValueError("Manifest must use exact review-only schema")
    if (type(manifest["schema"]) is not int or manifest["schema"] != 1
            or manifest["review_mode"] != "UNVERIFIED_LOCAL_MANIFEST"
            or manifest["declared_package_kind"] != "fpkg_patch"):
        raise ValueError("Unsupported manifest policy")
    title, cid = manifest["cusa"], manifest["content_id"]
    match = CONTENT_ID.fullmatch(cid) if isinstance(cid, str) else None
    if (not isinstance(title, str) or not CUSA.fullmatch(title)
            or not match or match.group(1) != title):
        raise ValueError("Invalid manifest CUSA/content ID binding")
    for key in ("patch_version", "required_base_version", "minimum_fw"):
        value = manifest[key]
        if not isinstance(value, str) or not VERSION.fullmatch(value):
            raise ValueError("Invalid manifest " + key)
    size = manifest["size_bytes"]
    if type(size) is not int or not 0 < size <= MAX_PKG_BYTES:
        raise ValueError("Invalid declared package size")
    sha = manifest["sha256"]
    if not isinstance(sha, str) or not SHA256.fullmatch(sha):
        raise ValueError("Invalid manifest SHA-256")
    # Return a normalized copy without caller-owned references.
    clean = dict(manifest)
    clean["sha256"] = sha.lower()
    return clean


def _stat_key(st):
    return (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns)


def review_local_patch(path, manifest, inventory, firmware="14.00"):
    """Read a locally owned PKG and compare observations to a declared hash.

    A match is still REVIEW ONLY: source signature, FPKG status, base
    authenticity and PlayStation installer behavior remain unverified.
    """
    claim = validate_manifest(manifest)
    running = numeric_version(firmware)
    if running is None:
        raise ValueError("Invalid target firmware")
    if (not isinstance(inventory, dict)
            or not isinstance(inventory.get("games"), list)
            or len(inventory["games"]) > 1000):
        raise ValueError("Invalid bounded inventory")
    report = {
        "schema": 1,
        "mode": "OFFLINE_FPKG_PATCH_EVIDENCE_REVIEW",
        "cusa": claim["cusa"],
        "claimed_patch_version": claim["patch_version"],
        "status": "UNREADABLE_OR_UNSUPPORTED_PKG",
        "sha256_matched": False,
        "pkg_header_sfo_consistent": False,
        "package_category_observed": None,
        "declared_package_kind": "fpkg_patch",
        "fpkg_confirmed": False,
        "publisher_rights_verified": False,
        "base_patch_compatibility_confirmed": False,
        "installed_confirmed": False,
        "download_allowed": False,
        "install_allowed": False,
    }
    # Fail closed if the platform cannot provide no-follow path opens.
    if not getattr(os, "O_NOFOLLOW", 0):
        report["status"] = "NOFOLLOW_UNAVAILABLE"
        return report
    flags = os.O_RDONLY | os.O_NOFOLLOW | getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NONBLOCK", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as error:
        report["error_type"] = type(error).__name__
        return report

    try:
        with os.fdopen(descriptor, "rb", buffering=0) as stream:
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode):
                report["status"] = "NOT_A_REGULAR_FILE"
                return report
            if not 0 < before.st_size <= MAX_PKG_BYTES:
                report["status"] = "INVALID_PKG_SIZE"
                return report
            if before.st_size != claim["size_bytes"]:
                report["status"] = "SIZE_MISMATCH"
                return report
            try:
                details = read_pkg_metadata_stream(stream)
            except (ValueError, OSError) as error:
                report["error_type"] = type(error).__name__
                return report
            report["package_category_observed"] = details["category"]
            if not details["header_sfo_id_match"]:
                report["status"] = "HEADER_SFO_ID_CONFLICT"
                return report
            report["pkg_header_sfo_consistent"] = True
            if details["title_id"] != details["cusa"]:
                report["status"] = "TITLE_ID_CONFLICT"
                return report
            if details["category"] != "gp":
                # Only PS4 Game Application Patch category documented as gp.
                # In particular, gp != independent FPKG authenticity proof.
                report["status"] = "NOT_GAME_PATCH_CATEGORY"
                return report
            if (details["cusa"] != claim["cusa"]
                    or details["header_content_id"] != claim["content_id"]):
                report["status"] = "MANIFEST_IDENTITY_MISMATCH"
                return report
            if details["app_ver"] != claim["patch_version"]:
                report["status"] = "MANIFEST_PATCH_VERSION_MISMATCH"
                return report
            if (details["declared_min_fw"] is None
                    or details["declared_min_fw"] != claim["minimum_fw"]):
                report["status"] = "MANIFEST_FIRMWARE_MISMATCH"
                return report

            stream.seek(0)
            digest = hashlib.sha256()
            remaining = before.st_size
            while remaining:
                block = stream.read(min(HASH_CHUNK, remaining))
                if not block:
                    report["status"] = "FILE_CHANGED_DURING_REVIEW"
                    return report
                digest.update(block)
                remaining -= len(block)
            after = os.fstat(stream.fileno())
            try:
                current_path = os.lstat(path)
            except OSError:
                report["status"] = "FILE_CHANGED_DURING_REVIEW"
                return report
            if (_stat_key(before) != _stat_key(after)
                    or not stat.S_ISREG(current_path.st_mode)
                    or (before.st_dev, before.st_ino)
                       != (current_path.st_dev, current_path.st_ino)):
                report["status"] = "FILE_CHANGED_DURING_REVIEW"
                return report
            if not hmac.compare_digest(
                    digest.hexdigest(), claim["sha256"]):
                report["status"] = "SHA256_MISMATCH"
                return report
            report["sha256_matched"] = True
    except OSError as error:
        report["status"] = "FILE_READ_ERROR"
        report["error_type"] = type(error).__name__
        return report

    games = [g for g in inventory["games"] if
             isinstance(g, dict) and g.get("cusa") == claim["cusa"]]
    if len(games) != 1:
        report["status"] = "GAME_NOT_UNIQUELY_IN_SNAPSHOT"
        return report
    base = games[0]
    if base.get("content_id") != claim["content_id"]:
        # Even a same-CUSA patch with different CID requires independently
        # verified base/patch binding. The offline reviewer cannot assert it.
        report["status"] = "BASE_CONTENT_ID_MISMATCH"
        return report
    current = numeric_version(base.get("app_ver"))
    expected = numeric_version(claim["required_base_version"])
    target = numeric_version(claim["patch_version"])
    if current is None or current != expected:
        report["status"] = "BASE_VERSION_MISMATCH"
    elif target is None or target <= current:
        report["status"] = "PATCH_NOT_NEWER"
    elif numeric_version(claim["minimum_fw"]) > running:
        report["status"] = "FIRMWARE_TOO_LOW"
    elif base.get("package_kind") == "retail":
        report["status"] = "RETAIL_BASE_UNSUPPORTED"
    elif base.get("package_kind") != "fpkg":
        report["status"] = "BASE_FPKG_KIND_UNCONFIRMED"
    else:
        # Even a claimed FPKG base + SHA-256 + identity match does NOT
        # provide an install permit, verified rights, or confirmed FPKG.
        report["status"] = "METADATA_HASH_MATCH_REVIEW_ONLY"
    return report


def _no_duplicate_keys(pairs):
    output = {}
    for key, value in pairs:
        if key in output:
            raise ValueError("Duplicate input JSON key")
        output[key] = value
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pkg", type=Path, help="Existing locally owned PKG")
    parser.add_argument("manifest", type=Path, help="Independent hash declaration")
    parser.add_argument("inventory", type=Path, help="Read-only inventory snapshot")
    parser.add_argument("--firmware", default="14.00")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    def load(path):
        if path.stat().st_size > 2 * 1024 * 1024:
            raise ValueError("JSON input exceeds 2 MiB")
        return json.loads(path.read_text(encoding="utf-8"),
                          object_pairs_hook=_no_duplicate_keys)
    try:
        result = review_local_patch(args.pkg, load(args.manifest),
                                    load(args.inventory), args.firmware)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    output = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.out:
        args.out.write_text(output, encoding="utf-8")
    print(output, end="")


if __name__ == "__main__":
    main()
