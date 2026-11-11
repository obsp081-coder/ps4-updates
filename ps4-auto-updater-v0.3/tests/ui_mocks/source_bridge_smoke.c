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
    UiSourceCandidate a[2]={row(UI_SOURCE_FPKG_SUBMITTED),
                            row(UI_SOURCE_FPKG_SUBMITTED)};
    strcpy(a[1].version,"01.10");
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
    /* The optional reviewer is read-only: a firmware conflict is
     * BLOCKED with an auditable reason, missing facts remain UNKNOWN.
     * Neither verdict permits downloading or installation. */
    UiCompatFacts review[2]={0};
    for(unsigned i=0;i<2;++i){
        review[i].base_cusa=a[i].cusa;
        review[i].patch_cusa=a[i].cusa;
        review[i].base_content_id=a[i].content_id;
        review[i].patch_content_id=a[i].content_id;
        review[i].base_version="01.07";
        review[i].patch_version=a[i].version;
        review[i].required_base_version="01.07";
        review[i].console_firmware="14.00";
        review[i].minimum_firmware="09.00";
        review[i].patch_size_bytes=1024;
        review[i].installed=UI_COMPAT_INSTALL_YES;
        review[i].base_kind=UI_COMPAT_FPKG;
        review[i].patch_kind=UI_COMPAT_FPKG;
        review[i].base_metadata_verified=1;
        review[i].identity_binding_verified=1;
        review[i].source_authorized=1;
        review[i].integrity_verified=1;
    }
    review[0].minimum_firmware="15.00";
    review[1].installed=UI_COMPAT_INSTALL_UNKNOWN;
    assert(ui_source_publish_reviews(a,review,2)==0);
    assert(ui_model_update_count()==2);
    assert(ui_model_update(0)->verdict==UI_UPDATE_BLOCKED);
    assert(ui_model_update(0)->compat_blockers &
           UI_COMPAT_FIRMWARE_TOO_LOW);
    assert(ui_model_update(1)->verdict==UI_UPDATE_UNKNOWN);
    assert(ui_model_update(1)->compat_unknowns &
           UI_COMPAT_INSTALL_UNCONFIRMED);
    assert(ui_model_update(0)->size_bytes==0);
    assert(ui_model_update(0)->minimum_fw[0]==0);

    /* REVIEW_ONLY (all positive synthetic facts) must still be UNKNOWN. */
    review[0].minimum_firmware="09.00";
    review[1].installed=UI_COMPAT_INSTALL_YES;
    assert(ui_source_publish_reviews(a,review,2)==0);
    for(unsigned i=0;i<2;++i){
        const UiUpdateInfo *u=ui_model_update(i);
        assert(u && u->verdict==UI_UPDATE_UNKNOWN);
        assert(u->compat_unknowns==0 && u->compat_blockers==0);
    }

    /* Full-batch rejection if the facts belong to a different package.
     * Never replace previously published candidate rows on error. */
    review[0].patch_content_id="EP9000-CUSA00411_00-UNCHARTEDTRILOGY";
    assert(ui_source_publish_reviews(a,review,2)==-4);
    assert(ui_model_update_count()==2);
    assert(ui_model_update(0)->verdict==UI_UPDATE_UNKNOWN);
    assert(!strcmp(ui_model_update(0)->content_id,a[0].content_id));
    review[0].patch_content_id=a[0].content_id;
    assert(ui_source_publish_reviews(a,NULL,2)==-1);
    assert(ui_source_publish_reviews(NULL,NULL,0)==0);
    assert(ui_model_update_count()==0);
    assert(ui_source_publish_candidates(a,2)==0);

    a[1].source_kind=(UiUpdateSourceKind)99;
    assert(ui_source_publish_candidates(a,2)<0);
    a[1]=row(UI_SOURCE_FPKG_SUBMITTED);
    strcpy(a[1].content_id,"EP9000-CUSA00001_00-UNCHARTEDTRILOGY");
    assert(ui_source_publish_candidates(a,2)<0);
    a[1]=row(UI_SOURCE_RETAIL_HTTP);
    assert(ui_source_publish_candidates(a,2)<0);
    assert(ui_model_update_count()==2);
    a[0]=row(UI_SOURCE_RETAIL_HTTP);
    a[1]=row(UI_SOURCE_FPKG_SUBMITTED);
    strcpy(a[1].version,"01.10");
    assert(ui_source_publish_candidates(a,2)<0);
    a[0]=row(UI_SOURCE_FPKG_SUBMITTED);
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
