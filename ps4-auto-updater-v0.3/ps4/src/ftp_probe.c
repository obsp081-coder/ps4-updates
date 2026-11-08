
/*
 * PS4 Auto Updater v0.3.5
 * Read-only game metadata through GoldHEN FTP.
 * Creates inventory.json and ftp_scan.txt.
 * Does NOT install or modify games.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <orbis/Net.h>

#define OUT_DIR "/data/PS4AutoUpdater"
#define META_ROOT "/system_data/priv/appmeta"
#define MAX_GAMES 128
#define MAX_SFO (128U * 1024U)

typedef struct {
    char cusa[10];
    char title[256];
    char version[32];
    char content_id[128];
    char category[32];
    int metadata_ok;
} Game;

static Game games[MAX_GAMES];
static unsigned game_count = 0;
static FILE *report = NULL;

static void addr_local(OrbisNetSockaddr *a, unsigned port)
{
    memset(a, 0, sizeof(*a));
    a->len = (uint8_t)sizeof(*a);
    a->sa_family = ORBIS_NET_AF_INET;
    a->sa_data[0] = (char)(port >> 8);
    a->sa_data[1] = (char)(port & 255U);
    a->sa_data[2] = 127;
    a->sa_data[3] = 0;
    a->sa_data[4] = 0;
    a->sa_data[5] = 1;
}

static int send_all(OrbisNetId s, const char *p)
{
    size_t left = strlen(p);
    while (left) {
        int n = sceNetSend(s, p, left, 0);
        if (n <= 0) return -1;
        p += n;
        left -= (size_t)n;
    }
    return 0;
}

static int read_line(OrbisNetId s, char *dst, size_t cap)
{
    size_t used = 0;
    char c;
    if (!cap) return -1;

    for (;;) {
        int n = sceNetRecv(s, &c, 1, 0);
        if (n <= 0) return -1;
        if (c == '\n') break;
        if (c != '\r' && used + 1 < cap)
            dst[used++] = c;
    }

    dst[used] = 0;
    return 0;
}

static int read_reply(OrbisNetId s, char *line, size_t cap)
{
    if (read_line(s, line, cap)) return -1;

    if (strlen(line) < 4 ||
        !isdigit((unsigned char)line[0]) ||
        !isdigit((unsigned char)line[1]) ||
        !isdigit((unsigned char)line[2]))
        return -1;

    int code = (line[0]-'0') * 100 +
               (line[1]-'0') * 10 +
               line[2]-'0';

    if (line[3] == '-') {
        for (int i = 0; i < 40; ++i) {
            if (read_line(s, line, cap)) return -1;
            if (strlen(line) >= 4 &&
                line[3] == ' ' &&
                line[0] == ('0' + code/100) &&
                line[1] == ('0' + (code/10)%10) &&
                line[2] == ('0' + code%10))
                return code;
        }
        return -1;
    }
    return code;
}

static int ftp_cmd(
    OrbisNetId ctl,
    const char *cmd,
    char *reply)
{
    if (send_all(ctl, cmd)) return -1;
    return read_reply(ctl, reply, 512);
}

static OrbisNetId ftp_pasv(
    OrbisNetId ctl,
    char *reply)
{
    if (ftp_cmd(ctl, "PASV\r\n", reply) != 227)
        return -1;

    char *s = strchr(reply, '(');
    unsigned v[6];

    if (!s ||
        sscanf(s, "(%u,%u,%u,%u,%u,%u)",
               &v[0], &v[1], &v[2],
               &v[3], &v[4], &v[5]) != 6)
        return -1;

    for (unsigned i = 0; i < 6; ++i)
        if (v[i] > 255) return -1;

    unsigned port = v[4]*256 + v[5];
    if (!port) return -1;

    OrbisNetId data = sceNetSocket(
        "PS4UpdaterData",
        ORBIS_NET_AF_INET,
        ORBIS_NET_SOCK_STREAM,
        0
    );

    if (data < 0) return -1;

    OrbisNetSockaddr a;
    addr_local(&a, port);

    if (sceNetConnect(data, &a, sizeof(a)) != 0) {
        sceNetSocketClose(data);
        return -1;
    }
    return data;
}

static int ftp_transfer_start(
    OrbisNetId ctl,
    const char *cmd,
    OrbisNetId *data,
    char *reply)
{
    *data = ftp_pasv(ctl, reply);
    if (*data < 0) return -1;

    int code = ftp_cmd(ctl, cmd, reply);

    if (code != 150 && code != 125) {
        sceNetSocketClose(*data);
        *data = -1;
        return code == -1 ? -1 : -2;
    }
    return 0;
}

static int valid_cusa(const char *s)
{
    if (strlen(s) != 9 || strncmp(s, "CUSA", 4))
        return 0;

    for (int i = 4; i < 9; ++i)
        if (!isdigit((unsigned char)s[i]))
            return 0;

    return 1;
}

static void parse_listing_line(char *line)
{
    if (line[0] != 'd') return;

    size_t n = strlen(line);

    while (n && (line[n-1] == '\r' ||
                 line[n-1] == ' '))
        line[--n] = 0;

    char *last = strrchr(line, ' ');

    if (!last || !valid_cusa(last+1))
        return;

    for (unsigned i = 0; i < game_count; ++i)
        if (!strcmp(games[i].cusa, last+1))
            return;

    if (game_count < MAX_GAMES) {
        strcpy(games[game_count].cusa, last+1);
        ++game_count;
    }
}

static int ftp_list_games(OrbisNetId ctl)
{
    OrbisNetId data = -1;
    char reply[512];

    if (ftp_transfer_start(
            ctl,
            "LIST " META_ROOT "\r\n",
            &data, reply))
        return -1;

    char buf[2048], line[1024];
    size_t used = 0;
    int overflow = 0, bad = 0;
    unsigned total = 0;

    for (;;) {
        int n = sceNetRecv(
            data, buf, sizeof(buf), 0);

        if (n < 0) {
            bad = 1;
            break;
        }
        if (!n) break;

        total += (unsigned)n;
        if (total > 256U * 1024U) {
            bad = 1;
            break;
        }

        for (int i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                if (!overflow) {
                    line[used] = 0;
                    parse_listing_line(line);
                }
                used = 0;
                overflow = 0;
            }
            else if (used + 1 < sizeof(line)) {
                line[used++] = buf[i];
            }
            else {
                overflow = 1;
            }
        }
    }

    if (!bad && used && !overflow) {
        line[used] = 0;
        parse_listing_line(line);
    }

    sceNetSocketClose(data);

    if (bad) return -1;

    return read_reply(
        ctl, reply, sizeof(reply)) == 226 ? 0 : -1;
}

static int ftp_get_sfo(
    OrbisNetId ctl,
    const char *cusa,
    unsigned char *b,
    size_t cap,
    size_t *out_len)
{
    char cmd[256], reply[512];
    OrbisNetId data = -1;

    snprintf(
        cmd, sizeof(cmd),
        "RETR " META_ROOT "/%s/param.sfo\r\n",
        cusa
    );

    if (ftp_transfer_start(
            ctl, cmd, &data, reply))
        return -1;

    size_t size = 0;
    int overflow = 0, bad = 0;

    for (;;) {
        unsigned char chunk[4096];

        int n = sceNetRecv(
            data, chunk, sizeof(chunk), 0);

        if (n < 0) {
            bad = 1;
            break;
        }
        if (!n) break;

        if ((size_t)n > cap - size) {
            overflow = 1;
            continue;
        }

        if (!overflow) {
            memcpy(b + size, chunk, (size_t)n);
            size += (size_t)n;
        }
    }

    sceNetSocketClose(data);

    if (bad || overflow) return -1;

    int code = read_reply(
        ctl, reply, sizeof(reply));

    if (code != 226 && code != 250)
        return -1;

    *out_len = size;
    return 0;
}

static uint16_t le16(const unsigned char *p)
{
    return (uint16_t)(
        (unsigned)p[0] |
        ((unsigned)p[1] << 8)
    );
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int valid_range(
    size_t start,
    size_t len,
    size_t size)
{
    return start <= size && len <= size-start;
}

static void copy_sfo_string(
    char *dst,
    size_t cap,
    const unsigned char *src,
    size_t len)
{
    size_t i = 0;
    if (!cap) return;

    while (i < len &&
           i + 1 < cap &&
           src[i]) {
        dst[i] = (char)src[i];
        ++i;
    }

    dst[i] = 0;
}

static int parse_sfo(
    const unsigned char *b,
    size_t len,
    Game *g)
{
    if (len < 20 ||
        le32(b) != 0x46535000U)
        return -1;

    size_t keybase = le32(b+8);
    size_t valbase = le32(b+12);
    size_t count = le32(b+16);

    if (count > 1024 ||
        !valid_range(20, count*16, len) ||
        keybase >= len ||
        valbase >= len)
        return -1;

    char sfo_id[16] = {0};

    for (size_t i = 0; i < count; ++i) {
        const unsigned char *e = b + 20 + i*16;

        size_t ko = keybase + le16(e);
        size_t vo = valbase + le32(e+12);
        size_t n = le32(e+4);

        if (!valid_range(ko, 1, len) ||
            !valid_range(vo, n, len))
            continue;

        const char *key = (const char *)(b+ko);

        if (!memchr(key, 0, len-ko))
            continue;

        if (!strcmp(key, "TITLE_ID"))
            copy_sfo_string(
                sfo_id, sizeof(sfo_id), b+vo, n);

        else if (!strcmp(key, "TITLE"))
            copy_sfo_string(
                g->title, sizeof(g->title), b+vo, n);

        else if (!strcmp(key, "APP_VER"))
            copy_sfo_string(
                g->version, sizeof(g->version), b+vo, n);

        else if (!strcmp(key, "CONTENT_ID"))
            copy_sfo_string(
                g->content_id,
                sizeof(g->content_id), b+vo, n);

        else if (!strcmp(key, "CATEGORY"))
            copy_sfo_string(
                g->category,
                sizeof(g->category), b+vo, n);
    }

    if (strcmp(sfo_id, g->cusa) != 0)
        return -1;

    g->metadata_ok = 1;
    return 0;
}

static void json_string(FILE *f, const char *s)
{
    fputc('"', f);

    for (const unsigned char *p =
         (const unsigned char *)s; *p; ++p) {

        if (*p == '"' || *p == '\\') {
            fputc('\\', f);
            fputc(*p, f);
        }
        else if (*p < 0x20) {
            fprintf(f, "\\u%04x", (unsigned)*p);
        }
        else {
            fputc(*p, f);
        }
    }

    fputc('"', f);
}

static int write_inventory(void)
{
    FILE *out = fopen(
        OUT_DIR "/inventory.json", "wb");

    if (!out) return -1;

    fputs(
        "{\n"
        "  \"schema\": 4,\n"
        "  \"source\": \"goldhen-local-ftp\",\n"
        "  \"metadata_only\": true,\n"
        "  \"games\": [\n",
        out
    );

    for (unsigned i = 0; i < game_count; ++i) {
        Game *g = &games[i];

        if (i) fputs(",\n", out);

        fputs("    {\"cusa\":", out);
        json_string(out, g->cusa);

        fputs(",\"name\":", out);
        json_string(out, g->title);

        fputs(",\"app_ver\":", out);
        json_string(out, g->version);

        fputs(",\"content_id\":", out);
        json_string(out, g->content_id);

        fputs(",\"category\":", out);
        json_string(out, g->category);

        fprintf(
            out,
            ",\"metadata_available\":%s,"
            "\"installed_confirmed\":false}",
            g->metadata_ok ? "true" : "false"
        );
    }

    fputs("\n  ]\n}\n", out);

    int failed = ferror(out);
    if (fclose(out) != 0)
        failed = 1;

    return failed ? -1 : 0;
}

int main(void)
{
    if (mkdir(OUT_DIR, 0777) &&
        errno != EEXIST)
        return 1;

    report = fopen(
        OUT_DIR "/ftp_scan.txt", "wb");

    if (!report) return 2;

    fputs(
        "PS4 Auto Updater FTP Inventory v0.3.5\n"
        "READ ONLY\n",
        report
    );

    if (sceNetInit()) {
        fputs("Result: NET_INIT_FAILED\n", report);
        fclose(report);
        return 3;
    }

    OrbisNetId ctl = sceNetSocket(
        "PS4UpdaterCtl",
        ORBIS_NET_AF_INET,
        ORBIS_NET_SOCK_STREAM,
        0
    );

    if (ctl < 0) {
        fputs("Result: SOCKET_FAILED\n", report);
        fclose(report);
        return 4;
    }

    OrbisNetSockaddr a;
    addr_local(&a, 2121);

    char reply[512];
    int ok = 0;

    if (sceNetConnect(
            ctl, &a, sizeof(a)))
        goto end;

    if (read_reply(
            ctl, reply, sizeof(reply)) != 220)
        goto end;

    int c = ftp_cmd(
        ctl, "USER anonymous\r\n", reply);

    if (c != 230 && c != 331)
        goto end;

    if (c == 331 &&
        ftp_cmd(ctl, "PASS anonymous\r\n",
                reply) != 230)
        goto end;

    if (ftp_cmd(
            ctl, "TYPE I\r\n", reply) != 200)
        goto end;

    if (ftp_list_games(ctl))
        goto end;

    fprintf(
        report,
        "CUSA directories: %u\n",
        game_count
    );

    unsigned valid = 0;

    unsigned char *buf =
        (unsigned char *)malloc(MAX_SFO);

    if (!buf) goto end;

    for (unsigned i = 0; i < game_count; ++i) {
        size_t n = 0;

        if (ftp_get_sfo(
                ctl, games[i].cusa,
                buf, MAX_SFO, &n) == 0 &&
            parse_sfo(
                buf, n, &games[i]) == 0) {

            ++valid;

            fprintf(
                report,
                "OK %s | %s | %s\n",
                games[i].cusa,
                games[i].version,
                games[i].title
            );
        }
        else {
            fprintf(
                report,
                "SFO_FAILED %s\n",
                games[i].cusa
            );
        }
    }

    free(buf);

    fprintf(
        report,
        "Metadata parsed: %u/%u\n",
        valid, game_count
    );

    if (write_inventory())
        goto end;

    fputs(
        "Installed confirmed: NO (metadata only)\n"
        "Result: INVENTORY_OK\n",
        report
    );

    ok = 1;

end:
    if (!ok)
        fputs("Result: FTP_OR_FILE_ERROR\n", report);

    send_all(ctl, "QUIT\r\n");
    sceNetSocketClose(ctl);
    fclose(report);

    return ok ? 0 : 5;
}
