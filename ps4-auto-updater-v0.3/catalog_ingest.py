
"""Validate candidate FPKG-update metadata for manual review.

Does not download packages, verify PKG contents or install updates.
A source link or declared hash is not proof of compatibility.
"""

import argparse
import ipaddress
import json
import re
from pathlib import Path
from urllib.parse import urlsplit

CUSA = re.compile(r"CUSA[0-9]{5}\Z")
CONTENT_ID = re.compile(
    r"[A-Z]{2}[0-9]{4}-(CUSA[0-9]{5})_[0-9]{2}-[A-Z0-9]{16}\Z"
)
VERSION = re.compile(r"[0-9]{1,4}\.[0-9]{1,4}\Z")
SHA256 = re.compile(r"[A-Fa-f0-9]{64}\Z")


def _version(value):
    if not isinstance(value, str) or not VERSION.fullmatch(value):
        raise ValueError("version must look like 01.02")
    return tuple(map(int, value.split(".")))


def _source(value):
    if not isinstance(value, str) or not (1 <= len(value) <= 2048):
        raise ValueError("source_page is required")

    try:
        parsed = urlsplit(value)
        host = parsed.hostname
        port = parsed.port
    except ValueError as exc:
        raise ValueError("invalid source_page") from exc

    if (
        parsed.scheme != "https"
        or not host
        or parsed.username is not None
        or parsed.password is not None
        or port not in (None, 443)
        or host.lower() == "localhost"
        or host.lower().endswith(
            (".localhost", ".local", ".internal")
        )
    ):
        raise ValueError(
            "source_page must be a public HTTPS page"
        )

    try:
        ipaddress.ip_address(host)
    except ValueError:
        pass
    else:
        raise ValueError(
            "IP addresses are not allowed as sources"
        )

    if "." not in host or any(ord(char) < 33 for char in value):
        raise ValueError("invalid source_page")

    return value


def ingest(document):
    """Produce a candidate catalog, never installation approval."""

    if not isinstance(document, dict) or document.get("schema") != 1:
        raise ValueError("input schema must be 1")

    if document.get("fixture_only") is True:
        raise ValueError(
            "demo fixtures cannot enter the candidate catalog"
        )

    entries = document.get("submissions")

    if not isinstance(entries, list) or len(entries) > 1000:
        raise ValueError(
            "submissions must be a list of up to 1000 entries"
        )

    cleaned = []
    seen = set()

    for index, entry in enumerate(entries, start=1):
        try:
            if not isinstance(entry, dict):
                raise ValueError("entry must be an object")

            if entry.get("demo_only") is True:
                raise ValueError("demo entry rejected")

            if any(key in entry for key in (
                "download_url",
                "pkg_url",
                "install_command",
                "auto_install",
            )):
                raise ValueError(
                    "direct download/install fields forbidden"
                )

            cusa = entry.get("cusa")
            cid = entry.get("content_id")

            if (
                not isinstance(cusa, str)
                or not CUSA.fullmatch(cusa)
            ):
                raise ValueError("invalid CUSA")

            match = (
                CONTENT_ID.fullmatch(cid)
                if isinstance(cid, str)
                else None
            )

            if not match or match.group(1) != cusa:
                raise ValueError(
                    "content_id does not match CUSA"
                )

            ver = entry.get("version")
            minimum = entry.get("min_fw")

            _version(ver)
            _version(minimum)

            base = entry.get("required_base_version")

            if base is not None:
                _version(base)

            if entry.get("package_kind") != "fpkg_patch":
                raise ValueError(
                    "package_kind must declare fpkg_patch"
                )

            source_page = _source(
                entry.get("source_page")
            )

            sha = entry.get("package_sha256")

            if sha is not None and (
                not isinstance(sha, str)
                or not SHA256.fullmatch(sha)
            ):
                raise ValueError(
                    "package_sha256 must be 64 hex characters"
                )

            identity = (cusa, cid, ver)

            if identity in seen:
                raise ValueError(
                    "duplicate CUSA/content_id/version"
                )

            seen.add(identity)

            item = {
                "cusa": cusa,
                "content_id": cid,
                "version": ver,
                "min_fw": minimum,
                "source_page": source_page,
                "declared_package_kind": "fpkg_patch",
                "patch_compatibility_confirmed": False,
            }

            if base is not None:
                item["required_base_version"] = base

            if sha is not None:
                item["package_sha256_unverified"] = sha.lower()

            cleaned.append(item)

        except ValueError as exc:
            raise ValueError(
                f"submission {index}: {exc}"
            ) from exc

    cleaned.sort(
        key=lambda x: (
            x["cusa"],
            _version(x["version"]),
        )
    )

    return {
        "schema": 1,
        "fixture_only": False,
        "status": "UNVERIFIED_METADATA_ONLY",
        "auto_install": False,
        "compatibility_confirmed": False,
        "updates": cleaned,
    }


def main():
    parser = argparse.ArgumentParser(
        description=__doc__
    )

    parser.add_argument(
        "submissions",
        type=Path
    )

    parser.add_argument(
        "--out",
        type=Path,
        required=True
    )

    args = parser.parse_args()

    if args.submissions.stat().st_size > 1024 * 1024:
        parser.error("submissions file is too large")

    document = json.loads(
        args.submissions.read_text(
            encoding="utf-8"
        )
    )

    try:
        result = ingest(document)
    except ValueError as exc:
        parser.error(str(exc))

    args.out.write_text(
        json.dumps(result, indent=2) + "\n",
        encoding="utf-8"
    )

    print(
        f"Validated {len(result['updates'])} candidate(s); "
        "none approved for install"
    )


if __name__ == "__main__":
    main()
