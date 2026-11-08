#include "ui_title_layout.h"
#include <string.h>

/* Classify only script features we can actually display:
 * ASCII Latin/digits and Arabic strong letters. Marks/punctuation are
 * neutral, and other script glyphs remain explicit renderer fallbacks.
 * This is NOT a substitute for Unicode Bidirectional Algorithm. */
static int strong_direction(uint32_t cp) {
    if ((cp >= 'A' && cp <= 'Z') ||
        (cp >= 'a' && cp <= 'z') ||
        (cp >= '0' && cp <= '9') ||
        (cp >= 0x0660u && cp <= 0x0669u) ||
        (cp >= 0x06F0u && cp <= 0x06F9u))
        return UI_TITLE_LTR;
    if ((cp >= 0x0620u && cp <= 0x08FFu) &&
        !(cp >= 0x064Bu && cp <= 0x065Fu) &&
        !(cp >= 0x0670u && cp <= 0x0670u) &&
        !(cp >= 0x06D6u && cp <= 0x06EDu))
        return UI_TITLE_RTL;
    /* Combining Arabic marks are direction-transparent. They must not
     * split a shaped Arabic word into synthetic LTR/RTL segments. */
    if ((cp >= 0x064Bu && cp <= 0x065Fu) || cp==0x0670u ||
        (cp >= 0x06D6u && cp <= 0x06EDu))
        return -1;
    if (cp >= 0x0080u)
        return UI_TITLE_LTR; /* Unsupported scripts use '?' placeholder. */
    return -1; /* ASCII separators/spaces inherit adjacent run. */
}

int ui_title_layout(const uint32_t *codepoints, size_t length,
                    UiTitleLayout *out) {
    if (!out || length > UI_TITLE_MAX_CODEPOINTS ||
        (length && !codepoints)) return -1;
    memset(out,0,sizeof *out);
    int base=UI_TITLE_LTR;
    for(size_t i=0;i<length;++i) {
        const int direction=strong_direction(codepoints[i]);
        if(direction>=0) {base=direction;break;}
    }
    out->paragraph=(UiTitleDirection)base;
    if(!length)return 0;
    out->runs[0].start=0;
    out->runs[0].direction=(UiTitleDirection)base;
    out->count=1;
    for(size_t i=0;i<length;++i) {
        const int direction=strong_direction(codepoints[i]);
        UiTitleRun *run=&out->runs[out->count-1];
        /* Attach neutral punctuation to preceding run. */
        if(direction>=0 && direction!=(int)run->direction) {
            if(out->count>=UI_TITLE_MAX_RUNS)return -1;
            run=&out->runs[out->count++];
            run->start=(unsigned)i;
            run->direction=(UiTitleDirection)direction;
        }
        ++run->length;
    }
    return 0;
}
