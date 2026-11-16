#ifndef PS4_DOWNLOAD_HTTP_POLICY_H
#define PS4_DOWNLOAD_HTTP_POLICY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Header facts must be supplied by a trusted HTTP parser/TLS adapter.
 * This module does NOT establish a TLS connection or download content.
 * Every checked fact is necessary, not sufficient, for safe transfer. */
#define DL_HTTP_ETAG_CAP 96u

typedef struct DlHttpFacts {
    unsigned status;
    uint64_t content_length;
    uint64_t range_start;
    uint64_t range_end;
    uint64_t range_total;
    unsigned content_length_count;
    unsigned content_range_count;
    unsigned etag_count;
    char etag[DL_HTTP_ETAG_CAP];
    unsigned tls_authenticated; /* asserted only by trusted TLS adapter */
    unsigned range_requested;
    unsigned if_range_sent;
    unsigned transfer_encoding_present;
    unsigned content_encoding_present;
    unsigned redirect_location_present;
} DlHttpFacts;

typedef enum DlHttpDecision {
    DL_HTTP_ACCEPT=0,
    DL_HTTP_INVALID=-1,
    DL_HTTP_TLS_UNVERIFIED=-2,
    DL_HTTP_STATUS_REJECTED=-3,
    DL_HTTP_LENGTH_MISMATCH=-4,
    DL_HTTP_RANGE_MISMATCH=-5,
    DL_HTTP_ETAG_MISMATCH=-6,
    DL_HTTP_UNSUPPORTED_ENCODING=-7
} DlHttpDecision;

/* expected_total: independently trusted entire-file size.
 * resume_offset=0 requires HTTP 200; resume_offset>0 requires HTTP 206
 * with exact remaining range, matching strong ETag and If-Range request.
 * Never use response headers to create/authorize an update package. */
DlHttpDecision dl_http_review(const DlHttpFacts *response,
                              uint64_t expected_total,
                              uint64_t resume_offset,
                              const char *saved_strong_etag);
#ifdef __cplusplus
}
#endif
#endif
