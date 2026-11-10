
"""Offline integration tests for FPKG candidate review.

All patch records below are FICTIONAL test data, not real downloads.
No PS4, network connection, PKG files, or installation required.
"""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT))

from catalog_ingest import ingest
from fpkg_review import review

CUSA = "CUSA02343"
CONTENT_ID = "EP9000-CUSA02343_00-UNCHARTEDTRILOGY"


class FpkgReviewTests(unittest.TestCase):
    def setUp(self):
        self.inventory = {
            "schema": 4,
            "games": [{
                "cusa": CUSA,
                "name": "Fictional test game",
                "app_ver": "01.00",
                "content_id": CONTENT_ID,
                # A fictional kind declaration is NOT actual PS4 proof.
                "package_kind": "fpkg",
            }],
        }
        self.entry = {
            "cusa": CUSA,
            "content_id": CONTENT_ID,
            "version": "01.02",
            "min_fw": "11.00",
            "required_base_version": "01.00",
            "package_kind": "fpkg_patch",
            "source_page": "https://example.org/fictional-fixture",
        }
        self.installed = {
            "schema": 2,
            "games": [{"cusa": CUSA, "installed": True}],
        }

    def catalog(self, entries=None):
        return ingest({
            "schema": 1,
            "submissions": self.entry_list(entries),
        })

    def entry_list(self, entries):
        return [self.entry] if entries is None else entries

    def check(self, catalog=None, installed=None, firmware="14.00"):
        if catalog is None:
            catalog = self.catalog()
        if installed is None:
            installed = self.installed
        return review(self.inventory, catalog, installed, firmware)

    def status(self, **kwargs):
        return self.check(**kwargs)["games"][0]["status"]

    def test_empty_catalog_no_fpkg_updates_claimed(self):
        report = review(self.inventory)
        self.assertEqual(
            report["games"][0]["status"],
            "NOT_IN_FPKG_CATALOG"
        )
        self.assertIsNone(
            report["games"][0]["fpkg_candidate_version"]
        )
        self.assertFalse(report["install_enabled"])
        self.assertFalse(report["download_enabled"])

    def test_candidate_with_install_report_still_requires_manual_review(self):
        report = self.check()
        self.assertEqual(
            report["games"][0]["status"],
            "MANUAL_REVIEW_REQUIRED"
        )
        self.assertEqual(
            report["games"][0]["fpkg_candidate_version"],
            "01.02"
        )
        self.assertFalse(report["fpkg_compatibility_confirmed"])
        self.assertFalse(report["games"][0]["install_enabled"])
        self.assertFalse(
            report["games"][0]["fpkg_compatibility_confirmed"]
        )

    def test_without_install_confirmation_is_blocked(self):
        self.assertEqual(
            review(
                self.inventory,
                self.catalog()
            )["games"][0]["status"],
            "INSTALL_UNCONFIRMED",
        )

    def test_known_absent_is_not_eligible(self):
        self.installed["games"][0]["installed"] = False
        self.assertEqual(
            self.status(),
            "NOT_INSTALLED_IN_REPORT"
        )

    def test_unknown_install_state_is_not_eligible(self):
        self.installed["games"][0]["installed"] = None
        self.assertEqual(
            self.status(),
            "INSTALL_UNCONFIRMED"
        )

    def test_unknown_kind_never_gets_an_fpkg_candidate(self):
        self.inventory["games"][0]["package_kind"] = "unknown"
        self.assertEqual(
            self.status(),
            "BASE_PACKAGE_KIND_UNCONFIRMED"
        )

    def test_missing_kind_never_gets_an_fpkg_candidate(self):
        del self.inventory["games"][0]["package_kind"]
        self.assertEqual(self.status(), "BASE_PACKAGE_KIND_UNCONFIRMED")

    def test_retail_base_excluded_even_if_fpkg_catalog_has_same_cusa(self):
        self.inventory["games"][0]["package_kind"] = "retail"
        report = self.check()
        self.assertEqual(report["games"][0]["status"], "RETAIL_UNSUPPORTED")
        self.assertIsNone(report["games"][0]["fpkg_candidate_version"])
        self.assertFalse(report["download_enabled"])
        self.assertFalse(report["install_enabled"])

    def test_content_id_mismatch_is_blocked(self):
        self.entry["content_id"] = (
            "EP9000-CUSA02343_00-OTHERCONTENT0000"
        )
        self.assertEqual(
            self.status(),
            "CONTENT_ID_MISMATCH"
        )

    def test_same_or_older_version_is_not_candidate(self):
        for value in ("01.00", "00.99"):
            with self.subTest(version=value):
                self.entry["version"] = value
                self.assertEqual(
                    self.status(),
                    "NO_NEWER_FPKG_CANDIDATE"
                )

    def test_incompatible_firmware_is_blocked(self):
        self.entry["min_fw"] = "15.00"
        self.assertEqual(
            self.status(),
            "FW_REQUIREMENT_NOT_MET"
        )

    def test_missing_base_version_is_blocked(self):
        self.entry.pop("required_base_version")
        self.assertEqual(
            self.status(),
            "BASE_VERSION_NOT_CONFIRMED"
        )

    def test_wrong_base_version_is_blocked(self):
        self.entry["required_base_version"] = "01.01"
        self.assertEqual(
            self.status(),
            "BASE_VERSION_NOT_CONFIRMED"
        )

    def test_select_highest_eligible_version(self):
        higher = dict(self.entry, version="01.10")
        incompatible = dict(
            self.entry,
            version="01.11",
            min_fw="15.00"
        )
        catalog = self.catalog([
            self.entry,
            higher,
            incompatible
        ])
        report = self.check(catalog=catalog)
        self.assertEqual(
            report["games"][0]["fpkg_candidate_version"],
            "01.10"
        )
        self.assertEqual(
            report["games"][0]["status"],
            "MANUAL_REVIEW_REQUIRED"
        )

    def test_demo_catalog_not_accepted(self):
        catalog = self.catalog()
        catalog["fixture_only"] = True
        with self.assertRaises(ValueError):
            self.check(catalog=catalog)

    def test_retail_catalog_not_accepted_as_fpkg(self):
        with self.assertRaises(ValueError):
            self.check(catalog={
                "schema": 1,
                "updates": []
            })

    def test_forged_download_install_claims_are_rejected(self):
        for key, value in (
            ("download_url", "https://example.org/x.pkg"),
            ("install_allowed", True),
            ("source_authorized", True),
            ("rights_verified", True),
            ("fpkg_compatibility_confirmed", True),
        ):
            with self.subTest(key=key):
                catalog = self.catalog()
                catalog["updates"][0][key] = value
                with self.assertRaises(ValueError):
                    self.check(catalog=catalog)

    def test_missing_false_permissions_and_retained_catalog_flags(self):
        catalog = self.catalog()
        del catalog["auto_install"]
        with self.assertRaises(ValueError):
            self.check(catalog=catalog)
        catalog = self.catalog()
        catalog["compatibility_confirmed"] = "false"
        with self.assertRaises(ValueError):
            self.check(catalog=catalog)

    def test_forged_source_page_is_not_a_trusted_download_source(self):
        catalog = self.catalog()
        catalog["updates"][0]["source_page"] = "http://example.org/patch"
        with self.assertRaises(ValueError):
            self.check(catalog=catalog)

    def test_user_claimed_fpkg_kind_is_still_unverified(self):
        report = self.check()
        self.assertEqual(report["package_policy"], "fpkg_only")
        self.assertFalse(report["games"][0]["base_kind_independently_verified"])
        self.assertFalse(report["fpkg_compatibility_confirmed"])
        self.assertFalse(report["install_enabled"])
        self.assertFalse(report["download_enabled"])

    def test_duplicate_install_reports_are_rejected(self):
        self.installed["games"].append({
            "cusa": CUSA,
            "installed": True
        })
        with self.assertRaises(ValueError):
            self.check()

    def test_invalid_comparison_is_rejected(self):
        self.inventory["games"][0][
            "app_ver"
        ] = "invalid"
        with self.assertRaises(ValueError):
            self.check()

    def test_cli_writes_report_without_altering_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            sources = {
                "inventory.json": self.inventory,
                "catalog.json": self.catalog(),
                "installed.json": self.installed,
            }
            original = {}

            for name, data in sources.items():
                path = root / name
                value = json.dumps(
                    data,
                    ensure_ascii=False
                ).encode("utf-8")
                path.write_bytes(value)
                original[name] = value

            output = root / "review.json"

            command = [
                sys.executable,
                str(PROJECT / "fpkg_review.py"),
                str(root / "inventory.json"),
                "--catalog",
                str(root / "catalog.json"),
                "--installed-check",
                str(root / "installed.json"),
                "--firmware",
                "14.00",
                "--out",
                str(output),
            ]

            process = subprocess.run(
                command,
                capture_output=True,
                text=True,
                timeout=10
            )

            self.assertEqual(
                process.returncode,
                0,
                process.stderr
            )

            result = json.loads(
                output.read_text(encoding="utf-8")
            )

            self.assertEqual(
                result["games"][0]["status"],
                "MANUAL_REVIEW_REQUIRED"
            )
            self.assertFalse(result["install_enabled"])
            self.assertFalse(result["download_enabled"])

            for name, content in original.items():
                self.assertEqual(
                    (root / name).read_bytes(),
                    content
                )


if __name__ == "__main__":
    unittest.main()
