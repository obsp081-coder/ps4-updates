"""Offline Host checks of an 8-byte installed-PKG header hint parser.

Synthetic bytes only; this is not a console test or FPKG authentication.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[1]
HEADER_DIR = PROJECT / "ps4" / "src"

SOURCE = r'''
#include "pkg_header_evidence.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void assert_hint(const unsigned char bytes[8],
                        PkgHeaderObservation expected, unsigned flags) {
    const PkgHeaderEvidence x=pkg_header_observe(bytes,8u);
    assert(x.observation==expected);
    assert(x.header_read==1u);
    assert(x.flags_be==flags);
}
int main(void) {
    const unsigned char fake[]={0x7F,'C','N','T',0,0,0,1};
    const unsigned char nonfake[]={0x7F,'C','N','T',0x83,0,0,1};
    const unsigned char playroom[]={0x7F,'C','N','T',0x81,0,0,1};
    const unsigned char unknown[]={0x7F,'C','N','T',0x91,0,0,1};
    const unsigned char corrupt[]={0x7F,'C','N','X',0,0,0,1};
    assert_hint(fake,PKG_OBS_FAKE_FLAG_CANDIDATE,0x00000001u);
    assert_hint(nonfake,PKG_OBS_NONFAKE_FLAG_CANDIDATE,0x83000001u);
    assert_hint(playroom,PKG_OBS_NONFAKE_FLAG_CANDIDATE,0x81000001u);
    assert_hint(unknown,PKG_OBS_OTHER_FLAGS,0x91000001u);
    assert(pkg_header_observe(corrupt,sizeof corrupt).observation
           ==PKG_OBS_BAD_MAGIC);
    assert(pkg_header_observe(NULL,0u).observation
           ==PKG_OBS_SHORT_READ);
    assert(pkg_header_observe(fake,7u).observation
           ==PKG_OBS_SHORT_READ);
    assert(!strcmp(pkg_header_observation_name(PKG_OBS_NOT_PROBED),
                   "NOT_PROBED"));
    assert(!strcmp(pkg_header_observation_name(
        PKG_OBS_FAKE_FLAG_CANDIDATE), "FAKE_FLAG_CANDIDATE"));
    puts("PKG_HEADER_HINT_HOST_OK");
    return 0;
}
'''


class PkgHeaderEvidenceTests(unittest.TestCase):
    def test_all_recognized_and_unknown_flags_are_only_hints(self):
        if not shutil.which("cc"):
            self.skipTest("No host C compiler")
        with tempfile.TemporaryDirectory() as root:
            source = Path(root) / "check.c"
            binary = Path(root) / "check"
            source.write_text(SOURCE, encoding="utf-8")
            compiled = subprocess.run([
                "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                "-I", str(HEADER_DIR), str(source), "-o", str(binary),
            ], capture_output=True, text=True, timeout=15)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            run = subprocess.run([str(binary)], capture_output=True,
                                 text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("PKG_HEADER_HINT_HOST_OK", run.stdout)

    def test_header_api_carries_no_trust_or_install_fields(self):
        header = (HEADER_DIR/"pkg_header_evidence.h").read_text(encoding="utf-8")
        self.assertIn("HEADER", header.upper())
        self.assertNotIn("download_allowed", header)
        self.assertNotIn("install_allowed", header)
        self.assertNotIn("sceAppInstUtil", header)
        self.assertNotIn("sceNet", header)
        self.assertNotIn("FILE *", header)
        self.assertNotIn("fopen(", header)


if __name__ == "__main__":
    unittest.main()
