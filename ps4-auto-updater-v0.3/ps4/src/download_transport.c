#include "download_transport.h"
#include "download_http_policy.h"
#include <string.h>

#ifndef __ORBIS__
/* Conservative HTTPS URLs: no userinfo, explicit ports, fragments, control
 * characters, IP literals or percent-encoded hostnames. Backend must also
 * enforce a verified hostname and forbid redirects/proxies independently.
 */
static int valid_https_url(const char *url) {
    if (!url) return 0;
    size_t n = 0;
    while (n < DT_MAX_URL && url[n]) ++n;
    if (n < 12u || n >= DT_MAX_URL || strncmp(url, "https://", 8u))
        return 0;
    const char *host = url + 8u;
    size_t host_length = 0, label_length = 0;
    int saw_dot = 0;
    for (size_t i = 8u; i < n; ++i) {
        const unsigned char c = (unsigned char)url[i];
        if (c == '/' || c == '?') break;
        if (c == '.') {
            if (!label_length || host_length > 252u ||
                url[i - 1u] == '-') return 0;
            saw_dot = 1;
            label_length = 0;
        } else if ((c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '-') {
            if (label_length == 0u && c == '-') return 0;
            ++label_length;
            if (label_length > 63u) return 0;
        } else return 0;
        ++host_length;
    }
    if (!saw_dot || label_length == 0u ||
        host_length > 253u || host[host_length - 1u] == '-')
        return 0;
    for (size_t i = 8u + host_length; i < n; ++i) {
        const unsigned char c = (unsigned char)url[i];
        if (c < 0x21u || c > 0x7eu || c == '#' || c == '\\')
            return 0;
    }
    return 1;
}

static int complete_port(const DtPort *p) {
    return p && p->open_headers && p->read_body &&
           p->abort_request && p->close_request;
}

static DtResult stop(DtSession *s, DtResult reason) {
    if (!s || !s->active) return reason;
    const int closed = s->port.close_request(s->port.context);
    s->active = 0u;
    s->stopped = 1u;
    return closed == 0 ? reason : DT_CLOSE_FAILED;
}
#endif

void dt_init(DtSession *s) {
    if (s) memset(s, 0, sizeof *s);
}

DtResult dt_begin(DtSession *s, const DtPort *port,
                  const DtRequest *request) {
#ifdef __ORBIS__
    (void)s;
    (void)port;
    (void)request;
    /* Do not execute callbacks, even when synthetic TLS flags are 1. */
    return DT_UNAVAILABLE;
#else
    if (!s || !port || !request || !complete_port(port) ||
        s->active || s->stopped || request->expected_total == 0u ||
        request->resume_offset >= request->expected_total)
        return DT_INVALID;
    if (!valid_https_url(request->url)) return DT_URL_REJECTED;
    if ((request->resume_offset > 0u && !request->strong_etag) ||
        (request->resume_offset == 0u && request->strong_etag))
        return DT_INVALID;

    char raw[DL_HTTP_HEADER_MAX_BYTES];
    size_t n = 0u;
    DtPeerFacts peer = {0};
    memset(raw, 0, sizeof raw);
    if (port->open_headers(port->context, request, raw, sizeof raw,
                           &n, &peer) != 0) return DT_CONNECT_FAILED;

    s->port = *port;
    s->active = 1u;
    s->remaining = request->expected_total - request->resume_offset;
    if (n == 0u || n > sizeof raw)
        return stop(s, DT_HEADERS_REJECTED);
    if (peer.certificate_chain_verified != 1u ||
        peer.hostname_verified != 1u)
        return stop(s, DT_TLS_UNVERIFIED);
    if ((request->resume_offset == 0u &&
         (peer.range_sent || peer.if_range_sent)) ||
        (request->resume_offset > 0u &&
         (peer.range_sent != 1u || peer.if_range_sent != 1u)))
        return stop(s, DT_POLICY_REJECTED);

    DlHttpFacts headers;
    if (dl_http_parse_headers(raw, n, &headers) != DL_HTTP_HEADER_OK)
        return stop(s, DT_HEADERS_REJECTED);
    headers.tls_authenticated = 1u;
    headers.range_requested = peer.range_sent;
    headers.if_range_sent = peer.if_range_sent;
    if (dl_http_review(&headers, request->expected_total,
                       request->resume_offset,
                       request->strong_etag) != DL_HTTP_ACCEPT)
        return stop(s, DT_POLICY_REJECTED);
    return DT_OK;
#endif
}

DtResult dt_read(DtSession *s, uint8_t *buffer, size_t capacity,
                 size_t *received) {
    if (received) *received = 0u;
    if (!s || !s->active || s->stopped || !buffer || !received ||
        !capacity || capacity > DT_MAX_READ || !s->port.read_body)
        return DT_INVALID;
    if (s->delivered == s->remaining) return DT_INVALID;
    const uint64_t left = s->remaining - s->delivered;
    const size_t bounded = (uint64_t)capacity > left ? (size_t)left : capacity;
    size_t n = 0u;
    if (s->port.read_body(s->port.context, buffer, bounded, &n) != 0)
        return DT_READ_FAILED;
    if (n > bounded) return DT_EXTRA_DATA;
    if (!n) return DT_INCOMPLETE;
    s->delivered += (uint64_t)n;
    *received = n;
    return DT_OK;
}

DtResult dt_finish(DtSession *s) {
    if (!s || !s->active || s->stopped || !s->port.read_body)
        return DT_INVALID;
    if (s->delivered != s->remaining) return DT_INCOMPLETE;
    uint8_t extra = 0u;
    size_t got = 0u;
    if (s->port.read_body(s->port.context, &extra, 1u, &got) != 0)
        return DT_READ_FAILED;
    if (got != 0u) return DT_EXTRA_DATA;
    return stop(s, DT_OK);
}

DtResult dt_cancel(DtSession *s) {
    if (!s || !s->active || s->stopped) return DT_INVALID;
    const int aborted = s->port.abort_request(s->port.context);
    const DtResult closed = stop(s, DT_OK);
    return aborted == 0 ? closed : DT_CLOSE_FAILED;
}
