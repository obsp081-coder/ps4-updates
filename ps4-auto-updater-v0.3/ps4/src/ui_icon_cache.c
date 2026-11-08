#include "ui_icon_cache.h"
#include <ctype.h>
#include <stdint.h>
#include <string.h>
typedef struct UiIconSlot {
    char id[10];
    uint32_t last_use;
    uint32_t pixels[UI_ICON_PIXELS];
    int valid;
} UiIconSlot;
static UiIconSlot slots[UI_ICON_CACHE_SLOTS];
static uint32_t tick;
/* Exactly one temporary thumbnail avoids evicting an intact cached image if
 * a malformed PNG is offered. Total cache is < 600 KiB for 8 slots. */
static uint32_t candidate[UI_ICON_PIXELS];
static int valid_id(const char *s) {
    if (!s) return 0;
    for (int i=0;i<9;++i) {
        if (!s[i]) return 0;
        if (i<4 && s[i] != "CUSA"[i]) return 0;
        if (i>=4 && !isdigit((unsigned char)s[i])) return 0;
    }
    return s[9]==0;
}
static uint32_t advance_clock(void) {
    if (++tick==0) {
        /* Deterministically reset age on rare wraparound. */
        for(unsigned i=0;i<UI_ICON_CACHE_SLOTS;++i)
            slots[i].last_use=0;
        tick=1;
    }
    return tick;
}
void ui_icon_cache_reset(void) {
    memset(slots,0,sizeof slots);
    tick=0;
}
const uint32_t *ui_icon_cache_find(const char *id) {
    if (!valid_id(id)) return NULL;
    for(unsigned i=0;i<UI_ICON_CACHE_SLOTS;++i) {
        if (slots[i].valid && strcmp(slots[i].id,id)==0) {
            slots[i].last_use=advance_clock();
            return slots[i].pixels;
        }
    }
    return NULL;
}
int ui_icon_cache_offer(const char *id,const unsigned char *png,size_t bytes) {
    if(!valid_id(id))return -1;
    int ret=ui_image_thumbnail(png,bytes,candidate,UI_ICON_PIXELS);
    if(ret<0)return ret;
    return ui_icon_cache_offer_pixels(id,candidate,UI_ICON_PIXELS);
}
int ui_icon_cache_offer_pixels(const char *id,const uint32_t *pixels,
                               size_t pixel_count) {
    if(!valid_id(id) || !pixels || pixel_count!=UI_ICON_PIXELS)return -1;
    unsigned chosen=0;
    uint32_t min_age=UINT32_MAX;
    for(unsigned i=0;i<UI_ICON_CACHE_SLOTS;++i) {
        if(slots[i].valid && strcmp(slots[i].id,id)==0) {
            chosen=i; break;
        }
        if(!slots[i].valid) { chosen=i; min_age=0; break; }
        if(slots[i].last_use<min_age) {
            min_age=slots[i].last_use;
            chosen=i;
        }
    }
    /* Find an existing entry first even when there is a free slot. */
    for(unsigned i=0;i<UI_ICON_CACHE_SLOTS;++i)
        if(slots[i].valid && strcmp(slots[i].id,id)==0) { chosen=i; break; }
    memcpy(slots[chosen].pixels,pixels,sizeof slots[chosen].pixels);
    memcpy(slots[chosen].id,id,10);
    slots[chosen].valid=1;
    slots[chosen].last_use=advance_clock();
    return 0;
}
