
"""
PS4 Auto Updater - Host Mock Tests

Tests the actual ftp_probe.c logic using mocked PS4 APIs.
No PlayStation console required.

IMPORTANT:
Mock installation results are fictional test data.
They are NOT the real installation status of the user's PS4.
"""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent / "ps4" / "src" / "ftp_probe.c"


SYS_HEADER = r"""
#pragma once
#include <stdint.h>

#define ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL 0x80000014u

uint32_t sceSysmoduleLoadModuleInternal(uint32_t moduleId);
"""


APP_HEADER = r"""
#pragma once
#include <stdint.h>

int32_t sceAppInstUtilInitialize(void);
int32_t sceAppInstUtilTerminate(void);
int32_t sceAppInstUtilAppExists(
    const char *titleId,
    int32_t *exists
);
"""


MOCK_SOURCE = r"""
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <orbis/Sysmodule.h>
#include <orbis/AppInstUtil.h>

uint32_t sceSysmoduleLoadModuleInternal(uint32_t moduleId)
{
    if (getenv("MOCK_MODULE_FAIL") ||
        moduleId != ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL)
        return 0x80020002u;

    return 0;
}

int32_t sceAppInstUtilInitialize(void)
{
    if (getenv("MOCK_INIT_FAIL"))
        return (int32_t)0x80020001u;

    return 0;
}

int32_t sceAppInstUtilTerminate(void)
{
    return 0;
}

int32_t sceAppInstUtilAppExists(
    const char *titleId,
    int32_t *exists)
{
    const char *fail = getenv("MOCK_EXISTS_ERROR");

    if (fail && !strcmp(fail, titleId))
        return (int32_t)0x80020003u;

    /*
     * Simulated results ONLY.
     * These are not real PS4 installation results.
     */
    *exists = (
        !strcmp(titleId, "CUSA02343") ||
        !strcmp(titleId, "CUSA00411")
    ) ? 1 : 0;

    return 0;
}
"""


class InstalledCheckMockTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.work = Path(cls.temp.name)

        cls.root = cls.work / "data"
        cls.root.mkdir()

        include_dir = cls.work / "orbis"
        include_dir.mkdir()

        (include_dir / "Sysmodule.h").write_text(
            SYS_HEADER,
            encoding="utf-8"
        )

        (include_dir / "AppInstUtil.h").write_text(
            APP_HEADER,
            encoding="utf-8"
        )

        (cls.work / "mock.c").write_text(
            MOCK_SOURCE,
            encoding="utf-8"
        )

        original = SOURCE.read_text(encoding="utf-8")

        needle = '#define ROOT "/data/PS4AutoUpdater"'

        if original.count(needle) != 1:
            raise AssertionError(
                "ROOT declaration changed. Update test harness."
            )

        # Replace the output directory in a temporary
        # HOST COPY ONLY. Original PS4 code is untouched.
        temporary_source = original.replace(
            needle,
            "#define ROOT " + json.dumps(str(cls.root))
        )

        source_copy = cls.work / "ftp_probe_host.c"

        source_copy.write_text(
            temporary_source,
            encoding="utf-8"
        )

        cls.executable = cls.work / "installed-check-host"

        subprocess.run(
            [
                "cc",
                "-std=c99",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-O2",
                "-I",
                str(cls.work),
                "-o",
                str(cls.executable),
                str(source_copy),
                str(cls.work / "mock.c"),
            ],
            check=True
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        for filename in (
            "inventory.json",
            "installed_check.txt",
            "installed_check.json",
        ):
            (self.root / filename).unlink(
                missing_ok=True
            )

    def write_inventory(self, ids):
        # Same one-game-per-line format as v0.3.5.
        lines = ['{"schema":4,"games":[']

        for ident in ids:
            lines.append(
                json.dumps(
                    {
                        "cusa": ident,
                        "name": "Mock title"
                    },
                    separators=(",", ":")
                )
            )

        lines.append("]}")

        (self.root / "inventory.json").write_text(
            "\n".join(lines),
            encoding="utf-8"
        )

    def run_app(self, **mock_env):
        env = os.environ.copy()

        for key in (
            "MOCK_MODULE_FAIL",
            "MOCK_INIT_FAIL",
            "MOCK_EXISTS_ERROR",
        ):
            env.pop(key, None)

        env.update(mock_env)

        return subprocess.run(
            [str(self.executable)],
            env=env,
            capture_output=True,
            text=True,
            timeout=5
        )

    def read_statuses(self):
        data = json.loads(
            (
                self.root / "installed_check.json"
            ).read_text(encoding="utf-8")
        )

        games = {
            game["cusa"]: game
            for game in data["games"]
        }

        return data, games

    def read_log(self):
        return (
            self.root / "installed_check.txt"
        ).read_text(encoding="utf-8")

    def test_nine_games_mixed_installation(self):
        ids = [
            "CUSA00001",
            "CUSA06122",
            "CUSA57220",
            "CUSA03506",
            "CUSA01116",
            "CUSA08032",
            "CUSA00411",
            "CUSA03551",
            "CUSA02343",
        ]

        self.write_inventory(ids)

        result = self.run_app()

        self.assertEqual(
            result.returncode,
            0,
            result.stderr
        )

        data, games = self.read_statuses()

        self.assertEqual(data["schema"], 2)
        self.assertEqual(len(games), 9)

        self.assertIs(
            games["CUSA02343"]["installed"],
            True
        )

        self.assertIs(
            games["CUSA00411"]["installed"],
            True
        )

        self.assertIs(
            games["CUSA03551"]["installed"],
            False
        )

        self.assertIn("Installed: 2", self.read_log())
        self.assertIn("Not installed: 7", self.read_log())
        self.assertIn("Result: CHECK_OK", self.read_log())

    def test_missing_inventory(self):
        result = self.run_app()

        self.assertEqual(result.returncode, 3)

        self.assertIn(
            "INVENTORY_NOT_FOUND",
            self.read_log()
        )

    def test_module_load_failure(self):
        self.write_inventory(["CUSA02343"])

        result = self.run_app(
            MOCK_MODULE_FAIL="1"
        )

        self.assertEqual(result.returncode, 4)

        self.assertIn(
            "SYSMODULE_LOAD_FAILED",
            self.read_log()
        )

    def test_initialize_failure(self):
        self.write_inventory(["CUSA02343"])

        result = self.run_app(
            MOCK_INIT_FAIL="1"
        )

        self.assertEqual(result.returncode, 5)

        self.assertIn(
            "APPINST_INIT_FAILED",
            self.read_log()
        )

    def test_individual_query_failure_is_unknown(self):
        self.write_inventory([
            "CUSA02343",
            "CUSA00411",
        ])

        result = self.run_app(
            MOCK_EXISTS_ERROR="CUSA00411"
        )

        self.assertEqual(result.returncode, 7)

        _, games = self.read_statuses()

        self.assertIs(
            games["CUSA02343"]["installed"],
            True
        )

        self.assertIsNone(
            games["CUSA00411"]["installed"]
        )

        self.assertEqual(
            games["CUSA00411"]["status"],
            "UNKNOWN"
        )

        self.assertIn(
            "Result: CHECK_PARTIAL",
            self.read_log()
        )

    def test_no_games_found(self):
        self.write_inventory([])

        result = self.run_app()

        self.assertEqual(result.returncode, 7)

        self.assertIn(
            "NO_GAMES_FOUND",
            self.read_log()
        )

    def test_skips_invalid_cusa(self):
        self.write_inventory([
            "BADID",
            "CUSA02343",
            "CUSAxx123",
        ])

        result = self.run_app()

        self.assertEqual(result.returncode, 0)

        _, games = self.read_statuses()

        self.assertEqual(
            list(games),
            ["CUSA02343"]
        )


if __name__ == "__main__":
    unittest.main()
