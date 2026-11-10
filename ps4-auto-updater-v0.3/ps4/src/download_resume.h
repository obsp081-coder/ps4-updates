#ifndef PS4_DOWNLOAD_RESUME_H
#define PS4_DOWNLOAD_RESUME_H

#include "download_http_headers.h"
#include "download_sha256.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A checkpoint is untrusted until compared against the independent
 * authorized manifest, the ACTUAL stored partial file, and authenticated
 * HTTP 206 response. This API makes NO network or file-system calls.
 * V1 does not activate downloads or restore an owned file on PS4. */
typedef struct DlResumeCheckpoint {
    char content_id[37];
    char version[16];
    char strong_etag[DL_HTTP_ETAG_CAP];
    uint64_t total_size;
    uint64_t offset;
    uint8_t full_sha256[32];
    uint8_t partial_sha256[32];
} DlResumeCheckpoint;

typedef enum DlResumeResult {
    DL_RESUME_OK=0,
    DL_RESUME_INVALID=-1,
    DL_RESUME_MANIFEST_MISMATCH=-2,
    DL_RESUME_PARTIAL_MISMATCH=-3,
    DL_RESUME_HEADERS_INVALID=-4,
    DL_RESUME_RESPONSE_REJECTED=-5,
    DL_RESUME_TLS_UNVERIFIED=-6
} DlResumeResult;

/* trusted_* MUST come from a separately authenticated, authorized catalog.
 * actual_partial_sha256 MUST be freshly hashed from the stored .part file,
 * not copied from the checkpoint or HTTP response.
 * request_range_sent/if_range_sent MUST be transport-observed request facts,
 * not headers controlled by the server.
 * tls_verified MUST be the authenticated transport's verified-chain result.
 * Returning DL_RESUME_OK is necessary but not sufficient for writing: the
 * backend must revalidate ownership, inode and offset before append.
 */
DlResumeResult dl_resume_review(
    const DlResumeCheckpoint *checkpoint,
    const char *trusted_content_id,
    const char *trusted_version,
    uint64_t trusted_total_size,
    const uint8_t trusted_full_sha256[32],
    uint64_t actual_partial_size,
    const uint8_t actual_partial_sha256[32],
    const char *http_response_headers,
    size_t header_length,
    int tls_verified,
    int request_range_sent,
    int if_range_sent);

#ifdef __cplusplus
}
#endif
#endif
