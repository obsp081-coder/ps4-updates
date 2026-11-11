"""The FW14 header diagnostic cannot replace or pollute the normal app.

These are source/build-contract checks, NOT a PS4 hardware test.
OpenOrbis CI separately builds both variants without distributing either.
"""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "ps4" / "Makefile"
SCANNER = ROOT / "ps4" / "src" / "ftp_scanner_v05.c"
WORKFLOW = ROOT.parent / ".github" / "workflows" / "ps4-build.yml"


def build_header():
    return BUILD.read_text(encoding="utf-8").split("TOOLCHAIN", 1)[0]


class Fw14DiagnosticBuildContractTests(unittest.TestCase):
    def test_installed_ids_are_distinct_and_both_content_ids_are_well_formed(self):
        header = build_header()
        ids = re.findall(r"^\s*TITLE_ID\s*:=\s*(\w+)\s*$",
                         header, re.MULTILINE)
        content_ids = re.findall(r"^\s*CONTENT_ID\s*:=\s*(\S+)\s*$",
                                 header, re.MULTILINE)
        self.assertEqual(ids, ["BREW00197", "BREW01998"])
        self.assertEqual(content_ids, [
            "IV0000-BREW00197_00-PS4SCANNERTEST00",
            "IV0000-BREW01998_00-FW14HEADERPROBE0",
        ])
        self.assertEqual(len(content_ids[0]), 36)
        self.assertEqual(len(content_ids[1]), 36)
        for title_id, cid in zip(ids, content_ids):
            self.assertEqual(cid[7:16], title_id)

    def test_diagnostic_id_only_selected_by_explicit_switch(self):
        header = build_header()
        self.assertIn("FW14_DIAGNOSTIC ?= 0", header)
        self.assertIn("ifeq ($(FW14_DIAGNOSTIC),1)", header)
        self.assertNotIn("FW14_DIAGNOSTIC ?= 1", header)
        build = BUILD.read_text(encoding="utf-8")
        self.assertIn("-DPS4_ENABLE_EXPERIMENTAL_PKG_HEADER_PROBE", build)
        self.assertIn("-DPS4_DIAGNOSTIC_ISOLATED", build)

    def test_scanner_caches_are_isolated_from_regular_app(self):
        source = SCANNER.read_text(encoding="utf-8")
        self.assertIn("#if defined(PS4_DIAGNOSTIC_ISOLATED)", source)
        self.assertIn('#define ROOT "/data/PS4AutoUpdaterProbe"', source)
        self.assertIn('#define ROOT "/data/PS4AutoUpdater"', source)
        self.assertIn('#error "An isolated diagnostic must enable', source)
        self.assertIn('ROOT "/inventory_v065.json"', source)
        self.assertIn('ROOT "/snapshot_v1.bin"', source)
        self.assertIn('ROOT "/diagnostic_v065.txt"', source)
        self.assertIn("pkg_header_observation", source)
        self.assertIn("fpkg_detection_confirmed", source)

    def test_diagnostic_delivery_cannot_be_automatic_on_push(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("fw14_header_probe_pkg:", workflow)
        self.assertIn("default: false", workflow)
        self.assertIn("FW14_DIAGNOSTIC=1", workflow)
        self.assertIn("make -C \"$APP\" clean", workflow)
        self.assertIn("IV0000-BREW01998_00-FW14HEADERPROBE0", workflow)
        self.assertIn("Upload opt-in FW14 diagnostic (NOT a release)", workflow)
        self.assertIn("github.event_name == 'workflow_dispatch'", workflow)
        self.assertIn("inputs.fw14_header_probe_pkg == true", workflow)
        self.assertIn("github.ref == 'refs/heads/fix/scanner-cid-and-inventory-capacity'", workflow)
        self.assertIn("retention-days: 7", workflow)

    def test_probe_includes_owned_homebrew_id_without_a_commercial_game(self):
        source = SCANNER.read_text(encoding="utf-8")
        self.assertIn('#if defined(PS4_DIAGNOSTIC_ISOLATED)', source)
        self.assertIn('probe_header_target("BREW01998"', source)
        self.assertIn('PKG_SELF_HEADER BREW01998 observation=', source)
        self.assertIn('probe_header_target(g->cusa', source)
        self.assertNotIn('open(path,O_WRONLY', source)
        self.assertNotIn('open(path,O_RDWR', source)
        self.assertIn('"package_kind"', source.replace('\\\"', '"'))

    def test_probe_access_is_read_only_nonblocking_and_no_follow(self):
        source = SCANNER.read_text(encoding="utf-8")
        self.assertIn("O_RDONLY|O_NOFOLLOW|O_NONBLOCK", source)
        self.assertIn("S_ISREG(st.st_mode)", source)
        self.assertIn("pkg_header_observe(raw,sizeof raw)", source)
        self.assertIn("PS4_ENABLE_EXPERIMENTAL_PKG_HEADER_PROBE", source)
        self.assertIn('fputs(",\\"package_kind\\":\\"unknown\\"", fp);', source)
        self.assertIn('fputs(",\\"fpkg_confirmed\\":false", fp);', source)


if __name__ == "__main__":
    unittest.main()
