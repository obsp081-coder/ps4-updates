"""Offline publisher-origin cross-check for authorized FPKG HOMEbrew sources.

A reviewed GitHub owner/repository is *not* evidence of distribution rights
or patch compatibility. This module never fetches a release, accepts a PKG
download URL, enables download/install, or labels source contents verified.

Policy is independent of untrusted submitted metadata. An operator must
separately review the publisher's rights and release provenance; the output
still remains manual-review-only even if a registry entry matches.
"""
from __future__ import annotations

import re

from catalog_ingest import CUSA

MAX_REVIEWED_SOURCES = 100
MAX_RECORDS = 1000
OWNER_REPO = re.compile(r"[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9._-]{1,100}\Z")
GITHUB_RELEASE = re.compile(
    r"https://github\.com/"
    r"(?P<repo>[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9._-]{1,100})"
    r"/releases/tag/(?P<tag>[A-Za-z0-9][A-Za-z0-9._-]{0,99})\Z"
)
EVIDENCE_URL = re.compile(
    r"https://github\.com/"
    r"(?P<repo>[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9._-]{1,100})"
    r"/(?:blob|tree)/[A-Za-z0-9._/-]{1,240}\Z"
)
FORBIDDEN = frozenset((
    "download_url", "pkg_url", "package_url", "install_url",
    "auto_install", "download_allowed", "install_allowed",
    "downloads_enabled", "install_enabled", "rights_verified",
    "source_authorized", "compatibility_confirmed",
))


def _repo(repo):
    if not isinstance(repo, str) or not OWNER_REPO.fullmatch(repo):
        raise ValueError("Invalid exact GitHub repository")
    owner, name = repo.split("/")
    if owner.endswith("-") or name in (".", "..") or name.endswith("."):
        raise ValueError("Invalid repository path")
    return repo.casefold()


def _policy(registry):
    if (not isinstance(registry, dict)
            or set(registry) != {"schema", "sources"}
            or type(registry.get("schema")) is not int
            or registry["schema"] != 1
            or not isinstance(registry.get("sources"), list)
            or len(registry["sources"]) > MAX_REVIEWED_SOURCES):
        raise ValueError("Invalid source review registry")
    approved = {}
    for item in registry["sources"]:
        if (not isinstance(item, dict)
                or set(item) != {
                    "cusa", "repository", "evidence_url",
                    "review_note", "review_ticket"
                }):
            raise ValueError("Source record must have exact audit fields")
        title = item["cusa"]
        if not isinstance(title, str) or not CUSA.fullmatch(title):
            raise ValueError("Invalid source title ID")
        repo = _repo(item["repository"])
        evidence = item["evidence_url"]
        match = EVIDENCE_URL.fullmatch(evidence) if isinstance(evidence, str) else None
        if not match or _repo(match.group("repo")) != repo:
            raise ValueError("Evidence URL must belong to exact publisher repo")
        path = evidence.split("/", 7)[-1]
        if path.startswith("/") or "//" in path or ".." in path.split("/"):
            raise ValueError("Unsafe evidence path")
        note, ticket = item["review_note"], item["review_ticket"]
        if (not isinstance(note, str) or not 8 <= len(note) <= 256
                or not isinstance(ticket, str)
                or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{3,63}", ticket)
                or any(ord(c) < 32 for c in note)):
            raise ValueError("Missing or malformed independent review record")
        key = (title, repo)
        if key in approved:
            raise ValueError("Duplicate publisher review entry")
        approved[key] = ticket
    return approved


def review_publisher_origins(report, policy):
    """Annotate ONLY source origin references, not source truth or rights.

    Accepts normalized FPKG metadata produced by catalog_ingest.ingest.
    A source not in the exact reviewer-maintained registry is never admitted.
    Neither admitted nor unregistered sources are installable or downloadable.
    """
    reviewed = _policy(policy)
    if (not isinstance(report, dict) or report.get("schema") != 1
            or report.get("status") != "UNVERIFIED_METADATA_ONLY"
            or report.get("fixture_only") is not False
            or report.get("auto_install") is not False
            or report.get("compatibility_confirmed") is not False
            or not isinstance(report.get("updates"), list)
            or len(report["updates"]) > MAX_RECORDS):
        raise ValueError("Invalid normalized FPKG report")
    if any(key in report for key in FORBIDDEN):
        raise ValueError("Untrusted catalog claims actions")
    output = {}
    seen = set()
    for row in report["updates"]:
        if not isinstance(row, dict) or any(k in row for k in FORBIDDEN):
            raise ValueError("Untrusted source row claims actions")
        cusa, cid, version = (row.get("cusa"), row.get("content_id"),
                              row.get("version"))
        if (not isinstance(cusa, str) or not CUSA.fullmatch(cusa)
                or not isinstance(cid, str)
                or not isinstance(version, str)
                or row.get("declared_package_kind") != "fpkg_patch"
                or row.get("patch_compatibility_confirmed") is not False):
            raise ValueError("Invalid FPKG source record")
        key = (cusa, cid, version)
        if key in seen:
            raise ValueError("Duplicate FPKG source identity")
        seen.add(key)
        source = row.get("source_page")
        matching = GITHUB_RELEASE.fullmatch(source) if isinstance(source, str) else None
        status = "UNREGISTERED_SOURCE"
        ticket = None
        if matching:
            repo = _repo(matching.group("repo"))
            ticket = reviewed.get((cusa, repo))
            if ticket is not None:
                status = "PUBLISHER_REFERENCE_REVIEWED_ONLY"
        output[key] = {
            "status": status,
            "review_ticket": ticket,
            "rights_verified": False,
            "patch_compatibility_verified": False,
            "download_allowed": False,
            "install_allowed": False,
        }
    return output
