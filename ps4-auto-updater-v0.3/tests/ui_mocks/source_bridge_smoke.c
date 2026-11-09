/* Native source metadata is never install authorization. */
#include "../../ps4/src/ui_source_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static UiSourceCandidate row(UiUpdateSourceKind kind){
    UiSourceCandidate x={0};
    strcpy(x.cusa,"CUSA02343");
    strcpy(x.content_id,"EP9000-CUSA02343_00-UNCHARTEDTRILOGY");
    strcpy(x.version,"01.09");
    x.source_kind=kind;
    return x;
}
int main(void){
    ui_model_reset();
    UiSourceCandidate a[2]={row(UI_SOURCE_RETAIL_HTTP),
                            row(UI_SOURCE_FPKG_SUBMITTED)};
    assert(ui_source_publish_candidates(a,2)==0);
    assert(ui_model_update_count()==2);
    assert(ui_model_download_count()==0);
    for(unsigned i=0;i<2;++i){
        const UiUpdateInfo *u=ui_model_update(i);
        assert(u->verdict==UI_UPDATE_UNKNOWN);
        assert(u->source_kind==a[i].source_kind);
        assert(u->size_bytes==0);
        assert(u->minimum_fw[0]==0);
        assert(!strcmp(u->content_id,a[i].content_id));
    }
    a[1].source_kind=(UiUpdateSourceKind)99;
    assert(ui_source_publish_candidates(a,2)<0);
    a[1]=row(UI_SOURCE_FPKG_SUBMITTED);
    strcpy(a[1].content_id,"EP9000-CUSA00001_00-UNCHARTEDTRILOGY");
    assert(ui_source_publish_candidates(a,2)<0);
    a[1]=row(UI_SOURCE_RETAIL_HTTP);
    assert(ui_source_publish_candidates(a,2)<0);
    a[1]=row(UI_SOURCE_FPKG_SUBMITTED);
    strcpy(a[1].version,"01.99999");
    assert(ui_source_publish_candidates(a,2)<0);
    a[1]=row(UI_SOURCE_FPKG_SUBMITTED);
    a[1].content_id[35]='!';
    assert(ui_source_publish_candidates(a,2)<0);
    assert(ui_model_update_count()==2);
    assert(ui_source_publish_candidates(NULL,1)<0);
    assert(ui_source_publish_candidates(a,UI_MODEL_MAX_UPDATES+1)<0);
    assert(ui_model_update_count()==2);
    assert(ui_source_publish_candidates(NULL,0)==0);
    assert(ui_model_update_count()==0);
    puts("UI_SOURCE_BRIDGE_HOST_OK");
    return 0;
}
