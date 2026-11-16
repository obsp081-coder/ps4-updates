"""No-network regression tests for reviewed publisher GitHub release metadata.

A release tag/.pkg file name cannot prove that the asset is a PS4 update
patch (rather than a full app), nor make any title available for download.
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

from fpkg_github_release_observer import (
    MAX_RESPONSE, MetadataRejected, _observed_tags,
    observe_reviewed_publishers,
)

REPO = "fictional-publisher/fictional-homebrew"
EXPECTED_URL = ("https://api.github.com/repos/"
                + REPO + "/releases?per_page=20")


def policy():
    return {"schema": 1, "sources": [{
        "cusa": "CUSA02343",
        "repository": REPO,
        "evidence_url": "https://github.com/" + REPO + "/blob/main/LICENSE",
        "review_note": "Test review, no real rights granted",
        "review_ticket": "REVIEW-0001",
    }]}


def response():
    return json.dumps([{
        "tag_name": "v1.02", "draft": False, "prerelease": False,
        "assets": [
            {"name": "homebrew-v1.02.pkg", "size": 2048,
             "browser_download_url": "https://untrusted.example/bad.pkg"},
            {"name": "homebrew-v1.02.pkg.sha256", "size": 88}
        ],
        "body": "<script>malicious and irrelevant release notes</script>",
    }]).encode("utf-8")


class FpkgGithubReleaseObserverTests(unittest.TestCase):
    def test_no_registered_publishers_triggers_zero_network_calls(self):
        def forbidden(url):
            self.fail("No approved publisher must never perform network")
        report = observe_reviewed_publishers(
            {"schema": 1, "sources": []}, fetch=forbidden
        )
        self.assertEqual(report["repositories"], [])
        self.assertFalse(report["network_requested"])
        self.assertFalse(report["download_allowed"])

    def test_exact_registered_repository_and_bounded_metadata(self):
        called = []
        def mock(url):
            called.append(url)
            return response()
        report = observe_reviewed_publishers(policy(), fetch=mock)
        self.assertEqual(called, [EXPECTED_URL])
        self.assertFalse(report["network_requested"])
        self.assertEqual(report["repositories"][0]["status"],
                         "RELEASE_TAGS_OBSERVED_UNVERIFIED")
        tag = report["repositories"][0]["observed_releases"][0]
        self.assertEqual(tag["tag"], "v1.02")
        self.assertEqual(tag["package_named_assets"], 1)
        for key in ("fpkg_verified", "patch_compatible",
                    "package_kind_verified", "rights_verified",
                    "download_allowed", "install_allowed"):
            self.assertIs(tag[key], False, key)
        dumped = json.dumps(report)
        self.assertNotIn("browser_download_url", dumped)
        self.assertNotIn("untrusted.example", dumped)
        self.assertNotIn("<script>", dumped)
        self.assertNotIn("homebrew-v1.02.pkg", dumped)
        self.assertFalse(report["download_allowed"])
        self.assertFalse(report["compatibility_confirmed"])

    def test_pkg_name_is_not_patch_proof(self):
        listed = _observed_tags(response())[0]
        self.assertEqual(listed["package_named_assets"], 1)
        self.assertFalse(listed["patch_compatible"])
        self.assertFalse(listed["fpkg_verified"])

    def test_no_pkg_asset_does_not_claim_update(self):
        document = [{
            "tag_name": "1.09", "draft": False, "prerelease": True,
            "assets": []
        }]
        tags = _observed_tags(json.dumps(document).encode())
        self.assertEqual(tags[0]["package_named_assets"], 0)
        self.assertTrue(tags[0]["prerelease"])
        self.assertFalse(tags[0]["download_allowed"])

    def test_draft_release_not_exposed(self):
        data = json.loads(response())
        data[0]["draft"] = True
        tags = _observed_tags(json.dumps(data).encode())
        self.assertEqual(tags, [])

    def test_untrusted_json_schema_bails_out(self):
        bad = (
            b'{"not": "list"}',
            b'{"error":"rate limit"}',
            b'[{"tag_name":"v1.02","draft":false,"prerelease":false}]',
            b'[{"tag_name":"v1.02","draft":false,"prerelease":"no","assets":[]}]',
            b'[{"tag_name":"v1.02","tag_name":"v1.03",'
            b'"draft":false,"prerelease":false,"assets":[]}]',
            b'[{"tag_name":"v1.02","draft":false,"prerelease":false,'
            b'"assets":[{"name":"x.pkg","size":true}]}]',
            b'[{"tag_name":"../v1.02","draft":false,"prerelease":false,'
            b'"assets":[]}]',
            b'not json',
            b'\xff',
            b'[]' * (MAX_RESPONSE // 2 + 1),
        )
        for payload in bad:
            with self.subTest(data=payload[:50]):
                with self.assertRaises(MetadataRejected):
                    _observed_tags(payload)

    def test_duplicate_tags_rejected(self):
        rows = json.loads(response())
        with self.assertRaises(MetadataRejected):
            _observed_tags(json.dumps(rows + rows).encode())

    def test_asset_limit_and_bad_names_rejected(self):
        document = json.loads(response())
        document[0]["assets"] = [{"name":"file.pkg","size":1}] * 65
        with self.assertRaises(MetadataRejected):
            _observed_tags(json.dumps(document).encode())
        for name in ("evil\n.pkg", "", "x"*256):
            with self.subTest(name=name[:20]):
                doc = json.loads(response())
                doc[0]["assets"][0]["name"] = name
                with self.assertRaises(MetadataRejected):
                    _observed_tags(json.dumps(doc).encode())

    def test_source_errors_cannot_enable_fallback_urls(self):
        def no_network(url):
            raise TimeoutError("local timeout fixture")
        report = observe_reviewed_publishers(policy(), fetch=no_network)
        entry = report["repositories"][0]
        self.assertEqual(entry["status"], "SOURCE_UNAVAILABLE")
        self.assertEqual(entry["error_type"], "TimeoutError")
        self.assertEqual(entry["observed_releases"], [])
        self.assertFalse(report["install_allowed"])
        self.assertFalse(report["download_allowed"])

    def test_corrupt_response_is_separate_from_no_releases(self):
        empty = observe_reviewed_publishers(policy(), fetch=lambda url: b"[]")
        self.assertEqual(empty["repositories"][0]["status"],
                         "NO_RELEASES_OBSERVED")
        bad = observe_reviewed_publishers(policy(), fetch=lambda url: b"{}")
        self.assertEqual(bad["repositories"][0]["status"],
                         "SOURCE_UNAVAILABLE")
        self.assertEqual(bad["repositories"][0]["error_type"],
                         "MetadataRejected")

    def test_policy_missing_rights_entry_rejected(self):
        invalid = policy()
        invalid["sources"][0]["evidence_url"] = (
            "https://github.com/attacker/other/blob/main/LICENSE"
        )
        with self.assertRaises(ValueError):
            observe_reviewed_publishers(invalid, fetch=lambda url: response())

    def test_cli_requires_explicit_network_opt_in(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            source = directory/"registry.json"
            result = directory/"result.json"
            source.write_text(json.dumps({"schema":1,"sources":[]}),
                              encoding="utf-8")
            proc = subprocess.run([
                sys.executable, str(ROOT/"fpkg_github_release_observer.py"),
                "--publisher-registry", str(source), "--out", str(result)
            ], capture_output=True, text=True, timeout=15)
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("--live is required", proc.stderr)
            self.assertFalse(result.exists())

    def test_cli_empty_registry_live_makes_no_network_requests(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            source = directory/"registry.json"
            result = directory/"result.json"
            source.write_text(json.dumps({"schema":1,"sources":[]}),
                              encoding="utf-8")
            proc = subprocess.run([
                sys.executable, str(ROOT/"fpkg_github_release_observer.py"),
                "--publisher-registry", str(source), "--live",
                "--out", str(result)
            ], capture_output=True, text=True, timeout=15)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            report = json.loads(result.read_text(encoding="utf-8"))
            self.assertEqual(report["repositories"], [])
            self.assertFalse(report["network_requested"])
            self.assertFalse(report["download_allowed"])
            self.assertFalse(report["install_allowed"])


if __name__ == "__main__":
    unittest.main()
