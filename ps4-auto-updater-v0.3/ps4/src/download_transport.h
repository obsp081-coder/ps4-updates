#ifndef PS4_DOWNLOAD_TRANSPORT_H
#define PS4_DOWNLOAD_TRANSPORT_H

#include "download_http_headers.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bounded, callback-driven transport boundary, NOT a TLS implementation.
 * The Host may inject a synthetic adapter to test policy and I/O edges.
 * On __ORBIS__, dt_begin() ALWAYS returns DT_UNAVAILABLE without calling
 * callbacks until SceHttp/SceSsl chain+hostname validation is demonstrated.
 * This module does not create files or authorize a package source.
 */
#define DT_MAX_READ 65536u
#define DT_MAX_URL 2048u
/* The transport may return this from open_headers ONLY for a genuine
 * certificate-chain or hostname-verification failure. Other connection,
 * DNS, timeout or I/O failures must return a different nonzero code.
 * The flag is never derived from server-controlled HTTP headers.
 */
#define DT_PORT_TLS_VERIFY_FAILED (-2)

typedef enum DtResult {
    DT_OK = 0,
    DT_INVALID = -1,
    DT_UNAVAILABLE = -2,
    DT_URL_REJECTED = -3,
    DT_CONNECT_FAILED = -4,
    DT_TLS_UNVERIFIED = -5,
    DT_HEADERS_REJECTED = -6,
    DT_POLICY_REJECTED = -7,
    DT_READ_FAILED = -8,
    DT_INCOMPLETE = -9,
    DT_EXTRA_DATA = -10,
    DT_CLOSE_FAILED = -11
} DtResult;

typedef struct DtRequest {
    const char *url;                 /* HTTPS URL without credentials */
    uint64_t expected_total;         /* trusted independent manifest */
    uint64_t resume_offset;          /* 0 for initial request */
    const char *strong_etag;         /* required only for resume */
} DtRequest;

/* Facts MUST be obtained from the real transport, not from HTTP headers.
 * In Host tests they are synthetic and NEVER proof of verified certificates.
 * open_headers must report precisely the request headers actually sent,
 * and use DT_PORT_TLS_VERIFY_FAILED only after real TLS identity failure.
 */
typedef struct DtPeerFacts {
    unsigned certificate_chain_verified;
    unsigned hostname_verified;
    unsigned range_sent;
    unsigned if_range_sent;
} DtPeerFacts;

typedef struct DtPort {
    void *context;
    int (*open_headers)(void *, const DtRequest *, char *, size_t,
                        size_t *, DtPeerFacts *);
    int (*read_body)(void *, uint8_t *, size_t, size_t *);
    int (*abort_request)(void *);
    int (*close_request)(void *);
} DtPort;

typedef struct DtSession {
    DtPort port;
    uint64_t remaining;
    uint64_t delivered;
    unsigned active;
    unsigned stopped;
} DtSession;

void dt_init(DtSession *session);
/* On Host only: verifies URL policy, observed TLS facts, exact response
 * headers, expected length, Content-Range and ETag before any body read.
 * Actual HTTPS certificate/hostname verification is backend responsibility.
 * No adapter implementing this responsibility is currently approved.
 */
DtResult dt_begin(DtSession *session, const DtPort *port,
                  const DtRequest *request);
/* Read no more than 64 KiB. Returned bytes are NOT download progress
 * until downstream storage confirms write_all() and final SHA-256 succeeds.
 */
DtResult dt_read(DtSession *session, uint8_t *buffer, size_t capacity,
                 size_t *received);
/* Requires all expected bytes and real EOF; rejects trailing bytes. */
DtResult dt_finish(DtSession *session);
/* Abort/close a started request; errors do not imply successful cancellation. */
DtResult dt_cancel(DtSession *session);

#ifdef __cplusplus
}
#endif
#endif
