"""Native UI host smoke tests with fake VideoOut/Pad calls.

Host tests exercise control flow and resource cleanup, not PS4 hardware.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
UI_SOURCE = ROOT / "ps4" / "src" / "scanner_ui.cpp"
UI_NAV = ROOT / "ps4" / "src" / "ui_navigation.c"
UI_TITLE_LAYOUT = ROOT / "ps4" / "src" / "ui_title_layout.c"
UI_IMAGE = ROOT / "ps4" / "src" / "ui_image.c"
UI_ICON_CACHE = ROOT / "ps4" / "src" / "ui_icon_cache.c"
UI_MODELS = ROOT / "ps4" / "src" / "ui_models.c"
MOCK_DIR = Path(__file__).resolve().parent / "ui_mocks"
MOCK_TEST = MOCK_DIR / "ui_smoke.cpp"
LABEL_SCRIPT = ROOT / "ps4" / "tools" / "generate_ui_arabic.py"


class NativeUiSmokeTests(unittest.TestCase):

    def test_all_fixed_arabic_captions_have_supported_script(self):
        # NotoSansArabic's fixed RTL atlas cannot shape Latin runs
        # correctly on the current build. Arabic-only captions prevent
        # opaque tofu squares on the real console.
        import ast
        import re
        tree = ast.parse(LABEL_SCRIPT.read_text(encoding="utf-8"))
        entries = None
        for node in tree.body:
            if (isinstance(node, ast.Assign)
                    and any(isinstance(target, ast.Name)
                            and target.id == "LABELS"
                            for target in node.targets)):
                entries = ast.literal_eval(node.value)
                break
        self.assertIsNotNone(entries)
        self.assertGreater(len(entries), 30)
        for name, caption in entries:
            with self.subTest(name=name):
                self.assertIsNone(
                    re.search(r"[A-Za-z]", caption),
                    "Latin glyph missing from fixed Arabic atlas"
                )

    def test_video_pad_navigation_and_cleanup(self):
        cxx = shutil.which("g++") or shutil.which("clang++")
        if cxx is None:
            self.skipTest("No C++ compiler available")

        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "ui-smoke"
            generated = Path(tmp) / "ui_arabic_generated.h"
            generation = subprocess.run(
                ["python3", str(LABEL_SCRIPT), "--output", str(generated)],
                capture_output=True, text=True, timeout=40
            )
            self.assertEqual(generation.returncode, 0,
                             generation.stdout + generation.stderr)
            compile_process = subprocess.run(
                [
                    cxx, "-std=c++17", "-O2", "-Wall", "-Wextra",
                    "-Werror", "-DPS4_UI_HOST_TEST", "-I", str(MOCK_DIR),
                    "-I", str(Path(tmp)),
                    str(UI_SOURCE), str(UI_NAV), str(UI_TITLE_LAYOUT),
                    str(UI_IMAGE),
                    str(UI_ICON_CACHE), str(UI_MODELS), str(MOCK_TEST),
                    "-o", str(binary),
                    "-lm",
                ],
                capture_output=True, text=True, timeout=40
            )
            self.assertEqual(compile_process.returncode, 0,
                             compile_process.stdout + compile_process.stderr)
            preview = Path(tmp) / "screenshots"
            preview.mkdir()
            env = dict(os.environ)
            env["PS4_UI_PREVIEW_DIR"] = str(preview)
            run = subprocess.run(
                [str(binary)], capture_output=True, text=True,
                timeout=30, env=env
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("UI_HOST_SMOKE_OK", run.stdout)

            # These are captures of the native C++ framebuffer renderer,
            # NOT proof of how it appears on an actual PS4 television.
            from PIL import Image
            export = os.environ.get("UI_PREVIEW_EXPORT_DIR")
            for page in ("home", "details", "downloads", "search", "search_ar", "settings", "cached", "stale", "empty", "mixed_ltr", "mixed_rtl", "long_ltr", "long_rtl"):
                ppm = preview / (page + ".ppm")
                self.assertTrue(ppm.is_file(), "Missing native UI page: " + page)
                with Image.open(ppm) as pixels:
                    self.assertEqual(pixels.size, (1920, 1080))
                    self.assertEqual(pixels.mode, "RGB")
                    # Main royal-blue header must be nonblank for every page.
                    self.assertNotEqual(pixels.getpixel((200, 45)),
                                        pixels.getpixel((0, 550)))
                    # Visual regression anchors for the approved royal-blue
                    # design. These are true native CPU framebuffer pixels,
                    # not a browser mock or actual PS4 screenshot.
                    if page == "home":
                        # The approved interface uses ONE wide Arabic
                        # sidebar, not four duplicate navigation tabs in
                        # the header. Two small read-only status cards
                        # replace those redundant top-page controls.
                        self.assertEqual(
                            pixels.getpixel((950, 50)), (10, 30, 62),
                            "Removed top navigation tab returned"
                        )
                        for label, bounds in (
                            ("read only", (1333, 46, 1535, 80)),
                            ("fw target", (1595, 46, 1840, 80)),
                        ):
                            ink = pixels.crop(bounds)
                            count = sum(
                                1 for red, green, blue in ink.getdata()
                                if red > 155 and green > 155 and blue > 155
                            )
                            self.assertGreater(
                                count, 18,
                                "Missing honest header badge: " + label
                            )
                        # All five honest navigation destinations must be
                        # readable Arabic labels in the 252px rail. The old
                        # icon-only rail had no text in these areas.
                        self.assertEqual(
                            pixels.getpixel((232, 245)), (21, 88, 157),
                            "Wide active navigation card missing"
                        )
                        self.assertEqual(
                            pixels.getpixel((210, 740)), (8, 29, 57),
                            "Sidebar should remain opaque and text-safe"
                        )
                        for rail_y in (190, 290, 390, 490, 590):
                            rail_text = pixels.crop(
                                (80, rail_y + 15, 231, rail_y + 69))
                            bright = sum(
                                1 for red, green, blue in rail_text.getdata()
                                if red >= 143 and green >= 143
                                and blue >= 143
                            )
                            self.assertGreater(
                                bright, 25,
                                "Missing Arabic navigation caption at y="
                                + str(rail_y)
                            )
                        # Native table labels are independent of the
                        # three mocked game rows; no invented update data
                        # is required to draw Name / Version / State.
                        for label, left, right in (
                            ("game", 375, 570),
                            ("version", 826, 953),
                            ("state", 965, 1155),
                        ):
                            area = pixels.crop((left, 305, right, 339))
                            bright = sum(
                                1 for red, green, blue in area.getdata()
                                if red >= 145 and green >= 145
                                and blue >= 145
                            )
                            self.assertGreater(
                                bright, 15,
                                "Missing table column heading: " + label
                            )
                        self.assertEqual(
                            pixels.getpixel((1250, 300)), (17, 44, 80),
                            "Selected cover hero frame changed"
                        )
                        self.assertEqual(
                            pixels.getpixel((1500, 884)), (21, 88, 157),
                            "Read-only details button is missing"
                        )
                    if page == "empty":
                        self.assertEqual(
                            pixels.getpixel((650, 620)), (16, 45, 80),
                            "Empty library panel is missing"
                        )
                        self.assertNotEqual(
                            pixels.getpixel((1500, 884)), (21, 88, 157),
                            "An empty library must not show a Details CTA"
                        )
                        empty_label = pixels.crop((529, 411, 900, 453))
                        amber = sum(
                            1 for r, g, b in empty_label.getdata()
                            if r > 200 and 130 < g < 230 and b < 200
                        )
                        self.assertGreater(
                            amber, 18,
                            "Zero-game state has no visible explanation"
                        )
                        # Empty / incomplete results are not evidence of a
                        # failed snapshot write; the old orange false error
                        # used to appear along the bottom of this screen.
                        false_error_area = pixels.crop((455, 952, 780, 994))
                        false_error_amber = sum(
                            1 for pixel in false_error_area.getdata()
                            if pixel == (255, 195, 125)
                        )
                        self.assertEqual(
                            false_error_amber, 0,
                            "Incomplete empty scan falsely reports save failure"
                        )
                        help_area = pixels.crop((528, 468, 900, 513))
                        help_ink = sum(
                            1 for r, g, b in help_area.getdata()
                            if 140 < r < 215 and 150 < g < 230
                            and 160 < b < 245
                        )
                        self.assertGreater(
                            help_ink, 12,
                            "Missing GoldHEN FTP troubleshooting guidance"
                        )
                    if page in ("cached", "stale"):
                        # Cache warnings have one bounded line above the
                        # footer; two-line warnings previously disappeared
                        # under the y=1000 native controller hints.
                        text_overflow = pixels.crop((301, 987, 875, 997))
                        bottom_ink = sum(
                            1 for pixel in text_overflow.getdata()
                            if pixel in ((255, 195, 125), (159, 184, 215))
                        )
                        self.assertEqual(
                            bottom_ink, 0,
                            "Cache warning text collides with footer"
                        )
                        # A stale/partial FTP scan may not cover the user
                        # path to read-only game Details, even with a six-
                        # row library. Banner lives in the lower list gutter.
                        self.assertEqual(
                            pixels.getpixel((800, 954)), (16, 45, 80),
                            "Cache banner must live BELOW the six rows"
                        )
                        self.assertEqual(
                            pixels.getpixel((1500, 884)), (21, 88, 157),
                            "Cached game Details button was obscured"
                        )
                    if page == "details":
                        self.assertEqual(
                            pixels.getpixel((400, 430)), (16, 45, 80),
                            "Metadata card grid is missing"
                        )
                        self.assertEqual(
                            pixels.getpixel((1450, 360)), (18, 49, 88),
                            "Dedicated right artwork panel is missing"
                        )
                    if page == "downloads":
                        self.assertEqual(
                            pixels.getpixel((320, 370)), (16, 45, 80),
                            "Missing actual no-jobs explanation panel"
                        )
                        self.assertEqual(
                            pixels.getpixel((1150, 700)), (16, 45, 80),
                            "Installation-disabled safety gate is missing"
                        )
                        # The page heading must stay above the three
                        # summary tiles, not be overpainted by their fill.
                        heading = pixels.crop((280, 135, 650, 178))
                        heading_ink = sum(
                            1 for r, g, b in heading.getdata()
                            if r > 145 and g > 145 and b > 145
                        )
                        self.assertGreater(heading_ink, 18)
                    if page == "search_ar":
                        # Dynamic Arabic glyphs must be visible inside key
                        # rectangles rather than tofu squares or blank keys.
                        for col in range(10):
                            x = 290 + col * 152
                            patch = pixels.crop((x + 10, 422, x + 130, 502))
                            bright = sum(1 for r, g, b in patch.getdata()
                                         if r >= 175 and g >= 175 and b >= 175)
                            self.assertGreater(
                                bright, 8,
                                "Arabic keyboard key has no visible glyph"
                            )
                    if page == "search":
                        # Search must show an actual matching title below
                        # the keyboard, not just its match count. This
                        # rectangle contains no keyboard key or footer text.
                        result_patch = pixels.crop((360, 920, 785, 955))
                        result_ink = sum(
                            1 for r, g, b in result_patch.getdata()
                            if r >= 175 and g >= 175 and b >= 175
                        )
                        self.assertGreater(
                            result_ink, 18,
                            "Search results preview has no visible game title"
                        )
                        # Regression: the 'm' in 'Verified Game' used to
                        # vanish because max_chars=1 incorrectly capped the
                        # Noto glyph to a 24px legacy cell. Check its actual
                        # ink, not merely that some neighbouring title text
                        # survived.
                        m_glyph = pixels.crop((493, 933, 516, 960))
                        m_ink = sum(
                            1 for r, g, b in m_glyph.getdata()
                            if r >= 175 and g >= 175 and b >= 175
                        )
                        self.assertGreater(
                            m_ink, 8,
                            "Wide 'm' glyph missing from search title"
                        )
                        # All A-J keys on the 10-column keyboard must have
                        # visible white glyphs; proportional fonts can be
                        # clipped if the old 5x7 fixed-cell width is reused.
                        for col in range(10):
                            x = 290 + col * 152
                            patch = pixels.crop((x + 45, 434, x + 116, 501))
                            bright = sum(1 for r, g, b in patch.getdata()
                                         if r >= 175 and g >= 175 and b >= 175)
                            self.assertGreater(
                                bright, 10,
                                "Keyboard Latin glyph invisible: " + chr(65+col)
                            )
                    if export:
                        destination = Path(export)
                        destination.mkdir(parents=True, exist_ok=True)
                        pixels.save(destination / (page + ".png"),
                                    optimize=True)

            # A scan that is still running and a failed scan must NOT
            # show the same state text ("جاري التحقق" after failure).
            from PIL import ImageChops
            with Image.open(preview / "cached.ppm") as active_cache, \
                 Image.open(preview / "stale.ppm") as failed_cache:
                self.assertIsNotNone(
                    ImageChops.difference(
                        active_cache.crop((300, 942, 880, 985)),
                        failed_cache.crop((300, 942, 880, 985))
                    ).getbbox(),
                    "Active-scan and failed-scan cache warnings are identical"
                )

            # Both directions must visibly truncate rather than erase
            # their entire long title or overwrite the Version column.
            for name in ("long_ltr", "long_rtl"):
                with Image.open(preview / (name + ".ppm")) as frame:
                    for left, right in ((377, 463), (701, 790)):
                        patch = frame.crop((left, 368, right, 399))
                        white = sum(
                            1 for pixel in patch.getdata()
                            if pixel == (234, 244, 255)
                        )
                        self.assertGreater(
                            white, 8,
                            name + " title lost its visible prefix or end"
                        )
                    gutter = frame.crop((792, 368, 824, 399))
                    self.assertFalse(
                        any(pixel == (234, 244, 255)
                            for pixel in gutter.getdata()),
                        name + " bled into the version column"
                    )

            # Both images must contain the rendered game title, not two
            # identical frames captured before any game metadata arrived.
            from PIL import ImageChops
            with Image.open(preview / "mixed_ltr.ppm") as ltr, \
                 Image.open(preview / "mixed_rtl.ppm") as rtl:
                self.assertIsNotNone(
                    ImageChops.difference(ltr, rtl).getbbox(),
                    "Mixed-script previews are identical / blank"
                )


if __name__ == "__main__":
    unittest.main()
