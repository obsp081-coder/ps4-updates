#ifndef PS4_DOWNLOAD_HTTP_HEADERS_H
#define PS4_DOWNLOAD_HTTP_HEADERS_H
#include "download_http_policy.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bounded, strict HTTP/1.1 response-header decoder (no sockets, no TLS).
 * This decoder never marks tls_authenticated or request intent as true.
 * A future authenticated transport MUST separately provide those facts.
 * It accepts ONLY one complete header block (no body or interim 1xx).
 *
 * This is intentionally a restrictive safe subset: rejects obs-fold,
 * non-ASCII, duplicate security-critical fields, and unsupported syntax.
 * The output is zeroed on all failures and contains no authorization.
 */
#define DL_HTTP_HEADER_MAX_BYTES 8192u
#define DL_HTTP_HEADER_MAX_LINE 1024u
#define DL_HTTP_HEADER_MAX_FIELDS 64u

typedef enum DlHttpHeaderResult {
    DL_HTTP_HEADER_OK=0,
    DL_HTTP_HEADER_INVALID=-1,
    DL_HTTP_HEADER_TOO_LARGE=-2,
    DL_HTTP_HEADER_DUPLICATE=-3,
    DL_HTTP_HEADER_OVERFLOW=-4
} DlHttpHeaderResult;

DlHttpHeaderResult dl_http_parse_headers(const char *data,size_t length,
                                         DlHttpFacts *out);

#ifdef __cplusplus
}
#endif
#endif
