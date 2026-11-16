"""Read-only GitHub release metadata observer for explicitly reviewed publishers.

This is NOT a PKG provider. Version tags and .pkg asset names do not
establish a PS4 patch, compatible FPKG, authorization, or installability.
A publisher registry must be reviewed separately and cannot grant actions.

The CLI has an explicit --live opt-in. Offline tests inject complete fake
HTTP response bytes, with no external network or real game assets.
"""
from __future__ import annotations

import argparse
import json
import re
import ssl
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import (HTTPRedirectHandler, HTTPSHandler, Request,
                            build_opener)

from fpkg_publisher_sources import _policy

MAX_RESPONSE = 256 * 1024
MAX_POLICY = 128 * 1024
MAX_RELEASES = 20
MAX_ASSETS = 64
MAX_OUTPUT = 100
TAG = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,99}\Z")


class MetadataRejected(ValueError):
    """Malformed or untrusted release metadata. Never fallback to a PKG URL."""


class _NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        raise MetadataRejected("Redirect rejected")


def _no_duplicate_keys(pairs):
    out = {}
    for name, value in pairs:
        if name in out:
            raise MetadataRejected("Duplicate JSON keys")
        out[name] = value
    return out


def _fetch_releases(url):
    """Deliberate public metadata GET only, with verified OS TLS and no redirects."""
    context = ssl.create_default_context()
    opener = build_opener(_NoRedirect(), HTTPSHandler(context=context))
    request = Request(url, headers={
        "Accept": "application/vnd.github+json",
        "User-Agent": "PS4-FPKG-Source-Review/0.1",
        "X-GitHub-Api-Version": "2022-11-28",
    })
    with opener.open(request, timeout=12) as response:
        if response.status != 200:
            raise MetadataRejected("Unexpected API status")
        media = response.headers.get("Content-Type", "").split(";", 1)[0].lower()
        if media not in ("application/json", "application/vnd.github+json"):
            raise MetadataRejected("Unexpected API media type")
        data = response.read(MAX_RESPONSE + 1)
    if len(data) > MAX_RESPONSE:
        raise MetadataRejected("Release metadata exceeds limit")
    return data


def _observed_tags(payload):
    """Never expose download URLs, asset filenames, HTML, or release bodies."""
    if not isinstance(payload, bytes) or len(payload) > MAX_RESPONSE:
        raise MetadataRejected("Bad or oversized release response")
    try:
        document = json.loads(payload.decode("utf-8"),
                              object_pairs_hook=_no_duplicate_keys)
    except (ValueError, UnicodeError) as exc:
        raise MetadataRejected("Malformed release JSON") from exc
    if not isinstance(document, list) or len(document) > MAX_RELEASES:
        raise MetadataRejected("Unexpected release count")
    tags = []
    seen = set()
    for item in document:
        if not isinstance(item, dict):
            raise MetadataRejected("Malformed release")
        tag = item.get("tag_name")
        if not isinstance(tag, str) or TAG.fullmatch(tag) is None:
            raise MetadataRejected("Unsafe release tag")
        if tag in seen:
            raise MetadataRejected("Duplicate release tag")
        seen.add(tag)
        if type(item.get("draft")) is not bool:
            raise MetadataRejected("Unknown draft status")
        if type(item.get("prerelease")) is not bool:
            raise MetadataRejected("Unknown prerelease status")
        assets = item.get("assets")
        if not isinstance(assets, list) or len(assets) > MAX_ASSETS:
            raise MetadataRejected("Unbounded release assets")
        pkg_count = 0
        for asset in assets:
            if not isinstance(asset, dict):
                raise MetadataRejected("Malformed release asset")
            name, size = asset.get("name"), asset.get("size")
            if (not isinstance(name, str) or not 1 <= len(name) <= 255
                    or any(ord(c) < 32 or ord(c) == 127 for c in name)
                    or type(size) is not int or size < 0
                    or size > 1 << 52):
                raise MetadataRejected("Invalid release asset identity")
            if name.lower().endswith(".pkg"):
                pkg_count += 1
        if item["draft"]:
            continue
        tags.append({
            "tag": tag,
            "prerelease": item["prerelease"],
            "package_named_assets": pkg_count,
            "package_kind_verified": False,
            "fpkg_verified": False,
            "patch_compatible": False,
            "rights_verified": False,
            "download_allowed": False,
            "install_allowed": False,
        })
    return tags


def observe_reviewed_publishers(registry, fetch=None):
    """Inspect only explicitly reviewed repositories. No catalog candidates."""
    policy = _policy(registry)  # strict record shape and repo/evidence checks
    repositories = sorted({repo for (_, repo) in policy})
    if len(repositories) > MAX_OUTPUT:
        raise ValueError("Too many publisher repositories")
    getter = _fetch_releases if fetch is None else fetch
    results = []
    for repo in repositories:
        url = ("https://api.github.com/repos/"
               + "/".join(quote(p, safe="") for p in repo.split("/"))
               + "/releases?per_page=20")
        row = {
            "repository": repo,
            "status": "SOURCE_UNAVAILABLE",
            "observed_releases": [],
        }
        try:
            tags = _observed_tags(getter(url))
            row["observed_releases"] = tags
            row["status"] = ("RELEASE_TAGS_OBSERVED_UNVERIFIED"
                             if tags else "NO_RELEASES_OBSERVED")
        except (HTTPError, URLError, OSError, TimeoutError,
                MetadataRejected) as error:
            row["error_type"] = type(error).__name__
        results.append(row)
    return {
        "schema": 1,
        "mode": "GITHUB_PUBLISHER_RELEASE_METADATA_ONLY",
        "package_policy": "fpkg_only",
        "network_requested": fetch is None and bool(repositories),
        "download_allowed": False,
        "install_allowed": False,
        "rights_verified": False,
        "compatibility_confirmed": False,
        "repositories": results,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--publisher-registry", type=Path, required=True)
    parser.add_argument("--live", action="store_true",
                        help="Explicitly fetch public GitHub release metadata")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if not args.live:
        parser.error("--live is required; no implicit network requests")
    if args.publisher_registry.stat().st_size > MAX_POLICY:
        parser.error("Publisher policy too large")
    registry = json.loads(args.publisher_registry.read_text(encoding="utf-8"),
                          object_pairs_hook=_no_duplicate_keys)
    result = observe_reviewed_publishers(registry)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Read-only publisher metadata review; zero PKG downloads.")


if __name__ == "__main__":
    main()
