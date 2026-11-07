/*
 * PS4 Auto Updater v0.3 - READ-ONLY inventory smoke test.
 * Native/OpenOrbis prototype. No game downloads, patching, installation,
 * database writes, user modifications or console firmware updates.
 *
 * It scans /system_data/priv/appmeta/{CUSAxxxxx}/param.sfo (+ external),
 * which is only metadata: it does NOT prove a game is currently installed.
 * The authoritative app.db join from v0.2 still needs an on-console test.
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_SFO (4U * 1024U * 1024U)
#define MAX_PATH 2048
#define MAX_GAMES 10000
#define OUT_DIR "/data/PS4AutoUpdater"
#define META_ROOT "/system_data/priv/appmeta"

typedef struct {
    char id[16], name[256], app_ver[32], content_id[128], category[32];
} game_t;

static unsigned g_count = 0;
static char g_seen[MAX_GAMES][16];
static unsigned g_errors = 0;
static uint16_t read16(const unsigned char *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t read32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int range_ok(size_t offset, size_t n, size_t total) {
    return offset <= total && n <= total - offset;
}
static int valid_cusa(const char *s) {
    if (!s || strlen(s) != 9 || strncmp(s, "CUSA", 4)) return 0;
    for (unsigned i = 4; i < 9; ++i)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}
static int seen(const char *id) {
    for (unsigned i = 0; i < g_count; ++i)
        if (!strcmp(id, g_seen[i])) return 1;
    return 0;
}
static void val_string(char *out, size_t outlen, const unsigned char *p, size_t plen) {
    size_t k = 0;
    if (!outlen) return;
    while (k < plen && k + 1 < outlen && p[k] != 0) {
        out[k] = (char)p[k]; ++k;
    }
    out[k] = 0;
}
static int parse_sfo(const char *path, game_t *game) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 0; }
    long l = ftell(f);
    if (l < 20 || l > (long)MAX_SFO) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET)) { fclose(f); return 0; }
    size_t size = (size_t)l;
    unsigned char *b = malloc(size);
    if (!b) { fclose(f); return 0; }
    int read_ok = fread(b, 1, size, f) == size;
    fclose(f);
    if (!read_ok || read32(b) != 0x46535000u) { free(b); return 0; }
    size_t keys = read32(b + 8), vals = read32(b + 12), cnt = read32(b + 16);
    if (cnt > 2048 || !range_ok(20, cnt * 16, size) || keys >= size || vals >= size) {
        free(b); return 0;
    }
    memset(game, 0, sizeof(*game));
    for (size_t i = 0; i < cnt; ++i) {
        const unsigned char *e = b + 20 + i * 16;
        size_t key_at = keys + read16(e), val_at = vals + read32(e + 12);
        size_t value_len = read32(e + 4);
        if (!range_ok(key_at, 1, size) || !range_ok(val_at, value_len, size)) continue;
        const char *key = (const char *)(b + key_at);
        if (!memchr(key, 0, size - key_at)) continue;
        if (!strcmp(key, "TITLE_ID")) val_string(game->id, sizeof(game->id), b + val_at, value_len);
        else if (!strcmp(key, "TITLE")) val_string(game->name, sizeof(game->name), b + val_at, value_len);
        else if (!strcmp(key, "APP_VER")) val_string(game->app_ver, sizeof(game->app_ver), b + val_at, value_len);
        else if (!strcmp(key, "CONTENT_ID")) val_string(game->content_id, sizeof(game->content_id), b + val_at, value_len);
        else if (!strcmp(key, "CATEGORY")) val_string(game->category, sizeof(game->category), b + val_at, value_len);
    }
    free(b);
    return 1;
}
static void write_json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p < 0x20) fprintf(f, "\\u%04x", *p);
        else fputc(*p, f);
    }
    fputc('"', f);
}
static unsigned scan_directory(FILE *out, const char *root, const char *location) {
    DIR *dir = opendir(root);
    if (!dir) { if (!strcmp(location, "internal")) ++g_errors; return 0; }
    unsigned found = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *id = entry->d_name;
        if (!valid_cusa(id) || seen(id) || g_count >= MAX_GAMES) continue;
        char path[MAX_PATH];
        int n = snprintf(path, sizeof(path), "%s/%s/param.sfo", root, id);
        if (n <= 0 || (size_t)n >= sizeof(path)) continue;
        game_t g;
        if (!parse_sfo(path, &g)) continue;
        if (!valid_cusa(g.id) || strcmp(g.id, id)) continue;
        if (g.category[0] && strcmp(g.category, "gd") && strcmp(g.category, "gde")) continue;
        if (g_count) fputs(",\n", out);
        fputs("    {\"cusa\":", out); write_json_string(out, g.id);
        fputs(",\"name\":", out); write_json_string(out, g.name);
        fputs(",\"app_ver\":", out); write_json_string(out, g.app_ver);
        fputs(",\"content_id\":", out); write_json_string(out, g.content_id);
        fputs(",\"category\":", out); write_json_string(out, g.category);
        fputs(",\"metadata_location\":", out); write_json_string(out, location);
        fputs(",\"metadata_available\":true,\"installed_confirmed\":false}", out);
        memcpy(g_seen[g_count], id, 10);
        ++g_count; ++found;
    }
    closedir(dir);
    return found;
}
static void status_file(const char *msg) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/status.txt", OUT_DIR);
    FILE *f = fopen(path, "wb");
    if (f) { fputs(msg, f); fclose(f); }
}
int main(int argc, char **argv) {
    const char *root = META_ROOT;
    const char *outname = OUT_DIR "/inventory.json";
#ifndef __ORBIS__
    /* Host tests can use fixtures; no such options are offered to PS4 users. */
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--appmeta") && i + 1 < argc) root = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outname = argv[++i];
        else { fprintf(stderr, "Usage: %s [--appmeta DIR] [--out FILE]\n", argv[0]); return 2; }
    }
#else
    (void)argc; (void)argv;
    if (mkdir(OUT_DIR, 0777) != 0 && errno != EEXIST) {
        printf("Unable to create %s; cannot export inventory.\n", OUT_DIR);
        return 1;
    }
#endif
    FILE *f = fopen(outname, "wb");
    if (!f) { fprintf(stderr, "Cannot create output %s (errno=%d)\n", outname, errno); return 1; }
    fputs("{\n  \"schema\": 3,\n  \"source\": \"ps4-appmeta-native-smoke-test\",\n"
          "  \"metadata_only\": true,\n  \"games\": [\n", f);
    scan_directory(f, root, "internal");
    char ext[MAX_PATH];
    int n = snprintf(ext, sizeof(ext), "%s/external", root);
    if (n > 0 && (size_t)n < sizeof(ext)) scan_directory(f, ext, "external");
    fputs("\n  ]\n}\n", f);
    int fail = ferror(f);
    if (fclose(f) != 0) fail = 1;
    if (fail) { status_file("ERROR: could not finish inventory.json\n"); return 1; }
    char msg[256];
    snprintf(msg, sizeof(msg), "Metadata records: %u\nScan errors: %u\nMetadata only: YES\nInstalled games confirmed: NO\n", g_count, g_errors);
    status_file(msg);
    printf("%s", msg);
    return 0;
}
