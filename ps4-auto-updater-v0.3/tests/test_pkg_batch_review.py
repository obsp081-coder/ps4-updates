"""Offline batch tests. Fixtures are synthetic PKG-shaped files, NOT game PKGs.

No download, PlayStation, package installation, or filesystem modification of
user data. Passing these tests does NOT establish real FPKG compatibility.
"""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT))

from pkg_batch_review import scan, validate_inventory
from test_pkg_local_inspector import synthetic_pkg

CUSA = "CUSA02343"
CID = "EP9000-CUSA02343_00-UNCHARTEDTRILOGY"
OTHER = "CUSA00411"
OTHER_CID = "EP1004-CUSA00411_00-GTAVDIGITALDOWNL"


class PkgBatchReviewTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.pkgs = self.root / "pkgs"
        self.pkgs.mkdir()
        self.inventory = {
            "schema": 4,
            "source": "synthetic_test_snapshot",
            "metadata_only": True,
            "games": [
                {"cusa": CUSA, "name": "Uncharted test", "app_ver": "01.00",
                 "content_id": CID, "installed_confirmed": False},
                {"cusa": OTHER, "name": "GTA V test", "app_ver": "01.57",
                 "content_id": OTHER_CID, "installed_confirmed": False},
            ],
        }

    def make_pkg(self, name="test.pkg", *, category="gp", version="01.02",
                 content_id=CID, title_id=CUSA, system_ver=0x11020000):
        path = self.pkgs / name
        synthetic_pkg(path, category=category, app_ver=version,
                      content_id=content_id, header_content_id=content_id,
                      title_id=title_id, system_ver=system_ver)
        return path

    def run_scan(self, firmware="14.00"):
        return scan(self.pkgs, self.inventory, firmware)

    def test_empty_folder_has_no_candidates(self):
        report = self.run_scan()
        self.assertEqual(report["summary"]["pkg_entries_scanned"], 0)
        self.assertEqual(report["summary"]["games_without_local_pkg"], 2)
        self.assertTrue(all(g["status"] == "NO_LOCAL_PKG"
                            for g in report["games"]))

    def test_candidate_is_metadata_only(self):
        self.make_pkg()
        report = self.run_scan()
        self.assertEqual(report["packages"][0]["status"],
                         "CANDIDATE_METADATA_ONLY")
        self.assertEqual(report["games"][0]["status"],
                         "METADATA_CANDIDATE_PRESENT")
        self.assertEqual(report["games"][1]["status"], "NO_LOCAL_PKG")
        self.assertEqual(report["summary"]["metadata_candidates_only"], 1)

    def test_safety_flags_remain_false(self):
        self.make_pkg()
        self.make_pkg("bad.pkg", category="gd")
        report = self.run_scan()
        for flag in ("download_enabled", "install_enabled",
                     "fpkg_compatibility_confirmed", "installation_confirmed"):
            self.assertIs(report[flag], False, flag)
        self.assertIs(report["inventory_is_live"], False)
        for package in report["packages"]:
            for flag in ("download_enabled", "install_enabled",
                         "fpkg_confirmed", "installed_confirmed",
                         "base_patch_compatibility_confirmed"):
                self.assertIs(package[flag], False, flag)

    def test_base_and_dlc_are_not_updates(self):
        self.make_pkg("base.pkg", category="gd")
        self.make_pkg("dlc.pkg", category="ac")
        report = self.run_scan()
        self.assertEqual([p["status"] for p in report["packages"]],
                         ["NOT_AN_UPDATE_PACKAGE"] * 2)
        self.assertEqual(report["games"][0]["status"],
                         "ONLY_UNVERIFIED_OR_REJECTED_FILES")

    def test_mixed_candidate_and_rejection(self):
        self.make_pkg("a.pkg")
        self.make_pkg("b.pkg", category="gd")
        report = self.run_scan()
        self.assertEqual(report["summary"]["metadata_candidates_only"], 1)
        self.assertEqual(report["summary"]["other_or_rejected_entries"], 1)
        self.assertEqual(len(report["games"][0]["package_checks"]), 2)

    def test_version_not_new_is_rejected(self):
        self.make_pkg(version="01.00")
        self.assertEqual(self.run_scan()["packages"][0]["status"],
                         "NOT_NEWER_THAN_SNAPSHOT")

    def test_firmware_too_low(self):
        self.make_pkg(system_ver=0x15000000)
        self.assertEqual(self.run_scan()["packages"][0]["status"],
                         "FIRMWARE_TOO_LOW")

    def test_unlisted_content_id_rejected(self):
        self.make_pkg(content_id="EP9000-CUSA02343_00-TESTONLY00000000")
        self.assertEqual(self.run_scan()["packages"][0]["status"],
                         "INVENTORY_CONTENT_ID_MISMATCH")

    def test_corrupt_pkg_is_reported_not_crash(self):
        (self.pkgs / "broken.pkg").write_bytes(b"not a package")
        report = self.run_scan()
        self.assertEqual(report["packages"][0]["status"],
                         "UNREADABLE_OR_UNSUPPORTED_PKG")
        self.assertEqual(report["summary"]["other_or_rejected_entries"], 1)

    def test_case_insensitive_extension(self):
        self.make_pkg("patch.PKG")
        self.assertEqual(self.run_scan()["summary"]["pkg_entries_scanned"], 1)

    def test_non_pkg_files_ignored(self):
        (self.pkgs / "notes.txt").write_text("nothing to install")
        self.assertEqual(self.run_scan()["packages"], [])

    def test_subfolders_ignored(self):
        nested = self.pkgs / "nested"
        nested.mkdir()
        (nested / "example.pkg").write_bytes(b"fake")
        self.assertEqual(self.run_scan()["summary"]["pkg_entries_scanned"], 0)

    def test_symlinked_pkg_skipped(self):
        target = self.root / "outside.pkg"
        target.write_bytes(b"fake")
        link = self.pkgs / "linked.pkg"
        try:
            link.symlink_to(target)
        except (NotImplementedError, OSError):
            self.skipTest("symlinks unavailable")
        report = self.run_scan()
        self.assertEqual(report["packages"][0]["status"],
                         "SKIPPED_NONREGULAR_OR_SYMLINK")
        self.assertFalse(report["packages"][0]["install_enabled"])

    def test_symlinked_folder_is_rejected(self):
        link = self.root / "linked_folder"
        try:
            link.symlink_to(self.pkgs, target_is_directory=True)
        except (NotImplementedError, OSError):
            self.skipTest("symlinks unavailable")
        with self.assertRaises(ValueError):
            scan(link, self.inventory)

    def test_duplicate_inventory_id_rejected(self):
        self.inventory["games"].append(self.inventory["games"][0].copy())
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.run_scan()

    def test_invalid_inventory_version_rejected(self):
        self.inventory["games"][0]["app_ver"] = "bad-version"
        with self.assertRaises(ValueError):
            validate_inventory(self.inventory)

    def test_invalid_firmware_rejected(self):
        with self.assertRaises(ValueError):
            self.run_scan(firmware="abc")

    def test_more_than_100_pkg_entries_rejected(self):
        for i in range(101):
            (self.pkgs / f"test{i:03}.pkg").write_bytes(b"")
        with self.assertRaisesRegex(ValueError, "more than 100"):
            self.run_scan()

    def test_deterministic_casefold_sorting(self):
        self.make_pkg("z.pkg")
        self.make_pkg("A.pkg")
        names = [p["file_name"] for p in self.run_scan()["packages"]]
        self.assertEqual(names, ["A.pkg", "z.pkg"])

    def test_cli_and_inputs_remain_unchanged(self):
        pkg = self.make_pkg()
        inventory_file = self.root / "inventory.json"
        inventory_file.write_text(json.dumps(self.inventory), encoding="utf-8")
        output = self.root / "batch.json"
        before_pkg = pkg.read_bytes()
        before_inv = inventory_file.read_bytes()
        process = subprocess.run([
            sys.executable, str(PROJECT / "pkg_batch_review.py"),
            str(self.pkgs), "--inventory", str(inventory_file),
            "--firmware", "14.00", "--out", str(output),
        ], capture_output=True, text=True, timeout=10)
        self.assertEqual(process.returncode, 0, process.stderr)
        report = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(report["summary"]["metadata_candidates_only"], 1)
        self.assertEqual(pkg.read_bytes(), before_pkg)
        self.assertEqual(inventory_file.read_bytes(), before_inv)


if __name__ == "__main__":
    unittest.main()
