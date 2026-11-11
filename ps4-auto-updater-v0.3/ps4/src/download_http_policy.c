#include "download_http_policy.h"
#include <stddef.h>
#include <string.h>

static size_t strnlen_cap(const char *s,size_t cap) {
    size_t i=0;
    if(s)while(i<cap && s[i])++i;
    return i;
}
/* Require RFC-style strong quoted ETag and reject CR/LF/control bytes.
 * This is a strict subset: rejecting a legal unusual tag is safe. */
static int strong_tag(const char *tag) {
    size_t n=strnlen_cap(tag,DL_HTTP_ETAG_CAP);
    if(n<3u || n>=DL_HTTP_ETAG_CAP || tag[0]!='"' || tag[n-1]!='"')
        return 0;
    for(size_t i=1;i<n-1u;++i) {
        const unsigned char c=(unsigned char)tag[i];
        if(c<33u || c>126u || c=='"')return 0;
    }
    return 1;
}
DlHttpDecision dl_http_review(const DlHttpFacts *r,uint64_t total,
                              uint64_t offset,const char *saved) {
    if(!r || !total || offset>=total ||
       r->content_length_count!=1u || r->content_range_count>1u ||
       r->etag_count>1u)return DL_HTTP_INVALID;
    if(r->tls_authenticated!=1u)return DL_HTTP_TLS_UNVERIFIED;
    if(r->transfer_encoding_present || r->content_encoding_present)
        return DL_HTTP_UNSUPPORTED_ENCODING;
    if(r->redirect_location_present)return DL_HTTP_STATUS_REJECTED;
    if(r->etag_count && !strong_tag(r->etag))return DL_HTTP_ETAG_MISMATCH;
    if(!r->etag_count && r->etag[0])return DL_HTTP_INVALID;

    if(offset==0u) {
        if(r->status!=200u || r->range_requested || r->if_range_sent)
            return DL_HTTP_STATUS_REJECTED;
        if(r->content_range_count)return DL_HTTP_RANGE_MISMATCH;
        if(r->content_length!=total)return DL_HTTP_LENGTH_MISMATCH;
        return DL_HTTP_ACCEPT;
    }
    if(!r->range_requested || !r->if_range_sent || r->status!=206u)
        return DL_HTTP_STATUS_REJECTED;
    if(!strong_tag(saved) || r->etag_count!=1u ||
       strcmp(r->etag,saved))return DL_HTTP_ETAG_MISMATCH;
    if(r->content_range_count!=1u || r->range_start!=offset ||
       r->range_total!=total || r->range_end!=total-1u ||
       r->range_end<r->range_start)return DL_HTTP_RANGE_MISMATCH;
    if(r->content_length!=total-offset)return DL_HTTP_LENGTH_MISMATCH;
    return DL_HTTP_ACCEPT;
}
