/* Host-only synthetic inputs for native C resume eligibility reviewer.
 * TLS trusted bit is test input, not a real TLS certificate verification.
 */
#include "../../ps4/src/download_resume.h"
#include "../../ps4/src/download_sha256.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *CID="IV0000-CUSA02343_00-SCANNER000000000";
static const char *ETAG="\"source-fixture-1\"";
static const char *PARTIAL=
    "HTTP/1.1 206 Partial Content\r\n"
    "Content-Length: 600\r\n"
    "Content-Range: bytes 400-999/1000\r\n"
    "ETag: \"source-fixture-1\"\r\n\r\n";

static void hash_bytes(const uint8_t *bytes,size_t length,uint8_t out[32]) {
    DsSha256 sha;
    ds_sha256_init(&sha);
    assert(ds_sha256_update(&sha,bytes,length)==1);
    assert(ds_sha256_final(&sha,out)==1);
}
static DlResumeResult assess(
    const DlResumeCheckpoint *c,uint64_t actual_bytes,
    const uint8_t observed_hash[32],const char *headers,
    int trusted,int range,int if_range) {
    return dl_resume_review(c,CID,"01.09",1000,c->full_sha256,
                            actual_bytes,observed_hash,
                            headers,strlen(headers),trusted,range,if_range);
}
int main(void) {
    uint8_t fixture[1000],actual[32],independent_full[32];
    for(unsigned i=0;i<1000u;++i)fixture[i]=(uint8_t)(i%251u);
    hash_bytes(fixture,sizeof fixture,independent_full);
    hash_bytes(fixture,400u,actual);

    DlResumeCheckpoint saved;
    memset(&saved,0,sizeof saved);
    memcpy(saved.content_id,CID,strlen(CID)+1u);
    memcpy(saved.version,"01.09",6u);
    memcpy(saved.strong_etag,ETAG,strlen(ETAG)+1u);
    saved.total_size=1000u;
    saved.offset=400u;
    memcpy(saved.full_sha256,independent_full,32u);
    memcpy(saved.partial_sha256,actual,32u);

    assert(assess(&saved,400u,actual,PARTIAL,1,1,1)==DL_RESUME_OK);
    assert(assess(&saved,400u,actual,PARTIAL,0,1,1)==
           DL_RESUME_TLS_UNVERIFIED);
    assert(assess(&saved,400u,actual,PARTIAL,1,0,1)==DL_RESUME_INVALID);
    assert(assess(&saved,400u,actual,PARTIAL,1,1,0)==DL_RESUME_INVALID);
    assert(assess(&saved,399u,actual,PARTIAL,1,1,1)==
           DL_RESUME_PARTIAL_MISMATCH);
    actual[0]^=1u;
    assert(assess(&saved,400u,actual,PARTIAL,1,1,1)==
           DL_RESUME_PARTIAL_MISMATCH);
    actual[0]^=1u;
    saved.total_size=999u;
    assert(assess(&saved,400u,actual,PARTIAL,1,1,1)==
           DL_RESUME_MANIFEST_MISMATCH);
    saved.total_size=1000u;
    saved.full_sha256[0]^=1u;
    uint8_t correct[32];
    memcpy(correct,independent_full,32u);
    assert(dl_resume_review(&saved,CID,"01.09",1000,correct,400u,actual,
                            PARTIAL,strlen(PARTIAL),1,1,1)==
           DL_RESUME_MANIFEST_MISMATCH);
    saved.full_sha256[0]^=1u;
    saved.content_id[7]='X';
    assert(assess(&saved,400u,actual,PARTIAL,1,1,1)==
           DL_RESUME_MANIFEST_MISMATCH);
    saved.content_id[7]='C';

    const char *wrong_etag=
        "HTTP/1.1 206 Partial Content\r\n"
        "Content-Length: 600\r\n"
        "Content-Range: bytes 400-999/1000\r\n"
        "ETag: \"changed\"\r\n\r\n";
    assert(assess(&saved,400u,actual,wrong_etag,1,1,1)==
           DL_RESUME_RESPONSE_REJECTED);

    const char *wrong_range=
        "HTTP/1.1 206 Partial Content\r\n"
        "Content-Length: 600\r\n"
        "Content-Range: bytes 399-998/1000\r\n"
        "ETag: \"source-fixture-1\"\r\n\r\n";
    assert(assess(&saved,400u,actual,wrong_range,1,1,1)==
           DL_RESUME_RESPONSE_REJECTED);

    const char *downgrade=
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 1000\r\n"
        "ETag: \"source-fixture-1\"\r\n\r\n";
    assert(assess(&saved,400u,actual,downgrade,1,1,1)==
           DL_RESUME_RESPONSE_REJECTED);

    const char *dup=
        "HTTP/1.1 206 Partial Content\r\n"
        "Content-Length: 600\r\nContent-Length: 600\r\n"
        "Content-Range: bytes 400-999/1000\r\n"
        "ETag: \"source-fixture-1\"\r\n\r\n";
    assert(assess(&saved,400u,actual,dup,1,1,1)==
           DL_RESUME_HEADERS_INVALID);
    saved.offset=0;
    assert(assess(&saved,400u,actual,PARTIAL,1,1,1)==DL_RESUME_INVALID);
    saved.offset=1000;
    assert(assess(&saved,400u,actual,PARTIAL,1,1,1)==DL_RESUME_INVALID);

    puts("DOWNLOAD_RESUME_POLICY_HOST_OK");
    return 0;
}
