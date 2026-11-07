#!/usr/bin/env python3
"""Build a minimal FPKGi UPDATES.json from an explicitly verified CSV list.

No downloads, no remote scans, no claims of compatibility are performed here.
"""
import csv
import json
import re
from datetime import datetime
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
INPUT = ROOT / "approved_updates.csv"
OUTPUT = ROOT / "UPDATES.json"
REQUIRED = {
    "game_name", "cusa", "region", "update_version", "base_description",
    "release_date", "size_bytes", "direct_pkg_url", "cover_url", "verified_compatible"
}
REGIONS = {"USA", "EUR", "JAP", "ASIA"}

def check_row(row, line):
    if row["verified_compatible"].strip().upper() != "YES":
        return None  # Unverified entries intentionally stay hidden.
    cusa = row["cusa"].strip().upper()
    if not re.fullmatch(r"CUSA\d{5}", cusa):
        raise ValueError(f"Line {line}: CUSA must look like CUSA12345")
    region = row["region"].strip().upper()
    if region not in REGIONS:
        raise ValueError(f"Line {line}: region must be one of {sorted(REGIONS)}")
    game = row["game_name"].strip()
    version = row["update_version"].strip()
    base = row["base_description"].strip()
    if not game or not version or not base:
        raise ValueError(f"Line {line}: game_name, version, base_description required")
    if not re.fullmatch(r"\d+(?:\.\d+){1,3}", version):
        raise ValueError(f"Line {line}: invalid version")
    release = row["release_date"].strip()
    if release:
        datetime.strptime(release, "%m-%d-%Y")
    size = int(row["size_bytes"].strip())
    if size <= 0:
        raise ValueError(f"Line {line}: file size must be positive")
    url = row["direct_pkg_url"].strip()
    parsed = urlparse(url)
    if parsed.scheme != "https" or not parsed.netloc or not parsed.path.lower().endswith(".pkg"):
        raise ValueError(f"Line {line}: expected direct https URL with .pkg path")
    cover = row["cover_url"].strip()
    if cover and urlparse(cover).scheme != "https":
        raise ValueError(f"Line {line}: cover URL must use https")
    display = f"{game} | {cusa} | {base} | Update {version}"
    return url, {
        "region": region,
        "name": display,
        "version": version,
        "release": release or None,
        "size": size,
        "min_fw": None,
        "cover_url": cover or None,
    }

def main():
    records = {}
    with INPUT.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames or not REQUIRED.issubset(reader.fieldnames):
            raise ValueError(f"Missing CSV columns: {sorted(REQUIRED - set(reader.fieldnames or []))}")
        for line, row in enumerate(reader, start=2):
            item = check_row(row, line)
            if item:
                url, data = item
                if url in records:
                    raise ValueError(f"Line {line}: duplicate URL {url}")
                records[url] = data
    OUTPUT.write_text(json.dumps({"DATA": records}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {OUTPUT.name} with {len(records)} verified updates")

if __name__ == "__main__":
    main()
