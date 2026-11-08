
/*
 * PS4 Auto Updater v0.3.7
 * Installed Game Verification
 *
 * Uses the PS4 AppInstUtil system module.
 * Read-only: no game or system file changes.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>

#include <orbis/Sysmodule.h>
#include <orbis/AppInstUtil.h>

#define ROOT "/data/PS4AutoUpdater"
#define INPUT ROOT "/inventory.json"
#define TEXT_OUT ROOT "/installed_check.txt"
#define JSON_OUT ROOT "/installed_check.json"

#define MAX_GAMES 128

static int get_cusa(const char *line, char id[10])
{
    const char *p = strstr(line, "\"cusa\":\"");

    if (!p)
        return 0;

    p += 8;

    if (strncmp(p, "CUSA", 4) != 0)
        return 0;

    for (int i = 4; i < 9; ++i) {
        if (!isdigit((unsigned char)p[i]))
            return 0;
    }

    if (p[9] != '"')
        return 0;

    memcpy(id, p, 9);
    id[9] = '\0';

    return 1;
}

int main(void)
{
    if (mkdir(ROOT, 0777) != 0 &&
        errno != EEXIST)
        return 1;

    FILE *log = fopen(TEXT_OUT, "wb");

    if (!log)
        return 2;

    fputs(
        "PS4 Auto Updater Installed Check v0.3.7\n"
        "Mode: READ ONLY\n"
        "Method: PS4 AppInstUtil system module\n",
        log
    );

    FILE *input = fopen(INPUT, "rb");

    if (!input) {
        fputs("Result: INVENTORY_NOT_FOUND\n", log);
        fclose(log);
        return 3;
    }

    uint32_t module_result =
        sceSysmoduleLoadModuleInternal(
            ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL
        );

    fprintf(
        log,
        "Sysmodule load: 0x%08X\n",
        module_result
    );

    if (module_result != 0) {
        fputs("Result: SYSMODULE_LOAD_FAILED\n", log);
        fclose(input);
        fclose(log);
        return 4;
    }

    int32_t init_result =
        sceAppInstUtilInitialize();

    fprintf(
        log,
        "AppInstUtil initialize: 0x%08X\n",
        (uint32_t)init_result
    );

    if (init_result != 0) {
        fputs("Result: APPINST_INIT_FAILED\n", log);
        fclose(input);
        fclose(log);
        return 5;
    }

    FILE *json = fopen(JSON_OUT, "wb");

    if (!json) {
        fputs("Result: JSON_OPEN_FAILED\n", log);
        sceAppInstUtilTerminate();
        fclose(input);
        fclose(log);
        return 6;
    }

    fputs(
        "{\n"
        "  \"schema\": 2,\n"
        "  \"source\": \"ps4-appinstutil\",\n"
        "  \"read_only\": true,\n"
        "  \"games\": [\n",
        json
    );

    char line[2048];
    char cusa[10];

    unsigned total = 0;
    unsigned installed = 0;
    unsigned not_installed = 0;
    unsigned unknown = 0;

    while (fgets(line, sizeof(line), input)) {

        if (!get_cusa(line, cusa))
            continue;

        if (total >= MAX_GAMES)
            break;

        int32_t exists_flag = -1;

        int32_t result =
            sceAppInstUtilAppExists(
                cusa,
                &exists_flag
            );

        const char *status;

        if (result != 0) {
            status = "UNKNOWN";
            ++unknown;
        }
        else if (exists_flag != 0) {
            status = "INSTALLED";
            ++installed;
        }
        else {
            status = "NOT_INSTALLED";
            ++not_installed;
        }

        fprintf(
            log,
            "%s | %s | ret=0x%08X | flag=%d\n",
            cusa,
            status,
            (uint32_t)result,
            exists_flag
        );

        if (total > 0)
            fputs(",\n", json);

        fprintf(
            json,
            "    {\"cusa\":\"%s\","
            "\"status\":\"%s\","
            "\"installed\":",
            cusa,
            status
        );

        if (result != 0)
            fputs("null", json);
        else if (exists_flag != 0)
            fputs("true", json);
        else
            fputs("false", json);

        fprintf(
            json,
            ",\"result_code\":%u}",
            (uint32_t)result
        );

        ++total;
    }

    fputs("\n  ]\n}\n", json);

    int output_error = ferror(json);

    if (fclose(json) != 0)
        output_error = 1;

    fclose(input);

    int32_t terminate_result =
        sceAppInstUtilTerminate();

    fprintf(
        log,
        "\nTotal checked: %u\n"
        "Installed: %u\n"
        "Not installed: %u\n"
        "Unknown: %u\n"
        "Terminate: 0x%08X\n",
        total,
        installed,
        not_installed,
        unknown,
        (uint32_t)terminate_result
    );

    if (output_error) {
        fputs("Result: OUTPUT_FAILED\n", log);
    }
    else if (total == 0) {
        fputs("Result: NO_GAMES_FOUND\n", log);
    }
    else if (unknown > 0) {
        fputs("Result: CHECK_PARTIAL\n", log);
    }
    else {
        fputs("Result: CHECK_OK\n", log);
    }

    fclose(log);

    return (output_error || total == 0 || unknown > 0)
        ? 7 : 0;
}
