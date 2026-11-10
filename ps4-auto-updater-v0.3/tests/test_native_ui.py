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
UI_SOURCE_BRIDGE = ROOT / "ps4" / "src" / "ui_source_bridge.c"
UI_COMPATIBILITY = ROOT / "ps4" / "src" / "ui_compatibility.c"
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
                    str(UI_ICON_CACHE), str(UI_MODELS), str(UI_SOURCE_BRIDGE),
                    str(MOCK_TEST),
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
            # Instrument the real 1920x1080 native CPU framebuffer code,
            # while VideoOut and Pad are Host mocks. The timing includes
            # mock navigation, so it is not PS4 frame time or a CI speed
            # threshold. The direct-memory allocation is 2MiB-aligned.
            import re
            render = re.search(
                r"HOST_ONLY_NATIVE_1080P_CPU_MS=([0-9]+\.[0-9]+) "
                r"FRAMES=([0-9]+) "
                r"CPU_MS_PER_SUBMISSION=([0-9]+\.[0-9]+) "
                r"FRAMEBUFFER_BYTES=([0-9]+)",
                run.stdout,
            )
            self.assertIsNotNone(
                render, "Missing native Host render resource measurement"
            )
            self.assertGreaterEqual(int(render.group(2)), 8)
            self.assertEqual(int(render.group(4)), 16 * 1024 * 1024)
            print(
                "HOST_ONLY_NATIVE_1080P_CPU_MS=" + render.group(1)
                + " FRAMES=" + render.group(2)
                + " MS_PER_SUBMISSION=" + render.group(3)
                + " FRAMEBUFFER_BYTES=" + render.group(4),
                flush=True,
            )

            # These are captures of the native C++ framebuffer renderer,
            # NOT proof of how it appears on an actual PS4 television.
            from PIL import Image
            export = os.environ.get("UI_PREVIEW_EXPORT_DIR")
            for page in ("home", "wide_art", "games", "available", "downloads", "queue", "installed", "about", "details", "search", "search_ar", "settings", "cached", "stale", "scanning_details", "stale_details", "search_restored_home", "empty", "mixed_ltr", "mixed_rtl", "long_ltr", "long_rtl"):
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
                        # Welcome and four truthful compact status tiles:
                        # no false update or source claims.
                        # The new panel is translucent and vertically
                        # graded; a fixed flat RGB assertion would reject
                        # the intended change or any valid local backdrop.
                        welcome_rgb = pixels.getpixel((275, 155))
                        self.assertGreater(welcome_rgb[2], welcome_rgb[1])
                        self.assertGreater(welcome_rgb[1], welcome_rgb[0]+15)
                        self.assertNotEqual(welcome_rgb, (12, 40, 80))
                        banner = pixels.crop((375, 127, 1220, 198))
                        self.assertGreater(sum(
                            1 for r,g,b in banner.getdata()
                            if r>155 and g>155 and b>155), 25)
                        for x in (269, 548, 827, 1106):
                            blue = pixels.getpixel((x+8, 242))
                            self.assertGreater(
                                blue[2], blue[1],
                                "Home glass status tile missing")
                            self.assertGreater(blue[1],blue[0])
                        # The approved interface uses ONE wide Arabic
                        # sidebar, not four duplicate navigation tabs in
                        # the header. Two small read-only status cards
                        # replace those redundant top-page controls.
                        self.assertEqual(
                            pixels.getpixel((950, 50)), (10, 30, 62),
                            "Removed top navigation tab returned"
                        )
                        # Reference hierarchy uses a large white masthead
                        # and still displays two honest non-clickable badges.
                        masthead = pixels.crop((155, 4, 615, 44))
                        white_ink = sum(
                            1 for red,green,blue in masthead.getdata()
                            if red>205 and green>205 and blue>205
                        )
                        self.assertGreater(
                            white_ink, 60,
                            "The white English masthead is missing")
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
                        # Eight compact 77px Sidebar destinations must be
                        # readable Arabic labels in the 252px rail. The old
                        # icon-only rail had no text in these areas.
                        self.assertEqual(
                            pixels.getpixel((232, 210)), (21, 88, 157),
                            "Wide active navigation card missing"
                        )
                        # The mockup's bright focus is now a bounded
                        # two-tone rim, not a full-width CPU gradient.
                        self.assertEqual(
                            pixels.getpixel((120, 158)), (55, 173, 255),
                            "Active sidebar highlight missing cyan rim"
                        )
                        self.assertEqual(
                            pixels.getpixel((600, 383)), (55, 173, 255),
                            "Selected game row missing cyan rim"
                        )
                        self.assertNotEqual(
                            pixels.getpixel((600, 450)), (55, 173, 255),
                            "Unselected game row gained active highlight"
                        )
                        self.assertEqual(
                            pixels.getpixel((210, 740)), (8, 29, 57),
                            "Sidebar should remain opaque and text-safe"
                        )
                        for rail_y in (155, 232, 309, 386, 463, 540, 617, 694):
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
                        # Approved screenshot uses the right-to-left game
                        # table. Only optional model-backed update metadata
                        # is rendered; empty provider fields stay unknown.
                        for label, left, right in (
                            ("game", 1177, 1281),
                            ("sfo version", 789, 947),
                            ("observed update", 632, 782),
                            ("size", 508, 608),
                            ("state", 296, 427),
                        ):
                            area = pixels.crop((left, 339, right, 375))
                            bright = sum(
                                1 for red, green, blue in area.getdata()
                                if red >= 145 and green >= 145
                                and blue >= 145
                            )
                            self.assertGreater(
                                bright, 15,
                                "Missing table column heading: " + label
                            )
                        # With exactly three model fixture records,
                        # the remaining slots are purely visual, with no
                        # fake CUSA, patch metadata or cover art.
                        self.assertEqual(
                            pixels.getpixel((325, 700)), (8, 27, 53),
                            "Empty fourth game slot lost its muted fill"
                        )
                        self.assertEqual(
                            pixels.getpixel((1292, 667)), (36, 80, 121),
                            "Unfilled icon outline in fourth slot missing"
                        )
                        self.assertEqual(
                            pixels.getpixel((1325, 687)), (8, 27, 53),
                            "Empty icon slot contains false artwork"
                        )
                        # A real local square icon is now at the right
                        # of each row, not at x285. No synthetic game art.
                        # The selected row's status lives in its own
                        # amber/blue capsule. A tint must be visible, while
                        # the adjacent size column remains untouched and
                        # no green compatibility claim appears.
                        capsule = pixels.getpixel((285, 429))
                        self.assertNotEqual(
                            capsule, (22, 72, 129),
                            "The reference-style status badge is missing"
                        )
                        self.assertEqual(
                            pixels.getpixel((500, 430)), (22, 72, 129),
                            "Status badge bled into update-size column"
                        )
                        self.assertNotEqual(
                            pixels.getpixel((1325, 425)),
                            pixels.getpixel((300, 430)),
                            "Right-side local icon is missing"
                        )
                        self.assertEqual(
                            pixels.getpixel((1440, 300)), (17, 44, 80),
                            "Selected cover hero frame changed"
                        )
                        self.assertEqual(
                            pixels.getpixel((1500, 884)), (21, 88, 157),
                            "Read-only details button is missing"
                        )
                    # A real native static bitmap hint now explains
                    # R1/L1 navigation across every available page.
                    shoulder = pixels.crop((1490, 1010, 1870, 1063))
                    cyan = sum(1 for r,g,b in shoulder.getdata()
                               if b>175 and g>90 and r<140)
                    self.assertGreater(cyan, 15)
                    if page == "wide_art":
                        # This is SYNTHETIC Host-only art. It establishes
                        # that an eventual verified local 16:9 picture can
                        # be displayed with correct aspect, not that FW14
                        # exposes or authorizes any particular PNG path.
                        self.assertEqual(
                            pixels.getpixel((1490,245)),(192,120,52))
                        self.assertEqual(
                            pixels.getpixel((1490,427)),(40,91,160))
                    if page == "empty":
                        # The grid-like horizontal stripes visible in the
                        # shadPS4 screenshot are replaced by a smooth
                        # low-contrast Navy gradient (no fake cover art).
                        upper = pixels.getpixel((1383, 540))
                        lower = pixels.getpixel((1383, 650))
                        nearby = pixels.getpixel((1383, 550))
                        self.assertNotEqual(upper, lower)
                        self.assertLess(max(abs(a-b) for a,b in
                                            zip(upper,nearby)), 3)
                    if page in ("home", "details"):
                        # The two long Arabic rail captions must not overlap
                        # the 252px sidebar's right divider.
                        gutter = pixels.crop((233, 357, 249, 403))
                        bright_gutter = sum(
                            1 for red, green, blue in gutter.getdata()
                            if red >= 143 and green >= 143 and blue >= 143
                        )
                        self.assertEqual(
                            bright_gutter, 0,
                            "Arabic Details rail label overlaps right gutter"
                        )
                    if page == "home":
                        # The focused native row has a genuine cyan inner
                        # halo, but its center remains legible/untinted.
                        # y=388 lies in the 8px rim; y=415 is mid-card.
                        glow = pixels.getpixel((600, 388))
                        center = pixels.getpixel((600, 415))
                        self.assertGreater(
                            glow[2], center[2]+8,
                            "The focused Home row lost its soft blue rim"
                        )
                        self.assertGreater(glow[1], center[1])
                    if page == "search_restored_home":
                        # Actual Pad sequence typed an unmatched Arabic
                        # query and exited Search; three real game records
                        # must be visible again on the Home screen.
                        self.assertEqual(
                            pixels.getpixel((600, 405)), (22, 72, 129),
                            "Search left an empty or unselected Home library"
                        )
                        self.assertEqual(
                            pixels.getpixel((1500, 884)), (21, 88, 157),
                            "No Details button after exiting zero-match Search"
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
                        # The "source" caption and "unavailable" value now
                        # occupy separate Arabic baselines in the right card.
                        # Previously they touched at x1613 on the same y.
                        missing_value = pixels.crop((1430, 708, 1720, 744))
                        amber = sum(
                            1 for r,g,b in missing_value.getdata()
                            if r>200 and 125<g<225 and b<195
                        )
                        self.assertGreater(
                            amber, 12,
                            "Right Details source value was clipped or lost"
                        )
                    if page == "downloads":
                        self.assertEqual(
                            pixels.getpixel((320, 410)), (16, 45, 80),
                            "Missing actual no-jobs explanation panel"
                        )
                        self.assertEqual(
                            pixels.getpixel((1150, 700)), (16, 45, 80),
                            "Installation-disabled safety gate is missing"
                        )
                        # The page heading stays above four REAL job counters,
                        # with a right-side selected-job pane and no fake ETA.
                        heading = pixels.crop((280, 135, 650, 178))
                        heading_ink = sum(
                            1 for r, g, b in heading.getdata()
                            if r > 145 and g > 145 and b > 145
                        )
                        self.assertGreater(heading_ink, 18)
                        selected_rail = pixels.getpixel((1410, 250))
                        self.assertGreater(
                            selected_rail[2], selected_rail[1],
                            "Selected-job translucent right rail is missing")
                        for x in (269, 548, 827, 1106):
                            blue = pixels.getpixel((x+8, 256))
                            self.assertGreater(
                                blue[2],blue[1],
                                "Four real gradient job counters missing")
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

            # In Details, a completed failed scan must say stale, never
            # "checking". Inspect the actual read-only warning pixels.
            with Image.open(preview / "scanning_details.ppm") as active, \
                 Image.open(preview / "stale_details.ppm") as stale:
                for frame in (active, stale):
                    self.assertEqual(
                        frame.getpixel((900, 954)), (16, 45, 80),
                        "Details cache warning panel disappeared"
                    )
                    # The lower right Details panel uses real
                    # background-aware glass now, rather than a solid
                    # (11,33,66) fill. Check its blue ordering and bounds.
                    lower_right = frame.getpixel((1500, 884))
                    self.assertGreater(
                        lower_right[2], lower_right[1],
                        "Details glass rail was unexpectedly overwritten"
                    )
                    self.assertGreater(lower_right[1], lower_right[0])
                self.assertIsNotNone(
                    ImageChops.difference(
                        active.crop((300, 940, 975, 986)),
                        stale.crop((300, 940, 975, 986))
                    ).getbbox(),
                    "Details warning still says checking after FTP failure"
                )

            # Both directions must visibly truncate rather than erase
            # their entire long title or overwrite the Version column.
            for name in ("long_ltr", "long_rtl"):
                with Image.open(preview / (name + ".ppm")) as frame:
                    for left, right in ((928, 1005), (1175, 1267)):
                        patch = frame.crop((left, 385, right, 421))
                        white = sum(
                            1 for pixel in patch.getdata()
                            if pixel == (234, 244, 255)
                        )
                        self.assertGreater(
                            white, 8,
                            name + " title lost its visible prefix or end"
                        )
                    gutter = frame.crop((1274, 385, 1292, 421))
                    self.assertFalse(
                        any(pixel == (234, 244, 255)
                            for pixel in gutter.getdata()),
                        name + " bled into the right-side game icon gutter"
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
