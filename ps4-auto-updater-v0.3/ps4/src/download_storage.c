#include "download_storage.h"
#include <stdio.h>
#include <string.h>

static size_t bound_len(const char *s,size_t cap) {
    size_t n=0;
    if(s)while(n<cap && s[n])++n;
    return n;
}
static int dec(char c) {return c>='0' && c<='9';}
static int upper(char c) {return c>='A' && c<='Z';}
static int safe_cid(const char *s) {
    if(bound_len(s,37u)!=36u || !upper(s[0]) || !upper(s[1]))
        return 0;
    for(unsigned i=2u;i<6u;++i)if(!dec(s[i]))return 0;
    if(s[6]!='-' || memcmp(s+7,"CUSA",4))return 0;
    for(unsigned i=11u;i<16u;++i)if(!dec(s[i]))return 0;
    if(s[16]!='_' || !dec(s[17]) || !dec(s[18]) ||
       s[19]!='-')return 0;
    for(unsigned i=20u;i<36u;++i)
        if(!upper(s[i]) && !dec(s[i]))return 0;
    return 1;
}
static int safe_version(const char *s) {
    const size_t n=bound_len(s,16u);
    if(n<3u || n>=16u)return 0;
    size_t i=0,whole=0,fraction=0;
    while(i<n && dec(s[i]) && whole<4u){++i;++whole;}
    if(!whole || i==n || s[i++]!='.')return 0;
    while(i<n && dec(s[i]) && fraction<4u){++i;++fraction;}
    return fraction>0u && i==n;
}
static int complete_port(const DsPort *p) {
    return p && p->create_exclusive && p->write_all && p->flush &&
           p->close && p->verify_sha256 && p->publish_exclusive &&
           p->unlink_part;
}
void ds_init(DsSession *s) {
    if(s)memset(s,0,sizeof *s);
}
DsResult ds_begin(DsSession *s,const DsPort *port,
                  const char *cid,const char *version,
                  uint64_t expected,const uint8_t digest[32]) {
    if(!s || !complete_port(port) || !safe_cid(cid) ||
       !safe_version(version) || !expected || !digest)return DS_INVALID;
    if(s->state!=DS_EMPTY || s->owns_part || s->file_open)
        return DS_BAD_STATE;

    DsSession stage;
    memset(&stage,0,sizeof stage);
    stage.port=*port;
    stage.expected_size=expected;
    memcpy(stage.expected_sha256,digest,32);
    const int final_n=snprintf(stage.final_leaf,sizeof stage.final_leaf,
                               "%s-%s.bin",cid,version);
    if(final_n<0 || (size_t)final_n>=sizeof stage.final_leaf)
        return DS_INVALID;
    const int part_n=snprintf(stage.part_leaf,sizeof stage.part_leaf,
                              "%s.part",stage.final_leaf);
    if(part_n<0 || (size_t)part_n>=sizeof stage.part_leaf)
        return DS_INVALID;
    /* create_exclusive MUST leave no new file/descriptor on failure. */
    if(stage.port.create_exclusive(stage.port.context,stage.part_leaf)!=0)
        return DS_OPEN_FAILED;
    stage.state=DS_WRITING;
    stage.owns_part=1u;
    stage.file_open=1u;
    *s=stage;
    return DS_OK;
}
DsResult ds_write(DsSession *s,const uint8_t *bytes,size_t count) {
    if(!s || !bytes || !count || count>DS_WRITE_CAP)return DS_INVALID;
    if(s->state!=DS_WRITING || !s->file_open)
        return DS_BAD_STATE;
    if((uint64_t)count>s->expected_size-s->written_size)
        return DS_SIZE_EXCEEDED;
    if(s->port.write_all(s->port.context,bytes,count)!=0) {
        /* Backend may have written some bytes. NEVER finalize them. */
        s->state=DS_FAILED;
        return DS_WRITE_FAILED;
    }
    s->written_size+=(uint64_t)count;
    return DS_OK;
}
DsResult ds_finalize(DsSession *s) {
    if(!s)return DS_INVALID;
    if(s->state!=DS_WRITING || !s->file_open)return DS_BAD_STATE;
    if(s->written_size!=s->expected_size)return DS_SIZE_EXCEEDED;

    const int synced=s->port.flush(s->port.context);
    const int closed=s->port.close(s->port.context);
    /* close contract: always releases handle, even if returning failure. */
    s->file_open=0u;
    if(synced!=0 || closed!=0) {
        s->state=DS_FAILED;
        return DS_FLUSH_FAILED;
    }
    s->state=DS_SEALED;
    /* SHA-256 backend must read the stored bytes from the .part path. */
    if(s->port.verify_sha256(s->port.context,s->part_leaf,
                             s->expected_sha256)!=1) {
        s->state=DS_FAILED;
        return DS_VERIFY_FAILED;
    }
    /* Must perform atomic publish-if-absent; never use overwriting rename. */
    if(s->port.publish_exclusive(s->port.context,s->part_leaf,
                                 s->final_leaf)!=0) {
        s->state=DS_FAILED;
        return DS_PUBLISH_FAILED;
    }
    s->state=DS_PUBLISHED;
    if(s->port.unlink_part(s->port.context,s->part_leaf)!=0)
        return DS_CLEANUP_FAILED;
    s->owns_part=0u;
    return DS_OK;
}
DsResult ds_abort(DsSession *s) {
    if(!s)return DS_INVALID;
    if(s->state==DS_EMPTY || s->state==DS_ABORTED ||
       (s->state==DS_PUBLISHED && !s->owns_part))
        return DS_BAD_STATE;

    int close_error=0;
    if(s->file_open) {
        close_error=s->port.close(s->port.context);
        s->file_open=0u;
    }
    if(s->owns_part) {
        if(s->port.unlink_part(s->port.context,s->part_leaf)!=0)
            return DS_CLEANUP_FAILED;
        s->owns_part=0u;
    }
    if(s->state!=DS_PUBLISHED)s->state=DS_ABORTED;
    return close_error ? DS_CLEANUP_FAILED : DS_OK;
}
