"""Host-only POSIX backend tests for a future PS4 storage adapter.

No native PS4 path is enabled by these tests. All files are created inside
fresh private temporary directories and removed in the same test.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ps4" / "src"
FIXTURE = ROOT / "tests" / "ui_mocks" / "file_backend_smoke.c"


class DownloadFileBackendHostTests(unittest.TestCase):
    def test_private_dirfd_and_exclusive_publish_fail_closed(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("No C compiler")
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "file-backend-smoke"
            build = subprocess.run(
                [cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                 str(SRC / "download_file_backend.c"),
                 str(SRC / "download_storage.c"),
                 str(SRC / "download_sha256.c"),
                 str(FIXTURE), "-o", str(binary)],
                text=True, capture_output=True, timeout=45,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(binary)], text=True, capture_output=True, timeout=30,
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("DOWNLOAD_FILE_BACKEND_HOST_OK", run.stdout)


    def test_ps4_backend_fails_closed_without_platform_proof(self):
        """Even a caller supplying a descriptor cannot activate PS4 writes."""
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("No C compiler")
        program = r"""
#include "download_file_backend.h"
#include "download_storage.h"
#include <assert.h>
int main(void) {
    DsFileBackend b;
    assert(ds_file_backend_attach(&b, 4)==-1);
    DsPort p=ds_file_backend_port(&b);
    assert(p.create_exclusive==0);
    assert(p.publish_exclusive==0);
    DsSession s;
    ds_init(&s);
    const unsigned char digest[32]={1};
    assert(ds_begin(&s,&p,
        "IV0000-CUSA02343_00-SCANNER000000000",
        "01.09",123,digest)==DS_INVALID);
    ds_file_backend_detach(&b);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "ps4-disabled.c"
            binary = Path(tmp) / "ps4-disabled"
            source.write_text(program)
            build = subprocess.run(
                [cc, "-D__ORBIS__", "-std=c99", "-O2", "-Wall", "-Wextra",
                 "-Werror", "-I", str(SRC),
                 str(SRC / "download_file_backend.c"),
                 str(SRC / "download_storage.c"), str(source),
                 "-o", str(binary)],
                text=True, capture_output=True, timeout=45,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(binary)], text=True, capture_output=True, timeout=15,
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
