#include "ui_source_bridge.h"
#include <stddef.h>
#include <string.h>
static int cusa_ok(const char *s) {
    if(!s||memcmp(s,"CUSA",4)!=0)return 0;
    for(int i=4;i<9;++i)
        if(s[i]<'0'||s[i]>'9')return 0;
    return s[9]==0;
}
static int cid_ok(const char *s,const char *cusa) {
    if(!s||s[36]!=0||s[0]<'A'||s[0]>'Z'||
       s[1]<'A'||s[1]>'Z')return 0;
    for(int i=2;i<6;++i)
        if(s[i]<'0'||s[i]>'9')return 0;
    if(s[6]!='-'||memcmp(s+7,cusa,9)!=0||
       s[16]!='_'||s[17]<'0'||s[17]>'9'||
       s[18]<'0'||s[18]>'9'||s[19]!='-')return 0;
    for(int i=20;i<36;++i) {
        char v=s[i];
        if(!((v>='A'&&v<='Z')||(v>='0'&&v<='9')))return 0;
    }
    return 1;
}
static int version_ok(const char *s) {
    int dots=0,digits=0;
    if(!s)return 0;
    for(int i=0;i<16;++i) {
        const char v=s[i];
        if(!v)return dots==1&&digits>0;
        if(v=='.') {
            if(dots++ || !digits)return 0;
            digits=0;
        } else if(v>='0'&&v<='9') {
            if(++digits>4)return 0;
        } else return 0;
    }
    return 0;
}
static int publish(const UiSourceCandidate *rows,const UiCompatFacts *facts,
                   unsigned count,int reviewed) {
    if(count>UI_MODEL_MAX_UPDATES||(count&&!rows)||
       (reviewed&&count&&!facts))return -1;
    /* A native Host regression compiles this .c under C++17 as well.
     * memset is portable across C and C++, including -Werror builds. */
    UiUpdateInfo stage[UI_MODEL_MAX_UPDATES];
    memset(stage,0,sizeof stage);
    for(unsigned i=0;i<count;++i) {
        const UiSourceCandidate *source=&rows[i];
        if(!cusa_ok(source->cusa) ||
           !cid_ok(source->content_id,source->cusa) ||
           !version_ok(source->version) ||
           source->source_kind!=UI_SOURCE_FPKG_SUBMITTED)return -2;
        memcpy(stage[i].cusa,source->cusa,sizeof stage[i].cusa);
        memcpy(stage[i].version,source->version,sizeof stage[i].version);
        memcpy(stage[i].content_id,source->content_id,
               sizeof stage[i].content_id);
        stage[i].source_kind=source->source_kind;
        stage[i].verdict=UI_UPDATE_UNKNOWN;
        if(reviewed) {
            const UiCompatFacts *f=&facts[i];
            /* Identity binding must be exact; never let an assessment
             * about one candidate label another candidate as BLOCKED. */
            if(!f->patch_cusa||!f->patch_content_id||!f->patch_version||
               strcmp(f->patch_cusa,source->cusa)||
               strcmp(f->patch_content_id,source->content_id)||
               strcmp(f->patch_version,source->version))return -4;
            const UiCompatAssessment a=ui_compat_assess(f);
            stage[i].compat_blockers=a.blockers;
            stage[i].compat_unknowns=a.unknowns;
            /* REVIEW_ONLY is not verified compatibility. Never promote
             * it to UI_UPDATE_VERIFIED or enable download/install. */
            if(a.decision==UI_COMPAT_BLOCKED)
                stage[i].verdict=UI_UPDATE_BLOCKED;
        }
        /* Claims from HTTP/XML and user FPKG submissions do not prove
         * size or min FW. Never copy unverified numbers into native UI. */
        stage[i].size_bytes=0u;
        stage[i].minimum_fw[0]=0;
        for(unsigned j=0;j<i;++j) {
            if(!strcmp(stage[i].cusa,stage[j].cusa) &&
               !strcmp(stage[i].version,stage[j].version) &&
               stage[i].source_kind==stage[j].source_kind)return -3;
        }
    }
    return ui_model_set_updates(stage,count);
}

/* Legacy candidates without independently reviewed base facts remain unknown. */
int ui_source_publish_candidates(const UiSourceCandidate *rows,unsigned count) {
    return publish(rows,NULL,count,0);
}
int ui_source_publish_reviews(const UiSourceCandidate *rows,
                              const UiCompatFacts *facts,unsigned count) {
    return publish(rows,facts,count,1);
}
