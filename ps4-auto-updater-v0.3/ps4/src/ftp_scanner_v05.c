
#define _POSIX_C_SOURCE 200809L

/*
 * PS4 Auto Updater v0.5
 * GoldHEN FTP loopback inventory scanner.
 * READ ONLY: no uploads, deletion, downloads or installs.
 */

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __ORBIS__
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>
#include <orbis/AppInstUtil.h>
#include <orbis/libkernel.h>
typedef OrbisNetId sock_t;
#define NET_BAD (-1)
#define NET_RECV sceNetRecv
#define NET_SEND sceNetSend
#define NET_CLOSE sceNetSocketClose
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define NET_BAD (-1)
#define NET_RECV recv
#define NET_SEND send
#define NET_CLOSE close
#endif

#define ROOT "/data/PS4AutoUpdater"
#define MAX_GAMES 256
#define MAX_SFO (256 * 1024)
#define MAX_LIST (128 * 1024)
#define FTP_PORT 2121
#define IO_TIMEOUT_SEC 4

typedef struct {
    char cusa[10];
    char name[192];
    char version[20];
    char cid[80];
    char category[12];
    char source[12];
    int installed;
} Game;

static Game games[MAX_GAMES];
static unsigned found;
static FILE *report;
static int errors;
static int api_stage = -1;
static int ftp_login_ok = 0;
static int primary_listing_ok = 0;
static unsigned directory_candidates = 0;
static unsigned sfo_invalid = 0;
typedef struct {
    char cusa[10];
    char source[12];
} DirectoryCandidate;
static DirectoryCandidate candidate_list[MAX_GAMES];
static unsigned candidate_count = 0;

static void logmsg(const char *name, int value) {
    if (report) {
        fprintf(report, "%s: %d\n", name, value);
        fflush(report);
    }
}

static int valid_id(const char *s) {
    if (strlen(s) != 9 || memcmp(s, "CUSA", 4))
        return 0;

    for (int i = 4; i < 9; ++i)
        if (!isdigit((unsigned char)s[i]))
            return 0;

    return 1;
}

/* PS4/FPKG Content IDs encode the title at offset 7:
 * XX0000-CUSA12345_00-XXXXXXXXXXXXXXXX. A substring elsewhere is
 * not reliable proof that the metadata belongs to this title. */
static int content_id_matches(const char *cid, const char *id) {
    if (strlen(cid) < 17 || !valid_id(id))
        return 0;
    if (!isupper((unsigned char)cid[0]) ||
        !isupper((unsigned char)cid[1]) ||
        !isdigit((unsigned char)cid[2]) ||
        !isdigit((unsigned char)cid[3]) ||
        !isdigit((unsigned char)cid[4]) ||
        !isdigit((unsigned char)cid[5]) ||
        cid[6] != '-' || cid[16] != '_')
        return 0;
    return memcmp(cid + 7, id, 9) == 0;
}

/* Directory observations are NOT proof of installed applications. */
static void remember_candidate(const char *id, const char *source) {
    for (unsigned i = 0; i < candidate_count; ++i)
        if (!strcmp(candidate_list[i].cusa, id))
            return;

    if (candidate_count == MAX_GAMES)
        return;

    DirectoryCandidate *c = &candidate_list[candidate_count++];
    memcpy(c->cusa, id, 10);
    snprintf(c->source, sizeof c->source, "%s", source);
}

static int duplicate(const char *s) {
    for (unsigned i = 0; i < found; ++i)
        if (!strcmp(games[i].cusa, s))
            return 1;
    return 0;
}

static uint16_t u16(const unsigned char *p) {
    return (uint16_t)(
        (uint16_t)p[0] |
        ((uint16_t)p[1] << 8)
    );
}

static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

static int bounds(
    size_t off,
    size_t len,
    size_t size
) {
    return off <= size && len <= size - off;
}

static void value(
    char *dst,
    size_t cap,
    const unsigned char *v,
    size_t n
) {
    size_t i = 0;
    if (!cap) return;

    while (i < n && i + 1 < cap && v[i]) {
        dst[i] = (char)v[i];
        ++i;
    }
    dst[i] = 0;
}

