#ifndef PS4_DOWNLOAD_PIPELINE_H
#define PS4_DOWNLOAD_PIPELINE_H
#include "download_queue.h"
#include "download_storage.h"
#include "download_http_headers.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Stage 5 orchestration contract. THIS IS NOT A PRODUCTION DOWNLOADER.
 * The pipeline has no socket, TLS, catalog, PKG or filesystem privileges.
 * An isolated Host-only adapter may feed actual localhost fixture bytes.
 * Runtime starts are deliberately disabled on __ORBIS__ until there is a
 * verified PS4 HTTPS+storage adapter, trusted source, and compatibility.
 * Resume is NOT implemented: nonzero offset is always rejected.
 */
typedef enum DlPipelineResult {
    DLP_OK=0,
    DLP_INVALID=-1,
    DLP_UNAVAILABLE=-2,
    DLP_HEADERS_REJECTED=-3,
    DLP_POLICY_REJECTED=-4,
    DLP_SPACE_REJECTED=-5,
    DLP_STORAGE_REJECTED=-6,
    DLP_BODY_REJECTED=-7,
    DLP_INCOMPLETE=-8,
    DLP_INTEGRITY_REJECTED=-9,
    DLP_CLEANUP_FAILED=-10,
    DLP_CANCELLED=-11
} DlPipelineResult;

typedef struct DlPipeline {
    DlqQueue *queue;
    unsigned index;
    DsPort storage_port;
    DsSession storage;
    uint8_t expected_sha256[32];
    unsigned initialized;
    unsigned started;
    unsigned stopped;
} DlPipeline;

/* Job must be independently configured with a trusted expected size,
 * CUSA/Content ID, version and independently known SHA-256. */
DlPipelineResult dlp_init(DlPipeline *pipe,DlqQueue *queue,
                          unsigned index,const DsPort *storage_port,
                          const uint8_t trusted_sha256[32]);

/* Headers MUST come from a validated HTTPS transport in production.
 * The tls_authenticated argument is ONLY meaningful if supplied by an
 * independently audited adapter. Host fixture tests simulate it with
 * explicit scope; on __ORBIS__ this API ALWAYS refuses to start.
 * This V1 pipeline never resumes existing partials. */
DlPipelineResult dlp_begin(DlPipeline *pipe,const char *header_bytes,
                            size_t header_length,int tls_authenticated,
                            uint64_t free_bytes,uint64_t workspace,
                            uint64_t reserve,uint64_t *shortage);

/* Count progress ONLY after a successful write_all() of real received
 * bytes. A chunk greater than 64KiB is rejected; no mock progress. */
DlPipelineResult dlp_feed(DlPipeline *pipe,const uint8_t *data,size_t count);

/* Fail closed on missing/overflowing bytes, failed stored-file SHA-256,
 * or any publish error; leave no successful queue completion behind. */
DlPipelineResult dlp_finish(DlPipeline *pipe);

/* Cancel the one active task and clean only its owned .part.
 * Failed cleanup returns a distinct result, never silent success. */
DlPipelineResult dlp_cancel(DlPipeline *pipe);

#ifdef __cplusplus
}
#endif
#endif
