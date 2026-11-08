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
UI_IMAGE = ROOT / "ps4" / "src" / "ui_image.c"
UI_ICON_CACHE = ROOT / "ps4" / "src" / "ui_icon_cache.c"
MOCK_DIR = Path(__file__).resolve().parent / "ui_mocks"
MOCK_TEST = MOCK_DIR / "ui_smoke.cpp"
LABEL_SCRIPT = ROOT / "ps4" / "tools" / "generate_ui_arabic.py"


class NativeUiSmokeTests(unittest.TestCase):

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
                    str(UI_SOURCE), str(UI_NAV), str(UI_IMAGE),
                    str(UI_ICON_CACHE), str(MOCK_TEST), "-o", str(binary),
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
            for page in ("home", "details", "downloads", "search"):
                ppm = preview / (page + ".ppm")
                self.assertTrue(ppm.is_file(), "Missing native UI page: " + page)
                with Image.open(ppm) as pixels:
                    self.assertEqual(pixels.size, (1920, 1080))
                    self.assertEqual(pixels.mode, "RGB")
                    # Main royal-blue header must be nonblank for every page.
                    self.assertNotEqual(pixels.getpixel((200, 45)),
                                        pixels.getpixel((0, 550)))
                    if page == "search":
                        # All A-J keys on the 10-column keyboard must have
                        # visible white glyphs; proportional fonts can be
                        # clipped if the old 5x7 fixed-cell width is reused.
                        for col in range(10):
                            x = 128 + col * 163
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


if __name__ == "__main__":
    unittest.main()
