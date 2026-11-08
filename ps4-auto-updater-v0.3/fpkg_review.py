
"""Conservative, offline FPKG candidate review for PS4 Auto Updater.

Inputs: a historical identity comparison and optional *unverified* FPKG metadata.
No network requests, PKG downloads, verification, or installs are performed.
Passing a metadata check NEVER means a patch is safe to install.
"""

import argparse
import json
import re
from pathlib import Path

CUSA = re.compile(r"CUSA\d{5}\Z")
CONTENT_ID = re.compile(r"[A-Z]{2}\d{4}-(CUSA\d{5})_\d{2}-[A-Z0-9]{16}\Z")
VERSION = re.compile(r"\d{1,4}\.\d{1,4}\Z")


def version(value):
    if not isinstance(value, str) or not VERSION.fullmatch(value):
        return None
    return tuple(map(int, value.split(".")))


def review(comparison, catalog=None, installed_check=None, firmware="14.00"):
    current_fw = version(firmware)
    if current_fw is None:
        raise ValueError("Firmware must look like 14.00")
    if not isinstance(comparison, dict) or not isinstance(comparison.get("games"), list):
        raise ValueError("Comparison must have a games list")

    if catalog is None:
        catalog = {"schema": 1, "status": "UNVERIFIED_METADATA_ONLY", "updates": []}
    if (not isinstance(catalog, dict)
            or catalog.get("schema") != 1
            or catalog.get("status") != "UNVERIFIED_METADATA_ONLY"
            or catalog.get("fixture_only") is True
            or catalog.get("auto_install") is True
            or catalog.get("compatibility_confirmed") is True
            or not isinstance(catalog.get("updates"), list)):
        raise ValueError("Catalog must be normalized, non-demo, unverified FPKG metadata")

    reports = {}
    if installed_check is not None:
        if (not isinstance(installed_check, dict)
                or not isinstance(installed_check.get("games"), list)):
            raise ValueError("Installed-check report must have a games list")
        for item in installed_check["games"]:
            if not isinstance(item, dict):
                continue
            ident = item.get("cusa")
            flag = item.get("installed")
            if (isinstance(ident, str) and CUSA.fullmatch(ident)
                    and (flag is None or isinstance(flag, bool))):
                if ident in reports:
                    raise ValueError("Duplicate installation report for " + ident)
                reports[ident] = flag

    updates = {}
    for item in catalog["updates"]:
        if not isinstance(item, dict):
            raise ValueError("Malformed FPKG catalog record")
        ident = item.get("cusa")
        cid = item.get("content_id")
        match = CONTENT_ID.fullmatch(cid) if isinstance(cid, str) else None
        if (not isinstance(ident, str) or not CUSA.fullmatch(ident)
                or not match or match.group(1) != ident
                or item.get("declared_package_kind") != "fpkg_patch"
                or version(item.get("version")) is None
                or version(item.get("min_fw")) is None
                or not isinstance(item.get("source_page"), str)
                or not item["source_page"].startswith("https://")):
            raise ValueError("Malformed FPKG metadata or source_page")
        updates.setdefault(ident, []).append(item)

    rows = []
    seen = set()
    for game in comparison["games"]:
        if not isinstance(game, dict):
            raise ValueError("Malformed comparison record")
        ident = game.get("cusa")
        base_version = game.get("last_scan_app_ver")
        base_cid = game.get("console_content_id")
        if (not isinstance(ident, str) or not CUSA.fullmatch(ident)
                or not isinstance(base_cid, str)
                or version(base_version) is None or ident in seen):
            raise ValueError("Invalid or duplicate game in comparison")
        seen.add(ident)

        row = {
            "cusa": ident,
            "name": game.get("name", ""),
            "last_scan_version": base_version,
            "retail_latest_version": game.get("retail_latest_version"),
            "fpkg_candidate_version": None,
            "status": "NOT_IN_FPKG_CATALOG",
            "installed_confirmed": False,
            "fpkg_compatibility_confirmed": False,
            "install_enabled": False,
        }
        rows.append(row)

        candidates = updates.get(ident, [])
        if not candidates:
            continue
        if game.get("content_id_match") is not True:
            row["status"] = "RETAIL_IDENTITY_NOT_CONFIRMED"
            continue

        same_cid = [i for i in candidates if i["content_id"] == base_cid]
        if not same_cid:
            row["status"] = "CONTENT_ID_MISMATCH"
            continue
        newer = [i for i in same_cid if version(i["version"]) > version(base_version)]
        if not newer:
            row["status"] = "NO_NEWER_FPKG_CANDIDATE"
            continue
        supported = [i for i in newer if version(i["min_fw"]) <= current_fw]
        if not supported:
            row["status"] = "FW_REQUIREMENT_NOT_MET"
            continue

        # A patch which doesn't say what base version it targets is not actionable.
        base_matched = [i for i in supported if i.get("required_base_version") == base_version]
        if not base_matched:
            row["status"] = "BASE_VERSION_NOT_CONFIRMED"
            continue

        candidate = max(base_matched, key=lambda i: version(i["version"]))
        row["fpkg_candidate_version"] = candidate["version"]
        row["source_page_for_manual_review"] = candidate["source_page"]
        row["status"] = "INSTALL_UNCONFIRMED"

        if reports.get(ident) is False:
            row["status"] = "NOT_INSTALLED_IN_REPORT"
        elif reports.get(ident) is True:
            row["status"] = "MANUAL_REVIEW_REQUIRED"
            # Do not turn this into confirmed compatibility or permission to install.

    return {
        "schema": 1,
        "mode": "offline_fpkg_candidate_review",
        "firmware": firmware,
        "retail_metadata_is_not_fpkg": True,
        "install_enabled": False,
        "download_enabled": False,
        "fpkg_compatibility_confirmed": False,
        "candidate_metadata_is_unverified": True,
        "games": rows,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("comparison", type=Path)
    parser.add_argument("--catalog", type=Path)
    parser.add_argument("--installed-check", type=Path)
    parser.add_argument("--firmware", default="14.00")
    parser.add_argument("--out", type=Path, default=Path("fpkg_review_report.json"))
    args = parser.parse_args()

    def load(path):
        return json.loads(path.read_text(encoding="utf-8")) if path else None

    result = review(load(args.comparison), load(args.catalog),
                    load(args.installed_check), args.firmware)
    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for item in result["games"]:
        print(f"{item['cusa']} | {item['status']} | {item['fpkg_candidate_version'] or '-'}")
    print("No downloads or installs; all metadata requires manual verification.")


if __name__ == "__main__":
    main()
