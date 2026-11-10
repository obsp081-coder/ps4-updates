#include "ui_models.h"
#include <stddef.h>
#include <string.h>
static UiUpdateInfo updates[UI_MODEL_MAX_UPDATES];
static UiDownloadInfo downloads[UI_MODEL_MAX_JOBS];
static unsigned updates_count;
static unsigned downloads_count;
static int valid_id(const char *s) {
    if(!s || memcmp(s,"CUSA",4)!=0)return 0;
    for(int i=4;i<9;++i)
        if(s[i]<'0'||s[i]>'9')return 0;
    return s[9]==0;
}
static int version_valid(const char *s,size_t cap,int required) {
    if(!s || !s[0])return !required;
    size_t n=0,dots=0,digits=0;
    for(;n<cap && s[n];++n) {
        if(s[n]=='.') {
            if(dots++ || !digits)return 0;
            digits=0;
        } else if(s[n]>='0'&&s[n]<='9') {
            if(++digits>4)return 0;
        } else return 0;
    }
    return n<cap && dots==1 && digits>0;
}
static int valid_title(const char *s,size_t cap) {
    if(!s)return 0;
    /* Bounded NUL check; UTF-8 itself is preserved for native rendering. */
    for(size_t i=0;i<cap;++i) if(s[i]==0)return 1;
    return 0;
}
void ui_model_reset(void) {
    updates_count=downloads_count=0;
    memset(updates,0,sizeof updates);
    memset(downloads,0,sizeof downloads);
}
int ui_model_set_updates(const UiUpdateInfo *rows,unsigned count) {
    if(count>UI_MODEL_MAX_UPDATES || (count>0 && !rows))return -1;
    for(unsigned i=0;i<count;++i) {
        const UiUpdateInfo *p=&rows[i];
        if(!valid_id(p->cusa) ||
           !version_valid(p->version,sizeof p->version,1) ||
           !version_valid(p->minimum_fw,sizeof p->minimum_fw,0) ||
           p->verdict<UI_UPDATE_UNKNOWN || p->verdict>=UI_UPDATE_VERIFIED ||
           (p->verdict==UI_UPDATE_BLOCKED && !p->compat_blockers) ||
           (p->verdict==UI_UPDATE_UNKNOWN && p->compat_blockers) ||
           p->source_kind!=UI_SOURCE_FPKG_SUBMITTED)
            return -2;
        for(unsigned j=0;j<i;++j)
            if(!strcmp(p->cusa,rows[j].cusa) &&
               !strcmp(p->version,rows[j].version) &&
               p->source_kind==rows[j].source_kind)return -3;
    }
    if(count)memcpy(updates,rows,(size_t)count*sizeof updates[0]);
    updates_count=count;
    return 0;
}
int ui_model_set_downloads(const UiDownloadInfo *rows,unsigned count) {
    if(count>UI_MODEL_MAX_JOBS || (count>0 && !rows))return -1;
    for(unsigned i=0;i<count;++i) {
        const UiDownloadInfo *p=&rows[i];
        if(!valid_id(p->cusa) || !valid_title(p->title,sizeof p->title) ||
           !version_valid(p->version,sizeof p->version,1) ||
           p->status<UI_JOB_QUEUED || p->status>UI_JOB_BLOCKED ||
           (p->bytes_total && p->bytes_done>p->bytes_total) ||
           (p->status==UI_JOB_COMPLETED &&
            (!p->bytes_total || p->bytes_done!=p->bytes_total)))
            return -2;
    }
    if(count)memcpy(downloads,rows,(size_t)count*sizeof downloads[0]);
    downloads_count=count;
    return 0;
}
unsigned ui_model_update_count(void){return updates_count;}
unsigned ui_model_download_count(void){return downloads_count;}
const UiUpdateInfo *ui_model_update(unsigned i) {
    return i<updates_count?&updates[i]:NULL;
}
const UiDownloadInfo *ui_model_download(unsigned i) {
    return i<downloads_count?&downloads[i]:NULL;
}
const UiUpdateInfo *ui_model_update_for(const char *cusa,unsigned index) {
    if(!valid_id(cusa))return NULL;
    unsigned n=0;
    for(unsigned i=0;i<updates_count;++i)
        if(strcmp(updates[i].cusa,cusa)==0 && n++==index)
            return &updates[i];
    return NULL;
}
