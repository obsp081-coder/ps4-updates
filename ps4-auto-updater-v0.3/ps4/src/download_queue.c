#include "download_queue.h"
#include <stddef.h>
#include <string.h>
#include <limits.h>

static size_t bounded_length(const char *s,size_t limit) {
    size_t n=0;
    if(s)while(n<limit && s[n])++n;
    return n;
}
static int digit(char c){return c>='0'&&c<='9';}
static int upper(char c){return c>='A'&&c<='Z';}
static int id_ok(const char *s) {
    if(bounded_length(s,10)!=9u || memcmp(s,"CUSA",4))return 0;
    for(int i=4;i<9;++i)if(!digit(s[i]))return 0;
    return 1;
}
static int cid_ok(const char *s,const char *cusa) {
    if(bounded_length(s,37)!=36u || !upper(s[0]) || !upper(s[1]))
        return 0;
    for(int i=2;i<6;++i)if(!digit(s[i]))return 0;
    if(s[6]!='-' || memcmp(s+7,cusa,9) ||
       s[16]!='_' || !digit(s[17]) || !digit(s[18]) ||
       s[19]!='-')return 0;
    for(int i=20;i<36;++i)
        if(!upper(s[i]) && !digit(s[i]))return 0;
    return 1;
}
static int version_ok(const char *s) {
    const size_t n=bounded_length(s,16);
    if(n<3u || n>=16u)return 0;
    size_t i=0,whole=0,fraction=0;
    while(i<n && digit(s[i]) && whole<4u){++whole;++i;}
    if(!whole || i>=n || s[i++]!='.')return 0;
    while(i<n && digit(s[i]) && fraction<4u){++fraction;++i;}
    return fraction>0u && i==n;
}
static DlqJob *job(DlqQueue *q,unsigned index) {
    if(!q || q->count>DLQ_MAX_JOBS || index>=q->count)return NULL;
    return &q->jobs[index];
}
static int has_active(const DlqQueue *q,unsigned exclude) {
    for(unsigned i=0;i<q->count;++i)
        if(i!=exclude &&
           (q->jobs[i].state==DLQ_ACTIVE ||
            q->jobs[i].state==DLQ_VERIFYING))return 1;
    return 0;
}
void dlq_init(DlqQueue *q) {
    if(q)memset(q,0,sizeof *q);
}
int dlq_enqueue(DlqQueue *q,const char *cusa,
                const char *cid,const char *version,uint64_t expected) {
    if(!q || q->count>DLQ_MAX_JOBS || !id_ok(cusa) ||
       !cid_ok(cid,cusa) || !version_ok(version) || !expected)
        return DLQ_INVALID;
    for(unsigned i=0;i<q->count;++i) {
        const DlqJob *p=&q->jobs[i];
        if(!strcmp(p->cusa,cusa) && !strcmp(p->content_id,cid) &&
           !strcmp(p->version,version))return DLQ_DUPLICATE;
    }
    if(q->count==DLQ_MAX_JOBS)return DLQ_FULL;
    DlqJob *p=&q->jobs[q->count];
    memset(p,0,sizeof *p);
    memcpy(p->cusa,cusa,10);
    memcpy(p->content_id,cid,37);
    memcpy(p->version,version,strlen(version)+1);
    p->expected_bytes=expected;
    p->state=DLQ_QUEUED;
    p->last_error=DLQ_OK;
    return (int)q->count++;
}
const DlqJob *dlq_get(const DlqQueue *q,unsigned index) {
    if(!q || q->count>DLQ_MAX_JOBS || index>=q->count)return NULL;
    return &q->jobs[index];
}
DlqError dlq_space_check(uint64_t expected,uint64_t workspace,
                         uint64_t reserve,uint64_t free_bytes,
                         uint64_t *shortage) {
    if(shortage)*shortage=0;
    if(!expected || UINT64_MAX-expected<workspace ||
       UINT64_MAX-(expected+workspace)<reserve)return DLQ_INVALID;
    const uint64_t need=expected+workspace+reserve;
    if(free_bytes<need) {
        if(shortage)*shortage=need-free_bytes;
        return DLQ_NO_SPACE;
    }
    return DLQ_OK;
}
DlqError dlq_start(DlqQueue *q,unsigned index,uint64_t free_bytes,
                   uint64_t workspace,uint64_t reserve,uint64_t *shortage) {
    if(shortage)*shortage=0;
    DlqJob *p=job(q,index);
    if(!p)return DLQ_INVALID;
    if(p->state!=DLQ_QUEUED || has_active(q,index))
        return DLQ_BAD_STATE;
    const DlqError check=dlq_space_check(
        p->expected_bytes,workspace,reserve,free_bytes,shortage);
    if(check!=DLQ_OK) {
        p->last_error=check;
        return check;
    }
    p->state=DLQ_ACTIVE;
    p->last_error=DLQ_OK;
    return DLQ_OK;
}
DlqError dlq_received(DlqQueue *q,unsigned index,uint64_t delta) {
    DlqJob *p=job(q,index);
    if(!p || !delta)return DLQ_INVALID;
    if(p->state!=DLQ_ACTIVE)return DLQ_BAD_STATE;
    if(delta>p->expected_bytes-p->received_bytes)return DLQ_SIZE_MISMATCH;
    p->received_bytes+=delta;
    return DLQ_OK;
}
DlqError dlq_pause(DlqQueue *q,unsigned index) {
    DlqJob *p=job(q,index);
    if(!p)return DLQ_INVALID;
    if(p->state!=DLQ_ACTIVE)return DLQ_BAD_STATE;
    p->state=DLQ_PAUSED;
    return DLQ_OK;
}
DlqError dlq_resume(DlqQueue *q,unsigned index,
                    int stable_source,int verified_partial) {
    DlqJob *p=job(q,index);
    if(!p)return DLQ_INVALID;
    if(p->state!=DLQ_PAUSED || has_active(q,index))
        return DLQ_BAD_STATE;
    if(!stable_source || !verified_partial) {
        p->last_error=DLQ_UNSAFE_RESUME;
        return DLQ_UNSAFE_RESUME;
    }
    p->state=DLQ_ACTIVE;
    p->last_error=DLQ_OK;
    return DLQ_OK;
}
DlqError dlq_begin_verification(DlqQueue *q,unsigned index) {
    DlqJob *p=job(q,index);
    if(!p)return DLQ_INVALID;
    if(p->state!=DLQ_ACTIVE)return DLQ_BAD_STATE;
    if(p->received_bytes!=p->expected_bytes)return DLQ_SIZE_MISMATCH;
    p->state=DLQ_VERIFYING;
    return DLQ_OK;
}
DlqError dlq_finish_verification(DlqQueue *q,unsigned index,
                                 int checksum_matches) {
    DlqJob *p=job(q,index);
    if(!p)return DLQ_INVALID;
    if(p->state!=DLQ_VERIFYING)return DLQ_BAD_STATE;
    if(!checksum_matches) {
        p->state=DLQ_FAILED;
        p->last_error=DLQ_INTEGRITY_FAILED;
        return DLQ_INTEGRITY_FAILED;
    }
    p->state=DLQ_COMPLETED;
    p->last_error=DLQ_OK;
    return DLQ_OK;
}
DlqError dlq_fail(DlqQueue *q,unsigned index,DlqError cause) {
    DlqJob *p=job(q,index);
    if(!p || cause>=DLQ_OK || cause<DLQ_INTEGRITY_FAILED)
        return DLQ_INVALID;
    if(p->state!=DLQ_ACTIVE && p->state!=DLQ_PAUSED &&
       p->state!=DLQ_VERIFYING)return DLQ_BAD_STATE;
    p->state=DLQ_FAILED;
    p->last_error=cause;
    return DLQ_OK;
}
DlqError dlq_retry_from_zero(DlqQueue *q,unsigned index) {
    DlqJob *p=job(q,index);
    if(!p)return DLQ_INVALID;
    if(p->state!=DLQ_FAILED)return DLQ_BAD_STATE;
    if(p->retries==UINT32_MAX)return DLQ_INVALID;
    ++p->retries;
    p->received_bytes=0;
    p->state=DLQ_QUEUED;
    p->last_error=DLQ_OK;
    return DLQ_OK;
}
DlqError dlq_cancel(DlqQueue *q,unsigned index) {
    DlqJob *p=job(q,index);
    if(!p)return DLQ_INVALID;
    if(p->state==DLQ_COMPLETED || p->state==DLQ_CANCELLED)
        return DLQ_BAD_STATE;
    p->state=DLQ_CANCELLED;
    return DLQ_OK;
}
