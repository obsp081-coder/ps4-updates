
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
#include "scanner_ui.h"
#include "ui_snapshot.h"
#include "ui_png_transport.h"
#include "ui_image.h"
#include "ui_icon_cache.h"
#include "ps4_shell_exit.h"
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
#ifndef MAX_GAMES
#define MAX_GAMES 256
#endif
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
/* All validated metadata, including gp/gpe/ac. Not install proof. */
static Game metadata_records[MAX_GAMES];
static unsigned metadata_found;
static FILE *report;
static int errors;
static int api_stage = -1;
static int ftp_login_ok = 0;
static int primary_listing_ok = 0;
static int external_listing_ok = 0;
static unsigned directory_candidates = 0;
static unsigned sfo_invalid = 0;
typedef struct {
    char cusa[10];
    char source[12];
} DirectoryCandidate;
static DirectoryCandidate candidate_list[MAX_GAMES];
static unsigned candidate_count = 0;
static int candidate_overflow = 0;

#ifdef __ORBIS__
/* Single producer (FTP worker) / single consumer (VideoOut main thread).
 * Only scan_status/scan_abort are shared while scanning.
 * Completed game records are consumed AFTER scePthreadJoin. */
static volatile uint32_t scan_status;
static volatile int scan_abort;
static int scan_native_ui;
static int scan_run_rc;
static int scan_inventory_saved;
static int scan_inventory_complete;
static ScannerUiEntry ui_items[MAX_GAMES];

/* One-slot SPSC mailbox: UI owns requests and LRU; cover worker owns
 * FTP, CRC and decode. State is published with release/acquire atomics.
 * 0 idle, 1 requested, 2 worker decoding, 3 result ready. No heap. */
static volatile uint32_t cover_state;
static volatile uint32_t cover_stop;
static volatile uint32_t cover_online; /* 0 starting, 1 ready, 2 stopped */
static unsigned cover_index;
static int cover_result;
static uint32_t cover_pixels[UI_ICON_PIXELS];
static unsigned char cover_failed[MAX_GAMES]; /* UI thread only */

static int scan_should_abort(void) {
    return __atomic_load_n(&scan_abort,__ATOMIC_ACQUIRE)!=0;
}
static void scan_publish_progress(unsigned current,unsigned total) {
    const uint32_t packed=((current & 0x7FFFu)<<16) | (total & 0xFFFFu);
    __atomic_store_n(&scan_status,packed,__ATOMIC_RELEASE);
}
#endif

static void logmsg(const char *name, int value) {
    if (report) {
        fprintf(report, "%s: %d\n", name, value);
        fflush(report);
    }
}

static int metadata_index(const char *cusa) {
    for (unsigned i = 0; i < metadata_found; ++i)
        if (strcmp(metadata_records[i].cusa, cusa) == 0)
            return (int)i;
    return -1;
}

static const char *metadata_kind(const char *category) {
    if (!strcmp(category, "gd") || !strcmp(category, "gda") ||
        !strcmp(category, "gdo") || !strcmp(category, "gde"))
        return "base_game";
    if (!strcmp(category, "gp") || !strcmp(category, "gdp"))
        return "patch_metadata";
    if (!strcmp(category, "ac"))
        return "dlc_metadata";
    return "other_metadata";
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
    /* A valid PS4 Content ID is exactly 36 characters:
       XX0000-CUSA12345_00-XXXXXXXXXXXXXXXX. */
    if (strlen(cid) != 36 || !valid_id(id))
        return 0;
    if (!isupper((unsigned char)cid[0]) ||
        !isupper((unsigned char)cid[1]) ||
        !isdigit((unsigned char)cid[2]) ||
        !isdigit((unsigned char)cid[3]) ||
        !isdigit((unsigned char)cid[4]) ||
        !isdigit((unsigned char)cid[5]) ||
        cid[6] != '-' || cid[16] != '_' ||
        !isdigit((unsigned char)cid[17]) ||
        !isdigit((unsigned char)cid[18]) || cid[19] != '-')
        return 0;
    for (size_t i = 20; i < 36; ++i)
        if (!isupper((unsigned char)cid[i]) &&
            !isdigit((unsigned char)cid[i]))
            return 0;
    return memcmp(cid + 7, id, 9) == 0;
}

