
#define _POSIX_C_SOURCE 200809L

/*
 * PS4 Auto Updater v0.4
 * Unified read-only diagnostic scanner.
 * No downloads, installs or system database changes.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __ORBIS__
#include <orbis/libkernel.h>
#endif

#define OUT_DIR "/data/PS4AutoUpdater"
#define MAX_GAMES 1024
#define MAX_SFO 1048576
#define MAX_PATH 1024

typedef struct {
    char cusa[10];
    char name[192];
    char ver[20];
    char cid[80];
    char category[12];
    char source[16];
    int installed;
} Game;

static Game games[MAX_GAMES];
static unsigned game_count;
static unsigned parse_failures;
static unsigned open_failures;

static int root_errno = -1;
static int external_errno = -1;
static int user_errno = -1;
static int user_app_errno = -1;
static int install_api_status = -1;

static uint16_t le16(const unsigned char *p) {
    return (uint16_t)(
        p[0] | ((uint16_t)p[1] << 8)
    );
}

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

static int in_bounds(
    size_t off, size_t len, size_t size
) {
    return off <= size && len <= size - off;
}

static int is_cusa(const char *s) {
    if (
        !s || strlen(s) != 9 ||
        strncmp(s, "CUSA", 4)
    ) return 0;

    for (int i = 4; i < 9; ++i)
        if (!isdigit((unsigned char)s[i]))
            return 0;

    return 1;
}

static int already_seen(const char *id) {
    for (unsigned i = 0; i < game_count; ++i)
        if (!strcmp(games[i].cusa, id))
            return 1;

    return 0;
}

static void copy_sfo(
    char *dst,
    size_t capacity,
    const unsigned char *src,
    size_t length
) {
    size_t i = 0;
    if (!capacity) return;

    while (
        i < length &&
        i + 1 < capacity &&
        src[i]
    ) {
        dst[i] = (char)src[i];
        ++i;
    }

    dst[i] = 0;
}

static int parse_sfo(
    const char *file,
    Game *game
) {
    FILE *fp = fopen(file, "rb");

    if (!fp) {
        ++open_failures;
        return 0;
    }

    if (fseek(fp, 0, SEEK_END)) {
        fclose(fp);
        return 0;
    }

    long length = ftell(fp);

    if (
        length < 20 ||
        length > MAX_SFO ||
        fseek(fp, 0, SEEK_SET)
    ) {
        fclose(fp);
        return 0;
    }

    size_t size = (size_t)length;
    unsigned char *bytes = malloc(size);

    if (!bytes) {
        fclose(fp);
        return 0;
    }

    int ok = fread(bytes, 1, size, fp) == size;
    fclose(fp);

    if (
        !ok ||
        le32(bytes) != 0x46535000u
    ) {
        free(bytes);
        return 0;
    }

    size_t keys = le32(bytes + 8);
    size_t values = le32(bytes + 12);
    size_t count = le32(bytes + 16);

    if (
        count > 1024 ||
        !in_bounds(20, count * 16, size) ||
        keys >= size ||
        values >= size
    ) {
        free(bytes);
        return 0;
    }

    memset(game, 0, sizeof(*game));
    game->installed = -1;

    for (size_t j = 0; j < count; ++j) {
        const unsigned char *entry =
            bytes + 20 + 16 * j;

        size_t key_at =
            keys + le16(entry);

        size_t val_at =
            values + le32(entry + 12);

        size_t val_len =
            le32(entry + 4);

        if (
            !in_bounds(key_at, 1, size) ||
            !in_bounds(val_at, val_len, size) ||
            !memchr(
                bytes + key_at,
                0,
                size - key_at
            )
        ) continue;

        const char *key =
            (const char *)(bytes + key_at);

        if (!strcmp(key, "TITLE_ID"))
            copy_sfo(
                game->cusa,
                sizeof(game->cusa),
                bytes + val_at,
                val_len
            );

        else if (!strcmp(key, "TITLE"))
            copy_sfo(
                game->name,
                sizeof(game->name),
                bytes + val_at,
                val_len
            );

        else if (!strcmp(key, "APP_VER"))
            copy_sfo(
                game->ver,
                sizeof(game->ver),
                bytes + val_at,
                val_len
            );

        else if (!strcmp(key, "CONTENT_ID"))
            copy_sfo(
                game->cid,
                sizeof(game->cid),
                bytes + val_at,
                val_len
            );

        else if (!strcmp(key, "CATEGORY"))
            copy_sfo(
                game->category,
                sizeof(game->category),
                bytes + val_at,
                val_len
            );
    }

    free(bytes);
    return 1;
}

static void scan_dir(
    const char *dirpath,
    const char *source,
    int *err
) {
    errno = 0;
    DIR *dir = opendir(dirpath);

    if (!dir) {
        *err = errno;
        return;
    }

    *err = 0;
    struct dirent *entry;

    while ((entry = readdir(dir))) {
        if (game_count >= MAX_GAMES)
            break;

        const char *cusa = entry->d_name;

        if (
            !is_cusa(cusa) ||
            already_seen(cusa)
        ) continue;

        char path[MAX_PATH];

        int n = snprintf(
            path, sizeof(path),
            "%s/%s/param.sfo",
            dirpath, cusa
        );

        if (
            n <= 0 ||
            (size_t)n >= sizeof(path)
        ) continue;

        Game candidate;

        if (!parse_sfo(path, &candidate)) {
            n = snprintf(
                path, sizeof(path),
                "%s/%s/sce_sys/param.sfo",
                dirpath, cusa
            );

            if (
                n <= 0 ||
                (size_t)n >= sizeof(path) ||
                !parse_sfo(path, &candidate)
            ) {
                ++parse_failures;
                continue;
            }
        }

        if (strcmp(candidate.cusa, cusa))
            continue;

        if (
            candidate.category[0] &&
            strcmp(candidate.category, "gd") &&
            strcmp(candidate.category, "gde") &&
            strcmp(candidate.category, "gda") &&
            strcmp(candidate.category, "gdo")
        ) continue;

        snprintf(
            candidate.source,
            sizeof(candidate.source),
            "%s", source
        );

        games[game_count++] = candidate;
    }

    closedir(dir);
}

static void json_string(
    FILE *fp,
    const char *s
) {
    fputc('"', fp);

    for (
        const unsigned char *p =
            (const unsigned char *)s;
        *p;
        ++p
    ) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', fp);
            fputc(*p, fp);
        } else if (*p < 0x20) {
            fprintf(fp, "\\u%04x", *p);
        } else {
            fputc(*p, fp);
        }
    }

    fputc('"', fp);
}

static void probe(
    FILE *fp,
    const char *path,
    int directory
) {
    struct stat st;

    errno = 0;
    int sr = stat(path, &st);
    int stat_err = sr == 0 ? 0 : errno;

    errno = 0;
    int access_err;

    if (directory) {
        DIR *d = opendir(path);
        access_err = d ? 0 : errno;

        if (d) closedir(d);
    } else {
        FILE *f = fopen(path, "rb");
        access_err = f ? 0 : errno;

        if (f) fclose(f);
    }

    fprintf(
        fp,
        "%s | stat=%d | %s=%d\n",
        path,
        stat_err,
        directory ? "opendir" : "fopen",
        access_err
    );
}

#ifdef __ORBIS__

typedef int32_t (*InitFn)(void);
typedef int32_t (*TermFn)(void);
typedef int32_t (*ExistsFn)(
    const char *, int32_t *
);

static void check_installed(FILE *report) {
    int32_t handle =
        (int32_t)sceKernelLoadStartModule(
            "/system/common/lib/libSceAppInstUtil.sprx",
            0, NULL, 0, NULL, NULL
        );

    fprintf(
        report,
        "AppInstUtil module handle: 0x%08x\n",
        (uint32_t)handle
    );

    if (handle < 0) {
        install_api_status = 1;
        return;
    }

    void *sym_init = NULL;
    void *sym_term = NULL;
    void *sym_exists = NULL;

    if (
        sceKernelDlsym(
            handle,
            "sceAppInstUtilInitialize",
            &sym_init
        ) != 0 ||

        sceKernelDlsym(
            handle,
            "sceAppInstUtilTerminate",
            &sym_term
        ) != 0 ||

        sceKernelDlsym(
            handle,
            "sceAppInstUtilAppExists",
            &sym_exists
        ) != 0 ||

        !sym_init ||
        !sym_term ||
        !sym_exists
    ) {
        install_api_status = 2;

        fputs(
            "AppInstUtil symbol lookup failed\n",
            report
        );
        return;
    }

    InitFn init = (InitFn)sym_init;
    TermFn term = (TermFn)sym_term;
    ExistsFn exists = (ExistsFn)sym_exists;

    int32_t result = init();

    fprintf(
        report,
        "AppInstUtil initialize: 0x%08x\n",
        (uint32_t)result
    );

    if (result) {
        install_api_status = 3;
        return;
    }

    install_api_status = 0;

    /* Known diagnostic anchor */
    int32_t flag = -1;

    result = exists("CUSA02343", &flag);

    fprintf(
        report,
        "Anchor CUSA02343: result=0x%08x, exists=%d\n",
        (uint32_t)result,
        flag
    );

    for (unsigned i = 0; i < game_count; ++i) {
        flag = -1;

        result = exists(
            games[i].cusa,
            &flag
        );

        if (result == 0)
            games[i].installed =
                flag ? 1 : 0;
    }

    fprintf(
        report,
        "AppInstUtil terminate: 0x%08x\n",
        (uint32_t)term()
    );
}
#endif

