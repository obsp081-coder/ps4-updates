"""Host C contract for read-only update/download UI models; not PS4 testing."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/"ps4"/"src"/"ui_models.c"
FIXTURE=ROOT/"tests"/"ui_mocks"/"models_smoke.c"


class UiModelTests(unittest.TestCase):
    def test_transactional_records_and_invalid_states(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if cc is None:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as temp:
            executable=Path(temp)/"models-smoke"
            build=subprocess.run(
                [cc,"-std=c99","-Wall","-Wextra","-Werror","-O2",
                 str(MODEL),str(FIXTURE),"-o",str(executable)],
                text=True,capture_output=True,timeout=45)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(executable)],
                               text=True,capture_output=True,timeout=20)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn("UI_MODEL_HOST_OK",run.stdout)


if __name__=="__main__":
    unittest.main()
