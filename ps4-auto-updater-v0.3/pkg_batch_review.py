"""Batch-review locally owned PS4 PKG files against a past inventory.

Reads only package metadata; never downloads, modifies or installs any PKG.
Matches are NOT proof of FPKG origin, signature, base/patch compatibility,
or current installation. Scans one folder (not its subfolders).
"""

import argparse
import json
import re
from collections import Counter
from pathlib import Path

from pkg_local_inspector import inspect, numeric_version

CUSA = re.compile(r"CUSA[0-9]{5}\Z")
MAX_FILES = 100
MAX_GAMES = 1000
MAX_INVENTORY_BYTES = 2 * 1024 * 1024


def validate_inventory(inventory):
    if not isinstance(inventory, dict) or not isinstance(inventory.get("games"), list):
        raise ValueError("inventory must contain a games list")
    games = inventory["games"]
    if len(games) > MAX_GAMES:
        raise ValueError("too many inventory games")
    seen = set()
    for game in games:
        if not isinstance(game, dict):
            raise ValueError("invalid inventory game")
        cusa = game.get("cusa")
        if not isinstance(cusa, str) or not CUSA.fullmatch(cusa):
            raise ValueError("invalid inventory CUSA")
        if cusa in seen:
            raise ValueError("duplicate inventory CUSA: " + cusa)
        seen.add(cusa)
        if (numeric_version(game.get("app_ver")) is None
                or not isinstance(game.get("content_id"), str)
                or not game["content_id"]):
            raise ValueError("incomplete inventory metadata: " + cusa)
    return games


def scan(folder, inventory, firmware="14.00"):
    if numeric_version(firmware) is None:
        raise ValueError("invalid firmware version")
    games = validate_inventory(inventory)
    root = Path(folder)
    if root.is_symlink() or not root.is_dir():
        raise ValueError("folder must be a real local directory")

    paths = sorted(
        (p for p in root.iterdir() if p.suffix.lower() == ".pkg"),
        key=lambda p: p.name.casefold()
    )
    if len(paths) > MAX_FILES:
        raise ValueError("more than 100 .pkg entries; split into smaller folders")

    by_cusa = {game["cusa"]: [] for game in games}
    packages = []
    for path in paths:
        if path.is_symlink() or not path.is_file():
            result = {
                "file_name": path.name,
                "status": "SKIPPED_NONREGULAR_OR_SYMLINK",
            }
        else:
            try:
                result = inspect(path, inventory=inventory, firmware=firmware)
            except (OSError, ValueError, UnicodeError) as error:
                result = {
                    "file_name": path.name,
                    "status": "INSPECTION_ERROR",
                    "error_type": type(error).__name__,
                }
        # One place sets these immutable safety guarantees, including for errors.
        result["fpkg_confirmed"] = False
        result["base_patch_compatibility_confirmed"] = False
        result["installed_confirmed"] = False
        result["download_enabled"] = False
        result["install_enabled"] = False
        packages.append(result)
        if result.get("cusa") in by_cusa:
            by_cusa[result["cusa"]].append({
                "file_name": result["file_name"],
                "status": result["status"],
                "version": result.get("app_ver"),
            })

    game_rows = []
    for game in games:
        checks = by_cusa[game["cusa"]]
        game_rows.append({
            "cusa": game["cusa"],
            "name": game.get("name", ""),
            "version_at_last_scan": game["app_ver"],
            "installed_confirmed": False,
            "status": (
                "NO_LOCAL_PKG" if not checks else
                "METADATA_CANDIDATE_PRESENT" if any(
                    item["status"] == "CANDIDATE_METADATA_ONLY"
                    for item in checks
                ) else "ONLY_UNVERIFIED_OR_REJECTED_FILES"
            ),
            "package_checks": checks,
        })

    status_counts = Counter(p["status"] for p in packages)
    return {
        "schema": 1,
        "mode": "offline_local_pkg_batch_review",
        "source": "historical_inventory_and_local_files",
        "firmware": firmware,
        "inventory_is_live": False,
        "download_enabled": False,
        "install_enabled": False,
        "fpkg_compatibility_confirmed": False,
        "installation_confirmed": False,
        "summary": {
            "games_in_inventory": len(games),
            "pkg_entries_scanned": len(packages),
            "metadata_candidates_only": status_counts["CANDIDATE_METADATA_ONLY"],
            "other_or_rejected_entries": len(packages) - status_counts["CANDIDATE_METADATA_ONLY"],
            "games_without_local_pkg": sum(
                row["status"] == "NO_LOCAL_PKG" for row in game_rows
            ),
        },
        "games": game_rows,
        "packages": packages,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path, help="Folder of existing .pkg files")
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--firmware", default="14.00")
    parser.add_argument("--out", type=Path, default=Path("pkg_batch_report.json"))
    args = parser.parse_args()
    if args.out.suffix.lower() == ".pkg":
        parser.error("--out must not point to a .pkg file")
    if args.inventory.stat().st_size > MAX_INVENTORY_BYTES:
        parser.error("inventory file exceeds 2 MiB")
    inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
    try:
        report = scan(args.folder, inventory, firmware=args.firmware)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    args.out.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8"
    )
    print(json.dumps(report["summary"], indent=2))
    print("Metadata only; no FPKG compatibility or installation verified.")


if __name__ == "__main__":
    main()
