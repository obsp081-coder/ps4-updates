
"""Offline PS4 update comparison.

No downloads, installs, or compatibility claims.
Matches are candidates, NOT verified FPKG updates.
"""

import argparse
import json
import re
from pathlib import Path

CUSA_PATTERN = re.compile(r"CUSA[0-9]{5}\Z")
VERSION_PATTERN = re.compile(r"[0-9]{1,4}\.[0-9]{1,4}\Z")


def parse_version(value):
    """Convert a version into numbers for comparison."""
    if not isinstance(value, str):
        return None

    if not VERSION_PATTERN.fullmatch(value):
        return None

    return tuple(int(part) for part in value.split("."))


def compare_updates(inventory, installed_check, catalog, firmware):
    """Find potential updates conservatively, without installation."""

    running_fw = parse_version(firmware)

    if running_fw is None:
        raise ValueError("Firmware must be like '14.00'")

    for document, key in (
        (inventory, "games"),
        (installed_check, "games"),
        (catalog, "updates"),
    ):
        if (
            not isinstance(document, dict)
            or not isinstance(document.get(key), list)
        ):
            raise ValueError(
                "Invalid input: missing list '" + key + "'"
            )

    # Reject any catalog asking the read-only reviewer to enable actions.
    # A producer-controlled flag never grants install/download permission.
    if any(catalog.get(key) is True for key in (
        "auto_install", "install_enabled", "downloads_enabled",
        "compatibility_confirmed", "fpkg_compatibility_confirmed",
    )):
        raise ValueError("Catalog cannot enable install or download actions")

    # Duplicate CUSA rows are ambiguous, including identical duplicates.
    # Prevent accidental multiple update decisions for the same title.
    counts = {}
    for game in inventory["games"]:
        if isinstance(game, dict) and isinstance(game.get("cusa"), str):
            ident = game["cusa"]
            counts[ident] = counts.get(ident, 0) + 1

    # A conflicting/duplicate installed-app report cannot prove presence.
    # Fail closed rather than letting the final row silently override it.
    installation = {}
    duplicate_installations = set()
    for item in installed_check["games"]:
        if not isinstance(item, dict):
            continue
        ident = item.get("cusa")
        flag = item.get("installed")
        if not isinstance(ident, str) or not CUSA_PATTERN.fullmatch(ident):
            continue
        if flag is not None and type(flag) is not bool:
            continue
        if ident in installation:
            duplicate_installations.add(ident)
        else:
            installation[ident] = flag

    results = []

    for game in inventory["games"]:
        if not isinstance(game, dict):
            continue

        cusa = game.get("cusa", "")
        name = game.get("name", "")
        current = game.get("app_ver", "")
        cid = game.get("content_id", "")

        row = {
            "cusa": cusa,
            "name": name,
            "current_version": current,
            "candidate_version": None,
            "status": None,
        }

        results.append(row)

        if (
            not isinstance(cusa, str)
            or not CUSA_PATTERN.fullmatch(cusa)
        ):
            row["status"] = "INVALID_GAME_ID"
            continue

        if counts.get(cusa, 0) != 1:
            row["status"] = "DUPLICATE_GAME_RECORD"
            continue

        installed = (None if cusa in duplicate_installations
                     else installation.get(cusa))

        if installed is not True:
            row["status"] = (
                "NOT_INSTALLED"
                if installed is False
                else "INSTALL_UNCONFIRMED"
            )
            continue

        current_number = parse_version(current)

        if (
            current_number is None
            or not isinstance(cid, str)
            or not cid
        ):
            row["status"] = "INVALID_GAME_METADATA"
            continue

        by_cusa = [
            item
            for item in catalog["updates"]
            if (
                isinstance(item, dict)
                and item.get("cusa") == cusa
            )
        ]

        if not by_cusa:
            row["status"] = "NO_UPDATE_LISTED"
            continue

        matched = [
            item
            for item in by_cusa
            if item.get("content_id") == cid
        ]

        if not matched:
            row["status"] = "CONTENT_ID_MISMATCH"
            continue

        valid = []

        for item in matched:
            target = parse_version(
                item.get("version")
            )

            minimum = parse_version(
                item.get("min_fw")
            )

            if target is not None and minimum is not None:
                valid.append(
                    (target, minimum, item)
                )

        if not valid:
            row["status"] = "INVALID_CATALOG_METADATA"
            continue

        newer = [
            item
            for item in valid
            if item[0] > current_number
        ]

        if not newer:
            row["status"] = "UP_TO_DATE_IN_CATALOG"
            continue

        supported = [
            item
            for item in newer
            if item[1] <= running_fw
        ]

        if not supported:
            row["status"] = "FIRMWARE_TOO_LOW"
            continue

        eligible = [
            item
            for item in supported
            if item[2].get("required_base_version") == current
        ]

        if not eligible:
            row["status"] = "BASE_VERSION_NOT_CONFIRMED"
            continue

        best_version = max(item[0] for item in eligible)
        matching_best = [item for item in eligible
                         if item[0] == best_version]
        if len(matching_best) != 1:
            row["status"] = "AMBIGUOUS_UPDATE_METADATA"
            continue
        best = matching_best[0][2]

        row["candidate_version"] = best["version"]

        # This does NOT confirm FPKG patch compatibility.
        row["status"] = "REVIEW_REQUIRED"

    return {
        "schema": 1,
        "mode": "offline_comparison",
        "firmware": firmware,
        "auto_install": False,
        "compatibility_confirmed": False,
        "results": results,
    }


def main():
    parser = argparse.ArgumentParser(
        description=__doc__
    )

    parser.add_argument("inventory", type=Path)
    parser.add_argument("installed_check", type=Path)
    parser.add_argument("catalog", type=Path)
    parser.add_argument(
        "--firmware",
        default="14.00"
    )

    args = parser.parse_args()

    documents = [
        json.loads(
            path.read_text(encoding="utf-8")
        )
        for path in (
            args.inventory,
            args.installed_check,
            args.catalog,
        )
    ]

    report = compare_updates(
        *documents,
        firmware=args.firmware
    )

    print(
        json.dumps(
            report,
            ensure_ascii=False,
            indent=2
        )
    )


if __name__ == "__main__":
    main()
