"""Source-level regression checks for PS4-only lazy FTP cover worker.

These assertions inspect the compiled source path. They are NOT socket-level
tests of the __ORBIS__ branch or proof of firmware 14.00 compatibility.
Separate integration tests exercise the host PASV-first scanner with a
mocked FTP server; hardware testing remains required for lazy covers.
"""
from pathlib import Path
import re
import unittest

SOURCE = (Path(__file__).resolve().parents[1] /
          "ps4" / "src" / "ftp_scanner_v05.c")


def function_body(source, signature):
    start = source.find(signature)
    if start < 0:
        raise AssertionError("Missing function " + signature)
    begin = source.find("{", start + len(signature))
    if begin < 0:
        raise AssertionError("Missing function body " + signature)
    depth = 0
    for i in range(begin, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[begin:i + 1]
    raise AssertionError("Unterminated function " + signature)


class CoverFTPSafetyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_lazy_covers_use_pasv_first_no_type_i_probe(self):
        cover = function_body(self.source,
                              "static void *cover_worker_entry(")
        fetch = function_body(self.source,
                              "static int fetch_local_icon(")
        self.assertNotRegex(
            cover, r'command\s*\([^;]*"TYPE I"',
            "TYPE I may desynchronize GoldHEN FTP on the console"
        )
        self.assertIn("fetch_local_icon(", cover)
        self.assertIn("transfer(control,cmd,png", fetch)
        self.assertIn("ui_png_ftp_validate(png,&bytes)", fetch)
        self.assertIn("ui_image_thumbnail(png,bytes", fetch)

    def test_anonymous_auth_attempts_are_per_control_connection(self):
        login = function_body(self.source, "static int login(")
        auth = function_body(self.source, "static int anonymous_auth(")
        self.assertRegex(login, r"ftp_auth_attempted\s*=\s*0\s*;")
        self.assertRegex(auth, r"ftp_auth_attempted\s*=\s*1\s*;")

    def test_cancel_skips_quit_on_desynchronized_control_channel(self):
        scan = function_body(self.source, "static int scan_readonly(")
        self.assertIn("FTP_CANCEL_SKIP_QUIT", scan)
        self.assertRegex(scan, r"if\s*\(!scan_should_abort\(\)\)")
        self.assertIn('"QUIT"', scan)

    def test_lazy_cover_worker_stages_pixels_and_never_installs(self):
        cover = function_body(self.source,
                              "static void *cover_worker_entry(")
        tick = function_body(self.source,
                             "static int cover_ui_tick(")
        for unsafe in ("sceAppInstUtilInstall", "sceAppInstUtilUninstall",
                       "sceAppInstUtilDownload"):
            self.assertNotIn(unsafe, cover + tick)
        self.assertIn("cover_result=fetch_local_icon(", cover)
        self.assertIn("SCANNER_UI_SCAN_DONE", self.source)
        self.assertIn("scanner_ui_offer_thumbnail(", tick)


if __name__ == "__main__":
    unittest.main()
