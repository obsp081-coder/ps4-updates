"""FPKG-only source discovery contract: no Sony metadata, no download rights.

Fixtures are fictional. The submitted kind is a declaration, not proof that
the installed game/PKG really is FPKG or that distribution is authorized.
"""
import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from catalog_ingest import ingest
from update_source_registry import build_source_index, MAX_CANDIDATES_PER_TITLE

CUSA = "CUSA02343"
CID = "EP9000-CUSA02343_00-UNCHARTEDTRILOGY"


class FpkgOnlySourceIndexTests(unittest.TestCase):
    def submission(self, *, version="01.10"):
        return {
            "cusa": CUSA,
            "content_id": CID,
            "version": version,
            "min_fw": "11.00",
            "required_base_version": "01.00",
            "package_kind": "fpkg_patch",
            "source_page": "https://example.org/manual/metadata",
        }

    def fpkg(self, submissions=None):
        if submissions is None:
            submissions = [self.submission()]
        return ingest({"schema": 1, "submissions": submissions})

    def test_no_fpkg_source_is_not_no_update_claim(self):
        index = build_source_index([CUSA])
        self.assertEqual(index["schema"], 2)
        self.assertEqual(index["package_policy"], "fpkg_only")
        self.assertEqual(index["titles"][0]["source_status"],
                         "NO_TRUSTED_UPDATE_SOURCE")
        self.assertEqual(index["titles"][0]["candidates"], [])
        self.assertFalse(index["download_allowed"])
        self.assertFalse(index["install_allowed"])
        self.assertFalse(index["compatibility_confirmed"])

    def test_declared_fpkg_metadata_only_never_grants_action(self):
        index = build_source_index([CUSA], fpkg_report=self.fpkg())
        row = index["titles"][0]
        self.assertEqual(index["phase"],
                         "FPKG_ONLY_SOURCE_DISCOVERY_METADATA_ONLY")
        self.assertEqual(row["source_status"], "UNVERIFIED_FPKG_CANDIDATES")
        self.assertEqual(row["fpkg_submission_status"],
                         "SUBMISSIONS_RECEIVED")
        self.assertEqual(len(row["candidates"]), 1)
        self.assertEqual(row["candidates"][0]["package_kind"], "fpkg_patch")
        self.assertEqual(row["candidates"][0]["source_status"],
                         "FPKG_SUBMISSION_UNVERIFIED")
        self.assertIsNone(row["candidates"][0]["size_bytes"])
        self.assertFalse(row["candidates"][0]["history_complete"])
        self.assertNotIn("retail", json.dumps(index).lower())
        self.assertNotIn("download_url", json.dumps(index))
        self.assertNotIn("source_page", json.dumps(index))
        self.assertFalse(row["download_allowed"])
        self.assertFalse(row["install_allowed"])

    def test_retail_report_is_not_a_parameter_any_more(self):
        with self.assertRaises(TypeError):
            build_source_index([CUSA], retail_report={
                "source_kind": "official_retail_metadata"
            })

    def test_retail_package_kind_is_not_accepted(self):
        for kind in ("retail_patch", "retail", None):
            with self.subTest(kind=kind):
                source = self.submission()
                source["package_kind"] = kind
                with self.assertRaises(ValueError):
                    self.fpkg([source])

    def test_duplicate_or_invalid_title_ids_fail_closed(self):
        for titles in ([CUSA, CUSA], ["CUSA00000", "BAD"], [], [CUSA]*101):
            with self.subTest(titles=titles[:3]):
                with self.assertRaises(ValueError):
                    build_source_index(titles)

    def test_fpkg_tamper_or_action_fields_rejected(self):
        for key, value in (
            ("patch_compatibility_confirmed", True),
            ("content_id", "EP9000-CUSA99999_00-UNCHARTEDTRILOGY"),
            ("download_url", "https://example.org/x.pkg"),
            ("install_command", "run"),
            ("source_page", "http://example.org/metadata"),
        ):
            with self.subTest(key=key):
                report = self.fpkg()
                report["updates"][0][key] = value
                with self.assertRaises(ValueError):
                    build_source_index([CUSA], fpkg_report=report)

    def test_report_action_claims_rejected(self):
        for key, value in (
            ("auto_install", True),
            ("download_allowed", True),
            ("download_url", "https://example.org/x.pkg"),
            ("compatibility_confirmed", True),
        ):
            report = self.fpkg()
            report[key] = value
            with self.subTest(key=key):
                with self.assertRaises(ValueError):
                    build_source_index([CUSA], fpkg_report=report)

    def test_duplicate_fpkg_candidate_rejected(self):
        report = self.fpkg()
        report["updates"].append(copy.deepcopy(report["updates"][0]))
        with self.assertRaises(ValueError):
            build_source_index([CUSA], fpkg_report=report)

    def test_title_not_in_known_inventory_rejected(self):
        with self.assertRaises(ValueError):
            build_source_index(["CUSA99999"], fpkg_report=self.fpkg())

    def test_more_than_24_candidates_discloses_truncation(self):
        report = self.fpkg([
            self.submission(version=f"01.{i:02d}")
            for i in range(MAX_CANDIDATES_PER_TITLE + 1)
        ])
        row = build_source_index([CUSA], fpkg_report=report)["titles"][0]
        self.assertTrue(row["truncated"])
        self.assertEqual(len(row["candidates"]), MAX_CANDIDATES_PER_TITLE)
        self.assertEqual(row["candidates"][0]["version"], "01.24")
        self.assertEqual(row["candidates"][-1]["version"], "01.01")

    def test_cli_fpkg_only_and_legacy_retail_option_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            folder = Path(d)
            src = folder / "submitted.json"
            output = folder / "index.json"
            src.write_text(json.dumps(self.fpkg()), encoding="utf-8")
            command = [sys.executable, str(ROOT / "update_source_registry.py"),
                       "--cusa", CUSA, "--fpkg-report", str(src),
                       "--out", str(output)]
            done = subprocess.run(
                command, capture_output=True, text=True, timeout=15
            )
            self.assertEqual(done.returncode, 0, done.stderr)
            index = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(index["package_policy"], "fpkg_only")
            self.assertEqual(len(index["titles"][0]["candidates"]), 1)
            self.assertFalse(index["install_allowed"])
            self.assertFalse(index["download_allowed"])

            legacy = subprocess.run(
                command + ["--retail-report", str(src)],
                capture_output=True, text=True, timeout=15
            )
            self.assertNotEqual(legacy.returncode, 0)
            self.assertIn("unrecognized arguments", legacy.stderr)


if __name__ == "__main__":
    unittest.main()
