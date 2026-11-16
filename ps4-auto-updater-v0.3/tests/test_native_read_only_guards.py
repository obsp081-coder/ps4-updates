"""Fail-closed source/build guardrails for experimental native PS4 preview.

Static checks only. A passing test does not certify PS4 firmware 14.00
compatibility or verify the behavior of runtime libraries on physical PS4.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1] / "ps4"
MAKEFILE = ROOT / "Makefile"
SCANNER = ROOT / "src" / "ftp_scanner_v05.c"
UI = ROOT / "src" / "scanner_ui.cpp"


class PS4ReadOnlyBuildGuardTests(unittest.TestCase):
    def test_pkg_does_not_enable_unverified_app_installer(self):
        build = MAKEFILE.read_text(encoding="utf-8")
        cflags = re.search(r"^CFLAGS\s*:=([^\n]*(?:\\\n[^\n]*)*)", build,
                           re.MULTILINE)
        self.assertIsNotNone(cflags)
        self.assertNotIn("PS4_ENABLE_EXPERIMENTAL_APPINSTUTIL",
                         cflags.group(1))
        self.assertIn("Read Only Preview", build)

    def test_no_firmware_update_or_install_entry_points_in_preview(self):
        sources = SCANNER.read_text(encoding="utf-8")
        ui = UI.read_text(encoding="utf-8")
        for forbidden in (
            "sceAppInstUtilInstall",
            "sceAppInstUtilUninstall",
            "sceAppInstUtilInstallAddcont",
            "sceAppInstUtilInstallPatch",
            "sceSystemUpdate",
            "sceNetHttpCreateRequest",
        ):
            self.assertNotIn(forbidden, sources + ui)
        # The preview explicitly rejects enabling download/install through
        # SFO/JSON claims. A future authorized feature must change this gate
        # with proof and separate hardware tests.
        self.assertIn('install_enabled\\":false', sources)

    def test_ps4_pkg_does_not_request_newer_system_firmware(self):
        build = MAKEFILE.read_text(encoding="utf-8")
        self.assertRegex(build,
                         r"sfo_setentry\s+\$@\s+SYSTEM_VER[^\n]*--value\s+0")


if __name__ == "__main__":
    unittest.main()
