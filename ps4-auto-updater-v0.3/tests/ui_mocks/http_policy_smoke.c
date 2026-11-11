/* Host-only contract; metadata synthetic, TLS is NOT actually established. */
#include "../../ps4/src/download_http_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static DlHttpFacts initial(void) {
    DlHttpFacts a={0};
    a.status=200;
    a.tls_authenticated=1; /* synthetic trusted-adapter fact */
    a.content_length=1000;
    a.content_length_count=1;
    a.etag_count=1;
    strcpy(a.etag,"\"fixture-etag-1\"");
    return a;
}
int main(void) {
    DlHttpFacts a=initial();
    assert(dl_http_review(&a,1000,0,NULL)==DL_HTTP_ACCEPT);
    assert(dl_http_review(NULL,1000,0,NULL)==DL_HTTP_INVALID);
    assert(dl_http_review(&a,0,0,NULL)==DL_HTTP_INVALID);
    assert(dl_http_review(&a,1000,1000,NULL)==DL_HTTP_INVALID);
    a.tls_authenticated=0;
    assert(dl_http_review(&a,1000,0,NULL)==DL_HTTP_TLS_UNVERIFIED);
    a=initial(); a.status=302;
    assert(dl_http_review(&a,1000,0,NULL)==DL_HTTP_STATUS_REJECTED);
    a=initial(); a.status=206;
    assert(dl_http_review(&a,1000,0,NULL)==DL_HTTP_STATUS_REJECTED);
    a=initial(); a.content_length=999;
    assert(dl_http_review(&a,1000,0,NULL)==DL_HTTP_LENGTH_MISMATCH);
    a=initial(); a.content_length_count=2;
    assert(dl_http_review(&a,1000,0,NULL)==DL_HTTP_INVALID);
    a=initial(); a.transfer_encoding_present=1;
    assert(dl_http_review(&a,1000,0,NULL)==DL_HTTP_UNSUPPORTED_ENCODING);
    a=initial(); a.content_encoding_present=1;
    assert(dl_http_review(&a,1000,0,NULL)==DL_HTTP_UNSUPPORTED_ENCODING);
    a=initial(); a.redirect_location_present=1;
    assert(dl_http_review(&a,1000,0,NULL)==DL_HTTP_STATUS_REJECTED);
    a=initial(); strcpy(a.etag,"W/\"weak\"");
    assert(dl_http_review(&a,1000,0,NULL)==DL_HTTP_ETAG_MISMATCH);
    a=initial(); strcpy(a.etag,"\"bad\r\ntag\"");
    assert(dl_http_review(&a,1000,0,NULL)==DL_HTTP_ETAG_MISMATCH);
    a=initial(); a.etag_count=2;
    assert(dl_http_review(&a,1000,0,NULL)==DL_HTTP_INVALID);

    a=initial(); a.status=206; a.range_requested=1; a.if_range_sent=1;
    a.content_range_count=1; a.range_start=500; a.range_end=999;
    a.range_total=1000; a.content_length=500;
    const char *saved="\"fixture-etag-1\"";
    assert(dl_http_review(&a,1000,500,saved)==DL_HTTP_ACCEPT);
    a.range_start=499;
    assert(dl_http_review(&a,1000,500,saved)==DL_HTTP_RANGE_MISMATCH);
    a.range_start=500; a.range_end=998;
    assert(dl_http_review(&a,1000,500,saved)==DL_HTTP_RANGE_MISMATCH);
    a.range_end=999; a.range_total=1001;
    assert(dl_http_review(&a,1000,500,saved)==DL_HTTP_RANGE_MISMATCH);
    a.range_total=1000; a.content_length=499;
    assert(dl_http_review(&a,1000,500,saved)==DL_HTTP_LENGTH_MISMATCH);
    a.content_length=500;
    assert(dl_http_review(&a,1000,500,"\"different\"")==DL_HTTP_ETAG_MISMATCH);
    assert(dl_http_review(&a,1000,500,"W/\"weak\"")==DL_HTTP_ETAG_MISMATCH);
    a.etag_count=0; a.etag[0]=0;
    assert(dl_http_review(&a,1000,500,saved)==DL_HTTP_ETAG_MISMATCH);
    a=initial(); a.status=200; a.range_requested=1; a.if_range_sent=1;
    assert(dl_http_review(&a,1000,500,saved)==DL_HTTP_STATUS_REJECTED);
    puts("DOWNLOAD_HTTP_POLICY_HOST_OK");
    return 0;
}
