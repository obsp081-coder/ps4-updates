#include "download_http_headers.h"
#include <stdint.h>
#include <string.h>

static int ascii_visible(unsigned char c) {
    return c>=32u && c<=126u;
}
static int ascii_digit(unsigned char c) {
    return c>='0' && c<='9';
}
static int header_name_char(unsigned char c) {
    return (c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
           (c>='0'&&c<='9') || c=='-';
}
static int equal_lower(const char *s,size_t n,const char *ascii) {
    size_t i=0;
    while(i<n && ascii[i]) {
        unsigned char c=(unsigned char)s[i];
        if(c>='A'&&c<='Z')c=(unsigned char)(c+('a'-'A'));
        if(c!=(unsigned char)ascii[i])return 0;
        ++i;
    }
    return i==n && ascii[i]==0;
}
static int next_line(const char *data,size_t length,size_t *offset,
                     const char **line,size_t *size) {
    const size_t start=*offset;
    for(size_t i=start;i<length;++i) {
        const unsigned char c=(unsigned char)data[i];
        if(c=='\r') {
            if(i+1u>=length || data[i+1u]!='\n' ||
               i-start>DL_HTTP_HEADER_MAX_LINE)return 0;
            *line=data+start;
            *size=i-start;
            *offset=i+2u;
            return 1;
        }
        if(c=='\n' || !ascii_visible(c) ||
           i-start>=DL_HTTP_HEADER_MAX_LINE)return 0;
    }
    return 0;
}
static DlHttpHeaderResult decimal(const char *s,size_t length,
                                   size_t *offset,uint64_t *value) {
    size_t i=*offset;
    uint64_t n=0;
    if(i>=length || !ascii_digit((unsigned char)s[i]))
        return DL_HTTP_HEADER_INVALID;
    do {
        const unsigned d=(unsigned)(s[i]-'0');
        if(n>(UINT64_MAX-d)/10u)return DL_HTTP_HEADER_OVERFLOW;
        n=n*10u+d;
        ++i;
    } while(i<length && ascii_digit((unsigned char)s[i]));
    *offset=i;
    *value=n;
    return DL_HTTP_HEADER_OK;
}
static DlHttpHeaderResult parse_range(const char *s,size_t n,
                                       DlHttpFacts *out) {
    if(n<10u || memcmp(s,"bytes ",6u))return DL_HTTP_HEADER_INVALID;
    size_t i=6u;
    DlHttpHeaderResult r=decimal(s,n,&i,&out->range_start);
    if(r!=DL_HTTP_HEADER_OK)return r;
    if(i>=n || s[i++]!='-')return DL_HTTP_HEADER_INVALID;
    r=decimal(s,n,&i,&out->range_end);
    if(r!=DL_HTTP_HEADER_OK)return r;
    if(i>=n || s[i++]!='/')return DL_HTTP_HEADER_INVALID;
    r=decimal(s,n,&i,&out->range_total);
    if(r!=DL_HTTP_HEADER_OK)return r;
    if(i!=n || out->range_start>out->range_end ||
       out->range_total==0u || out->range_end>=out->range_total)
        return DL_HTTP_HEADER_INVALID;
    return DL_HTTP_HEADER_OK;
}
static DlHttpHeaderResult parse_internal(const char *data,size_t length,
                                          DlHttpFacts *out) {
    if(!data || !length)return DL_HTTP_HEADER_INVALID;
    if(length>DL_HTTP_HEADER_MAX_BYTES)return DL_HTTP_HEADER_TOO_LARGE;
    if(length<16u || data[length-4u]!='\r' || data[length-3u]!='\n' ||
       data[length-2u]!='\r' || data[length-1u]!='\n')
        return DL_HTTP_HEADER_INVALID;

    size_t offset=0,line_length=0;
    const char *line=NULL;
    if(!next_line(data,length,&offset,&line,&line_length) ||
       line_length<12u || memcmp(line,"HTTP/1.1 ",9u) ||
       !ascii_digit((unsigned char)line[9]) ||
       !ascii_digit((unsigned char)line[10]) ||
       !ascii_digit((unsigned char)line[11]) ||
       (line_length>12u && line[12]!=' '))
        return DL_HTTP_HEADER_INVALID;
    const unsigned status=(unsigned)(line[9]-'0')*100u+
                          (unsigned)(line[10]-'0')*10u+
                          (unsigned)(line[11]-'0');
    /* Interim 1xx must be consumed by a separately reviewed transport.
     * A single parser invocation only covers one final header block. */
    if(status<200u || status>599u)return DL_HTTP_HEADER_INVALID;
    out->status=status;

    unsigned count=0;
    for(;;) {
        if(!next_line(data,length,&offset,&line,&line_length))
            return DL_HTTP_HEADER_INVALID;
        if(!line_length)
            return offset==length ? DL_HTTP_HEADER_OK
                                  : DL_HTTP_HEADER_INVALID;
        if(++count>DL_HTTP_HEADER_MAX_FIELDS)
            return DL_HTTP_HEADER_TOO_LARGE;
        size_t colon=0;
        while(colon<line_length && line[colon]!=':')++colon;
        if(colon==0u || colon==line_length)
            return DL_HTTP_HEADER_INVALID;
        for(size_t i=0;i<colon;++i)
            if(!header_name_char((unsigned char)line[i]))
                return DL_HTTP_HEADER_INVALID;
        size_t start=colon+1u,end=line_length;
        while(start<end && line[start]==' ')++start;
        while(end>start && line[end-1u]==' ')--end;
        const char *value=line+start;
        const size_t value_length=end-start;

        if(equal_lower(line,colon,"content-length")) {
            if(++out->content_length_count!=1u)
                return DL_HTTP_HEADER_DUPLICATE;
            size_t i=0;
            DlHttpHeaderResult r=decimal(value,value_length,&i,
                                          &out->content_length);
            if(r!=DL_HTTP_HEADER_OK)return r;
            if(i!=value_length)return DL_HTTP_HEADER_INVALID;
        } else if(equal_lower(line,colon,"content-range")) {
            if(++out->content_range_count!=1u)
                return DL_HTTP_HEADER_DUPLICATE;
            DlHttpHeaderResult r=parse_range(value,value_length,out);
            if(r!=DL_HTTP_HEADER_OK)return r;
        } else if(equal_lower(line,colon,"etag")) {
            if(++out->etag_count!=1u)return DL_HTTP_HEADER_DUPLICATE;
            if(!value_length || value_length>=DL_HTTP_ETAG_CAP)
                return DL_HTTP_HEADER_TOO_LARGE;
            memcpy(out->etag,value,value_length);
            out->etag[value_length]=0;
        } else if(equal_lower(line,colon,"transfer-encoding")) {
            if(out->transfer_encoding_present)
                return DL_HTTP_HEADER_DUPLICATE;
            out->transfer_encoding_present=1u;
        } else if(equal_lower(line,colon,"content-encoding")) {
            if(out->content_encoding_present)
                return DL_HTTP_HEADER_DUPLICATE;
            out->content_encoding_present=1u;
        } else if(equal_lower(line,colon,"location")) {
            if(out->redirect_location_present)
                return DL_HTTP_HEADER_DUPLICATE;
            out->redirect_location_present=1u;
        }
    }
}
DlHttpHeaderResult dl_http_parse_headers(const char *data,size_t length,
                                          DlHttpFacts *out) {
    if(!out)return DL_HTTP_HEADER_INVALID;
    DlHttpFacts staged;
    memset(&staged,0,sizeof staged);
    DlHttpHeaderResult r=parse_internal(data,length,&staged);
    /* No partial/unverified metadata may escape on parse failure. */
    memset(out,0,sizeof *out);
    if(r==DL_HTTP_HEADER_OK)*out=staged;
    return r;
}
