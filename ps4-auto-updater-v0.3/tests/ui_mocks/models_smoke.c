/* Host-only UI metadata model smoke test: never downloads or installs. */
#include "../../ps4/src/ui_models.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    ui_model_reset();
    assert(ui_model_update_count()==0u);
    assert(ui_model_download_count()==0u);

    UiUpdateInfo u[2]={0};
    strcpy(u[0].cusa,"CUSA02343");
    strcpy(u[0].version,"01.09");
    strcpy(u[0].minimum_fw,"05.05");
    u[0].size_bytes=5500000u;
    u[0].verdict=UI_UPDATE_UNKNOWN;
    u[1]=u[0];
    strcpy(u[1].version,"01.08");
    u[1].verdict=UI_UPDATE_BLOCKED;
    u[1].compat_blockers=1u; /* Synthetic mismatch reason. */
    assert(ui_model_set_updates(u,2)==0);
    assert(ui_model_update_count()==2u);
    assert(ui_model_update(1)->compat_blockers==1u);
    assert(ui_model_update_for("CUSA02343",0)!=NULL);
    assert(ui_model_update_for("CUSA02343",1)!=NULL);
    assert(ui_model_update_for("CUSA02343",2)==NULL);

    UiUpdateInfo unsupported=u[0];
    unsupported.source_kind=UI_SOURCE_RETAIL_HTTP;
    assert(ui_model_set_updates(&unsupported,1)<0);
    assert(ui_model_update_count()==2u);
    UiUpdateInfo bad[2]={u[0],u[0]};
    assert(ui_model_set_updates(bad,2)<0);
    assert(ui_model_update_count()==2u);
    assert(strcmp(ui_model_update(1)->version,"01.08")==0);
    strcpy(bad[0].cusa,"BADID");
    assert(ui_model_set_updates(bad,1)<0);
    assert(ui_model_update_count()==2u);
    /* A blocked label cannot exist without a machine-readable reason. */
    UiUpdateInfo false_block=u[1];
    false_block.compat_blockers=0u;
    assert(ui_model_set_updates(&false_block,1)<0);
    false_block.verdict=UI_UPDATE_UNKNOWN;
    false_block.compat_blockers=1u;
    assert(ui_model_set_updates(&false_block,1)<0);
    assert(ui_model_update_count()==2u);
    u[0].verdict=UI_UPDATE_VERIFIED;
    assert(ui_model_set_updates(u,1)<0); /* Phase 3 denies fake approval */
    u[0].verdict=UI_UPDATE_UNKNOWN;
    assert(ui_model_set_updates(NULL,1)<0);
    assert(ui_model_set_updates(u,UI_MODEL_MAX_UPDATES+1u)<0);

    UiDownloadInfo downloads[2]={0};
    strcpy(downloads[0].cusa,"CUSA02343");
    strcpy(downloads[0].title,"HOST FIXTURE: NO NETWORK");
    strcpy(downloads[0].version,"01.09");
    downloads[0].bytes_total=1000u;
    downloads[0].bytes_done=500u;
    downloads[0].status=UI_JOB_DOWNLOADING;
    downloads[1]=downloads[0];
    strcpy(downloads[1].cusa,"CUSA00002");
    downloads[1].bytes_done=1000u;
    downloads[1].status=UI_JOB_COMPLETED;
    assert(ui_model_set_downloads(downloads,2)==0);
    assert(ui_model_download_count()==2u);
    assert(ui_model_download(1)->status==UI_JOB_COMPLETED);

    downloads[0].bytes_done=1001u;
    assert(ui_model_set_downloads(downloads,2)<0);
    assert(ui_model_download_count()==2u);
    assert(ui_model_download(0)->bytes_done==500u);
    downloads[0].bytes_done=500u;
    downloads[1].bytes_total=0u; /* Impossible complete state. */
    assert(ui_model_set_downloads(downloads,2)<0);
    assert(ui_model_download(1)->bytes_total==1000u);
    assert(ui_model_set_downloads(NULL,1)<0);
    assert(ui_model_set_downloads(downloads,UI_MODEL_MAX_JOBS+1u)<0);

    assert(ui_model_set_updates(NULL,0)==0);
    assert(ui_model_set_downloads(NULL,0)==0);
    assert(ui_model_update_count()==0u);
    assert(ui_model_download_count()==0u);
    puts("UI_MODEL_HOST_OK");
    return 0;
}
