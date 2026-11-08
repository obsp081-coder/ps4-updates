
/*
 * PS4 Auto Updater v0.7 - isolated local HTTP probe.
 *
 * Binds ONLY 127.0.0.1:8127 on the console.
 * GET /health             - liveness, no console data
 * GET /api/inventory      - redacted HISTORICAL inventory summary
 * GET /                  - tiny local diagnostic page
 * No POST, remote listening, downloads, installs, FTP, or FPKG approval.
 *
 * The PS4 may suspend this application when its browser opens. This is
 * experimental and has not been verified on firmware 14.00.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ORBIS__
#include <orbis/Net.h>
typedef OrbisNetId socket_id;
#define SOCK_BAD (-1)
#define AF_INET_NATIVE ORBIS_NET_AF_INET
#define STREAM_NATIVE ORBIS_NET_SOCK_STREAM
#define SOCKET_CLOSE sceNetSocketClose
#define SOCKET_SEND sceNetSend
#define SOCKET_RECV sceNetRecv
#define SOCKET_ACCEPT sceNetAccept
#define SOCKET_LISTEN sceNetListen
#define SOCKET_BIND sceNetBind
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_id;
#define SOCK_BAD (-1)
#define AF_INET_NATIVE AF_INET
#define STREAM_NATIVE SOCK_STREAM
#define SOCKET_CLOSE close
#define SOCKET_SEND send
#define SOCKET_RECV recv
#define SOCKET_ACCEPT(fd, a, l) accept((fd), (struct sockaddr *)(a), (socklen_t *)(l))
#define SOCKET_LISTEN listen
#define SOCKET_BIND(fd, a, l) bind((fd), (struct sockaddr *)(a), (socklen_t)(l))
#endif

#define PORT 8127
#define INVENTORY_PATH "/data/PS4AutoUpdater/inventory.json"
#define MAX_GAMES 128
#define LINE_BYTES 4096
#define JSON_BYTES 32768

static int append(char *dst, size_t cap, size_t *length, const char *s)
{
    size_t n = strlen(s);
    if (*length >= cap || n >= cap - *length)
        return 0;
    memcpy(dst + *length, s, n + 1);
    *length += n;
    return 1;
}

/* Extract one quoted string from a scanner JSON line. No full JSON parser. */
static int field(const char *line, const char *key, char *out, size_t cap)
{
    char needle[40];
    if (snprintf(needle, sizeof needle, "\"%s\"", key) >= (int)sizeof needle)
        return 0;
    const char *p = strstr(line, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p && isspace((unsigned char)*p)) ++p;
    if (*p++ != ':') return 0;
    while (*p && isspace((unsigned char)*p)) ++p;
    if (*p++ != '"') return 0;
    size_t n = 0;
    while (*p && *p != '"' && *p != '\\') {
        if (n + 1 >= cap) return 0;
        out[n++] = *p++;
    }
    if (*p != '"') return 0;
    out[n] = 0;
    return 1;
}

static int valid_cusa(const char *s)
{
    if (strlen(s) != 9 || strncmp(s, "CUSA", 4)) return 0;
    for (int i = 4; i < 9; ++i)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

static int valid_ver(const char *s)
{
    size_t n = strlen(s), dots = 0, before = 0, after = 0;
    if (n < 3 || n > 9) return 0;
    for (size_t i = 0; i < n; ++i) {
        if (s[i] == '.') { if (++dots > 1) return 0; }
        else if (!isdigit((unsigned char)s[i])) return 0;
        else if (dots == 0) ++before;
        else ++after;
    }
    return dots == 1 && before >= 1 && before <= 4 && after >= 1 && after <= 4;
}

static const char *inventory_file(void)
{
#ifndef __ORBIS__
    const char *test = getenv("PS4_HTTP_TEST_INVENTORY");
    if (test && *test) return test;
#endif
    return INVENTORY_PATH;
}

/* Only known-safe fields: CUSA and historical app_ver. No Content ID/name. */
static int make_snapshot(char *out, size_t cap)
{
    FILE *f = fopen(inventory_file(), "rb");
    if (!f) return 0;
    size_t used = 0;
    int count = 0;
    int good = append(out, cap, &used,
        "{\"schema\":1,\"source\":\"historical_ftp_inventory\","
        "\"live_scan\":false,\"installed_confirmed\":false,\"games\":[");
    char line[LINE_BYTES], cusa[16], ver[16], record[100];
    while (good && fgets(line, sizeof line, f)) {
        if (count >= MAX_GAMES) break;
        if (!strchr(line, '\n') && !feof(f)) {
            int ch;
            while ((ch = fgetc(f)) != '\n' && ch != EOF) {}
            continue;
        }
        if (!field(line, "cusa", cusa, sizeof cusa) || !valid_cusa(cusa)) continue;
        if (!field(line, "app_ver", ver, sizeof ver) || !valid_ver(ver)) continue;
        int n = snprintf(record, sizeof record,
            "%s{\"cusa\":\"%s\",\"app_ver\":\"%s\"}",
            count ? "," : "", cusa, ver);
        if (n <= 0 || n >= (int)sizeof record) { good = 0; break; }
        good = append(out, cap, &used, record);
        ++count;
    }
    int read_error = ferror(f);
    fclose(f);
    if (!good || read_error || !append(out, cap, &used, "]}")) return 0;
    return 1;
}

static int send_all(socket_id fd, const char *data, size_t size)
{
    while (size) {
        int n = SOCKET_SEND(fd, data, size, 0);
        if (n <= 0) return 0;
        data += (size_t)n;
        size -= (size_t)n;
    }
    return 1;
}

static void reply(socket_id fd, int status, const char *mime, const char *body)
{
    char head[500];
    const char *label = status == 200 ? "OK" :
                        status == 503 ? "Service Unavailable" : "Not Found";
    int len = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\n\r\n",
        status, label, mime, strlen(body));
    if (len <= 0 || len >= (int)sizeof head) return;
    if (send_all(fd, head, (size_t)len))
        (void)send_all(fd, body, strlen(body));
}

