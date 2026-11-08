"""Offline tests for the read-only PS4 PKG metadata inspector.

Creates tiny SYNTHETIC .pkg-like files, never game packages.
No real console, network, external fixture, download, or installation needed.
"""

import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT))

from pkg_local_inspector import (
    firmware_from_system_ver,
    inspect,
    parse_sfo,
    read_pkg_metadata,
)

CUSA = "CUSA99999"
CID = "EP9000-CUSA99999_00-TESTONLY00000000"
OTHER_CID = "EP9000-CUSA99999_00-OTHERTST00000000"


def synthetic_sfo(category="gp", app_ver="01.02", system_ver=0x11020000,
                  content_id=CID, title_id=CUSA, include_firmware=True):
    """Make a minimal plaintext param.sfo in memory for testing only."""
    values = {
        "APP_VER": app_ver,
        "CATEGORY": category,
        "CONTENT_ID": content_id,
        "TITLE_ID": title_id,
        "TITLE": "SYNTHETIC TEST -- NOT A GAME",
    }
    if include_firmware:
        values["SYSTEM_VER"] = system_ver

    keys = bytearray()
    data = bytearray()
    indexes = bytearray()

    for key, value in values.items():
        key_offset = len(keys)
        keys.extend(key.encode("ascii") + b"\0")
        value_offset = len(data)
        if isinstance(value, int):
            fmt = 0x0404
            raw = struct.pack("<I", value)
        else:
            fmt = 0x0204
            raw = value.encode("utf-8") + b"\0"
        data.extend(raw)
        indexes.extend(struct.pack("<HHIII", key_offset, fmt,
                                   len(raw), len(raw), value_offset))

    key_start = 20 + len(indexes)
    data_start = key_start + len(keys)
    return (struct.pack("<4sIIII", b"\0PSF", 0x101,
                        key_start, data_start, len(values))
            + indexes + keys + data)


def synthetic_pkg(path, *, category="gp", app_ver="01.02",
                  system_ver=0x11020000, content_id=CID,
                  header_content_id=CID, title_id=CUSA,
                  include_firmware=True):
    """Write a small PKG-shaped fixture with one param.sfo table entry."""
    sfo = synthetic_sfo(category, app_ver, system_ver,
                        content_id, title_id, include_firmware)
    payload = bytearray(0x1000 + len(sfo))
    payload[:4] = b"\x7fCNT"
    struct.pack_into(">I", payload, 0x10, 1)
    struct.pack_into(">I", payload, 0x18, 0x100)
    payload[0x40:0x40 + len(header_content_id)] = header_content_id.encode()
    struct.pack_into(">IIIIIIQ", payload, 0x100, 0x1000, 0,
                     0, 0, 0x1000, len(sfo), 0)
    payload[0x1000:0x1000 + len(sfo)] = sfo
    path.write_bytes(payload)


class PkgLocalInspectorTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.pkg = self.root / "synthetic.pkg"
        self.inventory = {"metadata_only": True, "games": [{
            "cusa": CUSA,
            "app_ver": "01.00",
            "content_id": CID,
            "installed_confirmed": False,
        }]}
        synthetic_pkg(self.pkg)

    def test_sfo_readable(self):
        result = parse_sfo(synthetic_sfo())
        self.assertEqual(result["CATEGORY"], "gp")
        self.assertEqual(result["CONTENT_ID"], CID)

    def test_metadata_reads_from_pkg(self):
        result = read_pkg_metadata(self.pkg)
        self.assertEqual(result["cusa"], CUSA)
        self.assertEqual(result["declared_min_fw"], "11.02")
        self.assertEqual(result["package_kind_from_category"], "update")

    def test_candidate_never_approved(self):
        result = inspect(self.pkg, self.inventory)
        self.assertEqual(result["status"], "CANDIDATE_METADATA_ONLY")
        for flag in ("fpkg_confirmed", "base_patch_compatibility_confirmed",
                     "installed_confirmed", "download_enabled", "install_enabled",
                     "base_version_proven_compatible"):
            self.assertIs(result[flag], False, flag)

    def test_base_game_not_update(self):
        synthetic_pkg(self.pkg, category="gd")
        self.assertEqual(inspect(self.pkg, self.inventory)["status"],
                         "NOT_AN_UPDATE_PACKAGE")

    def test_dlc_not_update(self):
        synthetic_pkg(self.pkg, category="ac")
        self.assertEqual(inspect(self.pkg, self.inventory)["status"],
                         "NOT_AN_UPDATE_PACKAGE")

    def test_unknown_category_not_update(self):
        synthetic_pkg(self.pkg, category="unknown")
        self.assertEqual(inspect(self.pkg, self.inventory)["status"],
                         "NOT_AN_UPDATE_PACKAGE")

    def test_firmware_too_low(self):
        self.assertEqual(inspect(self.pkg, self.inventory, firmware="10.00")["status"],
                         "FIRMWARE_TOO_LOW")

    def test_missing_firmware(self):
        synthetic_pkg(self.pkg, include_firmware=False)
        self.assertEqual(inspect(self.pkg, self.inventory)["status"],
                         "INCOMPLETE_PATCH_METADATA")

    def test_corrupt_system_version(self):
        synthetic_pkg(self.pkg, system_ver=0)
        self.assertEqual(inspect(self.pkg, self.inventory)["status"],
                         "INCOMPLETE_PATCH_METADATA")

    def test_missing_inventory(self):
        self.assertEqual(inspect(self.pkg)["status"], "SNAPSHOT_REQUIRED")

    def test_inventory_cusa_missing(self):
        self.inventory["games"] = []
        self.assertEqual(inspect(self.pkg, self.inventory)["status"],
                         "GAME_NOT_UNIQUELY_IN_SNAPSHOT")

    def test_duplicate_cusa_in_inventory(self):
        self.inventory["games"].append(self.inventory["games"][0].copy())
        self.assertEqual(inspect(self.pkg, self.inventory)["status"],
                         "GAME_NOT_UNIQUELY_IN_SNAPSHOT")

    def test_inventory_content_id_mismatch(self):
        self.inventory["games"][0]["content_id"] = OTHER_CID
        self.assertEqual(inspect(self.pkg, self.inventory)["status"],
                         "INVENTORY_CONTENT_ID_MISMATCH")

    def test_sfo_header_content_id_mismatch(self):
        synthetic_pkg(self.pkg, content_id=OTHER_CID)
        self.assertEqual(inspect(self.pkg, self.inventory)["status"],
                         "CONTENT_ID_CONFLICT")

    def test_title_id_mismatch(self):
        synthetic_pkg(self.pkg, title_id="CUSA12345")
        self.assertEqual(inspect(self.pkg, self.inventory)["status"],
                         "TITLE_ID_CONFLICT")

    def test_same_version_not_new(self):
        synthetic_pkg(self.pkg, app_ver="01.00")
        self.assertEqual(inspect(self.pkg, self.inventory)["status"],
                         "NOT_NEWER_THAN_SNAPSHOT")

    def test_wrong_magic_rejected(self):
        content = bytearray(self.pkg.read_bytes())
        content[0:4] = b"FAKE"
        self.pkg.write_bytes(content)
        self.assertEqual(inspect(self.pkg)["status"],
                         "UNREADABLE_OR_UNSUPPORTED_PKG")

    def test_bad_sfo_magic_rejected(self):
        content = bytearray(self.pkg.read_bytes())
        content[0x1000:0x1004] = b"FAKE"
        self.pkg.write_bytes(content)
        self.assertEqual(inspect(self.pkg)["status"],
                         "UNREADABLE_OR_UNSUPPORTED_PKG")

    def test_bad_table_bounds_rejected(self):
        content = bytearray(self.pkg.read_bytes())
        struct.pack_into(">I", content, 0x10, 65537)
        self.pkg.write_bytes(content)
        self.assertEqual(inspect(self.pkg)["status"],
                         "UNREADABLE_OR_UNSUPPORTED_PKG")

    def test_read_only(self):
        before = self.pkg.read_bytes()
        inspect(self.pkg, self.inventory)
        self.assertEqual(self.pkg.read_bytes(), before)

    def test_system_version_bcd(self):
        self.assertEqual(firmware_from_system_ver(0x11020000), "11.02")
        self.assertIsNone(firmware_from_system_ver(0))
        self.assertIsNone(firmware_from_system_ver(0x1A020000))

    def test_cli_writes_json_and_does_not_change_pkg(self):
        inventory_file = self.root / "inventory.json"
        result_file = self.root / "result.json"
        inventory_file.write_text(json.dumps(self.inventory), encoding="utf-8")
        pkg_before = self.pkg.read_bytes()
        inventory_before = inventory_file.read_bytes()
        command = [sys.executable, str(PROJECT / "pkg_local_inspector.py"),
                   str(self.pkg), "--inventory", str(inventory_file),
                   "--firmware", "14.00", "--out", str(result_file)]
        result = subprocess.run(command, text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result_file.read_text(encoding="utf-8"))
        self.assertEqual(report["status"], "CANDIDATE_METADATA_ONLY")
        self.assertEqual(self.pkg.read_bytes(), pkg_before)
        self.assertEqual(inventory_file.read_bytes(), inventory_before)


if __name__ == "__main__":
    unittest.main()
