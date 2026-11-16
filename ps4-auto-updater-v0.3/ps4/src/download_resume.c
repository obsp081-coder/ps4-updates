#include "download_resume.h"
#include "download_http_policy.h"
#include <string.h>

static size_t bounded(const char *s,size_t cap) {
    size_t n=0;
    if(s)while(n<cap && s[n])++n;
    return n;
}

DlResumeResult dl_resume_review(
    const DlResumeCheckpoint *cp,
    const char *trusted_cid,
    const char *trusted_version,
    uint64_t trusted_size,
    const uint8_t trusted_sha256[32],
    uint64_t partial_size,
    const uint8_t partial_sha256[32],
    const char *headers,size_t length,
    int tls_verified,int range_sent,int if_range_sent) {

    if(!cp || !trusted_cid || !trusted_version || !trusted_sha256 ||
       !partial_sha256 || !headers || !length || !trusted_size ||
       bounded(trusted_cid,sizeof cp->content_id)!=36u ||
       bounded(trusted_version,sizeof cp->version)==0u ||
       bounded(trusted_version,sizeof cp->version)>=sizeof cp->version ||
       bounded(cp->content_id,sizeof cp->content_id)!=36u ||
       bounded(cp->version,sizeof cp->version)==0u ||
       bounded(cp->version,sizeof cp->version)>=sizeof cp->version ||
       bounded(cp->strong_etag,sizeof cp->strong_etag)==0u ||
       bounded(cp->strong_etag,sizeof cp->strong_etag)>=
           sizeof cp->strong_etag ||
       cp->offset==0u || cp->offset>=cp->total_size ||
       range_sent!=1 || if_range_sent!=1)return DL_RESUME_INVALID;

    /* Binding is to an independent trusted update, never just CUSA. */
    if(strcmp(cp->content_id,trusted_cid) ||
       strcmp(cp->version,trusted_version) ||
       cp->total_size!=trusted_size ||
       !ds_sha256_digest_equal(cp->full_sha256,trusted_sha256))
        return DL_RESUME_MANIFEST_MISMATCH;

    if(partial_size!=cp->offset ||
       !ds_sha256_digest_equal(partial_sha256,cp->partial_sha256))
        return DL_RESUME_PARTIAL_MISMATCH;

    DlHttpFacts response;
    if(dl_http_parse_headers(headers,length,&response)!=DL_HTTP_HEADER_OK)
        return DL_RESUME_HEADERS_INVALID;

    /* NEVER infer authentication from an HTTP header. The caller must
     * provide the verified certificate-chain/hostname result. */
    response.tls_authenticated=tls_verified==1?1u:0u;
    response.range_requested=1u;
    response.if_range_sent=1u;
    const DlHttpDecision d=dl_http_review(
        &response,cp->total_size,cp->offset,cp->strong_etag);
    if(d==DL_HTTP_TLS_UNVERIFIED)return DL_RESUME_TLS_UNVERIFIED;
    if(d!=DL_HTTP_ACCEPT)return DL_RESUME_RESPONSE_REJECTED;
    return DL_RESUME_OK;
}