/*
 * 1=game, 2=valid non-game/patch, 3=duplicate.
 * Negative: -30 empty, -31 bad magic, -32 bad header offsets,
 * -33 missing/invalid ID, -34 title-id mismatch, -35 content mismatch,
 * -36 inventory capacity.
 * Presence of an SFO does NOT confirm that an app is installed.
 */
static int parse_sfo(
    const unsigned char *p,
    size_t size,
    const char *id,
    const char *source
) {
    if (size == 0) return -30;
    if (size < 20 || u32(p) != 0x46535000u) return -31;

    size_t keys = u32(p + 8);
    size_t vals = u32(p + 12);
    size_t count = u32(p + 16);

    if (
        count == 0 || count > 1024 ||
        !bounds(20, count * 16, size) ||
        keys < 20 + count * 16 ||
        keys >= vals || vals >= size
    ) return -32;

    Game g;
    memset(&g, 0, sizeof g);
    g.installed = -1;

    for (size_t i = 0; i < count; ++i) {
        const unsigned char *entry = p + 20 + i * 16;
        size_t key_off = keys + (size_t)u16(entry);
        size_t val_off = vals + (size_t)u32(entry + 12);
        size_t len = (size_t)u32(entry + 4);
        uint16_t fmt = u16(entry + 2);

        if (
            key_off >= vals ||
            !bounds(val_off, len, size) ||
            !memchr(p + key_off, 0, vals - key_off)
        ) continue;

        if (fmt != 0x0204u && fmt != 0x0004u)
            continue;

        const char *key = (const char *)(p + key_off);
        if (!strcmp(key, "TITLE_ID"))
            value(g.cusa, sizeof g.cusa, p + val_off, len);
        else if (!strcmp(key, "TITLE"))
            value(g.name, sizeof g.name, p + val_off, len);
        else if (!strcmp(key, "APP_VER"))
            value(g.version, sizeof g.version, p + val_off, len);
        else if (!strcmp(key, "CONTENT_ID"))
            value(g.cid, sizeof g.cid, p + val_off, len);
        else if (!strcmp(key, "CATEGORY"))
            value(g.category, sizeof g.category, p + val_off, len);
    }

    if (report) {
        fprintf(
            report,
            "SFO FIELDS %s title_id=%.9s category=%.11s "
            "app_ver=%.19s cid_matches=%d\n",
            id, g.cusa[0] ? g.cusa : "(missing)",
            g.category[0] ? g.category : "(missing)",
            g.version[0] ? g.version : "(missing)",
            g.cid[0] ? content_id_matches(g.cid, id) : -1
        );
        fflush(report);
    }

    /* If TITLE_ID is absent, CONTENT_ID may independently confirm
       the directory ID. The directory alone is insufficient. */
    if (!g.cusa[0] && g.cid[0] && content_id_matches(g.cid, id)) {
        memcpy(g.cusa, id, 10);
        if (report)
            fprintf(report, "SFO ID from CONTENT_ID: %s\n", id);
    }

    if (!valid_id(g.cusa)) return -33;
    if (strcmp(g.cusa, id)) return -34;
    if (g.cid[0] && !content_id_matches(g.cid, id)) return -35;

    /* Keep playable base-game categories aligned with the project's
       PKG inspector: gd, gda and gdo. gde is a demo, also playable.
       gp/gdp are patches; ac is an add-on, not a base game. */
    if (
        g.category[0] &&
        strcmp(g.category, "gd") &&
        strcmp(g.category, "gda") &&
        strcmp(g.category, "gdo") &&
        strcmp(g.category, "gde")
    ) return 2;

    if (found >= MAX_GAMES) return -36;
    if (duplicate(id)) return 3;

    snprintf(g.source, sizeof g.source, "%s", source);
    games[found++] = g;
    return 1;
}

