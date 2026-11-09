#ifndef PS4_UI_LOCAL_ART_PROBE_H
#define PS4_UI_LOCAL_ART_PROBE_H
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Read-only experimental probe, NOT a FW14-verified asset path.
 * Older PS4 appmeta documentation mentions /user/appmeta/<CUSA>/pic1.png.
 * Never take a filename from FTP text or concatenate unchecked user data. */
static inline int ui_art_probe_path(char *dst,size_t cap,const char *cusa) {
    if(!dst || !cap || !cusa || strlen(cusa)!=9u ||
       memcmp(cusa,"CUSA",4)!=0)return 0;
    for(int i=4;i<9;++i)
        if(cusa[i]<'0' || cusa[i]>'9')return 0;
    const int n=snprintf(dst,cap,"LIST /user/appmeta/%s",cusa);
    return n>0 && (size_t)n<cap;
}

/* A LIST text entry ending in the exact basename is only a candidate.
 * It does not prove readable PNG bytes, permissions, or image dimensions.
 * Ignore misleading suffixes, prefixes, embedded paths and partial lines. */
static inline int ui_art_probe_mentions_pic1(const unsigned char *data,
                                               size_t size) {
    if(!data || !size)return 0;
    size_t start=0;
    while(start<size) {
        size_t end=start;
        while(end<size && data[end]!='\n')++end;
        /* A final line missing its delimiter may be a partial response. */
        if(end>=size)break;
        size_t last=end;
        while(last>start && (data[last-1]=='\r' ||
              data[last-1]==' ' || data[last-1]=='\t'))--last;
        size_t base=last;
        while(base>start && data[base-1]!=' ' &&
              data[base-1]!='\t' && data[base-1]!='/')--base;
        static const char name[]="pic1.png";
        if(last-base==sizeof(name)-1u &&
           memcmp(data+base,name,sizeof(name)-1u)==0)return 1;
        start=end+1;
    }
    return 0;
}
#endif
