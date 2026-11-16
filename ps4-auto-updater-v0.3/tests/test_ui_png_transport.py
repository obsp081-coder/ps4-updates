"""Validate real PNG chunk CRC after simulated GoldHEN ASCII transfer."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
BASE=Path(__file__).resolve().parents[1]
class PngFtpTransportTests(unittest.TestCase):
    def test_png_crc_and_safe_ascii_recovery(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if cc is None:self.skipTest("Host C compiler missing")
        src=BASE/"ps4"/"src"/"ui_png_transport.c"
        test=BASE/"tests"/"ui_mocks"/"png_transport_smoke.c"
        with tempfile.TemporaryDirectory() as tmp:
            exe=Path(tmp)/"png-ftp"
            b=subprocess.run([cc,"-std=c99","-O2","-Wall","-Wextra",
                              "-Werror",str(src),str(test),"-o",str(exe)],
                             capture_output=True,text=True,timeout=45)
            self.assertEqual(b.returncode,0,b.stdout+b.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertIn("UI_PNG_TRANSPORT_HOST_OK",r.stdout)