static void json_str(FILE *fp, const char *s) {
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

/*
 * Connect only to loopback.
 * PASV-provided addresses are deliberately ignored.
 */
static sock_t connect_local(unsigned port) {
#ifdef __ORBIS__
    sock_t fd = sceNetSocket(
        "ps4auto_ftp",
        ORBIS_NET_AF_INET,
        ORBIS_NET_SOCK_STREAM,
        0
    );

    if (fd < 0)
        return NET_BAD;

    struct {
        uint8_t len, family;
        uint16_t port;
        uint32_t addr;
        uint8_t zero[8];
    } addr = {0};

    addr.len = (uint8_t)sizeof(addr);
    addr.family = ORBIS_NET_AF_INET;
    addr.port = sceNetHtons((uint16_t)port);

    if (
        sceNetInetPton(
            ORBIS_NET_AF_INET,
            "127.0.0.1",
            &addr.addr
        ) != 1 ||
        sceNetConnect(
            fd,
            (const OrbisNetSockaddr *)&addr,
            sizeof(addr)
        ) < 0
    ) {
        NET_CLOSE(fd);
        return NET_BAD;
    }

    int32_t usec = IO_TIMEOUT_SEC * 1000000;

    (void)sceNetSetsockopt(
        fd, 0xffff, 0x1006, &usec, sizeof(usec)
    );
    (void)sceNetSetsockopt(
        fd, 0xffff, 0x1005, &usec, sizeof(usec)
    );

    return fd;
#else
    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
        return NET_BAD;

    struct timeval tv = { IO_TIMEOUT_SEC, 0 };

    (void)setsockopt(
        fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv
    );
    (void)setsockopt(
        fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv
    );

    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(0x7f000001U);

    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        NET_CLOSE(fd);
        return NET_BAD;
    }

    return fd;
#endif
}

static int send_all(
    sock_t sock,
    const char *s,
    size_t n
) {
    while (n) {
        int z = NET_SEND(sock, s, n, 0);

        if (z <= 0)
            return 0;

        s += (size_t)z;
        n -= (size_t)z;
    }

    return 1;
}

static int readline(
    sock_t fd,
    char *line,
    size_t cap
) {
    size_t n = 0;

    for (;;) {
        char c;
        int received = NET_RECV(fd, &c, 1, 0);

        if (received != 1) {
            /* 0 = peer closed, negative = socket error / timeout. */
            if (report) {
                fprintf(report, "FTP_CONTROL_RECV_RESULT: %d (hex=0x%08x)\n",
                        received, (unsigned)received);
                fflush(report);
            }
            return -1;
        }

        if (c == '\n')
            break;

        if (n + 1 >= cap)
            return -1;

        if (c != '\r')
            line[n++] = c;
    }

    line[n] = 0;
    return (int)n;
}

static int reply(
    sock_t fd,
    char *line,
    size_t cap
) {
    /* A numeric FTP reply needs three digits and a delimiter. */
    if (readline(fd, line, cap) < 4)
        return -1;

    if (
        !isdigit((unsigned char)line[0]) ||
        !isdigit((unsigned char)line[1]) ||
        !isdigit((unsigned char)line[2])
    ) return -1;

    int code =
        (line[0] - '0') * 100 +
        (line[1] - '0') * 10 +
        line[2] - '0';

    if (line[3] == '-') {
        for (int i = 0; i < 60; ++i) {
            if (readline(fd, line, cap) < 0)
                return -1;

            if (
                line[0] == (char)('0' + code / 100) &&
                line[1] == (char)('0' + code / 10 % 10) &&
                line[2] == (char)('0' + code % 10) &&
                line[3] == ' '
            ) return code;
        }

        return -1;
    }

    return code;
}

static int command(
    sock_t fd,
    char *replybuf,
    size_t cap,
    const char *cmd
) {
    size_t n = strlen(cmd);
    char wire[384];

    /*
     * Important: Some PS4 FTP implementations process one recv() as
     * one complete command and do NOT accumulate until CRLF.
     * The old two-send implementation sent "TYPE I" and CRLF in
     * separate TCP writes: a standalone CRLF became FTP 500, and
     * the next PASV read found the connection dead/out of sync.
     * Frame the command and CRLF in ONE send. Under normal sockets
     * the entire short write is transmitted together.
     */
    if (n > sizeof(wire) - 3)
        return -1;

    memcpy(wire, cmd, n);
    wire[n] = '\r';
    wire[n + 1] = '\n';

    if (!send_all(fd, wire, n + 2)) {
        logmsg("FTP_CONTROL_SEND_FAILED", 1);
        return -1;
    }

    return reply(fd, replybuf, cap);
}

