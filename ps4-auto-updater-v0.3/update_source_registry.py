"""Phase 3: combine *observed* PS4 patch metadata without trusting it.

Input is a report from official_patch_lookup.discover (historical Sony HTTP,
unauthenticated) and/or catalog_ingest.ingest (submitted FPKG metadata).
This module makes NO network, console, file-write, download or install calls.
The results are manual-review candidates, never confirmed update availability.
"""
import argparse
import json
from pathlib import Path

from catalog_ingest import CUSA, CONTENT_ID, VERSION, _source
from official_patch_lookup import patch_url

MAX_TITLES = 100
MAX_CANDIDATES_PER_TITLE = 24
DISALLOWED = frozenset((
    "download_url", "pkg_url", "package_url", "install_command",
    "auto_install", "install_allowed", "download_allowed",
    "package_file", "local_pkg_path",
))


def _must_no_actions(obj):
    if any(k in obj for k in DISALLOWED):
        raise ValueError("Untrusted metadata cannot carry actions")


def _version(text):
    if not isinstance(text, str) or not VERSION.fullmatch(text):
        raise ValueError("Invalid version")
    return tuple(int(p) for p in text.split("."))


def _cid(value, cusa):
    if (not isinstance(value, str)
            or (m := CONTENT_ID.fullmatch(value)) is None
            or m.group(1) != cusa):
        raise ValueError("Content ID does not bind to CUSA")


def _check_cusa(cusa):
    if not isinstance(cusa, str) or not CUSA.fullmatch(cusa):
        raise ValueError("Invalid title ID")


def _retail_rows(report, target):
    if report is None:
        return {}, False
    if (not isinstance(report, dict) or report.get("schema") != 1
            or report.get("source_kind") != "official_retail_metadata"
            or not isinstance(report.get("titles"), list)
            or len(report["titles"]) > MAX_TITLES):
        raise ValueError("Invalid retail report")
    _must_no_actions(report)
    if any(report.get(k) is not False for k in (
        "install_enabled", "downloads_enabled",
        "fpkg_compatibility_confirmed",
    )):
        raise ValueError("Retail report asserts actions")
    results = {}
    for row in report["titles"]:
        if not isinstance(row, dict):
            raise ValueError("Malformed retail row")
        _must_no_actions(row)
        title = row.get("cusa")
        _check_cusa(title)
        if title not in target or title in results:
            raise ValueError("Unexpected/duplicate retail title")
        if (row.get("source") != "sony-retail-patch-xml"
                or row.get("metadata_url") != patch_url(title)
                or any(row.get(k) is not False for k in (
                    "transport_authenticated", "rights_verified",
                    "fpkg_compatibility_confirmed", "install_enabled",
                    "downloads_enabled",
                ))):
            raise ValueError("Unproven Sony source/rights asserted")
        status = row.get("status")
        if status not in ("RETAIL_PATCH_LISTED", "NOT_LISTED",
                          "LOOKUP_FAILED"):
            raise ValueError("Invalid retail status")
        package_rows = []
        if status == "RETAIL_PATCH_LISTED":
            observed = row.get("observed_retail_versions")
            if observed is None:
                # Legacy reports may contain only the latest item.
                observed = [{
                    "version": row.get("latest_retail_version"),
                    "content_id": row.get("content_id"),
                }]
            if (not isinstance(observed, list) or not observed
                    or len(observed) > 128):
                raise ValueError("Invalid retail patch collection")
            seen = set()
            for obj in observed:
                if not isinstance(obj, dict):
                    raise ValueError("Malformed retail patch record")
                _must_no_actions(obj)
                ver, cid = obj.get("version"), obj.get("content_id")
                _version(ver)
                _cid(cid, title)
                key = (ver, cid)
                if key in seen:
                    raise ValueError("Duplicate retail patch metadata")
                seen.add(key)
                package_rows.append({
                    "package_kind": "retail_patch",
                    "version": ver,
                    "content_id": cid,
                    "minimum_fw": None,
                    "size_bytes": None,
                    "source_status": "SONY_HTTP_METADATA_UNAUTHENTICATED",
                    "history_complete": False,
                })
            if (_version(row.get("latest_retail_version"))
                    != max(_version(x["version"]) for x in package_rows)):
                raise ValueError("Inconsistent latest retail version")
            _cid(row.get("content_id"), title)
            if not any(x["version"] == row["latest_retail_version"]
                       and x["content_id"] == row["content_id"]
                       for x in package_rows):
                raise ValueError("Inconsistent latest retail Content ID")
        results[title] = (status, package_rows)
    return results, True


