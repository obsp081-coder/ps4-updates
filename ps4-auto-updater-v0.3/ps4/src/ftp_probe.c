
/*
 * PS4 Auto Updater v0.3.6
 * Installed-title verification using libSceAppInstUtil.
 *
 * Reads existing inventory.json.
 * Writes installed_check.txt and installed_check.json.
 * Does not modify games, system files or inventory.json.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <orbis/libkernel.h>

#define ROOT "/data/PS4AutoUpdater"
#define INPUT ROOT "/inventory.json"
#define TEXT_OUT ROOT "/installed_check.txt"
#define JSON_OUT ROOT "/installed_check.json"

typedef int32_t (*InitFn)(void);
typedef int32_t (*ExistsFn)(const char *, int32_t *);
typedef int32_t (*TermFn)(void);

static int next_cusa(const char *line, char out[10])
{
    const char *p = strstr(line, "\"cusa\":\"");

    if (!p)
        return 0;

    p += strlen("\"cusa\":\"");

    if (strncmp(p, "CUSA", 4) != 0)
        return 0;

    for (int i = 4; i < 9; ++i) {
        if (!isdigit((unsigned char)p[i]))
            return 0;
    }

    if (p[9] != '"')
        return 0;

    memcpy(out, p, 9);
    out[9] = 0;

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
        "PS4 Auto Updater Installed Check v0.3.6\n"
        "READ ONLY - source: inventory.json\n",
        log
    );

    FILE *in = fopen(INPUT, "rb");

    if (!in) {
        fputs("Result: INVENTORY_NOT_FOUND\n", log);
        fclose(log);
        return 3;
    }

    int32_t module = (int32_t)sceKernelLoadStartModule(
        "/system/common/lib/libSceAppInstUtil.sprx",
        0, NULL, 0, NULL, NULL
    );

    fprintf(
        log,
        "Module load: 0x%08X\n",
        (uint32_t)module
    );

    if (module < 0) {
        fputs("Result: MODULE_LOAD_FAILED\n", log);
        fclose(in);
        fclose(log);
        return 4;
    }

    InitFn initialize = NULL;
    ExistsFn exists = NULL;
    TermFn terminate = NULL;

    int a = sceKernelDlsym(
        module,
        "sceAppInstUtilInitialize",
        (void **)&initialize
    );

    int b = sceKernelDlsym(
        module,
        "sceAppInstUtilAppExists",
        (void **)&exists
    );

    int c = sceKernelDlsym(
        module,
        "sceAppInstUtilTerminate",
        (void **)&terminate
    );

    fprintf(
        log,
        "Symbols: init=%d exists=%d terminate=%d\n",
        a, b, c
    );

    if (a < 0 || b < 0 || c < 0 ||
        !initialize || !exists || !terminate) {

        fputs("Result: SYMBOL_NOT_FOUND\n", log);
        fclose(in);
        fclose(log);
        return 5;
    }

    int init_result = initialize();

    fprintf(
        log,
        "Initialize: 0x%08X\n",
        (uint32_t)init_result
    );

    if (init_result != 0) {
        fputs("Result: INIT_FAILED\n", log);
        fclose(in);
        fclose(log);
        return 6;
    }

    FILE *json = fopen(JSON_OUT, "wb");

    if (!json) {
        fputs("Result: OUTPUT_FAILED\n", log);
        terminate();
        fclose(in);
        fclose(log);
        return 7;
    }

    fputs(
        "{\n"
        "  \"schema\": 1,\n"
        "  \"source\": \"sceAppInstUtilAppExists\",\n"
        "  \"games\": [\n",
        json
    );

    char line[2048];
    char cusa[10];

    unsigned checked = 0;
    unsigned installed = 0;
    unsigned absent = 0;
    unsigned errors = 0;

    while (fgets(line, sizeof(line), in)) {

        if (!next_cusa(line, cusa))
            continue;

        if (checked >= 128)
            break;

        int32_t flag = -1;

        int32_t ret = exists(cusa, &flag);

        const char *status =
            ret != 0 ? "UNKNOWN" :
            flag ? "INSTALLED" : "NOT_INSTALLED";

        if (ret != 0)
            ++errors;
        else if (flag)
            ++installed;
        else
            ++absent;

        fprintf(
            log,
            "%s | %s | ret=0x%08X flag=%d\n",
            cusa, status, (uint32_t)ret, flag
        );

        if (checked)
            fputs(",\n", json);

        fprintf(
            json,
            "    {\"cusa\":\"%s\",\"installed\":",
            cusa
        );

        if (ret != 0)
            fputs("null", json);
        else
            fputs(flag ? "true" : "false", json);

        fprintf(
            json,
            ",\"result_code\":%u}",
            (uint32_t)ret
        );

        ++checked;
    }

    fputs("\n  ]\n}\n", json);

    int write_failed = ferror(json);

    if (fclose(json) != 0)
        write_failed = 1;

    fclose(in);

    int term_result = terminate();

    fprintf(
        log,
        "Total checked: %u\n"
        "Installed: %u\n"
        "Not installed: %u\n"
        "Unknown: %u\n"
        "Terminate: 0x%08X\n",
        checked,
        installed,
        absent,
        errors,
        (uint32_t)term_result
    );

    if (write_failed)
        fputs("Result: OUTPUT_FAILED\n", log);
    else if (!checked)
        fputs("Result: NO_CUSA_FOUND\n", log);
    else if (errors)
        fputs("Result: CHECK_PARTIAL\n", log);
    else
        fputs("Result: CHECK_OK\n", log);

    fclose(log);

    return (write_failed || !checked || errors) ? 8 : 0;
}
