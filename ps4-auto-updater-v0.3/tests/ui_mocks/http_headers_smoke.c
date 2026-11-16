/* Host-only strict HTTP/1.1 header decoding and policy handoff.
 * Transport/TLS status is a synthetic test assertion, not real TLS. */
#include "../../ps4/src/download_http_headers.h"
#include "../../ps4/src/download_http_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static DlHttpHeaderResult parse(const char *s,DlHttpFacts *out) {
    return dl_http_parse_headers(s,strlen(s),out);
}
static void rejects(const char *s,DlHttpHeaderResult expected) {
    DlHttpFacts got;
    memset(&got,0x5a,sizeof got);
    assert(parse(s,&got)==expected);
    const DlHttpFacts zero={0};
    assert(!memcmp(&got,&zero,sizeof got));
}
int main(void) {
    static const char *full=
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 10\r\n"
        "eTAG: \"known-source\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "X-TLS-Authenticated: true\r\n"
        "\r\n";
    DlHttpFacts got;
    assert(parse(full,&got)==DL_HTTP_HEADER_OK);
    assert(got.status==200u && got.content_length==10u);
    assert(got.content_length_count==1u && got.etag_count==1u);
    assert(!strcmp(got.etag,"\"known-source\""));
    assert(!got.tls_authenticated && !got.range_requested &&
           !got.if_range_sent);
    assert(dl_http_review(&got,10u,0u,NULL)==DL_HTTP_TLS_UNVERIFIED);
    /* TLS and request intent MUST be independently proven by adapter. */
    got.tls_authenticated=1u; /* synthetic test only */
    assert(dl_http_review(&got,10u,0u,NULL)==DL_HTTP_ACCEPT);

    static const char *partial=
        "HTTP/1.1 206 Partial Content\r\n"
        "Content-Length: 5\r\n"
        "Content-Range: bytes 5-9/10\r\n"
        "ETag: \"known-source\"\r\n"
        "\r\n";
    assert(parse(partial,&got)==DL_HTTP_HEADER_OK);
    assert(got.content_range_count==1u);
    assert(got.range_start==5u && got.range_end==9u &&
           got.range_total==10u && got.content_length==5u);
    got.tls_authenticated=1u;
    got.range_requested=1u;
    got.if_range_sent=1u;
    assert(dl_http_review(&got,10u,5u,"\"known-source\"")==DL_HTTP_ACCEPT);
    assert(dl_http_review(&got,10u,5u,"\"changed\"")==DL_HTTP_ETAG_MISMATCH);
    got.range_requested=0u;
    assert(dl_http_review(&got,10u,5u,"\"known-source\"")==
           DL_HTTP_STATUS_REJECTED);

    rejects("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n"
            "content-length: 10\r\n\r\n",DL_HTTP_HEADER_DUPLICATE);
    rejects("HTTP/1.1 206 X\r\nContent-Length: 5\r\n"
            "Content-Range: bytes 5-9/10\r\n"
            "content-range: bytes 5-9/10\r\n\r\n",
            DL_HTTP_HEADER_DUPLICATE);
    rejects("HTTP/1.1 200 X\r\nETag: \"a\"\r\n"
            "ETag: \"b\"\r\n\r\n",DL_HTTP_HEADER_DUPLICATE);
    rejects("HTTP/1.1 200 X\nContent-Length: 10\n\n",
            DL_HTTP_HEADER_INVALID);
    rejects("HTTP/1.1 200 X\r\n Content-Length: 10\r\n\r\n",
            DL_HTTP_HEADER_INVALID); /* obs-fold is forbidden */
    rejects("HTTP/1.1 200 X\r\nContent-Length : 10\r\n\r\n",
            DL_HTTP_HEADER_INVALID); /* whitespace before colon */
    rejects("HTTP/1.1 200 X\r\nContent-Length: 10, 10\r\n\r\n",
            DL_HTTP_HEADER_INVALID);
    rejects("HTTP/1.1 200 X\r\nContent-Length: +10\r\n\r\n",
            DL_HTTP_HEADER_INVALID);
    rejects("HTTP/1.1 200 X\r\nContent-Length: 18446744073709551616\r\n\r\n",
            DL_HTTP_HEADER_OVERFLOW);
    rejects("HTTP/1.1 206 X\r\nContent-Range: bytes 5-9/*\r\n\r\n",
            DL_HTTP_HEADER_INVALID);
    rejects("HTTP/1.1 206 X\r\nContent-Range: bytes 9-5/10\r\n\r\n",
            DL_HTTP_HEADER_INVALID);
    rejects("HTTP/1.1 206 X\r\nContent-Range: bytes 5-10/10\r\n\r\n",
            DL_HTTP_HEADER_INVALID);
    rejects("HTTP/1.1 100 Continue\r\n\r\n",DL_HTTP_HEADER_INVALID);
    rejects("HTTP/2 200 OK\r\n\r\n",DL_HTTP_HEADER_INVALID);
    rejects("HTTP/1.1 200 X\r\n\r\nBODY",DL_HTTP_HEADER_INVALID);
    rejects("HTTP/1.1 200 X\r\nContent-Length: 10\r\n",
            DL_HTTP_HEADER_INVALID);

    static const char *unsupported=
        "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n"
        "Transfer-Encoding: chunked\r\nContent-Encoding: gzip\r\n"
        "Location: https://bad.invalid/elsewhere\r\n\r\n";
    assert(parse(unsupported,&got)==DL_HTTP_HEADER_OK);
    assert(got.transfer_encoding_present && got.content_encoding_present &&
           got.redirect_location_present);
    got.tls_authenticated=1u;
    assert(dl_http_review(&got,10u,0u,NULL)==DL_HTTP_UNSUPPORTED_ENCODING);

    char many[10000];
    size_t n=0;
    n+=(size_t)snprintf(many+n,sizeof many-n,"HTTP/1.1 200 X\r\n");
    for(unsigned i=0;i<DL_HTTP_HEADER_MAX_FIELDS+1u;++i)
        n+=(size_t)snprintf(many+n,sizeof many-n,"X-Field: ok\r\n");
    n+=(size_t)snprintf(many+n,sizeof many-n,"\r\n");
    assert(n<sizeof many);
    rejects(many,DL_HTTP_HEADER_TOO_LARGE);
    memset(many,'A',sizeof many);
    many[0]='H';
    assert(dl_http_parse_headers(many,sizeof many,&got)==
           DL_HTTP_HEADER_TOO_LARGE);
    /* Truncated blocks and NUL injection cannot become valid. */
    const char nul_header[]="HTTP/1.1 200 OK\r\nContent-Length: 1\0X\r\n\r\n";
    assert(dl_http_parse_headers(nul_header,sizeof nul_header-1u,&got)==
           DL_HTTP_HEADER_INVALID);
    assert(dl_http_parse_headers(full,strlen(full),NULL)==
           DL_HTTP_HEADER_INVALID);
    puts("DOWNLOAD_HTTP_HEADERS_HOST_OK");
    return 0;
}
