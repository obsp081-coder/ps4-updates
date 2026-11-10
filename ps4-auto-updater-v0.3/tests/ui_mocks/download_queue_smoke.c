/* Host-only Stage-5 test. No HTTP, files, games or installs. */
#include "../../ps4/src/download_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static const char *cid="IV0000-CUSA02343_00-SCANNER000000000";
static void enqueue_one(DlqQueue *q) {
    dlq_init(q);
    assert(dlq_enqueue(q,"CUSA02343",cid,"01.09",1000u)==0);
}

int main(void) {
    DlqQueue q;
    uint64_t missing=777;

    assert(dlq_space_check(1000,300,100,1400,&missing)==DLQ_OK);
    assert(missing==0);
    assert(dlq_space_check(1000,300,100,1250,&missing)==DLQ_NO_SPACE);
    assert(missing==150);
    assert(dlq_space_check(0,300,100,0,&missing)==DLQ_INVALID);
    assert(missing==0);
    assert(dlq_space_check(UINT64_MAX,1,0,UINT64_MAX,&missing)==DLQ_INVALID);
    assert(dlq_space_check(UINT64_MAX-10,0,11,UINT64_MAX,
                           &missing)==DLQ_INVALID);
    assert(dlq_space_check(1,0,0,1,NULL)==DLQ_OK);

    enqueue_one(&q);
    assert(q.count==1);
    assert(dlq_get(&q,0)->state==DLQ_QUEUED);
    assert(dlq_get(&q,1)==NULL);
    assert(dlq_get(NULL,0)==NULL);
    assert(dlq_enqueue(&q,"CUSA02343",cid,"01.09",1000)==DLQ_DUPLICATE);
    assert(dlq_enqueue(&q,"CUSA00000",cid,"01.09",1000)==DLQ_INVALID);
    assert(dlq_enqueue(&q,"CUSA02343","invalid","01.09",1000)==DLQ_INVALID);
    assert(dlq_enqueue(&q,"CUSA02343",cid,"1.99999",1000)==DLQ_INVALID);
    assert(dlq_enqueue(&q,"CUSA02343",cid,"01.09",0)==DLQ_INVALID);
    assert(q.count==1);

    assert(dlq_start(&q,0,1250,300,100,&missing)==DLQ_NO_SPACE);
    assert(missing==150);
    assert(dlq_get(&q,0)->state==DLQ_QUEUED);
    assert(dlq_get(&q,0)->received_bytes==0);
    assert(dlq_start(&q,0,1400,300,100,&missing)==DLQ_OK);
    assert(missing==0);
    assert(dlq_get(&q,0)->state==DLQ_ACTIVE);
    assert(dlq_start(&q,0,1400,300,100,NULL)==DLQ_BAD_STATE);
    assert(dlq_received(&q,0,0)==DLQ_INVALID);
    assert(dlq_received(&q,0,500)==DLQ_OK);
    assert(dlq_received(&q,0,501)==DLQ_SIZE_MISMATCH);
    assert(dlq_get(&q,0)->received_bytes==500);
    assert(dlq_begin_verification(&q,0)==DLQ_SIZE_MISMATCH);

    /* One active/verification job at a time. */
    assert(dlq_enqueue(&q,"CUSA02343",cid,"01.10",500)==1);
    assert(dlq_start(&q,1,1000,0,0,NULL)==DLQ_BAD_STATE);

    assert(dlq_pause(&q,0)==DLQ_OK);
    assert(dlq_received(&q,0,1)==DLQ_BAD_STATE);
    assert(dlq_resume(&q,0,0,1)==DLQ_UNSAFE_RESUME);
    assert(dlq_get(&q,0)->state==DLQ_PAUSED);
    assert(dlq_resume(&q,0,1,0)==DLQ_UNSAFE_RESUME);
    assert(dlq_resume(&q,0,1,1)==DLQ_OK);
    assert(dlq_received(&q,0,500)==DLQ_OK);
    assert(dlq_get(&q,0)->received_bytes==1000);
    assert(dlq_received(&q,0,1)==DLQ_SIZE_MISMATCH);
    assert(dlq_begin_verification(&q,0)==DLQ_OK);
    assert(dlq_get(&q,0)->state==DLQ_VERIFYING);
    assert(dlq_start(&q,1,1000,0,0,NULL)==DLQ_BAD_STATE);
    assert(dlq_finish_verification(&q,0,0)==DLQ_INTEGRITY_FAILED);
    assert(dlq_get(&q,0)->state==DLQ_FAILED);
    assert(dlq_get(&q,0)->last_error==DLQ_INTEGRITY_FAILED);
    assert(dlq_retry_from_zero(&q,0)==DLQ_OK);
    assert(dlq_get(&q,0)->state==DLQ_QUEUED);
    assert(dlq_get(&q,0)->received_bytes==0);
    assert(dlq_get(&q,0)->retries==1);

    assert(dlq_start(&q,1,500,0,0,NULL)==DLQ_OK);
    assert(dlq_cancel(&q,1)==DLQ_OK);
    assert(dlq_get(&q,1)->state==DLQ_CANCELLED);
    assert(dlq_retry_from_zero(&q,1)==DLQ_BAD_STATE);
    assert(dlq_start(&q,1,500,0,0,NULL)==DLQ_BAD_STATE);

    assert(dlq_start(&q,0,1000,0,0,NULL)==DLQ_OK);
    assert(dlq_received(&q,0,1000)==DLQ_OK);
    assert(dlq_begin_verification(&q,0)==DLQ_OK);
    assert(dlq_finish_verification(&q,0,1)==DLQ_OK);
    assert(dlq_get(&q,0)->state==DLQ_COMPLETED);
    assert(dlq_retry_from_zero(&q,0)==DLQ_BAD_STATE);
    assert(dlq_cancel(&q,0)==DLQ_BAD_STATE);
    assert(dlq_received(&q,0,1)==DLQ_BAD_STATE);
    assert(dlq_finish_verification(&q,0,1)==DLQ_BAD_STATE);

    enqueue_one(&q);
    assert(dlq_cancel(&q,0)==DLQ_OK);
    assert(dlq_cancel(&q,0)==DLQ_BAD_STATE);

    enqueue_one(&q);
    assert(dlq_start(&q,0,1000,0,0,NULL)==DLQ_OK);
    assert(dlq_received(&q,0,100)==DLQ_OK);
    assert(dlq_fail(&q,0,DLQ_INVALID)==DLQ_OK);
    assert(dlq_get(&q,0)->state==DLQ_FAILED);
    assert(dlq_get(&q,0)->received_bytes==100);
    assert(dlq_retry_from_zero(&q,0)==DLQ_OK);
    assert(dlq_get(&q,0)->received_bytes==0);
    assert(dlq_get(&q,0)->retries==1);

    dlq_init(&q);
    for(unsigned i=0;i<DLQ_MAX_JOBS;++i) {
        char version[16];
        snprintf(version,sizeof version,"01.%02u",i);
        assert(dlq_enqueue(&q,"CUSA02343",cid,version,100)==(int)i);
    }
    assert(q.count==DLQ_MAX_JOBS);
    assert(dlq_enqueue(&q,"CUSA02343",cid,"02.00",100)==DLQ_FULL);
    assert(q.count==DLQ_MAX_JOBS);
    assert(dlq_get(&q,DLQ_MAX_JOBS)==NULL);
    puts("DOWNLOAD_QUEUE_HOST_OK");
    return 0;
}
