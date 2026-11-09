"""Stage 3: no-network PS4 retail/FPKG source provenance gates."""
import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from official_patch_lookup import discover
from catalog_ingest import ingest
from update_source_registry import build_source_index

CUSA = "CUSA02343"
CID = "EP9000-CUSA02343_00-UNCHARTEDTRILOGY"
RETAIL_XML = (
    b'<titlepatch><tag><package version="01.02" '
    b'content_id="EP9000-CUSA02343_00-UNCHARTEDTRILOGY"/>'
    b'<package version="01.09" '
    b'content_id="EP9000-CUSA02343_00-UNCHARTEDTRILOGY"/>'
    b'</tag></titlepatch>'
)


class UpdateSourceRegistryTests(unittest.TestCase):
    def retail(self):
        return discover([CUSA], lambda unused: RETAIL_XML)

    def fpkg(self):
        return ingest({"schema": 1, "submissions": [{
            "cusa": CUSA, "content_id": CID,
            "version": "01.10", "min_fw": "11.00",
            "required_base_version": "01.00",
            "package_kind": "fpkg_patch",
            "source_page": "https://example.org/manual/metadata",
        }]})

    def test_no_sources_means_no_trusted_update(self):
        o = build_source_index([CUSA])
        self.assertEqual(o["titles"][0]["source_status"],
                         "NO_TRUSTED_UPDATE_SOURCE")
        self.assertFalse(o["download_allowed"])
        self.assertFalse(o["install_allowed"])

    def test_http_retail_versions_do_not_grant_fpkg(self):
        row = build_source_index([CUSA], self.retail())["titles"][0]
        self.assertEqual([x["version"] for x in row["candidates"]],
                         ["01.09", "01.02"])
        self.assertEqual({x["package_kind"] for x in row["candidates"]},
                         {"retail_patch"})
        self.assertTrue(all(x["size_bytes"] is None for x in row["candidates"]))
        self.assertTrue(all(x["minimum_fw"] is None for x in row["candidates"]))
        self.assertTrue(all(x["history_complete"] is False for x in row["candidates"]))
        self.assertFalse(row["compatibility_confirmed"])
        self.assertFalse(row["install_allowed"])

    def test_untrusted_fpkg_submission_remains_separate(self):
        row = build_source_index([CUSA], self.retail(), self.fpkg())["titles"][0]
        self.assertEqual([x["package_kind"] for x in row["candidates"]],
                         ["fpkg_patch", "retail_patch", "retail_patch"])
        self.assertEqual(row["fpkg_submission_status"], "SUBMISSIONS_RECEIVED")
        self.assertEqual(row["candidates"][0]["source_status"],
                         "FPKG_SUBMISSION_UNVERIFIED")
        self.assertFalse(row["download_allowed"])
        self.assertFalse(row["install_allowed"])
        self.assertNotIn("source_page", json.dumps(row))
        self.assertNotIn("download_url", json.dumps(row))

    def test_http_error_does_not_mean_no_update(self):
        report = self.retail()
        report["titles"][0] = dict(report["titles"][0], status="LOOKUP_FAILED")
        row = build_source_index([CUSA], report)["titles"][0]
        self.assertEqual(row["source_status"], "LOOKUP_FAILED")
        self.assertEqual(row["candidates"], [])

    def test_not_listed_is_not_no_trusted_source_misstatement(self):
        report = self.retail()
        report["titles"][0]["status"] = "NOT_LISTED"
        row = build_source_index([CUSA], report)["titles"][0]
        self.assertEqual(row["retail_lookup_status"], "NOT_LISTED")
        self.assertEqual(row["source_status"], "NO_TRUSTED_UPDATE_SOURCE")

    def test_invalid_or_duplicate_titles_fail_closed(self):
        for titles in ([CUSA, CUSA], ["CUSA00000", "BAD"], [], [CUSA] * 101):
            with self.subTest(titles=titles[:3]):
                with self.assertRaises(ValueError):
                    build_source_index(titles)

    def test_untrusted_action_fields_never_pass(self):
        for key, val in (("download_url", "https://example.org/x.pkg"),
                         ("install_command", "run"),
                         ("auto_install", True)):
            report = self.retail()
            report["titles"][0][key] = val
            with self.subTest(field=key):
                with self.assertRaises(ValueError):
                    build_source_index([CUSA], report)

    def test_forged_retail_source_or_permission_fails(self):
        for key, val in (
            ("metadata_url", "https://example.org/fake"),
            ("transport_authenticated", True),
            ("rights_verified", True),
            ("downloads_enabled", True),
        ):
            report = self.retail()
            report["titles"][0][key] = val
            with self.subTest(key=key):
                with self.assertRaises(ValueError):
                    build_source_index([CUSA], report)

    def test_fake_fpkg_compatibility_and_malformed_cid_fails(self):
        for key, val in (
            ("patch_compatibility_confirmed", True),
            ("content_id", "EP9000-CUSA99999_00-UNCHARTEDTRILOGY"),
            ("download_url", "https://example.org/x.pkg"),
        ):
            f = self.fpkg()
            f["updates"][0][key] = val
            with self.subTest(key=key):
                with self.assertRaises(ValueError):
                    build_source_index([CUSA], fpkg_report=f)

    def test_retail_candidate_duplicate_detected(self):
        report = self.retail()
        report["titles"][0]["observed_retail_versions"].append(
            copy.deepcopy(report["titles"][0]["observed_retail_versions"][0]))
        with self.assertRaises(ValueError):
            build_source_index([CUSA], retail_report=report)

    def test_retail_latest_record_inconsistency_detected(self):
        report = self.retail()
        report["titles"][0]["latest_retail_version"] = "99.99"
        with self.assertRaises(ValueError):
            build_source_index([CUSA], retail_report=report)

    def test_target_not_in_submitted_catalog_rejected(self):
        with self.assertRaises(ValueError):
            build_source_index(["CUSA99999"], fpkg_report=self.fpkg())

    def test_cli_offline_combines_without_any_pkg_link(self):
        with tempfile.TemporaryDirectory() as d:
            folder = Path(d)
            p1, p2, out = [folder / x for x in ("retail.json", "fpkg.json", "index.json")]
            p1.write_text(json.dumps(self.retail()), encoding="utf-8")
            p2.write_text(json.dumps(self.fpkg()), encoding="utf-8")
            done = subprocess.run([
                sys.executable, str(ROOT / "update_source_registry.py"),
                "--cusa", CUSA, "--retail-report", str(p1),
                "--fpkg-report", str(p2), "--out", str(out)],
                capture_output=True, text=True, timeout=15)
            self.assertEqual(done.returncode, 0, done.stderr)
            result = json.loads(out.read_text(encoding="utf-8"))
            self.assertFalse(result["install_allowed"])
            self.assertFalse(result["download_allowed"])
            self.assertEqual(len(result["titles"][0]["candidates"]), 3)


if __name__ == "__main__":
    unittest.main()