/* Directory observations are NOT proof of installed applications. */
static void remember_candidate(const char *id, const char *source) {
    for (unsigned i = 0; i < candidate_count; ++i)
        if (!strcmp(candidate_list[i].cusa, id))
            return;

    if (candidate_count == MAX_GAMES) {
        if (!candidate_overflow) {
            candidate_overflow = 1;
            ++errors;
            logmsg("CANDIDATE_CAPACITY_EXCEEDED", 1);
        }
        return;
    }

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

/* Preserve JSON-valid UTF-8 even if a long title must be truncated.
 * Invalid UTF-8 becomes '?'; never split a multibyte code point. */
static size_t utf8_size(const unsigned char *p, size_t n) {
    if (n == 0 || p[0] == 0) return 0;
    if (p[0] < 0x80) return 1;
    size_t l = 0;
    if (p[0] >= 0xC2 && p[0] <= 0xDF) l = 2;
    else if (p[0] >= 0xE0 && p[0] <= 0xEF) l = 3;
    else if (p[0] >= 0xF0 && p[0] <= 0xF4) l = 4;
    else return 0;
    if (l > n) return 0;
    for (size_t i = 1; i < l; ++i)
        if (p[i] < 0x80 || p[i] > 0xBF) return 0;
    if (l == 3 && p[0] == 0xE0 && p[1] < 0xA0) return 0;
    if (l == 3 && p[0] == 0xED && p[1] >= 0xA0) return 0;
    if (l == 4 && p[0] == 0xF0 && p[1] < 0x90) return 0;
    if (l == 4 && p[0] == 0xF4 && p[1] >= 0x90) return 0;
    return l;
}

static void value(
    char *dst, size_t cap, const unsigned char *v, size_t n
) {
    size_t i = 0, written = 0;
    if (!cap) return;
    while (i < n && v[i] && written + 1 < cap) {
        size_t len = utf8_size(v + i, n - i);
        if (!len) {
            dst[written++] = '?';
            ++i;
            continue;
        }
        if (written + len >= cap) break;
        memcpy(dst + written, v + i, len);
        written += len;
        i += len;
    }
    dst[written] = 0;
}

/*
 * 1=game, 2=valid non-game/patch, 3=duplicate.
 * Negative: -30 empty, -31 bad magic, -32 bad header offsets,
 * -33 missing/invalid ID, -34 title-id mismatch, -35 content mismatch,
 * -36 inventory capacity, -37 missing CATEGORY,
 * -38 duplicate/conflicting metadata from different roots.
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

    /* Validate category for every record. Do not falsely describe
     * patch/add-on metadata as missing or invalid simply because it
     * is not a playable base-game category. */
    if (!g.category[0]) return -37;
    if (metadata_index(id) >= 0) return -38;
    if (metadata_found >= MAX_GAMES) return -36;
    snprintf(g.source, sizeof g.source, "%s", source);
    if (!strcmp(metadata_kind(g.category), "base_game")) {
        if (found >= MAX_GAMES) return -36;
        if (duplicate(id)) return -38;
        games[found++] = g;
    }
    metadata_records[metadata_found++] = g;
    return !strcmp(metadata_kind(g.category), "base_game") ? 1 : 2;
}

/* Recover CRLF-expanded SFO only after raw validation fails.
 * This preserves console files and revalidates every recovered record. */
