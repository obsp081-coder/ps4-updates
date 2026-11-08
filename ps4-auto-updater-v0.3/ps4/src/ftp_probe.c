
/*
 * PS4 Auto Updater v0.3.4
 * GoldHEN FTP CUSA directory scanner.
 * Read-only. No game or system modifications.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <orbis/Net.h>

#define OUT_DIR "/data/PS4AutoUpdater"
#define OUT_FILE OUT_DIR "/ftp_scan.txt"
#define ROOT "/system_data/priv/appmeta"
#define MAX_LINE 1024
#define MAX_TRANSFER (256U * 1024U)

static void make_addr(OrbisNetSockaddr *a, unsigned port)
{
    memset(a, 0, sizeof(*a));
    a->len = (uint8_t)sizeof(*a);
    a->sa_family = ORBIS_NET_AF_INET;
    a->sa_data[0] = (char)((port >> 8) & 255U);
    a->sa_data[1] = (char)(port & 255U);
    a->sa_data[2] = 127;
    a->sa_data[3] = 0;
    a->sa_data[4] = 0;
    a->sa_data[5] = 1;
}

static int send_cmd(OrbisNetId s, const char *cmd)
{
    size_t left = strlen(cmd);
    const char *p = cmd;

    while (left) {
        int n = sceNetSend(s, p, left, 0);
        if (n <= 0) return -1;
        left -= (size_t)n;
        p += n;
    }
    return 0;
}

static int read_line(OrbisNetId s, char *out, size_t cap)
{
    size_t used = 0;
    char ch;

    for (;;) {
        int n = sceNetRecv(s, &ch, 1, 0);
        if (n <= 0) return -1;
        if (ch == '\n') break;
        if (ch != '\r' && used + 1 < cap)
            out[used++] = ch;
    }

    out[used] = '\0';
    return 0;
}

static int read_reply(OrbisNetId s, char *line, size_t cap)
{
    if (read_line(s, line, cap) != 0)
        return -1;

    if (strlen(line) < 4 ||
        !isdigit((unsigned char)line[0]) ||
        !isdigit((unsigned char)line[1]) ||
        !isdigit((unsigned char)line[2]))
        return -1;

    int code = (line[0] - '0') * 100 +
               (line[1] - '0') * 10 +
               (line[2] - '0');

    if (line[3] == '-') {
        int done = 0;

        for (int i = 0; i < 40; ++i) {
            if (read_line(s, line, cap) != 0)
                return -1;

            if (strlen(line) >= 4 &&
                isdigit((unsigned char)line[0]) &&
                isdigit((unsigned char)line[1]) &&
                isdigit((unsigned char)line[2]) &&
                ((line[0] - '0') * 100 +
                 (line[1] - '0') * 10 +
                 (line[2] - '0')) == code &&
                line[3] == ' ') {
                done = 1;
                break;
            }
        }

        if (!done) return -1;
    }

    return code;
}

static int command_reply(
    OrbisNetId s,
    const char *cmd,
    char *reply,
    size_t cap)
{
    if (send_cmd(s, cmd) != 0)
        return -1;

    return read_reply(s, reply, cap);
}

static int valid_cusa(const char *name)
{
    if (strlen(name) != 9 ||
        strncmp(name, "CUSA", 4) != 0)
        return 0;

    for (int i = 4; i < 9; ++i) {
        if (!isdigit((unsigned char)name[i]))
            return 0;
    }

    return 1;
}

static void process_line(
    FILE *out,
    char *line,
    unsigned *count)
{
    size_t len = strlen(line);

    if (len && line[len - 1] == '\r')
        line[len - 1] = '\0';

    if (line[0] != 'd')
        return;

    char *last = strrchr(line, ' ');
    if (!last) return;

    const char *name = last + 1;

    if (valid_cusa(name)) {
        fprintf(out, "CUSA: %s\n", name);
        ++*count;
    }
}

static int receive_listing(
    OrbisNetId data,
    FILE *out,
    unsigned *count)
{
    char incoming[2048];
    char line[MAX_LINE];

    size_t used = 0;
    size_t total = 0;
    int overflow = 0;

    for (;;) {
        int n = sceNetRecv(
            data, incoming, sizeof(incoming), 0);

        if (n < 0) return -1;
        if (n == 0) break;

        total += (size_t)n;
        if (total > MAX_TRANSFER)
            return -2;

        for (int i = 0; i < n; ++i) {
            char c = incoming[i];

            if (c == '\n') {
                if (!overflow) {
                    line[used] = '\0';
                    process_line(out, line, count);
                }
                used = 0;
                overflow = 0;
            }
            else if (used + 1 < sizeof(line)) {
                line[used++] = c;
            }
            else {
                overflow = 1;
            }
        }
    }

    if (used && !overflow) {
        line[used] = '\0';
        process_line(out, line, count);
    }

    return 0;
}

int main(void)
{
    if (mkdir(OUT_DIR, 0777) != 0 &&
        errno != EEXIST)
        return 1;

    FILE *out = fopen(OUT_FILE, "wb");
    if (!out) return 2;

    fputs("PS4 Auto Updater FTP Scan v0.3.4\n", out);
    fputs("READ ONLY - metadata folders only\n", out);
    fputs("Source: " ROOT "\n", out);
    fflush(out);

    OrbisNetId control = -1;
    OrbisNetId data = -1;
    OrbisNetSockaddr addr;

    char reply[512];
    int step = 0;
    int code = 0;
    unsigned count = 0;

    if (sceNetInit() != 0) {
        step = 10;
        goto done;
    }

    control = sceNetSocket(
        "PS4UpdaterFTPControl",
        ORBIS_NET_AF_INET,
        ORBIS_NET_SOCK_STREAM,
        0
    );

    if (control < 0) {
        step = 11;
        goto done;
    }

    make_addr(&addr, 2121);

    if (sceNetConnect(
            control, &addr, sizeof(addr)) != 0) {
        step = 12;
        goto done;
    }

    code = read_reply(control, reply, sizeof(reply));
    if (code != 220) {
        step = 13;
        goto done;
    }

    code = command_reply(
        control, "USER anonymous\r\n",
        reply, sizeof(reply));

    if (code != 331 && code != 230) {
        step = 14;
        goto done;
    }

    if (code == 331) {
        code = command_reply(
            control, "PASS anonymous\r\n",
            reply, sizeof(reply));

        if (code != 230) {
            step = 15;
            goto done;
        }
    }

    code = command_reply(
        control, "TYPE A\r\n",
        reply, sizeof(reply));

    if (code != 200) {
        step = 16;
        goto done;
    }

    code = command_reply(
        control, "PASV\r\n",
        reply, sizeof(reply));

    if (code != 227) {
        step = 17;
        goto done;
    }

    char *opening = strchr(reply, '(');
    unsigned v[6];

    if (!opening ||
        sscanf(opening, "(%u,%u,%u,%u,%u,%u)",
               &v[0], &v[1], &v[2],
               &v[3], &v[4], &v[5]) != 6 ||
        v[4] > 255U ||
        v[5] > 255U ||
        (v[4] == 0U && v[5] == 0U)) {
        step = 18;
        goto done;
    }

    unsigned port = (v[4] << 8) | v[5];

    data = sceNetSocket(
        "PS4UpdaterFTPData",
        ORBIS_NET_AF_INET,
        ORBIS_NET_SOCK_STREAM,
        0
    );

    if (data < 0) {
        step = 19;
        goto done;
    }

    make_addr(&addr, port);

    if (sceNetConnect(
            data, &addr, sizeof(addr)) != 0) {
        step = 20;
        goto done;
    }

    code = command_reply(
        control,
        "LIST " ROOT "\r\n",
        reply,
        sizeof(reply)
    );

    if (code != 150 && code != 125) {
        step = 21;
        goto done;
    }

    int result = receive_listing(
        data, out, &count);

    sceNetSocketClose(data);
    data = -1;

    if (result != 0) {
        step = 22;
        goto done;
    }

    code = read_reply(
        control, reply, sizeof(reply));

    if (code != 226 && code != 250) {
        step = 23;
        goto done;
    }

    fprintf(out,
            "Found CUSA directories: %u\n",
            count);

    fputs("Result: LIST_OK\n", out);

done:
    if (step != 0) {
        fprintf(out,
                "Failure stage: %d\n"
                "FTP reply: %d\n"
                "Result: LIST_FAILED\n",
                step, code);
    }

    if (data >= 0)
        sceNetSocketClose(data);

    if (control >= 0) {
        if (step == 0)
            send_cmd(control, "QUIT\r\n");

        sceNetSocketClose(control);
    }

    fclose(out);
    return step ? 1 : 0;
}
