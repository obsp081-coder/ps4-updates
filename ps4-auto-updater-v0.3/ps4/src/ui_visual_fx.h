/*
 * Lightweight Royal Blue visual primitives.
 * The approved concept uses translucent blue glass, gradient depth and
 * restrained cyan focus haloes. These routines are deterministic CPU-only:
 * no GPU API, heap allocations, runtime font load, network or animation.
 * Panel fills preserve 25% of the actual underlying local-art background.
 */
#ifndef UI_VISUAL_FX_H
#define UI_VISUAL_FX_H
#include <stdint.h>
#include <stddef.h>

static inline uint32_t ui_fx_glass_mix(uint32_t under,uint32_t tint) {
    /* Mix 1/4 existing artwork + 3/4 tint in parallel RGB lanes.
       Mask low bits before shifts so there is no channel carry. */
    const uint32_t a=(under&0x00FCFCFCu)>>2;
    const uint32_t b=(tint&0x00FCFCFCu)>>2;
    return 0xFF000000u | (a+3u*b);
}
static inline uint32_t ui_fx_blend8(uint32_t under,uint32_t tint,
                                     unsigned alpha) {
    /* General-purpose bounded RGB alpha only for tiny glows. */
    if(alpha>256u)alpha=256u;
    const unsigned inv=256u-alpha;
    const unsigned r=((((under>>16)&255u)*inv+
                        ((tint>>16)&255u)*alpha)>>8);
    const unsigned g=((((under>>8)&255u)*inv+
                        ((tint>>8)&255u)*alpha)>>8);
    const unsigned b=(((under&255u)*inv+
                        (tint&255u)*alpha)>>8);
    return 0xFF000000u|(r<<16)|(g<<8)|b;
}
static inline uint32_t ui_fx_gradient_rgb(uint32_t top,uint32_t bottom,
                                            unsigned dy,unsigned h) {
    if(h==0u)return top;
    const unsigned a=dy;
    const unsigned z=h-a;
    const unsigned r=((((top>>16)&255u)*z+
                       ((bottom>>16)&255u)*a)/h);
    const unsigned g=((((top>>8)&255u)*z+
                       ((bottom>>8)&255u)*a)/h);
    const unsigned b=(((top&255u)*z+
                       (bottom&255u)*a)/h);
    return 0xFF000000u|(r<<16)|(g<<8)|b;
}

/* 8-step bilinear sampling used to upscale the already preblurred local
 * icon. Caller provides weight 0..7; uniform colors round-trip exactly.
 * Kept independent of GPU/runtime allocations, and channels never cross. */
static inline uint32_t ui_fx_lerp_rgb8(uint32_t a,uint32_t b,
                                        unsigned weight) {
    if(weight>7u)weight=7u;
    const unsigned inv=8u-weight;
    const unsigned red=((((a>>16)&255u)*inv+
                         ((b>>16)&255u)*weight+4u)>>3);
    const unsigned green=((((a>>8)&255u)*inv+
                           ((b>>8)&255u)*weight+4u)>>3);
    const unsigned blue=(((a&255u)*inv+
                          (b&255u)*weight+4u)>>3);
    return 0xFF000000u|(red<<16)|(green<<8)|blue;
}

/* Rounded, background-aware tint, soft static drop shadow and lit rim.
   Complexity O(w*h) like the existing panel, but no full-screen effects.
   Caller guarantees non-overlap with static text/UI masks. */
