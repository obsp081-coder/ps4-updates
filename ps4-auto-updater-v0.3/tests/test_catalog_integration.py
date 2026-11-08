
"""End-to-end offline tests for the demo PS4 catalog.

Uses simulated inventory and installation status.
Never downloads or installs game updates.
"""

import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
MATCHER = ROOT / "update_matcher.py"
CATALOG = ROOT / "demo_update_catalog.json"

UNCHARTED_ID = "DEMO-CUSA02343_00-MOCKONLY000000000"
GTA_ID = "DEMO-CUSA00411_00-MOCKONLY000000000"
DBD_ID = "DEMO-CUSA08032_00-MOCKONLY000000000"
ROBLOX_ID = "DEMO-CUSA06122_00-MOCKONLY000000000"


class CatalogIntegrationTests(unittest.TestCase):

    def setUp(self):
        self.catalog = json.loads(
            CATALOG.read_text(encoding="utf-8")
        )

        self.inventory = {
            "schema": 4,
            "metadata_only": True,
            "games": [
                {
                    "cusa": "CUSA02343",
                    "name": "Uncharted Collection",
                    "app_ver": "01.00",
                    "content_id": UNCHARTED_ID
                },
                {
                    "cusa": "CUSA00411",
                    "name": "Grand Theft Auto V",
                    "app_ver": "01.57",
                    "content_id": GTA_ID
                },
                {
                    "cusa": "CUSA08032",
                    "name": "Dead by Daylight",
                    "app_ver": "03.83",
                    "content_id": DBD_ID
                },
                {
                    "cusa": "CUSA06122",
                    "name": "Roblox",
                    "app_ver": "02.62",
                    "content_id": ROBLOX_ID
                }
            ]
        }

        self.installed = {
            "schema": 2,
            "games": [
                {
                    "cusa": "CUSA02343",
                    "installed": True
                },
                {
                    "cusa": "CUSA00411",
                    "installed": True
                },
                {
                    "cusa": "CUSA08032",
                    "installed": False
                },
                {
                    "cusa": "CUSA06122",
                    "installed": True
                }
            ]
        }

    def run_matcher(self, firmware="14.00"):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)

            inventory_path = folder / "inventory.json"
            installed_path = folder / "installed_check.json"
            catalog_path = folder / "catalog.json"

            documents = (
                (inventory_path, self.inventory),
                (installed_path, self.installed),
                (catalog_path, self.catalog)
            )

            for path, document in documents:
                path.write_text(
                    json.dumps(
                        document,
                        ensure_ascii=False,
                        indent=2
                    ),
                    encoding="utf-8"
                )

            before = {
                path.name: path.read_bytes()
                for path, _ in documents
            }

            command = [
                sys.executable,
                str(MATCHER),
                str(inventory_path),
                str(installed_path),
                str(catalog_path),
                "--firmware",
                firmware
            ]

            result = subprocess.run(
                command,
                capture_output=True,
                text=True,
                timeout=10
            )

            self.assertEqual(
                result.returncode,
                0,
                result.stderr
            )

            report = json.loads(result.stdout)

            # Confirm the CLI did not alter input files.
            for path, _ in documents:
                self.assertEqual(
                    path.read_bytes(),
                    before[path.name]
                )

            return report

    @staticmethod
    def status_map(report):
        return {
            item["cusa"]: item
            for item in report["results"]
        }

    def test_demo_catalog_is_clearly_fictional(self):
        self.assertIs(
            self.catalog.get("fixture_only"),
            True
        )

        self.assertEqual(
            len(self.catalog["updates"]),
            3
        )

        for update in self.catalog["updates"]:
            self.assertIs(
                update.get("demo_only"),
                True
            )

            for prohibited in (
                "url",
                "download_url",
                "pkg_url",
                "install_command"
            ):
                self.assertNotIn(
                    prohibited,
                    update
                )

    def test_four_comparison_results(self):
        report = self.run_matcher("14.00")
        games = self.status_map(report)

        self.assertEqual(
            games["CUSA02343"]["status"],
            "REVIEW_REQUIRED"
        )

        self.assertEqual(
            games["CUSA02343"]["candidate_version"],
            "01.02"
        )

        self.assertEqual(
            games["CUSA00411"]["status"],
            "FIRMWARE_TOO_LOW"
        )

        self.assertEqual(
            games["CUSA08032"]["status"],
            "NOT_INSTALLED"
        )

        self.assertEqual(
            games["CUSA06122"]["status"],
            "NO_UPDATE_LISTED"
        )

    def test_higher_firmware_changes_result(self):
        report = self.run_matcher("15.00")
        games = self.status_map(report)

        self.assertEqual(
            games["CUSA00411"]["status"],
            "REVIEW_REQUIRED"
        )

    def test_unconfirmed_installation_is_blocked(self):
        for game in self.installed["games"]:
            if game["cusa"] == "CUSA02343":
                game["installed"] = None

        report = self.run_matcher()
        games = self.status_map(report)

        self.assertEqual(
            games["CUSA02343"]["status"],
            "INSTALL_UNCONFIRMED"
        )

    def test_wrong_content_id_is_blocked(self):
        self.inventory["games"][0][
            "content_id"
        ] = "WRONG-CONTENT-ID"

        report = self.run_matcher()
        games = self.status_map(report)

        self.assertEqual(
            games["CUSA02343"]["status"],
            "CONTENT_ID_MISMATCH"
        )

    def test_report_never_claims_safe_installation(self):
        report = self.run_matcher()

        self.assertFalse(
            report["auto_install"]
        )

        self.assertFalse(
            report["compatibility_confirmed"]
        )

        self.assertEqual(
            report["mode"],
            "offline_comparison"
        )


if __name__ == "__main__":
    unittest.main()
