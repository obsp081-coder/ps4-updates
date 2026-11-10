
"""Candidate catalog tests. All entries are synthetic."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT))

from catalog_ingest import ingest
from update_matcher import compare_updates


class CatalogIngestTests(unittest.TestCase):

    def setUp(self):
        self.entry = {
            "cusa": "CUSA02343",
            "content_id": "EP9000-CUSA02343_00-0123456789ABCDEF",
            "version": "01.02",
            "min_fw": "11.00",
            "required_base_version": "01.00",
            "package_kind": "fpkg_patch",
            "source_page": "https://example.org/metadata/example",
        }

    def validate(self):
        return ingest({
            "schema": 1,
            "submissions": [self.entry],
        })

    def test_normalized_output_is_unverified(self):
        result = self.validate()

        self.assertEqual(len(result["updates"]), 1)
        self.assertEqual(
            result["status"],
            "UNVERIFIED_METADATA_ONLY"
        )

        self.assertFalse(result["auto_install"])
        self.assertFalse(result["compatibility_confirmed"])

        self.assertFalse(
            result["updates"][0][
                "patch_compatibility_confirmed"
            ]
        )

        self.assertNotIn(
            "download_url",
            result["updates"][0]
        )

    def test_demo_file_rejected(self):
        with self.assertRaises(ValueError):
            ingest({
                "schema": 1,
                "fixture_only": True,
                "submissions": [self.entry],
            })

    def test_demo_entry_rejected(self):
        self.entry["demo_only"] = True

        with self.assertRaises(ValueError):
            self.validate()

    def test_content_id_must_match_cusa(self):
        self.entry["content_id"] = (
            "EP9000-CUSA99999_00-0123456789ABCDEF"
        )

        with self.assertRaises(ValueError):
            self.validate()

    def test_http_and_localhost_rejected(self):
        for url in (
            "http://example.org/a",
            "https://localhost/file",
            "https://127.0.0.1/a",
            "https://192.168.1.5/a",
        ):
            with self.subTest(url=url):
                with self.assertRaises(ValueError):
                    self.entry["source_page"] = url
                    self.validate()

    def test_bad_min_firmware_rejected(self):
        self.entry["min_fw"] = "firmware14"

        with self.assertRaises(ValueError):
            self.validate()

    def test_direct_download_and_install_rejected(self):
        for key in (
            "download_url",
            "pkg_url",
            "install_command",
            "auto_install",
        ):
            with self.subTest(key=key):
                self.entry[key] = "ignored"

                with self.assertRaises(ValueError):
                    self.validate()

                self.entry.pop(key)

    def test_untrusted_source_cannot_claim_rights_or_actions(self):
        for field, value in (
            ("rights_verified", True),
            ("source_authorized", True),
            ("download_allowed", False),
            ("install_allowed", True),
            ("download_enabled", True),
            ("install_enabled", True),
            ("compatibility_confirmed", True),
            ("fpkg_compatibility_confirmed", False),
            ("package_file", "fake.pkg"),
            ("local_pkg_path", "/data/fake.pkg"),
        ):
            with self.subTest(field=field):
                self.entry[field] = value
                with self.assertRaises(ValueError):
                    self.validate()
                del self.entry[field]

    def test_schema_boolean_and_malformed_fixture_flags_rejected(self):
        for schema in (True, False, "1", None):
            with self.subTest(schema=schema):
                with self.assertRaises(ValueError):
                    ingest({"schema": schema, "submissions": [self.entry]})
        for flag in (True, "true", 1, None):
            with self.subTest(flag=flag):
                with self.assertRaises(ValueError):
                    ingest({"schema": 1, "fixture_only": flag,
                            "submissions": [self.entry]})
        for flag in (True, "true", 1, None):
            with self.subTest(flag=flag):
                self.entry["demo_only"] = flag
                with self.assertRaises(ValueError):
                    self.validate()
                del self.entry["demo_only"]

    def test_cli_duplicate_json_keys_cannot_override_submissions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "raw.json"
            output = root / "out.json"
            source.write_text(
                '{"schema":1,"schema":1,"submissions":[]}', encoding="utf-8"
            )
            proc = subprocess.run([
                sys.executable, str(PROJECT / "catalog_ingest.py"),
                str(source), "--out", str(output)
            ], capture_output=True, text=True, timeout=15)
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("Duplicate catalog JSON key", proc.stderr)
            self.assertFalse(output.exists())

    def test_duplicate_update_rejected(self):
        with self.assertRaises(ValueError):
            ingest({
                "schema": 1,
                "submissions": [
                    self.entry,
                    self.entry.copy(),
                ],
            })

    def test_hash_is_only_declared_not_verified(self):
        self.entry["package_sha256"] = "A" * 64

        result = self.validate()

        self.assertEqual(
            result["updates"][0][
                "package_sha256_unverified"
            ],
            "a" * 64
        )

        self.entry["package_sha256"] = "bad"

        with self.assertRaises(ValueError):
            self.validate()

    def test_cli_end_to_end(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)

            source = folder / "submissions.json"
            output = folder / "catalog.json"

            source.write_text(
                json.dumps({
                    "schema": 1,
                    "submissions": [self.entry],
                }),
                encoding="utf-8"
            )

            completed = subprocess.run(
                [
                    sys.executable,
                    str(PROJECT / "catalog_ingest.py"),
                    str(source),
                    "--out",
                    str(output),
                ],
                capture_output=True,
                text=True,
                timeout=10
            )

            self.assertEqual(
                completed.returncode,
                0,
                completed.stderr
            )

            catalog = json.loads(
                output.read_text(
                    encoding="utf-8"
                )
            )

            self.assertEqual(
                catalog["updates"][0]["cusa"],
                "CUSA02343"
            )

    def test_candidate_catalog_flows_into_matcher(self):
        catalog = self.validate()

        inventory = {
            "games": [{
                "cusa": self.entry["cusa"],
                "name": "Synthetic game",
                "app_ver": "01.00",
                "content_id": self.entry["content_id"],
            }]
        }

        installed = {
            "games": [{
                "cusa": self.entry["cusa"],
                "installed": True,
            }]
        }

        report = compare_updates(
            inventory,
            installed,
            catalog,
            "14.00"
        )

        self.assertEqual(
            report["results"][0]["status"],
            "REVIEW_REQUIRED"
        )

        self.assertFalse(
            report["auto_install"]
        )

        self.assertFalse(
            report["compatibility_confirmed"]
        )

    def test_empty_input_is_safe(self):
        result = ingest({
            "schema": 1,
            "submissions": [],
        })

        self.assertEqual(
            result["updates"],
            []
        )

        self.assertFalse(
            result["auto_install"]
        )


if __name__ == "__main__":
    unittest.main()