static size_t unexpand_ascii_newlines(
    const unsigned char *src, size_t size, unsigned char *dst
) {
    size_t written = 0;
    for (size_t i = 0; i < size; ++i) {
        if (src[i] == '\r' && i + 1 < size && src[i + 1] == '\n')
            continue;
        dst[written++] = src[i];
    }
    return written;
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
    /* A numeric FTP reply needs three digits and a valid delimiter.
     * Accepting 220X or reusing leftover bytes from a short multiline
     * response could falsely report a successful handshake. */
    if (readline(fd, line, cap) < 4)
        return -1;

    if (
        !isdigit((unsigned char)line[0]) ||
        !isdigit((unsigned char)line[1]) ||
        !isdigit((unsigned char)line[2]) ||
        (line[3] != ' ' && line[3] != '-')
    ) return -1;

    int code =
        (line[0] - '0') * 100 +
        (line[1] - '0') * 10 +
        line[2] - '0';

    if (line[3] == '-') {
        for (int i = 0; i < 60; ++i) {
            const int line_length=readline(fd, line, cap);
            if (line_length < 0)
                return -1;
            if (line_length < 4)
                continue; /* Legal intermediate text, never a final code. */

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
    /* Authentication is per FTP control connection. A successful metadata
     * scan and the later lazy-cover worker use different sockets: an
     * anonymous USER attempt on the first must not suppress a needed USER
     * on the second. The scan worker is joined before covers start. */
    ftp_auth_attempted=0;
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
#ifdef __ORBIS__
    int cancelled = 0;
#endif

    /*
     * Read until EOF. Never interpret a truncated LIST or SFO, and
     * cap the amount discarded after the caller's buffer fills.
     */
    for (;;) {
#ifdef __ORBIS__
        if(scan_should_abort()) {
            cancelled=1;
            break;
        }
#endif
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

#ifdef __ORBIS__
    if(cancelled)return -18; /* Closing control on abort is caller's job. */
#endif
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
    char ids[MAX_GAMES][10],
    int *truncated
) {
    size_t off = 0;
    int count = 0;
    *truncated = 0;

    /* Continue parsing after reaching capacity to detect a partial scan. */
    while (off < size) {
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
                    if (count == MAX_GAMES) {
                        *truncated = 1;
                    } else {
                        memcpy(ids[count], s, 10);
                        ++count;
                    }
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
#ifdef __ORBIS__
    if(scan_should_abort())return -1;
#endif
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

    if (optional)
        external_listing_ok = 1;
    else
        primary_listing_ok = 1;

    static char ids[MAX_GAMES][10];

    int truncated = 0;
    int count = listing_names(
        listing, bytes, ids, &truncated
    );
    if (truncated) {
        /* Never report an incomplete subset as a full inventory. */
        ++errors;
        logmsg("LIST_TRUNCATED", 1);
    }

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
#ifdef __ORBIS__
        if(scan_should_abort())return -1;
        scan_publish_progress((unsigned)i+1u,(unsigned)count);
#endif
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
            if (parsed < 0 && bytes >= 20 &&
                u32(sfo) == 0x46535000u) {
                /* Require a valid SFO for the same CUSA after recovery. */
                static unsigned char decoded[MAX_SFO];
                size_t clean_size = unexpand_ascii_newlines(
                    sfo, bytes, decoded
                );
                if (clean_size < bytes) {
                    int recovered = parse_sfo(
                        decoded, clean_size, ids[i], source
                    );
                    if (recovered >= 0) {
                        parsed = recovered;
                        if (report) {
                            fprintf(report,
                                "SFO ASCII_RECOVERED %s original=%lu cleaned=%lu\n",
                                ids[i], (unsigned long)bytes,
                                (unsigned long)clean_size);
                            fflush(report);
                        }
                    }
                }
            }
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


#ifdef __ORBIS__
/* Read-only GoldHEN FTP icon retrieval. A single PNG is decoded in the
 * background thread, and its 128x128 pixels remain private until READY.
 * GoldHEN variants may not support TYPE I: match scanner's PASV-first
 * transfer path. PNG CRC and optional ASCII newline recovery protect the
 * icon cache from corrupt transport data. */
static int fetch_local_icon(sock_t control,unsigned index,uint32_t *out) {
    if(index>=found || !out || scan_should_abort())return -1;
    const Game *g=&games[index];
    if(!valid_id(g->cusa))return -2;
    const char *root=strcmp(g->source,"external")==0
        ? "/system_data/priv/appmeta/external"
        : "/system_data/priv/appmeta";
    char cmd[320];
    const int len=snprintf(cmd,sizeof cmd,"RETR %s/%s/icon0.png",
                           root,g->cusa);
    if(len<0 || len>=(int)sizeof cmd)return -3;
    static unsigned char png[UI_ICON_MAX_PNG];
    size_t bytes=0;
    const int read_rc=transfer(control,cmd,png,sizeof png,&bytes);
    if(read_rc!=0)return read_rc;
    const int valid=ui_png_ftp_validate(png,&bytes);
    if(valid<0)return valid;
    if(valid==1)logmsg("ICON_FTP_ASCII_CRC_RECOVERED",1);
    return ui_image_thumbnail(png,bytes,out,UI_ICON_PIXELS);
}

static void *cover_worker_entry(void *unused) {
    (void)unused;
    sock_t control=connect_local(FTP_PORT);
    if(control==NET_BAD) {
        logmsg("ICON_CONTROL_CONNECT_FAILED",1);
        __atomic_store_n(&cover_online,2u,__ATOMIC_RELEASE);
        return NULL;
    }
    if(!login(control)) {
        logmsg("ICON_FTP_GREETING_FAILED",1);
        NET_CLOSE(control);
        __atomic_store_n(&cover_online,2u,__ATOMIC_RELEASE);
        return NULL;
    }
    /* Do not probe TYPE I here. Some PS4 FTP implementations reject it
     * and desynchronize subsequent PASV requests. The read-only scanner
     * already uses PASV first; the cover worker must do the same. */
    logmsg("ICON_FTP_PASV_FIRST",1);
    __atomic_store_n(&cover_online,1u,__ATOMIC_RELEASE);
    while(!scan_should_abort() &&
          !__atomic_load_n(&cover_stop,__ATOMIC_ACQUIRE)) {
        if(__atomic_load_n(&cover_state,__ATOMIC_ACQUIRE)==1u) {
            const unsigned index=cover_index; /* request published */
            __atomic_store_n(&cover_state,2u,__ATOMIC_RELEASE);
            cover_result=fetch_local_icon(control,index,cover_pixels);
            logmsg("ICON_LAZY_FETCH_RC",cover_result);
            __atomic_store_n(&cover_state,3u,__ATOMIC_RELEASE);
        } else {
            (void)sceKernelUsleep(30000);
        }
    }
    /* An interrupted FTP transfer may have an unread final response:
     * close instead of sending QUIT on the stale command stream. */
    NET_CLOSE(control);
    __atomic_store_n(&cover_online,2u,__ATOMIC_RELEASE);
    return NULL;
}

/* This callback is invoked by scanner_ui_results on the Pad owner thread.
 * Never block here; the single-slot mailbox accepts only one request.
 * Previously failed icons remain explicit placeholders for this session. */
static int cover_ui_tick(const char *cusa) {
    int changed=0;
    if(__atomic_load_n(&cover_state,__ATOMIC_ACQUIRE)==3u) {
        const unsigned index=cover_index;
        if(index<found) {
            if(cover_result==0 &&
               scanner_ui_offer_thumbnail(games[index].cusa,cover_pixels,
                                          UI_ICON_PIXELS)==0) {
                changed=1;
            } else {
                cover_failed[index]=1;
            }
        }
        __atomic_store_n(&cover_state,0u,__ATOMIC_RELEASE);
    }
    if(!cusa || scan_should_abort() ||
       __atomic_load_n(&cover_online,__ATOMIC_ACQUIRE)!=1u ||
       __atomic_load_n(&cover_state,__ATOMIC_ACQUIRE)!=0u ||
       ui_icon_cache_find(cusa)!=NULL)return changed;
    for(unsigned i=0;i<found;++i) {
        if(!cover_failed[i] && strcmp(games[i].cusa,cusa)==0) {
            cover_index=i;
            __atomic_store_n(&cover_state,1u,__ATOMIC_RELEASE);
            break;
        }
    }
    return changed;
}
#endif

/* The fw14 hardware crash after the scan is under investigation.
 * AppInstUtil direct init is EXPERIMENTAL and disabled in official builds.
 * Without a safe lookup, all installation states stay UNKNOWN. */
static void confirm_games(void) {
#ifdef __ORBIS__
#ifndef PS4_ENABLE_EXPERIMENTAL_APPINSTUTIL
    api_stage = 3;
    logmsg("AppInstUtil skipped (unverified on firmware 14)", 1);
    return;
#else
    /* ItemzFlow's read-only AppExists path initializes the linked
     * AppInstUtil import directly. Our old sysmodule load attempt
     * returned 0x80020008 on firmware 14.00 and prevented this call
     * from running at all. That load failure is NOT evidence that the
     * already-linked API is unavailable. No install API is called. */
    logmsg("AppInstUtil direct initialization attempted", 1);
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

        if (rc == 0 && (exists == 0 || exists == 1)) {
            games[i].installed = exists;
        } else if (report) {
            fprintf(report,
                    "AppInstUtil exists %s: rc=0x%08x state=%d\n",
                    games[i].cusa, (uint32_t)rc, (int)exists);
            fflush(report);
        }
    }

    int32_t done = sceAppInstUtilTerminate();

    if (report)
        fprintf(
            report,
            "AppInstUtil term: 0x%08x\n",
            (uint32_t)done
        );
#endif /* PS4_ENABLE_EXPERIMENTAL_APPINSTUTIL */
#else
    api_stage = -1;
#endif
}

static int save_inventory(const char *path) {
    char tmp[1024];
    int n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof tmp)
        return -1;
    FILE *fp = fopen(tmp, "wb");
    if (!fp)
        return -1;

    fputs(
        "{\n"
        "  \"schema\":6,\n"
        "  \"source\":\"ftp_loopback_readonly_v065\",\n"
        "  \"metadata_only\":true,\n"
        "  \"scan_complete\":",
        fp
    );
    fputs(
        ftp_login_ok && primary_listing_ok && external_listing_ok && errors == 0
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
        fputs(metadata_index(candidate_list[i].cusa) >= 0
                  ? ",\"metadata_verified\":true}"
                  : ",\"metadata_verified\":false}", fp);
    }

    fputs("\n  ],\n  \"metadata_records\":[\n", fp);
    for (unsigned i = 0; i < metadata_found; ++i) {
        const Game *g = &metadata_records[i];
        if (i) fputs(",\n", fp);
        fputs("    {\"cusa\":", fp); json_str(fp, g->cusa);
        fputs(",\"name\":", fp); json_str(fp, g->name);
        fputs(",\"app_ver\":", fp); json_str(fp, g->version);
        fputs(",\"content_id\":", fp); json_str(fp, g->cid);
        fputs(",\"content_id_verified\":", fp);
        fputs(g->cid[0] && content_id_matches(g->cid, g->cusa)
                  ? "true" : "false", fp);
        fputs(",\"category\":", fp); json_str(fp, g->category);
        fputs(",\"metadata_kind\":", fp);
        json_str(fp, metadata_kind(g->category));
        fputs(",\"source\":", fp); json_str(fp, g->source);
        fputs(",\"installed_confirmed\":false}", fp);
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

    if (!ok || rename(tmp, path) != 0) {
        (void)remove(tmp);
        return -1;
    }
    return 0;
}

static int scan_readonly(void) {
#ifdef __ORBIS__
    /* Keep v0.5 diagnostics intact for side-by-side comparison. */
    const char *outpath =
        ROOT "/inventory_v065.json";

    const char *reportpath =
        ROOT "/diagnostic_v065.txt";

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
            "PS4 Auto Updater v0.6.5 FTP loopback READ ONLY\n"
            "Network initialize: 0x%08x\n"
            "Network pool: %d\n",
            (unsigned)netstatus,
            pool
        );
        fflush(report);
    }

#ifdef __ORBIS__
    logmsg("NATIVE_UI_AVAILABLE",scan_native_ui);
#endif

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

#ifdef __ORBIS__
        if(scan_should_abort()) {
            logmsg("SCAN_ABORT_BEFORE_CONNECT",1);
            pool = -1;
        }
#endif
        sock_t control = pool < 0 ? NET_BAD : connect_local(port);

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

#ifdef __ORBIS__
                if(!scan_should_abort())
#endif
                scan_root(
                    control,
                    "/system_data/priv/appmeta/external",
                    "external",
                    1
                );
/* Covers are now requested lazily by the UI after the scan.
                 * The initial metadata pass does not wait for artwork. */
            } else {
                ++errors;
            }

            /* transfer() may stop halfway through a data stream when
             * Cancel is requested; its control reply is then unread.
             * Never issue QUIT on an out-of-sync command stream. */
#ifdef __ORBIS__
            if(!scan_should_abort()) {
#endif
                char line[512];
                (void)command(control,line,sizeof line,"QUIT");
#ifdef __ORBIS__
            } else {
                logmsg("FTP_CANCEL_SKIP_QUIT",1);
            }
#endif
            NET_CLOSE(control);
        }
    }

    logmsg("SCAN_STAGE_FINISHED", 1);