static void handle_client(socket_id fd)
{
    char req[1200];
    int n = SOCKET_RECV(fd, req, sizeof req - 1, 0);
    if (n <= 0) return;
    req[n] = 0;
    if (!strncmp(req, "GET /health ", 12)) {
        reply(fd, 200, "application/json",
              "{\"status\":\"OK\",\"read_only\":true,\"live_scan\":false}");
    } else if (!strncmp(req, "GET /api/inventory ", 19)) {
        static char snapshot[JSON_BYTES];
        if (make_snapshot(snapshot, sizeof snapshot))
            reply(fd, 200, "application/json", snapshot);
        else
            reply(fd, 503, "application/json",
                  "{\"status\":\"INVENTORY_UNAVAILABLE\",\"games\":[]}");
    } else if (!strncmp(req, "GET / ", 6)) {
        reply(fd, 200, "text/html",
              "<!doctype html><html lang=ar dir=rtl><meta charset=utf-8>"
              "<meta name=viewport content='width=device-width,initial-scale=1'>"
              "<title>PS4 Local Bridge</title>"
              "<body style='font:20px Tahoma;background:#101a30;color:white;padding:28px'>"
              "<h2>PS4 Auto Updater - Local Bridge v0.7</h2>"
              "<p>هذه تجربة اتصال محلي. البيانات من جرد سابق وليست فحصا حاليا.</p>"
              "<p><a style='color:#88caff' href='/health'>Health check</a></p>"
              "<p><a style='color:#88caff' href='/api/inventory'>Redacted inventory JSON</a></p>"
              "<p>لا يوجد تنزيل أو تثبيت أو كشف Content ID.</p></body></html>");
    } else {
        reply(fd, 404, "application/json", "{\"error\":\"NOT_FOUND\"}");
    }
}

static socket_id start_listener(void)
{
#ifdef __ORBIS__
    int init = sceNetInit();
    if (init != 0 && (uint32_t)init != 0x80410110U) return SOCK_BAD;
    int pool = sceNetPoolCreate("ps4_auto_local", 128 * 1024, 0);
    if (pool < 0) return SOCK_BAD;
    socket_id fd = sceNetSocket("ps4_auto_local", AF_INET_NATIVE, STREAM_NATIVE, 0);
    if (fd < 0) return SOCK_BAD;
    struct {
        uint8_t len;
        uint8_t family;
        uint16_t port;
        uint32_t address;
        uint8_t zero[8];
    } addr = {0};
    addr.len = (uint8_t)sizeof addr;
    addr.family = AF_INET_NATIVE;
    addr.port = sceNetHtons(PORT);
    if (sceNetInetPton(AF_INET_NATIVE, "127.0.0.1", &addr.address) != 1 ||
        SOCKET_BIND(fd, (const OrbisNetSockaddr *)&addr, sizeof addr) < 0 ||
        SOCKET_LISTEN(fd, 2) < 0) {
        SOCKET_CLOSE(fd);
        return SOCK_BAD;
    }
#else
    socket_id fd = socket(AF_INET_NATIVE, STREAM_NATIVE, 0);
    if (fd < 0) return SOCK_BAD;
    int opt = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET_NATIVE;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(0x7f000001U);
    if (SOCKET_BIND(fd, &addr, sizeof addr) < 0 || SOCKET_LISTEN(fd, 2) < 0) {
        SOCKET_CLOSE(fd);
        return SOCK_BAD;
    }
#endif
    return fd;
}

int main(void)
{
    FILE *log = fopen("/data/PS4AutoUpdater/local_http_status.txt", "wb");
    socket_id server = start_listener();
    if (server == SOCK_BAD) {
        if (log) { fputs("LOCAL_HTTP_BIND_FAILED\n", log); fclose(log); }
        return 1;
    }
    if (log) { fputs("LOCAL_HTTP_LISTENING 127.0.0.1:8127\n", log); fflush(log); }
    for (;;) {
        socket_id client = SOCKET_ACCEPT(server, NULL, NULL);
        if (client < 0) continue;
        handle_client(client);
        SOCKET_CLOSE(client);
    }
    /* Application exit closes the listener; it never installs packages. */
}