static int write_inventory(
    const char *path
) {
    FILE *out = fopen(path, "wb");

    if (!out) return 0;

    fputs(
        "{\n"
        "  \"schema\":3,\n"
        "  \"source\":\"ps4-native-diagnostic-v0.4\",\n"
        "  \"metadata_only\":true,\n"
        "  \"download_enabled\":false,\n"
        "  \"install_enabled\":false,\n"
        "  \"games\":[\n",
        out
    );

    for (unsigned i = 0; i < game_count; ++i) {
        const Game *g = &games[i];

        if (i) fputs(",\n", out);

        fputs("    {\"cusa\":", out);
        json_string(out, g->cusa);

        fputs(",\"name\":", out);
        json_string(out, g->name);

        fputs(",\"app_ver\":", out);
        json_string(out, g->ver);

        fputs(",\"content_id\":", out);
        json_string(out, g->cid);

        fputs(",\"category\":", out);
        json_string(out, g->category);

        fputs(",\"metadata_location\":", out);
        json_string(out, g->source);

        fputs(",\"installed_confirmed\":", out);

        fputs(
            g->installed == 1 ? "true" : "false",
            out
        );

        fputs(",\"install_state\":", out);

        fputs(
            g->installed == 1
                ? "\"INSTALLED\""
                : g->installed == 0
                    ? "\"NOT_INSTALLED\""
                    : "\"UNKNOWN\"",
            out
        );

        fputc('}', out);
    }

    fputs("\n  ]\n}\n", out);

    int error = ferror(out);

    if (fclose(out))
        error = 1;

    return !error;
}

