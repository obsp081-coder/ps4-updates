#include "../../ps4/src/ui_icon_cache.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const unsigned char png[] = {
0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x08,0x06,0x00,0x00,0x00,0x72,0xb6,0x0d,0x24,0x00,0x00,0x00,0x14,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0x94,0xf4,0xde,0xf2,0x9f,0x81,0x81,0x81,0x81,0x89,0x01,0x0a,0x00,0x1f,0x0d,0x02,0x1b,0x4c,0x07,0x75,0x81,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
};
int main(void) {
    ui_icon_cache_reset();
    assert(!ui_icon_cache_find("CUSA00000"));
    assert(ui_icon_cache_offer("BAD",png,sizeof png)<0);
    assert(!ui_icon_cache_find("CUSA00000"));
    for(unsigned i=0;i<UI_ICON_CACHE_SLOTS;++i) {
        char id[10];
        snprintf(id,sizeof id,"CUSA%05u",i);
        assert(ui_icon_cache_offer(id,png,sizeof png)==0);
    }
    const uint32_t *pix=ui_icon_cache_find("CUSA00000");
    assert(pix && pix[0]==0xFF194BB4u);
    /* Recently viewed CUSA00000 is preserved while LRU CUSA00001 evicts. */
    assert(ui_icon_cache_offer("CUSA00008",png,sizeof png)==0);
    assert(ui_icon_cache_find("CUSA00000")!=NULL);
    assert(ui_icon_cache_find("CUSA00001")==NULL);
    /* Worker-staged thumbnails are imported only after thread join.
     * Reject malformed handoff without evicting the current entry. */
    uint32_t staged[UI_ICON_PIXELS];
    for(size_t i=0;i<UI_ICON_PIXELS;++i)staged[i]=0xFF102D50u;
    assert(ui_icon_cache_offer_pixels("CUSA00000",NULL,UI_ICON_PIXELS)<0);
    assert(ui_icon_cache_offer_pixels("CUSA00000",staged,UI_ICON_PIXELS-1)<0);
    assert(ui_icon_cache_offer_pixels("BAD",staged,UI_ICON_PIXELS)<0);
    assert(ui_icon_cache_offer_pixels("CUSA00000",staged,UI_ICON_PIXELS)==0);
    assert(ui_icon_cache_find("CUSA00000")[0]==0xFF102D50u);
    const uint32_t *before=ui_icon_cache_find("CUSA00000");
    unsigned char corrupted[sizeof png];
    memcpy(corrupted,png,sizeof png);
    corrupted[0]='N';
    assert(ui_icon_cache_offer("CUSA00000",corrupted,sizeof corrupted)<0);
    assert(ui_icon_cache_find("CUSA00000")==before);
    ui_icon_cache_reset();
    assert(!ui_icon_cache_find("CUSA00000"));
    puts("UI_ICON_CACHE_HOST_OK");
    return 0;
}
