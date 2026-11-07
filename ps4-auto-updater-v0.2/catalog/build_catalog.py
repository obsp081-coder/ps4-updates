#!/usr/bin/env python3
"""Match trusted, manually confirmed updates to a PS4 metadata snapshot.

The catalog is deliberately fail-closed: missing fingerprint, version, proof,
verification or direct URL -> no catalog entry. This does NOT prove PS4 FPKG
compatibility; proof is an assertion made by a separate manual test.
"""
import argparse
import csv
import json
import re
from pathlib import Path
from urllib.parse import urlparse

CUSA = re.compile(r"^CUSA\d{5}$")
VERSION = re.compile(r"^\d+\.\d+(?:\.\d+)?$")
REQUIRED = {
    "game_name", "cusa", "base_content_id", "base_app_ver", "update_version",
    "region", "size_bytes", "direct_pkg_url", "verified_compatible", "proof_note"
}


def normalized_version(value):
    if not VERSION.fullmatch(value):
        raise ValueError(f"Invalid version {value!r}")
    return tuple(int(p) for p in value.split("."))


def is_pkg_link(url):
    u = urlparse(url)
    return u.scheme == "https" and bool(u.netloc) and u.path.lower().endswith(".pkg")


def build(inventory, csv_path):
    games = {}
    for g in inventory.get("games", []):
        cusa = g.get("cusa", "").strip().upper()
        # A metadata folder alone does not prove the game is installed.
        if CUSA.fullmatch(cusa) and g.get("installed_confirmed") is True:
            games[cusa] = g
    output = {}
    with open(csv_path, encoding="utf-8-sig", newline="") as fp:
        reader = csv.DictReader(fp)
        if not reader.fieldnames or not REQUIRED.issubset(reader.fieldnames):
            raise ValueError(f"Missing columns: {sorted(REQUIRED - set(reader.fieldnames or []))}")
        for row in reader:
            if row["verified_compatible"].strip().upper() != "YES":
                continue
            cusa = row["cusa"].strip().upper()
            g = games.get(cusa)
            if not g:
                continue
            # Strict match to exact package Content ID and installed app version.
            content_id = row["base_content_id"].strip()
            base_ver = row["base_app_ver"].strip()
            if not content_id or not base_ver or not g.get("content_id") or not g.get("app_ver"):
                continue
            if g["content_id"].strip() != content_id or g["app_ver"].strip() != base_ver:
                continue
            if not row["proof_note"].strip():
                continue
            url = row["direct_pkg_url"].strip()
            if not is_pkg_link(url):
                continue
            region = row["region"].strip().upper()
            if region not in {"USA", "EUR", "JAP", "ASIA", "UNK"}:
                continue
            try:
                size = int(row["size_bytes"])
                if size < 1 or normalized_version(row["update_version"].strip()) <= normalized_version(base_ver):
                    continue
            except ValueError:
                continue
            if url in output:
                raise ValueError("Duplicate update URL")
            output[url] = {
                "title_id": cusa,
                "region": region,
                "name": f"{g.get('name') or row['game_name']} | {cusa} | Update {row['update_version'].strip()}",
                "version": row["update_version"].strip(),
                "release": row.get("release_date", "").strip() or None,
                "size": size,
                "min_fw": None,
                "cover_url": row.get("cover_url", "").strip() or None,
            }
    return {"DATA": output}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--inventory", required=True)
    p.add_argument("--approved", required=True)
    p.add_argument("--out", required=True)
    args = p.parse_args()
    inv = json.loads(Path(args.inventory).read_text(encoding="utf-8"))
    obj = build(inv, args.approved)
    Path(args.out).write_text(json.dumps(obj, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(obj['DATA'])} catalog updates (manually verified exact-base matches only)")


if __name__ == "__main__":
    main()