int main(int argc, char **argv) {
    const char *root =
        "/system_data/priv/appmeta";

    const char *output =
        OUT_DIR "/inventory_v04.json";

#ifndef __ORBIS__
    for (int i = 1; i < argc; ++i) {
        if (
            !strcmp(argv[i], "--appmeta") &&
            i + 1 < argc
        ) {
            root = argv[++i];
        } else if (
            !strcmp(argv[i], "--out") &&
            i + 1 < argc
        ) {
            output = argv[++i];
        } else {
            fprintf(
                stderr,
                "Usage: %s [--appmeta DIR] "
                "[--out FILE]\n",
                argv[0]
            );
            return 2;
        }
    }
#else
    (void)argc;
    (void)argv;

    if (
        mkdir(OUT_DIR, 0777) != 0 &&
        errno != EEXIST
    ) return 2;
#endif

    char external[MAX_PATH];

    int n = snprintf(
        external,
        sizeof(external),
        "%s/external",
        root
    );

    FILE *report = NULL;

#ifdef __ORBIS__
    report = fopen(
        OUT_DIR "/diagnostic_v04.txt",
        "wb"
    );
#endif

    if (report) {
        fputs(
            "PS4 Auto Updater v0.4 | READ ONLY\n"
            "0=OK; 2=not found; "
            "13=permission denied\n",
            report
        );

        probe(report, "/data", 1);
        probe(report, "/user", 1);
        probe(report, "/system_data", 1);
        probe(report, "/system_data/priv", 1);
        probe(
            report,
            "/system_data/priv/appmeta",
            1
        );
        probe(
            report,
            "/system_data/priv/appmeta/"
            "CUSA02343/param.sfo",
            0
        );
        probe(
            report,
            "/system_data/priv/mms/app.db",
            0
        );
        probe(report, "/user/appmeta", 1);
        probe(report, "/user/app", 1);
        probe(report, "/mnt/sandbox", 1);

        fflush(report);
    }

    scan_dir(
        root,
        "internal",
        &root_errno
    );

    if (
        n > 0 &&
        (size_t)n < sizeof(external)
    ) {
        scan_dir(
            external,
            "external",
            &external_errno
        );
    }

#ifdef __ORBIS__
    scan_dir(
        "/user/appmeta",
        "user",
        &user_errno
    );

    scan_dir(
        "/user/app",
        "userapp",
        &user_app_errno
    );

    if (report)
        check_installed(report);
#endif

    int saved = write_inventory(output);

    if (report) {
        fprintf(
            report,
            "Metadata records: %u\n"
            "Root opendir errno: %d\n"
            "External opendir errno: %d\n"
            "User appmeta errno: %d\n"
            "User app errno: %d\n"
            "SFO open failures: %u\n"
            "SFO parse failures: %u\n"
            "Installation API status: %d\n"
            "Inventory write success: %s\n"
            "Installed count (confirmed): ",
            game_count,
            root_errno,
            external_errno,
            user_errno,
            user_app_errno,
            open_failures,
            parse_failures,
            install_api_status,
            saved ? "YES" : "NO"
        );

        unsigned confirmed = 0;

        for (unsigned i = 0; i < game_count; ++i)
            if (games[i].installed == 1)
                ++confirmed;

        fprintf(report, "%u\n", confirmed);
        fclose(report);
    }

    return saved ? 0 : 1;
}
