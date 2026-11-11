/* Host compilation with __ORBIS__ defines: assert production pipeline
 * cannot start without FW14 transport/storage/source verification.
 * This is a unit test, NOT a real PS4 hardware or emulator test.
 */
#include "../../ps4/src/download_pipeline.h"
#include "../../ps4/src/download_file_backend.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    DlqQueue q;
    dlq_init(&q);
    assert(dlq_enqueue(&q,"CUSA02343",
                       "IV0000-CUSA02343_00-SCANNER000000000",
                       "01.09",100u)==0);

    DsFileBackend b;
    assert(ds_file_backend_attach(&b,42)==-1);
    DsPort port=ds_file_backend_port(&b);
    assert(!port.create_exclusive && !port.publish_exclusive);

    const uint8_t digest[32]={1};
    DlPipeline p;
    assert(dlp_init(&p,&q,0,&port,digest)==DLP_OK);
    const char *header=
        "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n"
        "ETag: \"trusted-test\"\r\n\r\n";
    /* Even a forged transport-authenticated bit MUST NOT activate PS4. */
    assert(dlp_begin(&p,header,strlen(header),1,100000u,0u,0u,NULL)==
           DLP_UNAVAILABLE);
    assert(dlq_get(&q,0)->state==DLQ_QUEUED);
    assert(dlq_get(&q,0)->received_bytes==0u);
    assert(dlp_feed(&p,(const uint8_t *)"x",1u)==DLP_INVALID);
    assert(dlp_finish(&p)==DLP_INVALID);
    assert(!p.storage.owns_part);
    ds_file_backend_detach(&b);
    puts("PIPELINE_PS4_RUNTIME_GATE_OK");
    return 0;
}