/*
 * GoldHEN deployments differ: some allow TYPE I on a fresh control
 * connection, while others require USER anonymous first. Try the
 * no-credential path before sending a USER command; only negotiate
 * authentication after an explicit FTP 530 response. Never guess
 * passwords, send extra FTP feature probes, or print credentials.
 */
static int ftp_auth_attempted = 0;

/*
 * For this PS4 GoldHEN version avoid TYPE, FEAT, SYST, PWD and other
 * optional commands. The FTPS4/GoldHEN-style server already streams
 * raw file bytes. We start with PASV, and authenticate only if PASV
 * explicitly returns 530. There is no need to negotiate TYPE I.
 */
static int anonymous_auth(sock_t fd) {
    char line[512];
    if (ftp_auth_attempted)
        return 0;
    ftp_auth_attempted = 1;

    int code = command(fd, line, sizeof line, "USER anonymous");
    logmsg("FTP_USER_CODE", code);

    if (code == 331) {
        code = command(fd, line, sizeof line, "PASS guest@");
        logmsg("FTP_PASS_CODE", code);
    }
    if (code != 230 && code != 202)
        return 0;

    logmsg("FTP_AUTH_MODE (0=no login,1=anonymous)", 1);
    return 1;
}

/*
 * The FTP greeting is not authentication. A PASS/USER is only used
 * when PASV responds with 530. The initial control request is PASV.
 */
static int login(sock_t fd) {
    char line[512];
    int code = reply(fd, line, sizeof line);
    if (code == 120) {
        logmsg("FTP_GREETING_WAIT_CODE", code);
        code = reply(fd, line, sizeof line);
    }
    logmsg("FTP_GREETING_CODE", code);
    if (report && code == 220) {
        fprintf(report, "FTP_GREETING_TEXT: %.180s\n", line);
        fflush(report);
    }
    if (code != 220)
        return 0;
    logmsg("FTP_COMMAND_MODE (0=PASV-first, no TYPE)", 0);
    return 1;
}

static int pasv_port(const char *line) {
    const char *p = strchr(line, '(');
    int a[6], n = 0;

    if (!p)
        return -1;

    ++p;

    for (int i = 0; i < 6; ++i) {
        while (*p == ' ')
            ++p;

        if (!isdigit((unsigned char)*p))
            return -1;

        n = 0;

        while (isdigit((unsigned char)*p)) {
            n = n * 10 + (*p++ - '0');
            if (n > 255)
                return -1;
        }

        a[i] = n;

        if (i < 5 && *p++ != ',')
            return -1;
    }

    if (*p != ')')
        return -1;

    int port = a[4] * 256 + a[5];

    return port >= 1024 ? port : -1;
}

static int transfer(
    sock_t control,
    const char *cmd,
    unsigned char *buf,
    size_t cap,
    size_t *actual
) {
    char line[512];
    *actual = 0;

    int code = command(
        control, line, sizeof line, "PASV"
    );
    logmsg("FTP_PASV_CODE", code);

    /* TYPE may be unsupported and PASV may be the first auth check. */
    if (code == 530 && anonymous_auth(control)) {
        code = command(control, line, sizeof line, "PASV");
        logmsg("FTP_PASV_AFTER_AUTH_CODE", code);
    }

    if (code != 227)
        return -10;

    int port = pasv_port(line);
    logmsg("FTP_PASV_PORT", port);

    if (port < 0)
        return -11;

    sock_t data = connect_local((unsigned)port);

    if (data == NET_BAD) {
        logmsg("FTP_DATA_CONNECT_FAILED", port);
        return -12;
    }

    code = command(
        control, line, sizeof line, cmd
    );
    if (report) {
        fprintf(report, "FTP_READ_COMMAND: %s\n", cmd);
        fprintf(report, "FTP_READ_START_CODE: %d\n", code);
        fflush(report);
    }

    if (code != 125 && code != 150) {
        NET_CLOSE(data);
        return -13;
    }

    int overflow = 0;
    int fail = 0;
    size_t discarded = 0;
    unsigned char discard[2048];

    /*
     * Read until EOF. Never interpret a truncated LIST or SFO, and
     * cap the amount discarded after the caller's buffer fills.
     */
    for (;;) {
        unsigned char *destination;
        size_t available;

        if (*actual < cap) {
            destination = buf + *actual;
            available = cap - *actual;
        } else {
            destination = discard;
            available = sizeof(discard);
        }

        int nr = NET_RECV(data, destination, available, 0);
        if (nr < 0) {
            fail = 1;
            break;
        }
        if (nr == 0)
            break;

        if (*actual < cap) {
            *actual += (size_t)nr;
        } else {
            overflow = 1;
            discarded += (size_t)nr;
            if (discarded >= cap)
                break;
        }
    }

    NET_CLOSE(data);

    code = reply(control, line, sizeof line);
    if (report) {
        fprintf(report, "FTP_READ_END_CODE: %d\n", code);
        fflush(report);
    }

    /* Preserve the most informative diagnostic on oversized files. */
    if (overflow)
        return -16;
    if (fail)
        return -15;
    if (code != 226 && code != 250)
        return -14;
    return 0;
}

