#include "ui_compatibility.h"
#include <stddef.h>
#include <string.h>
static size_t lenmax(const char *s,size_t cap) {
    size_t i=0;
    if(s)while(i<cap && s[i])++i;
    return i;
}
static int dig(char c){return c>='0'&&c<='9';}
static int up(char c){return c>='A'&&c<='Z';}
static int alnum(char c){return up(c)||dig(c);}
static int valid_cusa(const char *s){
    if(!s||lenmax(s,10)!=9||memcmp(s,"CUSA",4))return 0;
    for(int i=4;i<9;++i)if(!dig(s[i]))return 0;
    return 1;
}
/* XXdddd-CUSAxxxxx_nn-16 uppercase/digit chars; validate full 36 bytes. */
static int valid_cid(const char *s){
    if(!s||lenmax(s,37)!=36||!up(s[0])||!up(s[1]))return 0;
    for(int i=2;i<6;++i)if(!dig(s[i]))return 0;
    if(s[6]!='-'||memcmp(s+7,"CUSA",4))return 0;
    for(int i=11;i<16;++i)if(!dig(s[i]))return 0;
    if(s[16]!='_'||!dig(s[17])||!dig(s[18])||s[19]!='-')return 0;
    for(int i=20;i<36;++i)if(!alnum(s[i]))return 0;
    return 1;
}
/* Missing=0, invalid=-1, parsed=1. Numeric, not lexical order. */
static int version(const char *s,uint32_t *value){
    if(!s||!s[0])return 0;
    const size_t n=lenmax(s,12);
    if(n>=12||n<3)return -1;
    unsigned a=0,b=0,k=0;
    size_t i=0;
    while(i<n&&dig(s[i])) {
        if(++k>4)return -1;
        a=a*10u+(unsigned)(s[i++]-'0');
    }
    if(!k||i>=n||s[i++]!='.')return -1;
    k=0;
    while(i<n&&dig(s[i])) {
        if(++k>4)return -1;
        b=b*10u+(unsigned)(s[i++]-'0');
    }
    if(!k||i!=n)return -1;
    *value=a*10000u+b;
    return 1;
}
UiCompatAssessment ui_compat_assess(const UiCompatFacts *f) {
    UiCompatAssessment a={UI_COMPAT_BLOCKED,0,0,0,0};
    if(!f){a.blockers=UI_COMPAT_BAD_TITLE;return a;}
    const int base=valid_cusa(f->base_cusa),patch=valid_cusa(f->patch_cusa);
    if(!base||!patch)a.blockers|=UI_COMPAT_BAD_TITLE;
    else if(strcmp(f->base_cusa,f->patch_cusa))
        a.blockers|=UI_COMPAT_TITLE_MISMATCH;

    const int cb=valid_cid(f->base_content_id);
    const int cp=valid_cid(f->patch_content_id);
    if(!f->base_content_id||!f->base_content_id[0]||
       !f->patch_content_id||!f->patch_content_id[0])
        a.unknowns|=UI_COMPAT_CID_UNKNOWN;
    else if(!cb||!cp)a.blockers|=UI_COMPAT_BAD_CID;
    if(cb&&base&&memcmp(f->base_content_id+7,f->base_cusa,9))
        a.blockers|=UI_COMPAT_CID_TITLE_MISMATCH;
    if(cp&&patch&&memcmp(f->patch_content_id+7,f->patch_cusa,9))
        a.blockers|=UI_COMPAT_CID_TITLE_MISMATCH;
    if(cb&&cp){
        if(memcmp(f->base_content_id,f->patch_content_id,2))
            a.blockers|=UI_COMPAT_REGION_MISMATCH;
        if(memcmp(f->base_content_id+2,f->patch_content_id+2,4))
            a.blockers|=UI_COMPAT_PUBLISHER_MISMATCH;
        /* Base and patch may have DIFFERENT full Content IDs. Equal CUSA
         * never proves the binding: demand independent provenance. */
        if(strcmp(f->base_content_id,f->patch_content_id)!=0 &&
           !f->identity_binding_verified)
            a.unknowns|=UI_COMPAT_BINDING_UNKNOWN;
    }
    if(f->installed==UI_COMPAT_INSTALL_NO)
        a.blockers|=UI_COMPAT_NOT_INSTALLED;
    else if(f->installed!=UI_COMPAT_INSTALL_YES)
        a.unknowns|=UI_COMPAT_INSTALL_UNCONFIRMED;
    const int bk=(f->base_kind==UI_COMPAT_RETAIL||
                  f->base_kind==UI_COMPAT_FPKG);
    const int pk=(f->patch_kind==UI_COMPAT_RETAIL||
                  f->patch_kind==UI_COMPAT_FPKG);
    if(!bk||!pk)a.unknowns|=UI_COMPAT_KIND_UNCONFIRMED;
    if(bk&&pk&&f->base_kind!=f->patch_kind)
        a.blockers|=UI_COMPAT_KIND_MISMATCH;

    uint32_t curr=0,target=0,required=0,fw=0,minfw=0;
    const int c=version(f->base_version,&curr);
    const int t=version(f->patch_version,&target);
    const int r=version(f->required_base_version,&required);
    const int v=version(f->console_firmware,&fw);
    const int m=version(f->minimum_firmware,&minfw);
    if(c<0||t<0||r<0||v<0||m<0)
        a.blockers|=UI_COMPAT_INVALID_VERSION;
    if(c==0||t==0)a.unknowns|=UI_COMPAT_VERSION_UNKNOWN;
    if(r==0)a.unknowns|=UI_COMPAT_BASE_REQUIRED_UNKNOWN;
    if(v==0||m==0)a.unknowns|=UI_COMPAT_FIRMWARE_UNKNOWN;
    if(c==1&&t==1&&target<=curr)a.blockers|=UI_COMPAT_NOT_NEWER;
    if(c==1&&r==1&&required!=curr)a.blockers|=UI_COMPAT_BASE_MISMATCH;
    if(v==1&&m==1&&minfw>fw)a.blockers|=UI_COMPAT_FIRMWARE_TOO_LOW;
    if(!f->base_metadata_verified)a.unknowns|=UI_COMPAT_METADATA_UNKNOWN;
    if(!f->source_authorized)a.unknowns|=UI_COMPAT_SOURCE_UNKNOWN;
    if(!f->integrity_verified)a.unknowns|=UI_COMPAT_INTEGRITY_UNKNOWN;
    if(!f->patch_size_bytes)a.unknowns|=UI_COMPAT_SIZE_UNKNOWN;

    if(a.blockers)a.decision=UI_COMPAT_BLOCKED;
    else if(a.unknowns)a.decision=UI_COMPAT_UNVERIFIED;
    else a.decision=UI_COMPAT_REVIEW_ONLY;
    /* Even REVIEW_ONLY is NOT compatible/authorized for installation. */
    return a;
}
const char *ui_compat_reason_key(uint32_t bit) {
    switch(bit){
    case UI_COMPAT_BAD_TITLE:return "INVALID_TITLE_ID";
    case UI_COMPAT_TITLE_MISMATCH:return "TITLE_ID_MISMATCH";
    case UI_COMPAT_BAD_CID:return "INVALID_CONTENT_ID";
    case UI_COMPAT_CID_TITLE_MISMATCH:return "CONTENT_TITLE_MISMATCH";
    case UI_COMPAT_REGION_MISMATCH:return "REGION_MISMATCH";
    case UI_COMPAT_PUBLISHER_MISMATCH:return "PUBLISHER_MISMATCH";
    case UI_COMPAT_KIND_MISMATCH:return "RETAIL_FPKG_MISMATCH";
    case UI_COMPAT_NOT_INSTALLED:return "BASE_NOT_INSTALLED";
    case UI_COMPAT_INVALID_VERSION:return "INVALID_VERSION";
    case UI_COMPAT_NOT_NEWER:return "PATCH_NOT_NEWER";
    case UI_COMPAT_BASE_MISMATCH:return "BASE_VERSION_MISMATCH";
    case UI_COMPAT_FIRMWARE_TOO_LOW:return "FIRMWARE_TOO_LOW";
    case UI_COMPAT_CID_UNKNOWN:return "CONTENT_ID_UNKNOWN";
    case UI_COMPAT_INSTALL_UNCONFIRMED:return "INSTALL_UNCONFIRMED";
    case UI_COMPAT_KIND_UNCONFIRMED:return "KIND_UNCONFIRMED";
    case UI_COMPAT_VERSION_UNKNOWN:return "VERSION_UNCONFIRMED";
    case UI_COMPAT_FIRMWARE_UNKNOWN:return "FIRMWARE_UNCONFIRMED";
    case UI_COMPAT_BASE_REQUIRED_UNKNOWN:return "REQUIRED_BASE_UNKNOWN";
    case UI_COMPAT_BINDING_UNKNOWN:return "PATCH_IDENTITY_UNCONFIRMED";
    case UI_COMPAT_SOURCE_UNKNOWN:return "SOURCE_UNCONFIRMED";
    case UI_COMPAT_INTEGRITY_UNKNOWN:return "PKG_INTEGRITY_UNCONFIRMED";
    case UI_COMPAT_METADATA_UNKNOWN:return "BASE_METADATA_UNCONFIRMED";
    case UI_COMPAT_SIZE_UNKNOWN:return "PATCH_SIZE_UNCONFIRMED";
    default:return "UNKNOWN_REASON";
    }
}
