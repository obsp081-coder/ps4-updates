
"""Offline tests for Sony retail patch metadata."""

import pathlib
import sys
import unittest
from urllib.error import HTTPError

sys.path.insert(
    0,
    str(pathlib.Path(__file__).resolve().parents[1])
)

from official_patch_lookup import (
    discover,
    inspect_title,
    parse_patch_xml,
    patch_url
)


EXAMPLE = b"""<titlepatch><tag>
<package
  version="01.02"
  content_id="EP9000-CUSA02343_00-UNCHARTEDTRILOGY">
  <paramsfo><title>Test game</title></paramsfo>
</package>
<package
  version="01.09"
  content_id="EP9000-CUSA02343_00-UNCHARTEDTRILOGY"/>
</tag></titlepatch>"""


class OfficialPatchLookupTests(unittest.TestCase):

    def test_known_url_hmac(self):
        self.assertIn(
            "/CUSA00001/"
            "1123f23c1f00810a5e43fcb409ada7823bc5ad21b357817e314b6c4832cf6f9f/",
            patch_url("CUSA00001")
        )

    def test_parses_latest_version(self):
        result = parse_patch_xml(
            EXAMPLE,
            "CUSA02343"
        )

        self.assertEqual(
            result["latest_retail_version"],
            "01.09"
        )

    def test_rejects_wrong_cusa(self):
        with self.assertRaises(ValueError):
            parse_patch_xml(
                EXAMPLE,
                "CUSA99999"
            )

    def test_rejects_malformed_xml(self):
        with self.assertRaises(ValueError):
            parse_patch_xml(
                b"<titlepatch>",
                "CUSA02343"
            )

    def test_offline_lookup(self):
        result = inspect_title(
            "CUSA02343",
            lambda url: EXAMPLE
        )

        self.assertEqual(
            result["status"],
            "RETAIL_PATCH_LISTED"
        )

        self.assertNotIn(
            "download_url",
            result
        )

    def test_404_is_not_listed(self):
        def fake_fetch(url):
            raise HTTPError(
                url, 404, "Not Found", {}, None
            )

        result = inspect_title(
            "CUSA02343",
            fake_fetch
        )

        self.assertEqual(
            result["status"],
            "NOT_LISTED"
        )

    def test_errors_are_not_no_update(self):
        def fake_fetch(url):
            raise TimeoutError()

        result = inspect_title(
            "CUSA02343",
            fake_fetch
        )

        self.assertEqual(
            result["status"],
            "LOOKUP_FAILED"
        )

    def test_read_only_and_deduplication(self):
        report = discover(
            ["CUSA02343", "CUSA02343"],
            lambda url: EXAMPLE
        )

        self.assertEqual(
            len(report["titles"]),
            1
        )

        self.assertFalse(
            report["install_enabled"]
        )

        self.assertFalse(
            report["downloads_enabled"]
        )

        self.assertFalse(
            report["fpkg_compatibility_confirmed"]
        )

    def test_invalid_ids_rejected(self):
        def no_network(url):
            raise AssertionError(
                "Unexpected network request"
            )

        with self.assertRaises(ValueError):
            discover(
                ["CUSA02343", "INVALID"],
                no_network
            )

    def test_bad_content_id_rejected(self):
        invalid = EXAMPLE.replace(
            b"EP9000-CUSA02343",
            b"EP9000-CUSA11111"
        )

        with self.assertRaises(ValueError):
            parse_patch_xml(
                invalid,
                "CUSA02343"
            )


if __name__ == "__main__":
    unittest.main()
