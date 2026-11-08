/*
 * Tiny bounded local cache for the native PS4 library preview.
 * No JSON decoder, heap retained after call, network or install operations.
 * The CRC catches accidental truncation/corruption, NOT malicious tampering.
 * Cache metadata is always presented as unverified until rescanned.
 */
#include "ui_snapshot.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SNAPSHOT_BYTES 234u
#define CACHE_HEADER_BYTES 16u
static const unsigned char MAGIC[8] = {'P','S','4','U','I','C','0','1'};
_Static_assert(sizeof(UiSnapshotGame)==SNAPSHOT_BYTES,
               "snapshot ABI must be exactly 234 bytes");

static int valid_id(const char *s) {
    if(!s || memcmp(s,"CUSA",4)!=0 || s[9]!=0)return 0;
    for(unsigned i=4;i<9;++i)
        if(s[i]<'0' || s[i]>'9')return 0;
    return 1;
}
static int has_nul(const char *p,size_t capacity) {
    return memchr(p,0,capacity)!=NULL;
}
static int valid_game(const UiSnapshotGame *g) {
    if(!valid_id(g->cusa) ||
       !has_nul(g->title,sizeof g->title) ||
       !has_nul(g->version,sizeof g->version) ||
       !has_nul(g->category,sizeof g->category))return 0;
    /* A cached patch/DLC is not a base game and must not appear as one. */
    return !strcmp(g->category,"gd") ||
           !strcmp(g->category,"gda") ||
           !strcmp(g->category,"gdo") ||
           !strcmp(g->category,"gde");
}
static int valid_rows(const UiSnapshotGame *rows,unsigned count) {
    if(count>UI_SNAPSHOT_LIMIT || (count && !rows))return 0;
    for(unsigned i=0;i<count;++i) {
        if(!valid_game(&rows[i]))return 0;
        for(unsigned j=0;j<i;++j)
            if(!strcmp(rows[i].cusa,rows[j].cusa))return 0;
    }
    return 1;
}
static uint32_t crc_step(uint32_t crc,const unsigned char *p,size_t len) {
    for(size_t i=0;i<len;++i) {
        crc ^= (uint32_t)p[i];
        for(unsigned b=0;b<8;++b)
            crc = (crc>>1) ^ (0xEDB88320u & (0u-(crc&1u)));
    }
    return crc;
}
static uint32_t read32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static void write32(unsigned char *p,uint32_t v) {
    for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(8u*i));
}
static uint32_t checksum(const unsigned char hdr[CACHE_HEADER_BYTES],
                         const UiSnapshotGame *rows,unsigned count) {
    uint32_t crc=crc_step(0xFFFFFFFFu,hdr+8,4);
    if(count)
        crc=crc_step(crc,(const unsigned char*)rows,
                     (size_t)count*SNAPSHOT_BYTES);
    return crc^0xFFFFFFFFu;
}

int ui_snapshot_save(const char *path,const UiSnapshotGame *rows,
                     unsigned count) {
    if(!path || !*path || !valid_rows(rows,count))return -1;
    char temporary[512];
    const int n=snprintf(temporary,sizeof temporary,"%s.tmp",path);
    if(n<1 || (size_t)n>=sizeof temporary)return -1;
    unsigned char hdr[CACHE_HEADER_BYTES]={0};
    memcpy(hdr,MAGIC,sizeof MAGIC);
    write32(hdr+8,count);
    write32(hdr+12,checksum(hdr,rows,count));
    FILE *f=fopen(temporary,"wb");
    if(!f)return -2;
    int ok=fwrite(hdr,1,sizeof hdr,f)==sizeof hdr;
    if(ok && count)
        ok=fwrite(rows,sizeof *rows,count,f)==count;
    if(fclose(f)!=0)ok=0;
    if(ok)ok=rename(temporary,path)==0;
    if(!ok) {
        (void)remove(temporary);
        return -3;
    }
    return 0;
}

int ui_snapshot_load(const char *path,UiSnapshotGame *out,
                     unsigned capacity,unsigned *count) {
    if(count)*count=0;
    if(!count || !path || !*path || (capacity && !out))return -1;
    FILE *f=fopen(path,"rb");
    if(!f)return errno==ENOENT?0:-1;
    unsigned char hdr[CACHE_HEADER_BYTES];
    if(fread(hdr,1,sizeof hdr,f)!=sizeof hdr ||
       memcmp(hdr,MAGIC,sizeof MAGIC)!=0) {
        (void)fclose(f);
        return -1;
    }
    const uint32_t n=read32(hdr+8);
    if(n>UI_SNAPSHOT_LIMIT || n>capacity) {
        (void)fclose(f);
        return -1;
    }
    UiSnapshotGame *copy=NULL;
    if(n) {
        copy=(UiSnapshotGame*)malloc((size_t)n*sizeof *copy);
        if(!copy) {
            (void)fclose(f);
            return -1;
        }
    }
    int ok=1;
    if(n && fread(copy,sizeof *copy,n,f)!=n)ok=0;
    if(ok && fgetc(f)!=EOF)ok=0; /* Reject appended data. */
    if(ferror(f))ok=0;
    if(fclose(f)!=0)ok=0;
    if(ok && checksum(hdr,copy,n)!=read32(hdr+12))ok=0;
    if(ok && !valid_rows(copy,n))ok=0;
    if(ok && n)memcpy(out,copy,(size_t)n*sizeof *copy);
    free(copy);
    if(!ok)return -1;
    *count=n;
    return 1;
}
