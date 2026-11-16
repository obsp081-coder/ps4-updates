#ifndef PS4_DOWNLOAD_QUEUE_H
#define PS4_DOWNLOAD_QUEUE_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Stage 5: transport-independent, in-memory state machine.
 * NO network, filesystem, package installation or permission grant.
 * It is NOT wired to the production UI or enabled for PS4 downloads. */
#define DLQ_MAX_JOBS 16u

typedef enum DlqState {
    DLQ_QUEUED=0,
    DLQ_ACTIVE=1,
    DLQ_PAUSED=2,
    DLQ_VERIFYING=3,
    DLQ_COMPLETED=4,
    DLQ_FAILED=5,
    DLQ_CANCELLED=6
} DlqState;

typedef enum DlqError {
    DLQ_OK=0,
    DLQ_INVALID=-1,
    DLQ_FULL=-2,
    DLQ_DUPLICATE=-3,
    DLQ_BAD_STATE=-4,
    DLQ_NO_SPACE=-5,
    DLQ_UNSAFE_RESUME=-6,
    DLQ_SIZE_MISMATCH=-7,
    DLQ_INTEGRITY_FAILED=-8
} DlqError;

typedef struct DlqJob {
    char cusa[10];
    char content_id[37];
    char version[16];
    uint64_t expected_bytes;
    uint64_t received_bytes;
    uint32_t retries;
    DlqState state;
    DlqError last_error;
} DlqJob;

typedef struct DlqQueue {
    DlqJob jobs[DLQ_MAX_JOBS];
    unsigned count;
} DlqQueue;

void dlq_init(DlqQueue *queue);
int dlq_enqueue(DlqQueue *queue,const char *cusa,
                const char *content_id,const char *version,
                uint64_t expected_bytes);
const DlqJob *dlq_get(const DlqQueue *queue,unsigned index);

/* Preflight calculation for DOWNLOAD bytes + extra installation workspace
 * + user-defined safety reserve. These three values must be grounded by
 * a trusted upstream owner; passing 0 workspace does NOT prove install space.
 * shortage is always set when provided. Overflow fails closed. */
DlqError dlq_space_check(uint64_t expected_bytes,
                         uint64_t install_workspace,
                         uint64_t safety_reserve,uint64_t free_bytes,
                         uint64_t *shortage);

/* Caller remains responsible for trusted provenance, TLS, storage and
 * real transferred-byte accounting; none is implemented here. */
DlqError dlq_start(DlqQueue *queue,unsigned index,
                   uint64_t available_bytes,
                   uint64_t install_workspace,uint64_t safety_reserve,
                   uint64_t *shortage);
DlqError dlq_received(DlqQueue *queue,unsigned index,uint64_t delta);
DlqError dlq_pause(DlqQueue *queue,unsigned index);
/* Both proofs must come from a real transport/filesystem validator:
 * stable source (e.g. ETag/Range) AND verified partial file. */
DlqError dlq_resume(DlqQueue *queue,unsigned index,
                    int source_stable,int partial_verified);
DlqError dlq_begin_verification(DlqQueue *queue,unsigned index);
DlqError dlq_finish_verification(DlqQueue *queue,unsigned index,
                                 int checksum_matches);
DlqError dlq_fail(DlqQueue *queue,unsigned index,DlqError cause);
DlqError dlq_retry_from_zero(DlqQueue *queue,unsigned index);
DlqError dlq_cancel(DlqQueue *queue,unsigned index);

#ifdef __cplusplus
}
#endif
#endif
