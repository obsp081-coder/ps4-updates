#include "download_pipeline.h"
#include <string.h>

/* Callbacks are never invoked by a production PS4 binary. */
static DlPipelineResult stop_failed(DlPipeline *p,DlqError reason,
                                    DlPipelineResult outcome) {
    if(!p)return DLP_INVALID;
    if(p->started) {
        (void)dlq_fail(p->queue,p->index,reason);
        p->started=0u;
    }
    p->stopped=1u;
    if(p->storage.owns_part || p->storage.file_open) {
        if(ds_abort(&p->storage)!=DS_OK)return DLP_CLEANUP_FAILED;
    }
    return outcome;
}
DlPipelineResult dlp_init(DlPipeline *p,DlqQueue *q,
                          unsigned index,const DsPort *port,
                          const uint8_t digest[32]) {
    if(!p || !q || !port || !digest ||
       !dlq_get(q,index) ||
       dlq_get(q,index)->state!=DLQ_QUEUED)return DLP_INVALID;
    memset(p,0,sizeof *p);
    p->queue=q;
    p->index=index;
    p->storage_port=*port;
    memcpy(p->expected_sha256,digest,32);
    ds_init(&p->storage);
    p->initialized=1u;
    return DLP_OK;
}
DlPipelineResult dlp_begin(DlPipeline *p,const char *bytes,
                            size_t length,int tls_authenticated,
                            uint64_t free_bytes,uint64_t workspace,
                            uint64_t reserve,uint64_t *shortage) {
    if(shortage)*shortage=0;
    if(!p || !p->initialized || p->started || p->stopped ||
       !bytes || !length)return DLP_INVALID;
#ifdef __ORBIS__
    /* Deliberately hard-disabled, even when caller spoofs TLS trust.
     * FW14 transport, authorized update sources and exclusive publish
     * are NOT proven. No files, sockets or queue jobs are started. */
    (void)tls_authenticated;
    (void)free_bytes;
    (void)workspace;
    (void)reserve;
    return DLP_UNAVAILABLE;
#else
    const DlqJob *job=dlq_get(p->queue,p->index);
    if(!job || job->state!=DLQ_QUEUED)return DLP_INVALID;
    DlHttpFacts headers;
    if(dl_http_parse_headers(bytes,length,&headers)!=DL_HTTP_HEADER_OK)
        return DLP_HEADERS_REJECTED;
    /* Header text cannot assert this flag: only transport-supplied fact. */
    headers.tls_authenticated=tls_authenticated==1?1u:0u;
    if(dl_http_review(&headers,job->expected_bytes,0u,NULL)!=DL_HTTP_ACCEPT)
        return DLP_POLICY_REJECTED;
    DlqError rc=dlq_start(p->queue,p->index,free_bytes,workspace,
                         reserve,shortage);
    if(rc==DLQ_NO_SPACE)return DLP_SPACE_REJECTED;
    if(rc!=DLQ_OK)return DLP_INVALID;
    p->started=1u;
    if(ds_begin(&p->storage,&p->storage_port,job->content_id,
                job->version,job->expected_bytes,
                p->expected_sha256)!=DS_OK)
        return stop_failed(p,DLQ_INVALID,DLP_STORAGE_REJECTED);
    return DLP_OK;
#endif
}
DlPipelineResult dlp_feed(DlPipeline *p,const uint8_t *bytes,size_t size) {
    if(!p || !p->initialized || !p->started || p->stopped)
        return DLP_INVALID;
    const DlqJob *job=dlq_get(p->queue,p->index);
    if(!job || job->state!=DLQ_ACTIVE)return DLP_INVALID;
    if(!bytes || size==0u || size>DS_WRITE_CAP ||
       (uint64_t)size>job->expected_bytes-job->received_bytes)
        return stop_failed(p,DLQ_SIZE_MISMATCH,DLP_BODY_REJECTED);
    if(ds_write(&p->storage,bytes,size)!=DS_OK)
        return stop_failed(p,DLQ_INVALID,DLP_STORAGE_REJECTED);
    if(dlq_received(p->queue,p->index,(uint64_t)size)!=DLQ_OK)
        return stop_failed(p,DLQ_INVALID,DLP_BODY_REJECTED);
    return DLP_OK;
}
DlPipelineResult dlp_finish(DlPipeline *p) {
    if(!p || !p->initialized || !p->started || p->stopped)
        return DLP_INVALID;
    if(dlq_begin_verification(p->queue,p->index)!=DLQ_OK)
        return stop_failed(p,DLQ_SIZE_MISMATCH,DLP_INCOMPLETE);
    const DsResult rc=ds_finalize(&p->storage);
    if(rc!=DS_OK) {
        const DlqError reason=rc==DS_VERIFY_FAILED ?
                             DLQ_INTEGRITY_FAILED:DLQ_INVALID;
        return stop_failed(p,reason,
                           rc==DS_VERIFY_FAILED ? DLP_INTEGRITY_REJECTED :
                           rc==DS_CLEANUP_FAILED ? DLP_CLEANUP_FAILED :
                           DLP_STORAGE_REJECTED);
    }
    if(dlq_finish_verification(p->queue,p->index,1)!=DLQ_OK)
        return stop_failed(p,DLQ_INVALID,DLP_STORAGE_REJECTED);
    p->started=0u;
    p->stopped=1u;
    return DLP_OK;
}
DlPipelineResult dlp_cancel(DlPipeline *p) {
    if(!p || !p->initialized || !p->started || p->stopped)
        return DLP_INVALID;
    if(dlq_cancel(p->queue,p->index)!=DLQ_OK)return DLP_INVALID;
    p->started=0u;
    p->stopped=1u;
    if(p->storage.owns_part || p->storage.file_open)
        if(ds_abort(&p->storage)!=DS_OK)return DLP_CLEANUP_FAILED;
    return DLP_CANCELLED;
}