static inline void ui_fx_glass_panel(uint32_t *p,int sw,int sh,
                                      int x,int y,int w,int h,
                                      uint32_t top,uint32_t bottom,
                                      int focused) {
    if(!p || x<7 || y<6 || w<24 || h<22 ||
       x+w+6>=sw || y+h+6>=sh)return;
    /* Underlay shadow occupies only 2 narrow perimeter strips. */
    for(int d=5;d>=2;--d) {
        const unsigned alpha=(unsigned)(8-d)*10u;
        for(int xx=x+10;xx<x+w-10;++xx) {
            const size_t idx=(size_t)(y+h+d)*sw+xx;
            p[idx]=ui_fx_blend8(p[idx],0xFF020A1Bu,alpha);
        }
    }
    for(int dy=0;dy<h;++dy) {
        const int inset=dy==0||dy==h-1?9:
                        dy==1||dy==h-2?6:
                        dy==2||dy==h-3?3:1;
        const int left=x+inset,right=x+w-inset;
        const uint32_t tint=ui_fx_gradient_rgb(
            top,bottom,(unsigned)dy,(unsigned)(h-1));
        uint32_t *dst=p+(size_t)(y+dy)*sw;
        for(int dx=left;dx<right;++dx)
            dst[dx]=ui_fx_glass_mix(dst[dx],tint);
        if(dy==0||dy==h-1) {
            for(int dx=left;dx<right;++dx)
                dst[dx]=ui_fx_blend8(dst[dx],
                    focused?0xFF36C6FFu:0xFF3687CDu,
                    focused?190u:108u);
        } else if(dy>2 && dy<h-3) {
            dst[left]=ui_fx_blend8(dst[left],
                focused?0xFF36C6FFu:0xFF2970B6u,
                focused?180u:105u);
            dst[right-1]=ui_fx_blend8(dst[right-1],
                focused?0xFF36C6FFu:0xFF2970B6u,
                focused?180u:105u);
        }
        /* A slight specular edge breaks up the flat blue fields. */
        if(dy==3) {
            for(int dx=x+12;dx<x+w-12;++dx)
                dst[dx]=ui_fx_blend8(dst[dx],0xFF70C6FFu,
                    focused?100u:47u);
        }
    }
    if(focused) {
        for(int d=1;d<=3;++d) {
            const unsigned alpha=(unsigned)(4-d)*26u;
            for(int xx=x+10;xx<x+w-10;++xx) {
                size_t t=(size_t)(y-d)*sw+xx;
                size_t b=(size_t)(y+h+d-1)*sw+xx;
                p[t]=ui_fx_blend8(p[t],0xFF0876FFu,alpha);
                p[b]=ui_fx_blend8(p[b],0xFF0876FFu,alpha);
            }
        }
    }
}

/* Small illuminated focus rim for the real selected game row.
 * Never tint the center of a row: metadata remains sharp and existing
 * row identity/pixel-based tests continue to read the same content.
 * Four pixels of outer light fit the existing inter-row gutter.
 * O(w*12 + h*12), not an expensive full-card per-pixel blur. */
static inline void ui_fx_focus_row_glow(uint32_t *p,int sw,int sh,
                                         int x,int y,int w,int h) {
    if(!p || x<5 || y<5 || w<30 || h<25 ||
       x+w+5>=sw || y+h+4>=sh)return;
    for(int d=0;d<6;++d) {
        const unsigned alpha=(unsigned)(6-d)*14u;
        const int upper=y+8+d;
        const int lower=y+h-9-d;
        uint32_t *top=p+(size_t)upper*sw;
        uint32_t *bottom=p+(size_t)lower*sw;
        for(int xx=x+10;xx<x+w-10;++xx) {
            top[xx]=ui_fx_blend8(top[xx],0xFF248CFBu,alpha);
            bottom[xx]=ui_fx_blend8(bottom[xx],0xFF248CFBu,alpha);
        }
    }
    for(int d=1;d<=3;++d) {
        const unsigned alpha=(unsigned)(4-d)*20u;
        uint32_t *top=p+(size_t)(y-d)*sw;
        uint32_t *bottom=p+(size_t)(y+h-1+d)*sw;
        for(int xx=x+11;xx<x+w-11;++xx) {
            top[xx]=ui_fx_blend8(top[xx],0xFF0876FFu,alpha);
            bottom[xx]=ui_fx_blend8(bottom[xx],0xFF0876FFu,alpha);
        }
    }
}

/* Background-only static light pool. Approximate an atmospheric cyan halo
 * while keeping the scene pixel source legitimate: a muted navy fallback
 * or the user's installed-game icon, never a shipped copyrighted poster.
 * A 1920-wide precomputed x-distance table avoids per-pixel divisions.
 * No animation, timers, shader or runtime allocation. */
static inline void ui_fx_radial_haze(uint32_t *p,int sw,int sh,
                                      int cx,int cy,int rx,int ry,
                                      uint32_t tint,unsigned max_alpha) {
    if(!p || sw<1 || sw>1920 || sh<1 || rx<16 || ry<16)return;
    int32_t dx2[1920];
    int x0=cx-rx;if(x0<0)x0=0;
    int x1=cx+rx;if(x1>sw)x1=sw;
    int y0=cy-ry;if(y0<0)y0=0;
    int y1=cy+ry;if(y1>sh)y1=sh;
    for(int x=x0;x<x1;++x) {
        const int dx=((x-cx)*256)/rx;
        dx2[x]=dx*dx;
    }
    for(int y=y0;y<y1;++y) {
        const int dy=((y-cy)*256)/ry;
        const int dy2=dy*dy;
        uint32_t *dst=p+(size_t)y*sw;
        for(int x=x0;x<x1;++x) {
            const int radius2=dx2[x]+dy2;
            if(radius2>=65536)continue;
            const unsigned alpha=
                (max_alpha*(unsigned)(65536-radius2))>>16;
            if(alpha)
                dst[x]=ui_fx_blend8(dst[x],tint,alpha);
        }
    }
}
#endif /* UI_VISUAL_FX_H */