static int listing_names(
    const unsigned char *data,
    size_t size,
    char ids[MAX_GAMES][10]
) {
    size_t off = 0;
    int count = 0;

    while (off < size && count < MAX_GAMES) {
        size_t end = off;

        while (end < size && data[end] != '\n')
            ++end;

        size_t e = end;

        while (
            e > off &&
            (data[e - 1] == '\r' || data[e - 1] == ' ')
        ) --e;

        size_t start = e;

        while (
            start > off &&
            data[start - 1] != ' ' &&
            data[start - 1] != '\t'
        ) --start;

        if (e - start == 9) {
            char s[10];
            memcpy(s, data + start, 9);
            s[9] = 0;

            if (valid_id(s)) {
                int seen = 0;

                for (int i = 0; i < count; ++i)
                    if (!strcmp(ids[i], s))
                        seen = 1;

                if (!seen) {
                    memcpy(ids[count], s, 10);
                    ++count;
                }
            }
        }

        off = end == size ? size : end + 1;
    }

    return count;
}

static int scan_root(
    sock_t control,
    const char *path,
    const char *source,
    int optional
) {
    static unsigned char listing[MAX_LIST];
    size_t bytes = 0;
    char cmd[320];

    if (
        snprintf(
            cmd, sizeof cmd, "LIST %s", path
        ) >= (int)sizeof cmd
    ) return -1;

    int status = transfer(
        control, cmd, listing, sizeof listing, &bytes
    );

    if (report)
        fprintf(
            report,
            "LIST %s: %d, bytes=%lu\n",
            source,
            status,
            (unsigned long)bytes
        );

    if (status != 0) {
        if (!optional)
            ++errors;
        return -1;
    }

    if (!optional)
        primary_listing_ok = 1;

    static char ids[MAX_GAMES][10];

    int count = listing_names(
        listing, bytes, ids
    );

    if (report)
        fprintf(
            report,
            "LIST candidate %s: %d\n",
            source, count
        );

    if (count > 0)
        directory_candidates += (unsigned)count;

    for (int i = 0; i < count; ++i)
        remember_candidate(ids[i], source);

    static unsigned char sfo[MAX_SFO];

    for (int i = 0; i < count; ++i) {
        if (duplicate(ids[i]))
            continue;

        if (
            snprintf(
                cmd,
                sizeof cmd,
                "RETR %s/%s/param.sfo",
                path,
                ids[i]
            ) >= (int)sizeof cmd
        ) {
            ++errors;
            continue;
        }

        bytes = 0;

        status = transfer(
            control, cmd, sfo, sizeof sfo, &bytes
        );

        if (status == 0) {
            /* Log only the SFO header and selected metadata fields, never
             * game file contents or private SFO fields. */
            if (report) {
                fprintf(
                    report,
                    "SFO FILE %s bytes=%lu magic=0x%08x "
                    "entries=%u keys=%u values=%u\n",
                    ids[i], (unsigned long)bytes,
                    bytes >= 4 ? u32(sfo) : 0,
                    bytes >= 20 ? u32(sfo + 16) : 0,
                    bytes >= 20 ? u32(sfo + 8) : 0,
                    bytes >= 20 ? u32(sfo + 12) : 0
                );
                fflush(report);
            }

            int parsed = parse_sfo(sfo, bytes, ids[i], source);
            if (parsed < 0) {
                status = parsed;
                ++sfo_invalid;
            }
        }

        if (status != 0) {
            ++errors;

            if (report)
                fprintf(
                    report,
                    "SFO %s: %d\n",
                    ids[i],
                    status
                );
        }
    }

    return count;
}

