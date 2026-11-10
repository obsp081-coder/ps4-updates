"""Phase 3: FPKG-only candidate metadata index; fail closed.

Only accepts submissions normalized by catalog_ingest.ingest. Historical
Sony/Retail metadata is explicitly excluded, not merged or used as fallback.
No network, console, downloads or installs. A submitted FPKG package-kind
is a declaration, not proof of a matching installed FPKG or distribution rights.
"""
import argparse
import json
from pathlib import Path

from catalog_ingest import CUSA, CONTENT_ID, VERSION, _source

MAX_TITLES = 100
MAX_CANDIDATES_PER_TITLE = 24
DISALLOWED = frozenset((
    "download_url", "pkg_url", "package_url", "install_command",
    "auto_install", "install_allowed", "download_allowed",
    "package_file", "local_pkg_path",
))


def _must_no_actions(obj):
    # catalog_ingest explicitly emits auto_install=False as a negative
    # safety assertion. Never reject that safe value, but reject any
    # truthy/malformed permission or other action-bearing field.
    for key in DISALLOWED:
        if key in obj and not (key == "auto_install"
                              and obj[key] is False):
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


def build_source_index(cusas, fpkg_report=None):
    """Read-only, FPKG-only metadata for known CUSA IDs; never authorizes I/O."""
    if (not isinstance(cusas, (list, tuple))
            or not 1 <= len(cusas) <= MAX_TITLES):
        raise ValueError("Expected 1..100 title IDs")
    for title in cusas:
        _check_cusa(title)
    if len(set(cusas)) != len(cusas):
        raise ValueError("Ambiguous duplicate game IDs")
    allowed = set(cusas)
    fpkg, fpkg_supplied = _fpkg_rows(fpkg_report, allowed)
    results = []
    for title in cusas:
        candidates = list(fpkg.get(title, []))
        candidates.sort(key=lambda x: (
            _version(x["version"]), x["content_id"]
        ), reverse=True)
        overflow = len(candidates) > MAX_CANDIDATES_PER_TITLE
        # Truncation is disclosed; no candidate is actionable.
        candidates = candidates[:MAX_CANDIDATES_PER_TITLE]
        results.append({
            "cusa": title,
            "fpkg_submission_status": (
                "SUBMISSIONS_RECEIVED" if fpkg.get(title)
                else "NO_SUBMISSIONS" if fpkg_supplied else "NOT_PROVIDED"
            ),
            "source_status": (
                "UNVERIFIED_FPKG_CANDIDATES" if candidates
                else "NO_TRUSTED_UPDATE_SOURCE"
            ),
            "truncated": overflow,
            "candidates": candidates,
            "download_allowed": False,
            "install_allowed": False,
            "compatibility_confirmed": False,
        })
    return {
        "schema": 2,
        "phase": "FPKG_ONLY_SOURCE_DISCOVERY_METADATA_ONLY",
        "package_policy": "fpkg_only",
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
        output = build_source_index(args.cusa, load(args.fpkg_report))
    except (ValueError, OSError) as err:
        parser.error(str(err))
    args.out.write_text(json.dumps(output, ensure_ascii=False, indent=2)
                        + "\n", encoding="utf-8")
    print("Metadata-only source index written. No trusted downloads/install.")


if __name__ == "__main__":
    main()