#ifdef __ORBIS__
    logmsg("UI_SCAN_CANCEL_REQUESTED",scan_should_abort());
#endif
    confirm_games();
    logmsg("SCAN_STAGE_CONFIRM_DONE", 1);

#ifdef __ORBIS__
    /* Critical cache invariant: never erase an existing, valid library
     * with a partial scan. A successful internal scan combined with a
     * failed optional external LIST may otherwise lose all external games
     * after restarting the UI. A partial first scan can seed a snapshot
     * only when there is no valid previous cache at all. */
    const int base_verified=ftp_login_ok && primary_listing_ok &&
        errors==0 && sfo_invalid==0 && !candidate_overflow &&
        !scan_should_abort();
    const int complete_scan=base_verified && external_listing_ok;
    const int partial_seed=base_verified && found>0u;
    int prior_valid=0;
    if(!complete_scan && partial_seed) {
        static UiSnapshotGame prior[MAX_GAMES];
        unsigned prior_count=0;
        const int status=ui_snapshot_load(
            ROOT "/snapshot_v1.bin",prior,MAX_GAMES,&prior_count);
        prior_valid=(status==1); /* valid empty snapshot still counts */
        logmsg("UI_CACHE_PRIOR_LOAD_RC",status);
    }
    if(ui_snapshot_should_save(complete_scan,partial_seed,prior_valid)) {
        static UiSnapshotGame current[MAX_GAMES];
        for(unsigned i=0;i<found;++i) {
            memset(&current[i],0,sizeof current[i]);
            memcpy(current[i].cusa,games[i].cusa,
                   sizeof current[i].cusa);
            memcpy(current[i].title,games[i].name,
                   sizeof current[i].title);
            memcpy(current[i].version,games[i].version,
                   sizeof current[i].version);
            memcpy(current[i].category,games[i].category,
                   sizeof current[i].category);
        }
        if(!complete_scan)logmsg("UI_CACHE_PARTIAL_FIRST_SNAPSHOT",1);
        logmsg("UI_CACHE_WRITE_RC",ui_snapshot_save(
            ROOT "/snapshot_v1.bin",current,found));
    } else {
        logmsg("UI_CACHE_PRESERVED_DUE_TO_INCOMPLETE_SCAN",1);
    }