static void confirm_games(void) {
#ifdef __ORBIS__
    uint32_t load =
        sceSysmoduleLoadModuleInternal(
            ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL
        );

    if (report)
        fprintf(
            report,
            "AppInstUtil module load: 0x%08x\n",
            load
        );

    if (load != 0) {
        /* 0x80020008 = kernel ENOEXEC; do not claim app absent. */
        if (load == 0x80020008u)
            logmsg("AppInstUtil ENOEXEC", 1);
        api_stage = 1;
        return;
    }

    int32_t init = sceAppInstUtilInitialize();

    if (report)
        fprintf(
            report,
            "AppInstUtil init: 0x%08x\n",
            (uint32_t)init
        );

    if (init != 0) {
        api_stage = 2;
        return;
    }

    api_stage = 0;

    for (unsigned i = 0; i < found; ++i) {
        int32_t exists = -1;

        int32_t rc = sceAppInstUtilAppExists(
            games[i].cusa,
            &exists
        );

        if (rc == 0)
            games[i].installed = exists ? 1 : 0;
    }

    int32_t done = sceAppInstUtilTerminate();

    if (report)
        fprintf(
            report,
            "AppInstUtil term: 0x%08x\n",
            (uint32_t)done
        );
#else
    api_stage = -1;
#endif
}

static int save_inventory(const char *path) {
    FILE *fp = fopen(path, "wb");

    if (!fp)
        return -1;

    fputs(
        "{\n"
        "  \"schema\":5,\n"
        "  \"source\":\"ftp_loopback_readonly_v061\",\n"
        "  \"metadata_only\":true,\n"
        "  \"scan_complete\":",
        fp
    );
    fputs(
        ftp_login_ok && primary_listing_ok && errors == 0
            ? "true" : "false",
        fp
    );
    fputs(
        ",\n"
        "  \"download_enabled\":false,\n"
        "  \"install_enabled\":false,\n"
        "  \"directory_candidates\":[\n",
        fp
    );

    for (unsigned i = 0; i < candidate_count; ++i) {
        if (i)
            fputs(",\n", fp);
        fputs("    {\"cusa\":", fp);
        json_str(fp, candidate_list[i].cusa);
        fputs(",\"source\":", fp);
        json_str(fp, candidate_list[i].source);
        fputs(duplicate(candidate_list[i].cusa)
                  ? ",\"metadata_verified\":true}"
                  : ",\"metadata_verified\":false}", fp);
    }

    fputs("\n  ],\n  \"games\":[\n", fp);

    for (unsigned i = 0; i < found; ++i) {
        Game *g = &games[i];

        if (i)
            fputs(",\n", fp);

        fputs("    {\"cusa\":", fp);
        json_str(fp, g->cusa);

        fputs(",\"name\":", fp);
        json_str(fp, g->name);

        fputs(",\"app_ver\":", fp);
        json_str(fp, g->version);

        fputs(",\"content_id\":", fp);
        json_str(fp, g->cid);

        fputs(",\"category\":", fp);
        json_str(fp, g->category);

        fputs(",\"source\":", fp);
        json_str(fp, g->source);

        fputs(",\"installed_confirmed\":", fp);
        fputs(
            g->installed == 1 ? "true" : "false",
            fp
        );

        fputs(",\"install_state\":", fp);
        fputs(
            g->installed == 1
                ? "\"INSTALLED\""
                : g->installed == 0
                    ? "\"NOT_INSTALLED\""
                    : "\"UNKNOWN\"",
            fp
        );

        fputc('}', fp);
    }

    fputs("\n  ]\n}\n", fp);

    int ok = !ferror(fp);

    if (fclose(fp) != 0)
        ok = 0;

    return ok ? 0 : -1;
}

