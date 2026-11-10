/* Host-only synthetic TLS facts. Never claims to verify certificates. */
#include "../../ps4/src/download_transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct Fixture {
    const char *headers;
    const unsigned char *data;
    size_t size, offset;
    unsigned chain, hostname, range, if_range;
    unsigned opens, aborts, closes;
} Fixture;

static int open_mock(void *arg, const DtRequest *r, char *dst, size_t cap,
                     size_t *size, DtPeerFacts *peer) {
    Fixture *f = (Fixture *)arg;
    (void)r;
    const size_t n = strlen(f->headers);
    if (n > cap) return -1;
    memcpy(dst, f->headers, n);
    *size = n;
    peer->certificate_chain_verified = f->chain;
    peer->hostname_verified = f->hostname;
    peer->range_sent = f->range;
    peer->if_range_sent = f->if_range;
    f->opens++;
    return 0;
}
static int read_mock(void *arg, uint8_t *dst, size_t cap, size_t *got) {
    Fixture *f = (Fixture *)arg;
    const size_t left = f->size - f->offset;
    *got = cap < left ? cap : left;
    if (*got) memcpy(dst, f->data + f->offset, *got);
    f->offset += *got;
    return 0;
}
static int abort_mock(void *arg) {
    ((Fixture *)arg)->aborts++;
    return 0;
}
static int close_mock(void *arg) {
    ((Fixture *)arg)->closes++;
    return 0;
}
static const char *OK =
    "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n"
    "ETag: \"source-v1\"\r\n\r\n";
static const char *PARTIAL =
    "HTTP/1.1 206 Partial Content\r\nContent-Length: 3\r\n"
    "Content-Range: bytes 3-5/6\r\n"
    "ETag: \"source-v1\"\r\n\r\n";

static DtPort port(Fixture *f) {
    DtPort p = {0};
    p.context = f;
    p.open_headers = open_mock;
    p.read_body = read_mock;
    p.abort_request = abort_mock;
    p.close_request = close_mock;
    return p;
}
static Fixture fixture(const char *headers) {
    Fixture f = {0};
    static const unsigned char DATA[] = "abcdef";
    f.headers = headers;
    f.data = DATA;
    f.size = 6u;
    f.chain = f.hostname = 1u;
    return f;
}
int main(void) {
    DtRequest request = {"https://fixture.example/test", 6u, 0u, NULL};
    Fixture f = fixture(OK);
    DtPort p = port(&f);
    DtSession s;
    dt_init(&s);
#ifdef __ORBIS__
    assert(dt_begin(&s, &p, &request) == DT_UNAVAILABLE);
    assert(f.opens == 0u && f.closes == 0u && f.aborts == 0u);
    puts("DOWNLOAD_TRANSPORT_PS4_DISABLED_OK");
#else
    uint8_t bytes[8] = {0};
    size_t got = 0;
    assert(dt_begin(&s, &p, &request) == DT_OK);
    assert(dt_read(&s, bytes, 6u, &got) == DT_OK);
    assert(got == 6u && memcmp(bytes, "abcdef", 6u) == 0);
    assert(dt_finish(&s) == DT_OK);
    assert(f.opens == 1u && f.closes == 1u);

    f = fixture(OK);
    p = port(&f);
    dt_init(&s);
    request.url = "http://fixture.example/test";
    assert(dt_begin(&s, &p, &request) == DT_URL_REJECTED);
    request.url = "https://u:p@fixture.example/test";
    assert(dt_begin(&s, &p, &request) == DT_URL_REJECTED);
    assert(f.opens == 0u);
    request.url = "https://fixture.example/test";

    f = fixture(OK); f.chain = 0u;
    p = port(&f); dt_init(&s);
    assert(dt_begin(&s, &p, &request) == DT_TLS_UNVERIFIED);
    assert(f.closes == 1u);
    f = fixture(OK); f.hostname = 0u;
    p = port(&f); dt_init(&s);
    assert(dt_begin(&s, &p, &request) == DT_TLS_UNVERIFIED);

    f = fixture("HTTP/1.1 302 Found\r\nContent-Length: 6\r\n"
                "Location: https://other.example/\r\n\r\n");
    p = port(&f); dt_init(&s);
    assert(dt_begin(&s, &p, &request) == DT_POLICY_REJECTED);
    assert(f.closes == 1u);

    f = fixture(OK); f.size = 3u;
    p = port(&f); dt_init(&s);
    assert(dt_begin(&s, &p, &request) == DT_OK);
    assert(dt_read(&s, bytes, 8u, &got) == DT_OK && got == 3u);
    assert(dt_read(&s, bytes, 8u, &got) == DT_INCOMPLETE);
    assert(dt_cancel(&s) == DT_OK && f.aborts == 1u && f.closes == 1u);

    f = fixture(OK); f.size = 7u;
    static const unsigned char EXTRA[] = "abcdefX";
    f.data = EXTRA;
    p = port(&f); dt_init(&s);
    assert(dt_begin(&s, &p, &request) == DT_OK);
    assert(dt_read(&s, bytes, 6u, &got) == DT_OK);
    assert(dt_finish(&s) == DT_EXTRA_DATA);
    assert(dt_cancel(&s) == DT_OK);

    request.resume_offset = 3u;
    request.strong_etag = "\"source-v1\"";
    f = fixture(PARTIAL); f.offset = 3u;
    f.range = f.if_range = 1u;
    p = port(&f); dt_init(&s);
    assert(dt_begin(&s, &p, &request) == DT_OK);
    assert(dt_read(&s, bytes, 3u, &got) == DT_OK && got == 3u);
    assert(dt_finish(&s) == DT_OK);

    f = fixture(PARTIAL); f.range = 1u;
    p = port(&f); dt_init(&s);
    assert(dt_begin(&s, &p, &request) == DT_POLICY_REJECTED);
    f = fixture(OK); f.range = f.if_range = 1u;
    p = port(&f); dt_init(&s);
    assert(dt_begin(&s, &p, &request) == DT_POLICY_REJECTED);
    puts("DOWNLOAD_TRANSPORT_HOST_POLICY_OK");
#endif
    return 0;
}
