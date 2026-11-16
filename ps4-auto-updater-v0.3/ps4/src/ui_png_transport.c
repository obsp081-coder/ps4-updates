#include "ui_png_transport.h"
#include <stdint.h>
#include <string.h>
/* Use PNG's exact CRC-32 polynomial on every chunk type+payload. */
static uint32_t rd32be(const unsigned char *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|p[3];
}
static uint32_t chunk_crc(const unsigned char *data,size_t count) {
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<count;++i) {
        crc^=data[i];
        for(unsigned bit=0;bit<8;++bit)
            crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return crc^0xffffffffu;
}
int ui_png_crc_valid(const unsigned char *buf,size_t size) {
    static const unsigned char sig[8]={137,80,78,71,13,10,26,10};
    if(!buf||size<8+25+12||memcmp(buf,sig,8))return 0;
    size_t at=8;
    int has_idat=0,has_ihdr=0;
    while(at<size) {
        if(size-at<12)return 0;
        size_t length=rd32be(buf+at);
        if(length>size-at-12)return 0; /* no pointer overflow */
        const unsigned char *type=buf+at+4;
        const unsigned char *chunk=buf+at+4;
        if(chunk_crc(chunk,length+4)!=rd32be(buf+at+8+length))
            return 0;
        if(!has_ihdr) {
            if(memcmp(type,"IHDR",4)!=0||length!=13)return 0;
            has_ihdr=1;
        } else if(memcmp(type,"IHDR",4)==0) return 0;
        if(memcmp(type,"IDAT",4)==0) has_idat=1;
        if(memcmp(type,"IEND",4)==0) {
            if(length!=0 || !has_idat || at+12!=size) return 0;
            return 1;
        }
        at+=12+length;
    }
    return 0;
}
int ui_png_ftp_validate(unsigned char *buf,size_t *size) {
    if(!buf||!size||*size>2u*1024u*1024u)return -1;
    if(ui_png_crc_valid(buf,*size))return 0;
    size_t w=0,original=*size;
    for(size_t i=0;i<original;++i) {
        if(buf[i]=='\r'&&i+1<original&&buf[i+1]=='\n')
            continue;
        buf[w++]=buf[i];
    }
    if(w==original || !ui_png_crc_valid(buf,w))return -2;
    *size=w;
    return 1;
}