int main(void) {
#ifdef __ORBIS__
    if (
        mkdir(ROOT, 0777) != 0 &&
        errno != EEXIST
    ) return 2;

    /* Keep v0.5 diagnostics intact for side-by-side comparison. */
    const char *outpath =
        ROOT "/inventory_v061.json";

    const char *reportpath =
        ROOT "/diagnostic_v061.txt";

    int netstatus = sceNetInit();
    int pool = -1;

    if (
        netstatus == 0 ||
        (uint32_t)netstatus == 0x80410110u
    )
        pool = sceNetPoolCreate(
            "ps4auto_ftp",
            128 * 1024,
            0
        );
#else
    const char *outpath =
        getenv("PS4_FTP_TEST_OUT");

    if (!outpath)
        outpath = "inventory_v05.json";

    const char *reportpath =
        getenv("PS4_FTP_TEST_REPORT");

    if (!reportpath)
        reportpath = "diagnostic_v05.txt";

    int netstatus = 0;
    int pool = 0;
#endif

    report = fopen(reportpath, "wb");

    if (report) {
        fprintf(
            report,
            "PS4 Auto Updater v0.6.1 FTP loopback READ ONLY\n"
            "Network initialize: 0x%08x\n"
            "Network pool: %d\n",
            (unsigned)netstatus,
            pool
        );
        fflush(report);
    }

    if (pool < 0) {
        logmsg("NET_UNAVAILABLE", pool);
        ++errors;
    } else {
        unsigned port = FTP_PORT;

#ifndef __ORBIS__
        const char *test =
            getenv("PS4_FTP_TEST_PORT");

        if (test && *test) {
            char *end;

            unsigned long v = strtoul(
                test, &end, 10
            );

            if (
                !*end &&
                v > 1023 &&
                v <= 65535
            ) port = (unsigned)v;
        }
#endif

        sock_t control = connect_local(port);

        if (control == NET_BAD) {
            logmsg("CONTROL_CONNECT_FAILED", -1);
            ++errors;
        } else {
            logmsg("FTP_CONTROL_CONNECT_OK", 1);
            int authenticated = login(control);

            /* Greeting accepted: authentication may not be necessary
             * and may still fail later at PASV. */
            logmsg("FTP_GREETING_ACCEPTED", authenticated);
            ftp_login_ok = authenticated;

            if (authenticated) {
                scan_root(
                    control,
                    "/system_data/priv/appmeta",
                    "internal",
                    0
                );

                scan_root(
                    control,
                    "/system_data/priv/appmeta/external",
                    "external",
                    1
                );
            } else {
                ++errors;
            }

            char line[512];

            (void)command(
                control,
                line,
                sizeof line,
                "QUIT"
            );

            NET_CLOSE(control);
        }
    }

    confirm_games();

    int saved = save_inventory(outpath);

    unsigned confirmed = 0;
    unsigned unknown = 0;

    for (unsigned i = 0; i < found; ++i) {
        if (games[i].installed == 1)
            ++confirmed;

        if (games[i].installed < 0)
            ++unknown;
    }

    logmsg("Directory candidates", (int)candidate_count);
    logmsg("Directory listings observed", (int)directory_candidates);
    logmsg("SFO invalid", (int)sfo_invalid);
    logmsg("Metadata records", (int)found);
    logmsg("Installed confirmed", (int)confirmed);
    logmsg("Installation unknown", (int)unknown);

    logmsg(
        "AppInstUtil status (0=OK,-1=unavailable)",
        api_stage
    );

    logmsg("Scan errors", errors);
    logmsg("Primary listing available", primary_listing_ok);
    logmsg("Scan complete", ftp_login_ok && primary_listing_ok && errors == 0);
    logmsg("Inventory saved (0=OK)", saved);

    if (report)
        fclose(report);

    return saved == 0 ? 0 : 1;
}
