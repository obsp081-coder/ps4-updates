
"""Read PS4 PKG header and embedded param.sfo, without installing or modifying it.

An apparent patch is metadata-only; this does NOT authenticate an FPKG,
verify signatures, or prove that a patch is compatible with a base game.
"""

import argparse
import json
import re
import struct
from pathlib import Path

PKG_MAGIC = b"\x7fCNT"
SFO_MAGIC = b"\x00PSF"
CONTENT_ID = re.compile(r"[A-Z]{2}[0-9]{4}-([A-Z0-9]{9})_[0-9]{2}-[A-Z0-9]{16}\Z")
VERSION = re.compile(r"[0-9]{1,4}\.[0-9]{1,4}\Z")
MAX_ENTRIES = 65536
MAX_SFO = 512 * 1024


def numeric_version(text):
    if not isinstance(text, str) or not VERSION.fullmatch(text):
        return None
    return tuple(map(int, text.split(".")))


def firmware_from_system_ver(value):
    """Interpret firmware's BCD major/minor bytes, e.g. 0x11020000 = 11.02."""
    if not isinstance(value, int) or value == 0:
        return None
    major = (value >> 24) & 0xFF
    minor = (value >> 16) & 0xFF
    if any((byte >> 4) > 9 or (byte & 15) > 9 for byte in (major, minor)):
        return None
    return f"{major:02X}.{minor:02X}"


def parse_sfo(data):
    if len(data) < 20 or data[:4] != SFO_MAGIC:
        raise ValueError("No readable param.sfo magic")
    _magic, _revision, keys_at, values_at, count = struct.unpack_from(
        "<4sIIII", data, 0
    )
    if not (1 <= count <= 256 and
            20 + count * 16 <= keys_at <= values_at <= len(data)):
        raise ValueError("Invalid SFO table bounds")
    result = {}
    for i in range(count):
        key_rel, fmt, length, maximum, value_rel = struct.unpack_from(
            "<HHIII", data, 20 + i * 16
        )
        key_pos = keys_at + key_rel
        value_pos = values_at + value_rel
        if not (keys_at <= key_pos < values_at and length <= maximum and
                value_pos >= values_at and value_pos + length <= len(data)):
            raise ValueError("SFO entry is out of range")
        end = data.find(b"\0", key_pos, values_at)
        if end == -1:
            raise ValueError("Unterminated SFO key")
        key = data[key_pos:end].decode("ascii")
        if not key or key in result:
            raise ValueError("Missing or repeated SFO key")
        raw = data[value_pos:value_pos + length]
        if fmt == 0x0404 and length == 4:
            result[key] = struct.unpack("<I", raw)[0]
        elif fmt in (0x0004, 0x0204):
            result[key] = raw.partition(b"\0")[0].decode("utf-8", "replace")
        else:
            continue
    return result


def read_pkg_metadata(path):
    """Read PS4 PKG metadata via an ordinary read-only local file path."""
    with Path(path).open("rb") as stream:
        return read_pkg_metadata_stream(stream)


def read_pkg_metadata_stream(stream):
    """Inspect the already-open seekable PKG file: no new path lookup.

    This is needed when a caller holds a no-follow regular-file descriptor
    for metadata+SHA-256 review. Header/SFO do not prove signed/FPKG status.
    """
    stream.seek(0, 2)
    total_size = stream.tell()
    stream.seek(0)
    header = stream.read(0x80)
    if len(header) < 0x80 or header[:4] != PKG_MAGIC:
        raise ValueError("Not a recognizable PS4 PKG header")
    entry_count = struct.unpack_from(">I", header, 0x10)[0]
    table_at = struct.unpack_from(">I", header, 0x18)[0]
    raw_id = header[0x40:0x64].partition(b"\0")[0]
    try:
        header_id = raw_id.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("Non-ASCII PKG content ID") from error
    if not CONTENT_ID.fullmatch(header_id):
        raise ValueError("Malformed PKG content ID")
    if not (0 < entry_count <= MAX_ENTRIES and
            0x80 <= table_at <= total_size and
            entry_count * 32 <= total_size - table_at):
        raise ValueError("Invalid PKG file table bounds")

    stream.seek(table_at)
    table = stream.read(entry_count * 32)
    matches = []
    for index in range(entry_count):
        entry_id, _filename_off, flags1, _flags2, pos, size, _pad = \
            struct.unpack_from(">IIIIIIQ", table, index * 32)
        if entry_id == 0x1000:
            matches.append((flags1, pos, size))
    if len(matches) != 1:
        raise ValueError("Expected exactly one param.sfo entry")
    flags1, pos, size = matches[0]
    if not (0 < size <= MAX_SFO and pos <= total_size and
            size <= total_size - pos):
        raise ValueError("Invalid param.sfo bounds")
    stream.seek(pos)
    sfo = parse_sfo(stream.read(size))

    sfo_id = sfo.get("CONTENT_ID")
    category = sfo.get("CATEGORY")
    return {
        "header_content_id": header_id,
        "sfo_content_id": sfo_id,
        "header_sfo_id_match": header_id == sfo_id,
        "cusa": CONTENT_ID.fullmatch(header_id).group(1),
        "title_id": sfo.get("TITLE_ID"),
        "title": sfo.get("TITLE"),
        "category": category,
        "package_kind_from_category": (
            "update" if category == "gp" else
            "base_game" if category in ("gd", "gda", "gdo") else
            "dlc" if category == "ac" else "unknown"
        ),
        "app_ver": sfo.get("APP_VER"),
        "declared_min_fw": firmware_from_system_ver(
            sfo.get("SYSTEM_VER") or sfo.get("SYSTEM_ROOT_VER")
        ),
        "sfo_entry_flags1": f"0x{flags1:08X}",
    }