#endif

    int saved;
#ifdef __ORBIS__
    /* An aborted partial scan must not overwrite the old inventory. */
    saved=scan_should_abort() ? -2 : save_inventory(outpath);
#else
    saved=save_inventory(outpath);
#endif
    logmsg("SCAN_STAGE_INVENTORY_SAVE_RETURNED", saved);

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
    logmsg("Base-game records", (int)found);
    logmsg("SFO metadata verified", (int)metadata_found);
    /* Counts across base games, patches and DLC; not only games[]. */
    logmsg("Metadata records", (int)metadata_found);
    logmsg("Installed confirmed", (int)confirmed);
    logmsg("Installation unknown", (int)unknown);

    logmsg(
        "AppInstUtil status (0=OK,2=init failed,3=skipped,-1=host)",
        api_stage
    );

    logmsg("Scan errors", errors);
    logmsg("Primary listing available", primary_listing_ok);
    logmsg("External listing available", external_listing_ok);
    logmsg("Scan complete", ftp_login_ok && primary_listing_ok && external_listing_ok && errors == 0);
    logmsg("Inventory saved (0=OK)", saved);

    logmsg("SCAN_STAGE_RESULTS_NEXT", 1);

#ifdef __ORBIS__
    /* UI library lists validated BASE games only. Directory candidates,
     * Patch and DLC remain separate in the read-only diagnostic inventory.
     * Metadata verification is NOT installation confirmation. */
    for (unsigned i = 0; i < found; ++i) {
        const Game *g = &games[i];
        ui_items[i].cusa = g->cusa;
        ui_items[i].title = g->name;
        ui_items[i].version = g->version;
        ui_items[i].metadata_verified = 1;
        ui_items[i].install_state = g->installed;
        ui_items[i].category = g->category;
    }
    scan_inventory_saved=(saved==0);
    scan_inventory_complete=ftp_login_ok && primary_listing_ok &&
        external_listing_ok && errors==0 && !scan_should_abort();
    logmsg("UI_RESULTS_STAGED",(int)found);
