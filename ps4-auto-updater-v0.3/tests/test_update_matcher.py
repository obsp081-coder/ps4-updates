
"""Offline tests with fictional PS4 data."""

import pathlib
import sys
import unittest

sys.path.insert(
    0,
    str(pathlib.Path(__file__).resolve().parents[1])
)

from update_matcher import compare_updates, parse_version


DEMO_CONTENT_ID = (
    "DEMO-CUSA02343_00-MOCKONLY000000000"
)


class UpdateMatcherTests(unittest.TestCase):

    def setUp(self):
        self.inventory = {
            "games": [{
                "cusa": "CUSA02343",
                "name": "Example game",
                "app_ver": "01.00",
                "content_id": DEMO_CONTENT_ID,
            }]
        }

        self.check = {
            "games": [{
                "cusa": "CUSA02343",
                "installed": True,
            }]
        }

        self.catalog = {
            "updates": [{
                "cusa": "CUSA02343",
                "content_id": DEMO_CONTENT_ID,
                "version": "01.02",
                "min_fw": "11.00",
                "required_base_version": "01.00",
            }]
        }

    def result(self, firmware="14.00"):
        return compare_updates(
            self.inventory,
            self.check,
            self.catalog,
            firmware
        )

    def status(self, firmware="14.00"):
        return self.result(firmware)["results"][0]["status"]

    def test_newer_candidate_never_auto_installs(self):
        report = self.result()

        self.assertEqual(
            report["results"][0]["status"],
            "REVIEW_REQUIRED"
        )

        self.assertEqual(
            report["results"][0]["candidate_version"],
            "01.02"
        )

        self.assertFalse(report["auto_install"])
        self.assertFalse(report["compatibility_confirmed"])

    def test_duplicate_inventory_cusa_is_not_actionable(self):
        self.inventory["games"].append(dict(self.inventory["games"][0]))
        rows = self.result()["results"]
        self.assertEqual(len(rows), 2)
        self.assertTrue(all(item["status"] == "DUPLICATE_GAME_RECORD"
                            and item["candidate_version"] is None
                            for item in rows))

    def test_duplicate_best_patch_version_is_not_actionable(self):
        self.catalog["updates"].append(dict(self.catalog["updates"][0]))
        self.assertEqual(self.status(), "AMBIGUOUS_UPDATE_METADATA")
        self.assertIsNone(self.result()["results"][0]["candidate_version"])

    def test_unauthorized_catalog_install_flag_is_rejected(self):
        for flag in ("auto_install", "install_enabled",
                     "downloads_enabled", "compatibility_confirmed",
                     "fpkg_compatibility_confirmed"):
            with self.subTest(flag=flag):
                self.catalog[flag] = True
                with self.assertRaises(ValueError):
                    self.result()
                del self.catalog[flag]

    def test_not_installed_is_filtered(self):
        self.check["games"][0]["installed"] = False

        self.assertEqual(
            self.status(),
            "NOT_INSTALLED"
        )

    def test_unconfirmed_is_not_considered_installed(self):
        self.check["games"][0]["installed"] = None

        self.assertEqual(
            self.status(),
            "INSTALL_UNCONFIRMED"
        )

    def test_duplicate_install_records_never_confirm_presence(self):
        self.check["games"].append({
            "cusa": "CUSA02343", "installed": False
        })
        self.assertEqual(self.status(), "INSTALL_UNCONFIRMED")

    def test_invalid_install_type_is_ignored(self):
        self.check["games"][0]["installed"] = "yes"
        self.assertEqual(self.status(), "INSTALL_UNCONFIRMED")

    def test_missing_install_record_is_unconfirmed(self):
        self.check["games"] = []

        self.assertEqual(
            self.status(),
            "INSTALL_UNCONFIRMED"
        )

    def test_content_id_mismatch(self):
        self.catalog["updates"][0]["content_id"] = (
            "OTHER-CONTENT"
        )

        self.assertEqual(
            self.status(),
            "CONTENT_ID_MISMATCH"
        )

    def test_equal_version_is_not_update(self):
        self.catalog["updates"][0]["version"] = "01.00"

        self.assertEqual(
            self.status(),
            "UP_TO_DATE_IN_CATALOG"
        )

    def test_older_version_is_not_update(self):
        self.catalog["updates"][0]["version"] = "00.99"

        self.assertEqual(
            self.status(),
            "UP_TO_DATE_IN_CATALOG"
        )

    def test_firmware_minimum(self):
        self.catalog["updates"][0]["min_fw"] = "15.00"

        self.assertEqual(
            self.status(),
            "FIRMWARE_TOO_LOW"
        )

    def test_invalid_catalog_version(self):
        self.catalog["updates"][0]["version"] = "something"

        self.assertEqual(
            self.status(),
            "INVALID_CATALOG_METADATA"
        )

    def test_no_update_in_catalog(self):
        self.catalog["updates"] = []

        self.assertEqual(
            self.status(),
            "NO_UPDATE_LISTED"
        )

    def test_base_version_restriction(self):
        self.catalog["updates"][0][
            "required_base_version"
        ] = "01.01"

        self.assertEqual(
            self.status(),
            "BASE_VERSION_NOT_CONFIRMED"
        )

    def test_missing_base_requirement_never_counts_as_review_eligible(self):
        del self.catalog["updates"][0]["required_base_version"]
        self.assertEqual(self.status(), "BASE_VERSION_NOT_CONFIRMED")
        self.assertIsNone(self.result()["results"][0]["candidate_version"])

    def test_choose_highest_eligible_not_highest_unsupported(self):
        self.catalog["updates"].append({
            "cusa": "CUSA02343",
            "content_id": DEMO_CONTENT_ID,
            "version": "01.08",
            "min_fw": "16.00",
        })

        self.assertEqual(
            self.result()["results"][0]["candidate_version"],
            "01.02"
        )

    def test_versions_are_numeric(self):
        self.assertLess(
            parse_version("01.09"),
            parse_version("01.10")
        )

    def test_reject_bad_firmware(self):
        with self.assertRaises(ValueError):
            self.result("unknown")

    def test_reject_invalid_game_id(self):
        self.inventory["games"][0]["cusa"] = "BAD"

        self.assertEqual(
            self.status(),
            "INVALID_GAME_ID"
        )

    def test_invalid_game_version(self):
        self.inventory["games"][0]["app_ver"] = "abc"

        self.assertEqual(
            self.status(),
            "INVALID_GAME_METADATA"
        )


if __name__ == "__main__":
    unittest.main()
