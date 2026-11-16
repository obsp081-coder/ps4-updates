"""Check that preview settings do not imply unavailable PS4 capabilities.

The current app is read-only: a real authorized update source, downloader and
installer have NOT been validated on firmware 14.00. These assertions must be
revisited only after actual runtime functionality and safety evidence exist.
"""
from pathlib import Path
import runpy
import unittest


ROOT = Path(__file__).resolve().parents[1]
ATLAS_GENERATOR = ROOT / "ps4" / "tools" / "generate_ui_arabic.py"


class SettingsTruthTests(unittest.TestCase):
    def test_settings_labels_disclose_missing_provider(self):
        labels = dict(runpy.run_path(str(ATLAS_GENERATOR))["LABELS"])
        self.assertIn("غير مفعّل", labels["INTERNAL_SETTING"])
        self.assertIn("لم يربط", labels["TRUSTED_SETTING"])
        self.assertIn("التحقق", labels["INSTALL_OFF_SETTING"])


if __name__ == "__main__":
    unittest.main()