#endif

#ifdef __ORBIS__
    return saved == 0 ? 0 : 1;
#else
    logmsg("APP_MAIN_RETURNING", saved == 0 ? 0 : 1);
    if (report) {
        fflush(report);
        fclose(report);
        report = NULL;
    }
    return saved == 0 ? 0 : 1;
#endif
}

#ifdef __ORBIS__
static void *scan_worker_entry(void *unused) {
    (void)unused;
    scan_run_rc=scan_readonly();
    /* Release publishes scanner completion. The main UI thread still joins
     * before consuming games[], ui_items[] and diagnostic state. */
    __atomic_store_n(&scan_status,SCANNER_UI_SCAN_DONE,__ATOMIC_RELEASE);
    return NULL;
}

int main(void) {
    if (mkdir(ROOT,0777)!=0 && errno!=EEXIST)
        ps4_shell_exit(2,NULL);

    scan_native_ui=scanner_ui_begin();
    /* Keep the safe headless diagnostic path if VideoOut is unavailable. */
    if(!scan_native_ui) {
        const int rc=scan_readonly();
        ps4_shell_exit(rc,logmsg);
    }

    static UiSnapshotGame previous_games[MAX_GAMES];
    static ScannerUiEntry previous_items[MAX_GAMES];
    unsigned previous_count=0;
    const int previous_status=ui_snapshot_load(
        ROOT "/snapshot_v1.bin",previous_games,MAX_GAMES,&previous_count);
    if(previous_status==1 && previous_count) {
        for(unsigned i=0;i<previous_count;++i) {
            previous_items[i].cusa=previous_games[i].cusa;
            previous_items[i].title=previous_games[i].title;
            previous_items[i].version=previous_games[i].version;
            previous_items[i].category=previous_games[i].category;
            previous_items[i].metadata_verified=0;
            previous_items[i].install_state=-1;
        }
        (void)scanner_ui_preview_snapshot(previous_items,previous_count);
    }

    /* Verified OpenOrbis libkernel API. Stack deliberately larger than the
     * default scePthread stack; FTP data buffers themselves are static. */
    OrbisPthreadAttr attributes;
    int rc=scePthreadAttrInit(&attributes);
    const int attributes_valid=(rc==0);
    if(rc==0)rc=scePthreadAttrSetstacksize(&attributes,256u*1024u);
    OrbisPthread worker;
    if(rc==0)
        rc=scePthreadCreate(&worker,&attributes,scan_worker_entry,NULL,
                            "ps4auto_scan");
    if(attributes_valid)(void)scePthreadAttrDestroy(&attributes);
    if(rc!=0) {
        /* A failed worker must never fall back to a blocking network scan
         * on the UI thread or present a false list as freshly verified. */
        FILE *failure=fopen(ROOT "/diagnostic_v065.txt","wb");
        if(failure) {
            fprintf(failure,"SCAN_THREAD_CREATE_FAILED: 0x%08x\n",
                    (unsigned)rc);
            fclose(failure);
        }
        scanner_ui_results(NULL,0,0,1,0,0);
        scanner_ui_shutdown(NULL);
        ps4_shell_exit(3,NULL);
    }

    const int user_exit=scanner_ui_wait_for_scan(&scan_status);
    if(user_exit)
        __atomic_store_n(&scan_abort,1,__ATOMIC_RELEASE);
    const int join_rc=scePthreadJoin(worker,NULL);
    if(join_rc!=0) {
        /* Worker ownership could not be confirmed; do not access its
         * output, game state, thumbnails, or log FILE*. */
        scanner_ui_shutdown(NULL);
        ps4_shell_exit(4,NULL);
    }

    if(!user_exit) {
        /* A failed optional artwork thread must not block navigation.
         * No FTP work or PNG decoding is ever performed by VideoOut. */
        OrbisPthread cover_worker;
        OrbisPthreadAttr cover_attr;
        int attr_rc=scePthreadAttrInit(&cover_attr);
        const int attr_valid=(attr_rc==0);
        if(attr_rc==0)
            attr_rc=scePthreadAttrSetstacksize(&cover_attr,256u*1024u);
        int cover_started=0;
        if(attr_rc==0) {
            memset(cover_failed,0,sizeof cover_failed);
            __atomic_store_n(&cover_state,0u,__ATOMIC_RELEASE);
            __atomic_store_n(&cover_stop,0u,__ATOMIC_RELEASE);
            __atomic_store_n(&cover_online,0u,__ATOMIC_RELEASE);
            const int create_rc=scePthreadCreate(
                &cover_worker,&cover_attr,cover_worker_entry,NULL,
                "ps4auto_covers");
            if(create_rc==0) {
                cover_started=1;
                scanner_ui_set_cover_tick(cover_ui_tick);
            } else logmsg("ICON_WORKER_CREATE_FAILED",create_rc);
        } else logmsg("ICON_WORKER_ATTR_FAILED",attr_rc);
        if(attr_valid)(void)scePthreadAttrDestroy(&cover_attr);

        scanner_ui_results(ui_items,found,found,(unsigned)errors,
                            scan_inventory_complete,scan_inventory_saved);
        scanner_ui_set_cover_tick(NULL);
        if(cover_started) {
            __atomic_store_n(&cover_stop,1u,__ATOMIC_RELEASE);
            __atomic_store_n(&scan_abort,1,__ATOMIC_RELEASE);
            if(scePthreadJoin(cover_worker,NULL)!=0) {
                /* Never release shared worker memory while it may run. */
                scanner_ui_shutdown(NULL);
                ps4_shell_exit(5,NULL);
            }
        }
    }
    logmsg("UI_RESULTS_RETURNED",1);
    scanner_ui_shutdown(logmsg);
    logmsg("UI_SHUTDOWN_RETURNED",1);
    /* Never return to PS4 musl's _exit trampoline. */
    ps4_shell_exit(scan_run_rc,logmsg);
}
#else
int main(void) {
    return scan_readonly();
}
#endif
