#include "ui_backdrop.h"
#include "ui_icon_cache.h"
#include <string.h>
/* Small, fixed buffers (18 KiB) instead of full-screen blur textures.
 * Recompute only on title selection change; rendering paints a static
 * 64x36 image, upscaled beneath opaque UI panels.
 */
static uint32_t blurred[UI_BACKDROP_CELLS];
static uint32_t sampled[UI_BACKDROP_CELLS];
static char selected_id[10];
static int available;

static int validate_id(const char *id) {
    if (!id) return 0;
    for (unsigned i=0; i<9; ++i) {
        const unsigned char c=(unsigned char)id[i];
        if (i<4 && c!=(unsigned char)"CUSA"[i]) return 0;
        if (i>=4 && (c<'0'||c>'9')) return 0;
    }
    return id[9]==0;
}
static uint32_t soften(uint32_t src) {
    /* 27% image / 73% royal-blue panel, preserving legibility. */
    const unsigned r=(((src>>16)&255u)*27u+6u*73u)/100u;
    const unsigned g=(((src>>8)&255u)*27u+17u*73u)/100u;
    const unsigned b=((src&255u)*27u+38u*73u)/100u;
    return 0xFF000000u|(r<<16)|(g<<8)|b;
}
void ui_backdrop_reset(void) {
    available=0;
    selected_id[0]=0;
}
int ui_backdrop_select(const char *id) {
    if (!validate_id(id)) {
        ui_backdrop_reset();
        return -1;
    }
    const uint32_t *src=ui_icon_cache_find(id);
    if (!src) {
        ui_backdrop_reset();
        return 0;
    }
    if (available && strcmp(selected_id,id)==0) return 1;
    for (unsigned y=0;y<UI_BACKDROP_ROWS;++y)
        for (unsigned x=0;x<UI_BACKDROP_COLS;++x) {
            const unsigned sy=(y*UI_ICON_SIDE)/UI_BACKDROP_ROWS;
            const unsigned sx=(x*UI_ICON_SIDE)/UI_BACKDROP_COLS;
            sampled[y*UI_BACKDROP_COLS+x]=src[sy*UI_ICON_SIDE+sx];
        }

    /* A 5x5 uniform low-resolution blur, run once per selected title.
     * Clamp edges instead of allocating padded buffers. */
    for (unsigned y=0;y<UI_BACKDROP_ROWS;++y)
        for (unsigned x=0;x<UI_BACKDROP_COLS;++x) {
            unsigned r=0,g=0,b=0,n=0;
            for (int dy=-2;dy<=2;++dy)
                for (int dx=-2;dx<=2;++dx) {
                    int ix=(int)x+dx,iy=(int)y+dy;
                    if (ix<0)ix=0;
                    if (iy<0)iy=0;
                    if (ix>=(int)UI_BACKDROP_COLS)ix=(int)UI_BACKDROP_COLS-1;
                    if (iy>=(int)UI_BACKDROP_ROWS)iy=(int)UI_BACKDROP_ROWS-1;
                    const uint32_t p=sampled[(unsigned)iy*UI_BACKDROP_COLS+(unsigned)ix];
                    r+=(p>>16)&255u;
                    g+=(p>>8)&255u;
                    b+=p&255u;
                    ++n;
                }
            blurred[y*UI_BACKDROP_COLS+x]=soften(
                0xFF000000u|((r/n)<<16)|((g/n)<<8)|(b/n)
            );
        }
    memcpy(selected_id,id,10);
    available=1;
    return 1;
}
int ui_backdrop_render(uint32_t *dst,size_t capacity,
                       unsigned width,unsigned height) {
    if (!available || !dst || width==0 || height==0 ||
        width>4096u || height>2160u || capacity<(size_t)width*height)
        return 0;
    for (unsigned y=0;y<height;++y) {
        const unsigned sy=(unsigned)(((uint64_t)y*UI_BACKDROP_ROWS)/height);
        const uint32_t *row=blurred+sy*UI_BACKDROP_COLS;
        uint32_t *out=dst+(size_t)y*width;
        for (unsigned x=0;x<width;++x) {
            const unsigned sx=(unsigned)(((uint64_t)x*UI_BACKDROP_COLS)/width);
            out[x]=row[sx];
        }
    }
    return 1;
}