def inspect(path, inventory=None, firmware="14.00"):
    current_fw = numeric_version(firmware)
    if current_fw is None:
        raise ValueError("Invalid firmware string")
    report = {
        "schema": 1,
        "source_kind": "local_pkg_file",
        "file_name": Path(path).name,
        "status": "UNREADABLE_OR_UNSUPPORTED_PKG",
        "fpkg_confirmed": False,
        "base_patch_compatibility_confirmed": False,
        "installed_confirmed": False,
        "download_enabled": False,
        "install_enabled": False,
    }
    try:
        details = read_pkg_metadata(path)
    except (ValueError, OSError, struct.error) as error:
        report["error_type"] = type(error).__name__
        return report
    report.update(details)
    if not details["header_sfo_id_match"]:
        report["status"] = "CONTENT_ID_CONFLICT"
    elif details["title_id"] and details["title_id"] != details["cusa"]:
        report["status"] = "TITLE_ID_CONFLICT"
    elif details["package_kind_from_category"] != "update":
        report["status"] = "NOT_AN_UPDATE_PACKAGE"
    elif (numeric_version(details["app_ver"]) is None or
          numeric_version(details["declared_min_fw"]) is None):
        report["status"] = "INCOMPLETE_PATCH_METADATA"
    elif numeric_version(details["declared_min_fw"]) > current_fw:
        report["status"] = "FIRMWARE_TOO_LOW"
    elif inventory is None:
        report["status"] = "SNAPSHOT_REQUIRED"
    elif not isinstance(inventory, dict) or not isinstance(inventory.get("games"), list):
        raise ValueError("Inventory must contain games list")
    else:
        matches = [g for g in inventory["games"] if isinstance(g, dict)
                   and g.get("cusa") == details["cusa"]]
        if len(matches) != 1:
            report["status"] = "GAME_NOT_UNIQUELY_IN_SNAPSHOT"
        elif matches[0].get("content_id") != details["header_content_id"]:
            report["status"] = "INVENTORY_CONTENT_ID_MISMATCH"
        elif numeric_version(matches[0].get("app_ver")) is None:
            report["status"] = "INVALID_INVENTORY_VERSION"
        elif numeric_version(details["app_ver"]) <= numeric_version(matches[0]["app_ver"]):
            report["status"] = "NOT_NEWER_THAN_SNAPSHOT"
        else:
            report["status"] = "CANDIDATE_METADATA_ONLY"
            report["base_version_proven_compatible"] = False
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pkg", type=Path, help="Existing local .pkg file")
    parser.add_argument("--inventory", type=Path, help="Previous PS4 inventory JSON")
    parser.add_argument("--firmware", default="14.00")
    parser.add_argument("--out", type=Path, help="Save JSON report")
    args = parser.parse_args()
    inventory = (json.loads(args.inventory.read_text(encoding="utf-8"))
                 if args.inventory else None)
    result = inspect(args.pkg, inventory, args.firmware)
    output = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if args.out:
        args.out.write_text(output, encoding="utf-8")
    print(output, end="")


if __name__ == "__main__":
    main()
