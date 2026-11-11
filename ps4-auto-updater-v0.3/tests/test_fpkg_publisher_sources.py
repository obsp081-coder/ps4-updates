"""Offline-only checks for the publisher reference policy.

All GitHub names and pages here are synthetic and never fetched.
A policy match is NOT a legal grant, PKG hash check or compatible patch.
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
from fpkg_publisher_sources import review_publisher_origins
from update_source_registry import build_source_index

CUSA = "CUSA02343"
CID = "EP9000-CUSA02343_00-UNCHARTEDTRILOGY"
REPO = "fictional-publisher/fictional-homebrew"
RELEASE = "https://github.com/" + REPO + "/releases/tag/v1.02"
EVIDENCE = "https://github.com/" + REPO + "/blob/main/LICENSE"


def submission(source=RELEASE):
    return {
        "cusa": CUSA, "content_id": CID,
        "version": "01.02", "min_fw": "11.00",
        "required_base_version": "01.00",
        "package_kind": "fpkg_patch", "source_page": source,
    }


def report(source=RELEASE):
    return ingest({"schema": 1, "submissions": [submission(source)]})


def policy():
    return {"schema": 1, "sources": [{
        "cusa": CUSA, "repository": REPO,
        "evidence_url": EVIDENCE,
        "review_note": "Fixture: external rights assessment is NOT complete",
        "review_ticket": "REVIEW-0001",
    }]}


class PublisherOriginTests(unittest.TestCase):
    def test_no_policy_is_no_publisher_review(self):
        index = build_source_index([CUSA], report())
        row = index["titles"][0]["candidates"][0]
        self.assertEqual(row["publisher_origin_review"], "NOT_REVIEWED")
        self.assertFalse(index["publisher_registry_supplied"])
        self.assertFalse(index["download_allowed"])

    def test_empty_policy_does_not_claim_any_source(self):
        audit = review_publisher_origins(report(), {"schema": 1, "sources": []})
        result = audit[(CUSA, CID, "01.02")]
        self.assertEqual(result["status"], "UNREGISTERED_SOURCE")
        self.assertIsNone(result["review_ticket"])
        self.assertFalse(result["rights_verified"])
        self.assertFalse(result["patch_compatibility_verified"])
        self.assertFalse(result["download_allowed"])
        self.assertFalse(result["install_allowed"])

    def test_exact_publisher_release_reference_requires_manual_review(self):
        audit = review_publisher_origins(report(), policy())
        row = audit[(CUSA, CID, "01.02")]
        self.assertEqual(row["status"], "PUBLISHER_REFERENCE_REVIEWED_ONLY")
        self.assertEqual(row["review_ticket"], "REVIEW-0001")
        self.assertFalse(row["rights_verified"])
        self.assertFalse(row["patch_compatibility_verified"])
        indexed = build_source_index([CUSA], report(), policy())
        candidate = indexed["titles"][0]["candidates"][0]
        self.assertEqual(candidate["publisher_origin_review"],
                         "PUBLISHER_REFERENCE_REVIEWED_ONLY")
        self.assertEqual(candidate["source_status"],
                         "FPKG_SUBMISSION_UNVERIFIED")
        self.assertEqual(indexed["titles"][0]["source_status"],
                         "UNVERIFIED_FPKG_CANDIDATES")
        self.assertNotIn("github.com", json.dumps(indexed))
        self.assertFalse(indexed["download_allowed"])
        self.assertFalse(indexed["install_allowed"])
        self.assertFalse(indexed["compatibility_confirmed"])

    def test_unregistered_repos_or_lookalike_urls_never_qualify(self):
        for url in (
            "https://github.com/other/fictional-homebrew/releases/tag/v1.02",
            "https://github.com/" + REPO + "/releases",
            RELEASE + "?download=1",
            RELEASE + "/../v1.03",
            "https://github.com/" + REPO + "/releases/download/v1.02/x.pkg",
            "https://github.com.evil.example/" + REPO + "/releases/tag/v1.02",
            "https://example.org/releases/v1.02",
        ):
            with self.subTest(url=url):
                audit = review_publisher_origins(report(url), policy())
                self.assertEqual(audit[(CUSA, CID, "01.02")]["status"],
                                 "UNREGISTERED_SOURCE")

    def test_cross_title_substitution_is_not_a_review(self):
        p = policy()
        p["sources"][0]["cusa"] = "CUSA00001"
        audit = review_publisher_origins(report(), p)
        self.assertEqual(audit[(CUSA, CID, "01.02")]["status"],
                         "UNREGISTERED_SOURCE")

    def test_reject_malformed_policy_and_duplicate_entries(self):
        for invalid in (
            {"schema": True, "sources": []},
            {"schema": 1, "sources": {}, "download_allowed": True},
            {"schema": 1, "sources": [policy()["sources"][0]] * 2},
            {"schema": 1, "sources": [dict(
                policy()["sources"][0], repository="../evil")]},
            {"schema": 1, "sources": [dict(
                policy()["sources"][0], evidence_url=
                "https://github.com/other/repo/blob/main/LICENSE")]},
            {"schema": 1, "sources": [dict(
                policy()["sources"][0], evidence_url=
                "https://github.com/" + REPO + "/blob/main/../SECRET")]},
            {"schema": 1, "sources": [dict(
                policy()["sources"][0], review_ticket="bad ticket")]},
            {"schema": 1, "sources": [dict(
                policy()["sources"][0], review_note="short")]},
        ):
            with self.subTest(invalid=invalid):
                with self.assertRaises(ValueError):
                    review_publisher_origins(report(), invalid)

    def test_reject_untrusted_action_claims_in_catalog_and_row(self):
        for key, value in (
            ("download_url", "https://example.org/patch.pkg"),
            ("source_authorized", True),
            ("install_allowed", True),
            ("rights_verified", False),
            ("compatibility_confirmed", True),
        ):
            with self.subTest(key=key):
                document = report()
                document["updates"][0][key] = value
                with self.assertRaises(ValueError):
                    review_publisher_origins(document, policy())
        document = report()
        document["auto_install"] = True
        with self.assertRaises(ValueError):
            review_publisher_origins(document, policy())

    def test_policy_does_not_bypass_catalog_consistency_checks(self):
        document = report()
        document["updates"][0]["content_id"] = "EP9000-CUSA00001_00-UNCHARTEDTRILOGY"
        with self.assertRaises(ValueError):
            build_source_index([CUSA], document, policy())

    def test_duplicate_untrusted_release_identity_rejected(self):
        doc = report()
        doc["updates"].append(copy.deepcopy(doc["updates"][0]))
        with self.assertRaises(ValueError):
            review_publisher_origins(doc, policy())

    def test_policy_without_candidate_report_fails_closed(self):
        with self.assertRaises(ValueError):
            build_source_index([CUSA], source_policy=policy())

    def test_cli_local_policy_is_read_only_and_does_not_grant_action(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            input_file = p / "fpkg.json"
            policy_file = p / "policy.json"
            output_file = p / "output.json"
            input_file.write_text(json.dumps(report()), encoding="utf-8")
            policy_file.write_text(json.dumps(policy()), encoding="utf-8")
            files_before = (input_file.read_bytes(), policy_file.read_bytes())
            proc = subprocess.run([
                sys.executable, str(ROOT / "update_source_registry.py"),
                "--cusa", CUSA, "--fpkg-report", str(input_file),
                "--source-policy", str(policy_file),
                "--out", str(output_file)
            ], capture_output=True, text=True, timeout=15)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            result = json.loads(output_file.read_text(encoding="utf-8"))
            self.assertTrue(result["publisher_registry_supplied"])
            self.assertFalse(result["download_allowed"])
            self.assertFalse(result["install_allowed"])
            self.assertEqual(files_before,
                             (input_file.read_bytes(), policy_file.read_bytes()))


if __name__ == "__main__":
    unittest.main()
