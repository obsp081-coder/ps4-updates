"""Host-only FPKG-only CI scope and release-safety regression checks.

These checks inspect the development branch, NOT the already published
default branch. They do not replace source rights or PS4 hardware tests.
"""
from pathlib import Path
import json
import unittest

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parent
RETAIL = ROOT / ".github" / "workflows" / "retail-patch-report.yml"
BUILD = ROOT / ".github" / "workflows" / "ps4-build.yml"
SOURCES = PROJECT / "source_registry" / "reviewed_publishers.json"
TRANSPORT = PROJECT / "ps4" / "src" / "download_transport.c"
PIPELINE = PROJECT / "ps4" / "src" / "download_pipeline.c"
STORAGE = PROJECT / "ps4" / "src" / "download_file_backend.c"


class FpkgOnlyScopeCiTests(unittest.TestCase):
    def test_legacy_retail_workflow_no_longer_scrapes_or_pushes(self):
        workflow = RETAIL.read_text(encoding="utf-8")
        self.assertIn("workflow_dispatch:", workflow)
        for forbidden in (
            "schedule:", "cron:", "git push", "git commit",
            "official_patch_lookup", "from official_patch_lookup",
            "contents: write", "retail_patch_report.json",
            "public_inventory.json",
        ):
            with self.subTest(field=forbidden):
                self.assertNotIn(forbidden, workflow)

    def test_source_registry_never_preapproves_unreviewed_publishers(self):
        registry = json.loads(SOURCES.read_text(encoding="utf-8"))
        self.assertEqual(registry["schema"], 1)
        self.assertEqual(registry["sources"], [])
        # Replace this with a rights/provenance verification test before
        # adding a real approved publisher to the registry.

    def test_native_transport_storage_and_pipeline_remain_disabled(self):
        transport = TRANSPORT.read_text(encoding="utf-8")
        pipeline = PIPELINE.read_text(encoding="utf-8")
        storage = STORAGE.read_text(encoding="utf-8")
        self.assertIn("#ifdef __ORBIS__", transport)
        self.assertIn("return DT_UNAVAILABLE;", transport)
        self.assertIn("#ifdef __ORBIS__", pipeline)
        self.assertIn("return DLP_UNAVAILABLE;", pipeline)
        self.assertIn("#ifdef __ORBIS__", storage)
        self.assertIn("return -1;", storage)
        self.assertIn("p.create_exclusive=fs_create;", storage)
        # Native connection must never silently fall back to Host POSIX.

    def test_normal_build_not_equal_to_diagnostic_or_public_release(self):
        workflow = BUILD.read_text(encoding="utf-8")
        self.assertIn("fw14_header_probe_pkg:", workflow)
        self.assertIn("default: false", workflow)
        self.assertIn("FW14_DIAGNOSTIC=1", workflow)
        self.assertIn("Run workflow", (
            ROOT / "FW14_HEADER_DIAGNOSTIC_INSTALL_AR.md"
        ).read_text(encoding="utf-8"))
        self.assertIn("github.event_name == 'workflow_dispatch'", workflow)
        self.assertIn("inputs.fw14_header_probe_pkg == true", workflow)
        self.assertIn("vars.ENABLE_PS4_RELEASE == 'true'", workflow)
        self.assertIn("github.ref == 'refs/heads/main'", workflow)


if __name__ == "__main__":
    unittest.main()
