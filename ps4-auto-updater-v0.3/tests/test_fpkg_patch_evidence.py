"""Host-only fake PKG/SFO bytes: this is NOT a PS4-installable artifact.

All paths use a tiny synthetic PKG-like test file. The manifest's SHA-256
does not prove publisher identity, FPKG authenticity, binding or install rights.
"""
import copy
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from fpkg_patch_evidence import review_local_patch, validate_manifest
from pkg_local_inspector import read_pkg_metadata
from test_pkg_local_inspector import synthetic_pkg

CUSA = "CUSA99999"
CID = "EP9000-CUSA99999_00-TESTONLY00000000"
OTHER_CID = "EP9000-CUSA99999_00-OTHERTST00000000"


class FpkgPatchEvidenceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.pkg = self.root / "dummy-synthetic.pkg"
        synthetic_pkg(self.pkg)
        self.manifest = {
            "schema": 1,
            "review_mode": "UNVERIFIED_LOCAL_MANIFEST",
            "declared_package_kind": "fpkg_patch",
            "cusa": CUSA,
            "content_id": CID,
            "patch_version": "01.02",
            "required_base_version": "01.00",
            "minimum_fw": "11.02",
            "size_bytes": self.pkg.stat().st_size,
            "sha256": hashlib.sha256(self.pkg.read_bytes()).hexdigest(),
        }
        self.inventory = {"games": [{
            "cusa": CUSA, "app_ver": "01.00",
            "content_id": CID, "package_kind": "fpkg",
        }]}

    def review(self, pkg=None, firmware="14.00"):
        return review_local_patch(pkg or self.pkg, self.manifest,
                                  self.inventory, firmware)

    def refresh_claim_hash(self):
        self.manifest["sha256"] = hashlib.sha256(self.pkg.read_bytes()).hexdigest()

    def safety(self, result):
        for flag in ("fpkg_confirmed", "publisher_rights_verified",
                     "base_patch_compatibility_confirmed",
                     "installed_confirmed", "download_allowed",
                     "install_allowed"):
            self.assertIs(result[flag], False, flag)

    def test_self_contained_synthetic_metadata_and_hash_match_is_review_only(self):
        before = self.pkg.read_bytes()
        result = self.review()
        self.assertEqual(result["status"], "METADATA_HASH_MATCH_REVIEW_ONLY")
        self.assertTrue(result["sha256_matched"])
        self.assertTrue(result["pkg_header_sfo_consistent"])
        self.assertEqual(result["package_category_observed"], "gp")
        self.safety(result)
        self.assertEqual(before, self.pkg.read_bytes())
        self.assertNotIn("download_url", result)

    def test_repeated_review_is_read_only(self):
        one = self.review()
        two = self.review()
        self.assertEqual(one, two)
        self.safety(two)

    def test_sha_tamper_rejected_even_when_header_sfo_still_parse(self):
        data = bytearray(self.pkg.read_bytes())
        data[0x600] ^= 1  # padding, outside synthetic SFO
        self.pkg.write_bytes(data)
        result = self.review()
        self.assertEqual(result["status"], "SHA256_MISMATCH")
        self.assertFalse(result["sha256_matched"])
        self.safety(result)

    def test_size_mismatch_rejected_before_expensive_hash(self):
        self.manifest["size_bytes"] -= 1
        result = self.review()
        self.assertEqual(result["status"], "SIZE_MISMATCH")
        self.assertFalse(result["sha256_matched"])
        self.safety(result)

    def test_non_update_categories_rejected_even_with_matching_hash(self):
        for category in ("gd", "ac", "gdp", "gpe", "unknown"):
            with self.subTest(category=category):
                synthetic_pkg(self.pkg, category=category)
                self.manifest["size_bytes"] = self.pkg.stat().st_size
                self.refresh_claim_hash()
                result = self.review()
                self.assertEqual(result["status"], "NOT_GAME_PATCH_CATEGORY")
                self.assertFalse(result["sha256_matched"])
                self.safety(result)

    def test_manifest_cannot_rename_pkg_as_different_game_or_version(self):
        self.manifest["content_id"] = OTHER_CID
        self.assertEqual(self.review()["status"], "MANIFEST_IDENTITY_MISMATCH")
        self.manifest["content_id"] = CID
        self.manifest["patch_version"] = "01.03"
        self.assertEqual(self.review()["status"], "MANIFEST_PATCH_VERSION_MISMATCH")

    def test_pkg_header_and_sfo_disagree(self):
        synthetic_pkg(self.pkg, content_id=OTHER_CID)
        self.manifest["size_bytes"] = self.pkg.stat().st_size
        self.refresh_claim_hash()
        result = self.review()
        self.assertEqual(result["status"], "HEADER_SFO_ID_CONFLICT")
        self.assertFalse(result["pkg_header_sfo_consistent"])

    def test_sfo_title_id_conflict(self):
        synthetic_pkg(self.pkg, title_id="CUSA12345")
        self.manifest["size_bytes"] = self.pkg.stat().st_size
        self.refresh_claim_hash()
        self.assertEqual(self.review()["status"], "TITLE_ID_CONFLICT")

    def test_manifest_firmware_claim_must_match_sfo(self):
        self.manifest["minimum_fw"] = "09.00"
        self.assertEqual(self.review()["status"], "MANIFEST_FIRMWARE_MISMATCH")

    def test_target_firmware_gate(self):
        result = self.review(firmware="10.00")
        self.assertEqual(result["status"], "FIRMWARE_TOO_LOW")
        self.assertTrue(result["sha256_matched"])
        self.safety(result)

    def test_required_base_version_must_match_inventory(self):
        self.manifest["required_base_version"] = "01.01"
        self.assertEqual(self.review()["status"], "BASE_VERSION_MISMATCH")

    def test_patch_must_be_newer_than_base(self):
        synthetic_pkg(self.pkg, app_ver="01.00")
        self.manifest["patch_version"] = "01.00"
        self.manifest["size_bytes"] = self.pkg.stat().st_size
        self.refresh_claim_hash()
        self.assertEqual(self.review()["status"], "PATCH_NOT_NEWER")

    def test_mismatch_or_duplicate_base_never_confirms_binding(self):
        self.inventory["games"][0]["content_id"] = OTHER_CID
        self.assertEqual(self.review()["status"], "BASE_CONTENT_ID_MISMATCH")
        self.inventory["games"][0]["content_id"] = CID
        self.inventory["games"].append(copy.deepcopy(self.inventory["games"][0]))
        self.assertEqual(self.review()["status"], "GAME_NOT_UNIQUELY_IN_SNAPSHOT")

    def test_retail_and_unknown_base_kind_are_never_eligible(self):
        self.inventory["games"][0]["package_kind"] = "retail"
        self.assertEqual(self.review()["status"], "RETAIL_BASE_UNSUPPORTED")
        del self.inventory["games"][0]["package_kind"]
        self.assertEqual(self.review()["status"], "BASE_FPKG_KIND_UNCONFIRMED")

    def test_symlink_and_directory_rejected_without_hanging(self):
        symlink = self.root / "symlink.pkg"
        symlink.symlink_to(self.pkg)
        result = self.review(symlink)
        self.assertEqual(result["status"], "UNREADABLE_OR_UNSUPPORTED_PKG")
        self.safety(result)
        result = self.review(self.root)
        self.assertEqual(result["status"], "NOT_A_REGULAR_FILE")

    def test_file_changed_after_sfo_read_never_trusted(self):
        import fpkg_patch_evidence as module
        original = module.read_pkg_metadata_stream
        def edited(stream):
            details = original(stream)
            data = bytearray(self.pkg.read_bytes())
            data[0x600] ^= 1
            self.pkg.write_bytes(data)
            return details
        with patch.object(module, "read_pkg_metadata_stream", side_effect=edited):
            result = self.review()
        self.assertEqual(result["status"], "FILE_CHANGED_DURING_REVIEW")
        self.safety(result)

    def test_filename_replaced_by_symlink_after_open_is_rejected(self):
        import fpkg_patch_evidence as module
        original = module.read_pkg_metadata_stream
        def swap(stream):
            details = original(stream)
            other = self.root / "saved-original.pkg"
            os.rename(self.pkg, other)
            self.pkg.symlink_to(other)
            return details
        with patch.object(module, "read_pkg_metadata_stream", side_effect=swap):
            result = self.review()
        self.assertEqual(result["status"], "FILE_CHANGED_DURING_REVIEW")
        self.safety(result)

    def test_invalid_manifest_action_claims_and_type_confusion_fail_closed(self):
        for key, value in (
            ("download_allowed", True),
            ("install_command", "run"),
            ("rights_verified", True),
            ("package_url", "https://evil.example/abc.pkg"),
            ("declared_package_kind", "retail_patch"),
            ("declared_package_kind", "base_game"),
            ("schema", True),
            ("size_bytes", True),
            ("sha256", "fake"),
            ("minimum_fw", "xx.xx"),
        ):
            with self.subTest(key=key, value=value):
                document = dict(self.manifest)
                document[key] = value
                with self.assertRaises(ValueError):
                    validate_manifest(document)

    def test_invalid_firmware_or_malformed_inventory_is_rejected(self):
        with self.assertRaises(ValueError):
            self.review(firmware="invalid")
        with self.assertRaises(ValueError):
            review_local_patch(self.pkg, self.manifest, {"games": "wrong"})

    def test_cli_produces_report_without_modifying_inputs(self):
        manifest_file = self.root / "claim.json"
        inventory_file = self.root / "inventory.json"
        output_file = self.root / "review.json"
        manifest_file.write_text(json.dumps(self.manifest), encoding="utf-8")
        inventory_file.write_text(json.dumps(self.inventory), encoding="utf-8")
        original = (self.pkg.read_bytes(), manifest_file.read_bytes(),
                    inventory_file.read_bytes())
        cmd = [
            sys.executable, str(ROOT/"fpkg_patch_evidence.py"),
            str(self.pkg), str(manifest_file), str(inventory_file),
            "--firmware", "14.00", "--out", str(output_file)
        ]
        done = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
        self.assertEqual(done.returncode, 0, done.stderr)
        result = json.loads(output_file.read_text(encoding="utf-8"))
        self.assertEqual(result["status"], "METADATA_HASH_MATCH_REVIEW_ONLY")
        self.assertEqual(
            original,
            (self.pkg.read_bytes(), manifest_file.read_bytes(),
             inventory_file.read_bytes())
        )

    def test_cli_rejects_duplicate_json_keys(self):
        manifest_file = self.root / "claim.json"
        inventory_file = self.root / "inventory.json"
        output_file = self.root / "review.json"
        manifest_file.write_text('{"schema":1,"schema":1}', encoding="utf-8")
        inventory_file.write_text(json.dumps(self.inventory), encoding="utf-8")
        cmd = [
            sys.executable, str(ROOT/"fpkg_patch_evidence.py"),
            str(self.pkg), str(manifest_file), str(inventory_file),
            "--out", str(output_file)
        ]
        done = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
        self.assertNotEqual(done.returncode, 0)
        self.assertIn("Duplicate input JSON key", done.stderr)
        self.assertFalse(output_file.exists())


if __name__ == "__main__":
    unittest.main()