def _fpkg_rows(report, target):
    if report is None:
        return {}, False
    if (not isinstance(report, dict) or report.get("schema") != 1
            or report.get("fixture_only") is not False
            or report.get("status") != "UNVERIFIED_METADATA_ONLY"
            or not isinstance(report.get("updates"), list)
            or len(report["updates"]) > 1000):
        raise ValueError("Invalid FPKG submission report")
    _must_no_actions(report)
    if (report.get("auto_install") is not False
            or report.get("compatibility_confirmed") is not False):
        raise ValueError("FPKG approval assertion is forbidden")
    output = {}
    seen = set()
    for obj in report["updates"]:
        if not isinstance(obj, dict):
            raise ValueError("Malformed FPKG row")
        _must_no_actions(obj)
        title, cid, ver = (obj.get("cusa"), obj.get("content_id"),
                           obj.get("version"))
        _check_cusa(title)
        if title not in target:
            raise ValueError("Unexpected FPKG title")
        _cid(cid, title)
        _version(ver)
        _version(obj.get("min_fw"))
        req = obj.get("required_base_version")
        if req is not None:
            _version(req)
        if (obj.get("declared_package_kind") != "fpkg_patch"
                or obj.get("patch_compatibility_confirmed") is not False):
            raise ValueError("Cannot assert FPKG compatibility")
        _source(obj.get("source_page"))  # syntax only; no fetch or rights grant
        key = (title, cid, ver)
        if key in seen:
            raise ValueError("Duplicate FPKG metadata")
        seen.add(key)
        output.setdefault(title, []).append({
            "package_kind": "fpkg_patch",
            "version": ver,
            "content_id": cid,
            "minimum_fw": obj["min_fw"],  # submitted, unverified
            "required_base_version": req,
            "size_bytes": None,
            "source_status": "FPKG_SUBMISSION_UNVERIFIED",
            "history_complete": False,
        })
    return output, True


def build_source_index(cusas, retail_report=None, fpkg_report=None):
    """Fail-closed, bounded, deterministic inventory keyed by known titles."""
    if (not isinstance(cusas, (list, tuple))
            or not 1 <= len(cusas) <= MAX_TITLES):
        raise ValueError("Expected 1..100 title IDs")
    for title in cusas:
        _check_cusa(title)
    if len(set(cusas)) != len(cusas):
        raise ValueError("Ambiguous duplicate game IDs")
    allowed = set(cusas)
    retail, retail_supplied = _retail_rows(retail_report, allowed)
    fpkg, fpkg_supplied = _fpkg_rows(fpkg_report, allowed)
    results = []
    for title in cusas:
        status, retail_candidates = retail.get(title, ("NOT_QUERIED", []))
        candidates = retail_candidates + fpkg.get(title, [])
        candidates.sort(key=lambda x: (
            _version(x["version"]), x["package_kind"], x["content_id"]
        ), reverse=True)
        overflow = len(candidates) > MAX_CANDIDATES_PER_TITLE
        # Do not silently discard; report truncation, even though every
        # candidate remains unverified and not actionable.
        candidates = candidates[:MAX_CANDIDATES_PER_TITLE]
        results.append({
            "cusa": title,
            "retail_lookup_status": status,
            "fpkg_submission_status": (
                "SUBMISSIONS_RECEIVED" if fpkg.get(title)
                else "NO_SUBMISSIONS" if fpkg_supplied else "NOT_PROVIDED"
            ),
            "source_status": (
                "UNVERIFIED_CANDIDATES" if candidates
                else "LOOKUP_FAILED" if status == "LOOKUP_FAILED"
                else "NO_TRUSTED_UPDATE_SOURCE"
            ),
            "truncated": overflow,
            "candidates": candidates,
            "download_allowed": False,
            "install_allowed": False,
            "compatibility_confirmed": False,
        })
    return {
        "schema": 1,
        "phase": "SOURCE_DISCOVERY_METADATA_ONLY",
        "retail_report_supplied": retail_supplied,
        "fpkg_report_supplied": fpkg_supplied,
        "history_complete": False,
        "download_allowed": False,
        "install_allowed": False,
        "compatibility_confirmed": False,
        "titles": results,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cusa", nargs="+", required=True)
    parser.add_argument("--retail-report", type=Path)
    parser.add_argument("--fpkg-report", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    def load(path):
        if path is None:
            return None
        if path.stat().st_size > 1024 * 1024:
            raise ValueError("Report exceeds 1 MiB")
        return json.loads(path.read_text(encoding="utf-8"))
    try:
        output = build_source_index(args.cusa, load(args.retail_report),
                                    load(args.fpkg_report))
    except (ValueError, OSError) as err:
        parser.error(str(err))
    args.out.write_text(json.dumps(output, ensure_ascii=False, indent=2)
                        + "\n", encoding="utf-8")
    print("Metadata-only source index written. No trusted downloads/install.")


if __name__ == "__main__":
    main()
