/*
 * Minimal PS4 native read-only scanner UI.
 * CPU framebuffer via OpenOrbis VideoOut; DualShock input via ScePad.
 * No installation, download, or game-file writes.
 */
#include "scanner_ui.h"
#include "ui_navigation.h"
#include "ui_icon_cache.h"
#include "ui_models.h"
#include "ui_arabic_generated.h"
#ifdef PS4_UI_HOST_TEST
#include "scanner_ui_host_mock.h"
#else
#include <orbis/VideoOut.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 1920
#define HEIGHT 1080
#define PIXELS ((size_t)WIDTH * HEIGHT)
#define BUFFER_SIZE (PIXELS * 4)
#define ALIGNMENT 0x200000UL
#define ROWS UI_VISIBLE_ROWS

typedef struct {char name; uint8_t rows[7];} Glyph;
static const Glyph font[] = {
{' ',{0,0,0,0,0,0,0}}, {'?',{14,17,1,2,4,0,4}},
{'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
{'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
{'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
{'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
{'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}},
{'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
{'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
{'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
{'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
{'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
{'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
{'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
{'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
{'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
{'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
{'4',{2,6,10,18,31,2,2}}, {'5',{31,16,30,1,1,17,14}},
{'6',{6,8,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
{'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,2,12}},
{':',{0,4,4,0,4,4,0}}, {'.',{0,0,0,0,0,6,6}},
{'-',{0,0,0,31,0,0,0}}, {'_',{0,0,0,0,0,0,31}},
{'/',{1,2,2,4,8,8,16}}, {'=',{0,31,0,31,0,0,0}},
{'(',{2,4,8,8,8,4,2}}, {')',{8,4,2,2,2,4,8}}
};

static int video = -1, pad = -1, usable = 0, next_buffer = 0;
static int buffers_registered = 0;
static off_t mem_off = 0;
static size_t mem_size = 0;
static void *mapped = nullptr;
static uint32_t *surface[2] = {nullptr, nullptr};
static int64_t frame_id = 0;
static char progress_line[128] = "CONNECTING TO GOLDHEN FTP";
static UiNavigation navigation;
static unsigned job_selected=0;
static unsigned job_scroll=0;
static UiRecord navigation_records[UI_MAX_TITLES];

static const uint8_t *glyph(char c) {
    unsigned char x = (unsigned char)c;
    if (x >= 'a' && x <= 'z') x = (unsigned char)(x - 'a' + 'A');
    for (size_t i = 0; i < sizeof(font)/sizeof(font[0]); ++i)
        if (font[i].name == (char)x) return font[i].rows;
    return font[1].rows;
}

static void rect(uint32_t *pixels, int x, int y,
                 int width, int height, uint32_t color) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        x >= WIDTH || y >= HEIGHT) return;
    if (width > WIDTH - x) width = WIDTH - x;
    if (height > HEIGHT - y) height = HEIGHT - y;
    for (int j = y; j < y + height; ++j) {
        uint32_t *row = pixels + (size_t)j * WIDTH + x;
        for (int i = 0; i < width; ++i) row[i] = color;
    }
}

/* Proportional OFL Noto Sans Latin glyphs are rasterized at build time,
 * so PS4 never loads a font or runs a dynamic text-shaping engine.
 * Three sizes map to existing 2/3/4 UI scales; the old 5x7 font remains
 * only for uncommon sizes. Text remains bounded to each caller's column. */
static int latin_width(const char *s,int scale,int max_chars) {
    if(!s || scale<2 || scale>4 || max_chars<=0) return 0;
    int width=0;
    for(int i=0; s[i] && i<max_chars; ++i) {
        unsigned c=(unsigned char)s[i];
        if(c<32u || c>126u)c='?';
        width+=(int)UI_LATIN_GLYPHS[scale-2][c-32u].advance;
    }
    return width;
}
static void text(uint32_t *pixels, int x, int y, const char *s,
                 uint32_t color, int scale, int max_chars) {
    if (!pixels || !s || max_chars<=0 || scale<=0) return;
    if(scale>=2 && scale<=4) {
        /* A 42px proportional 'W' is wider than the old 5x7 cell.
         * One-character keyboard buttons and the compact PS4 badge need
         * a glyph-sized cap or letters vanish entirely. Long game titles
         * retain their conservative column budget. */
        const int cell_width=max_chars<=4 ? 12*scale : 6*scale;
        const int cap=x+max_chars*cell_width<WIDTH ?
                      x+max_chars*cell_width:WIDTH;
        int pen=x;
        for(int i=0; s[i] && i<max_chars; ++i) {
            unsigned c=(unsigned char)s[i];
            if(c<32u || c>126u)c='?';
            const UiLatinGlyph g=UI_LATIN_GLYPHS[scale-2][c-32u];
            if(pen+(int)g.width>cap)break;
            if(pen>=0 && y>=0 && y+(int)g.height<HEIGHT) {
                for(unsigned row=0;row<g.height;++row)
                    for(unsigned col=0;col<g.width;++col) {
                        const size_t k=(size_t)row*g.width+col;
                        const unsigned a=(UI_LATIN_BITS[g.offset+k/4]>>
                                          (2*(3-k%4)))&3u;
                        if(!a)continue;
                        uint32_t *dst=&pixels[(size_t)(y+(int)row)*WIDTH+
                                               (size_t)(pen+(int)col)];
                        if(a==3u) {*dst=color;continue;}
                        const uint32_t old=*dst;
                        const unsigned r=(((old>>16)&255u)*(3-a)+
                                          ((color>>16)&255u)*a)/3u;
                        const unsigned gch=(((old>>8)&255u)*(3-a)+
                                            ((color>>8)&255u)*a)/3u;
                        const unsigned b=((old&255u)*(3-a)+
                                          (color&255u)*a)/3u;
                        *dst=0xFF000000u|(r<<16)|(gch<<8)|b;
                    }
            }
            pen+=(int)g.advance;
        }
        return;
    }
    for (int i = 0; s[i] && i < max_chars; ++i) {
        const int bx=x+i*6*scale;
        if(bx+5*scale>=WIDTH)break;
        const uint8_t *rows=glyph(s[i]);
        for(int row=0;row<7;++row)
            for(int bit=0;bit<5;++bit)
                if(rows[row]&(1u<<(4-bit)))
                    rect(pixels,bx+bit*scale,y+row*scale,
                         scale,scale,color);
    }
}

/* Royal-blue native dashboard. Draws only on state changes; no animation
 * timer, network access or image decoding on the VideoOut thread. Images and
 * fully shaped Unicode text remain explicit follow-up acceptance gates. */
static const uint32_t BG = 0xFF061126u;
static const uint32_t PANEL = 0xFF0C244Au;
static const uint32_t ACCENT = 0xFF116BCEu;
static const uint32_t CYAN = 0xFF37ADFFu;
static const uint32_t WHITE = 0xFFEAF4FFu;
static const uint32_t MUTED = 0xFF9FB8D7u;
static const uint32_t AMBER = 0xFFFFC37Du;

/* Two-rectangle rounded panels, a one-pixel royal-blue border and
 * optional selection halo. All CPU raster primitives: no fragment shaders,
 * shadow textures, alpha framebuffers or per-frame allocations. */
static void panel(uint32_t *p, int x, int y, int w, int h,
                  uint32_t color) {
    if(w<18 || h<18) return;
    const int focused=(color==ACCENT || color==0xFF164881u);
    const uint32_t edge=focused?CYAN:0xFF205079u;
    if(focused && y>=4) {
        rect(p,x+12,y-3,w-24,2,0xFF15518Du);
        rect(p,x+12,y+h+1,w-24,2,0xFF15518Du);
    }
    rect(p,x+8,y,w-16,h,color);
    rect(p,x,y+8,w,h-16,color);
    rect(p,x+8,y,w-16,1,edge);
    rect(p,x+8,y+h-1,w-16,1,edge);
    rect(p,x,y+8,1,h-16,edge);
    rect(p,x+w-1,y+8,1,h-16,edge);
}
static void ascii(uint32_t *p, int x, int y, const char *s,
                  uint32_t color, int scale, int max_chars) {
    text(p,x,y,s,color,scale,max_chars);
}
/* The fixed Arabic UI captions are rasterized at build-time using Noto Sans
 * Arabic + Raqm. They draw directly into the same two native framebuffers.
 * 2 bits per source pixel keeps package memory small and text antialiased. */
static void arabic(uint32_t *p,int x,int y,UiArabicLabel label,uint32_t color) {
    if(!p || label<0 || label>=UI_AR_COUNT) return;
    UiArabicGlyph g=UI_AR_GLYPHS[label];
    if(x<0 || y<0 || x+(int)g.width>WIDTH || y+(int)g.height>HEIGHT) return;
    for(unsigned row=0;row<g.height;++row) {
        for(unsigned col=0;col<g.width;++col) {
            size_t k=(size_t)row*g.width+col;
            unsigned alpha=(UI_AR_PIXELS[g.offset+k/4] >> ((3-k%4)*2))&3u;
            if(!alpha) continue;
            uint32_t *dst=&p[(size_t)(y+(int)row)*WIDTH+x+col];
            if(alpha==3) { *dst=color; continue; }
            uint32_t old=*dst;
            unsigned rr=(((old>>16)&255u)*(3-alpha)+((color>>16)&255u)*alpha)/3u;
            unsigned gg=(((old>>8)&255u)*(3-alpha)+((color>>8)&255u)*alpha)/3u;
            unsigned bb=((old&255u)*(3-alpha)+(color&255u)*alpha)/3u;
            *dst=0xFF000000u|(rr<<16)|(gg<<8)|bb;
        }
    }
}
/* A compact conservative Arabic title renderer: presentation-form joining
 * for Arabic code points, byte-validating UTF-8, basic LTR ASCII runs.
 * Does not claim full ICU/HarfBuzz Unicode bidi coverage. Static navigation
 * captions are fully Raqm-shaped by the separate build-time atlas. */
static size_t utf8_title(const char *text,uint32_t out[192]) {
    size_t count=0;
    if(!text)return 0;
    const unsigned char *s=(const unsigned char *)text;
    while(*s && count<191) {
        uint32_t cp=0;
        unsigned char c=*s++;
        if(c<0x80) cp=c;
        else if(c>=0xC2 && c<=0xDF && (s[0]&0xC0)==0x80) {
            cp=((uint32_t)(c&31)<<6)|(s[0]&63); ++s;
        } else if(c>=0xE0 && c<=0xEF && s[0] && s[1] &&
                  (s[0]&0xC0)==0x80 && (s[1]&0xC0)==0x80 &&
                  !(c==0xE0 && s[0]<0xA0) &&
                  !(c==0xED && s[0]>=0xA0)) {
            cp=((uint32_t)(c&15)<<12)|((uint32_t)(s[0]&63)<<6)|(s[1]&63);
            s+=2;
        } else if(c>=0xF0 && c<=0xF4 && s[0] && s[1] && s[2] &&
                  (s[0]&0xC0)==0x80 && (s[1]&0xC0)==0x80 &&
                  (s[2]&0xC0)==0x80 && !(c==0xF0 && s[0]<0x90) &&
                  !(c==0xF4 && s[0]>=0x90)) {
            cp=((uint32_t)(c&7)<<18)|((uint32_t)(s[0]&63)<<12)|
               ((uint32_t)(s[1]&63)<<6)|(s[2]&63);
            s+=3;
        } else cp='?';
        out[count++]=cp;
    }
    return count;
}
static const UiArabicJoining *ui_joining(uint32_t cp) {
    for(unsigned i=0;i<UI_AR_JOINING_COUNT;++i)
        if(UI_AR_JOINING[i].base==cp)return &UI_AR_JOINING[i];
    return nullptr;
}
static void ui_draw_arabic_form(uint32_t *p,int x,int y,
                                uint16_t form,uint32_t color) {
    if(form>=UI_AR_DYNAMIC_COUNT)return;
    const UiArabicDynamicGlyph g=UI_AR_DYNAMIC_GLYPHS[form];
    if(x<0||y<0||x+(int)g.width>WIDTH||y+(int)g.height>HEIGHT)return;
    for(unsigned row=0;row<g.height;++row)for(unsigned col=0;col<g.width;++col) {
        const size_t k=(size_t)row*g.width+col;
        const unsigned alpha=(UI_AR_DYNAMIC_BITS[g.offset+k/4]>>
                               (2*(3-k%4)))&3u;
        if(!alpha)continue;
        uint32_t *dst=&p[(size_t)(y+(int)row)*WIDTH+x+col];
        if(alpha==3u) {*dst=color;continue;}
        const uint32_t old=*dst;
        const unsigned r=(((old>>16)&255u)*(3-alpha)+((color>>16)&255u)*alpha)/3u;
        const unsigned gch=(((old>>8)&255u)*(3-alpha)+((color>>8)&255u)*alpha)/3u;
        const unsigned b=((old&255u)*(3-alpha)+(color&255u)*alpha)/3u;
        *dst=0xFF000000u|(r<<16)|(gch<<8)|b;
    }
}
static void ui_title(uint32_t *p,int x,int y,int width,
                     const char *string,uint32_t color,int ascii_scale) {
    if(!string || width<=0)return;
    uint32_t cp[192]={};
    size_t n=utf8_title(string,cp);
    int has_arabic=0;
    for(size_t i=0;i<n;++i)
        if(ui_joining(cp[i])) {has_arabic=1;break;}
    if(!has_arabic) {
        ascii(p,x,y,string,color,ascii_scale,width/(6*ascii_scale));
        return;
    }
    int pen=x+width;
    for(size_t i=0;i<n && pen>x;) {
        const UiArabicJoining *curr=ui_joining(cp[i]);
        if(curr) {
            const UiArabicJoining *prev=i?ui_joining(cp[i-1]):nullptr;
            const UiArabicJoining *next=i+1<n?ui_joining(cp[i+1]):nullptr;
            int connected_prev=prev && curr->final!=65535u &&
                prev->initial!=65535u;
            int connected_next=next && curr->initial!=65535u &&
                next->final!=65535u;
            uint16_t shape=curr->isolated;
            if(connected_prev && connected_next && curr->medial!=65535u)
                shape=curr->medial;
            else if(connected_prev && curr->final!=65535u)
                shape=curr->final;
            else if(connected_next && curr->initial!=65535u)
                shape=curr->initial;
            if(shape<UI_AR_DYNAMIC_COUNT) {
                pen-=(int)UI_AR_DYNAMIC_GLYPHS[shape].width;
                if(pen<x)break;
                ui_draw_arabic_form(p,pen,y-4,shape,color);
            }
            ++i;
        } else {
            /* ASCII segment remains LTR within this RTL title. */
            char segment[96]={};
            size_t length=0;
            while(i<n && cp[i]<128 && !ui_joining(cp[i]) && length<95)
                segment[length++]=(char)cp[i++];
            if(length) {
                const int w=latin_width(segment,ascii_scale,
                                         (int)length);
                pen-=w;
                if(pen<x)break;
                const int chars_by_width=(w+6*ascii_scale-1)/
                                         (6*ascii_scale);
                const int allowed=chars_by_width>(int)length ?
                                  chars_by_width:(int)length;
                ascii(p,pen,y+18,segment,color,ascii_scale,allowed);
            } else {
                pen-=12;
                ++i;
            }
        }
    }
}
static UiArabicLabel category_label(const ScannerUiEntry *g) {
    if (!g || !g->metadata_verified) return UI_AR_UNVERIFIED_METADATA;
    if (!g->category) return UI_AR_OTHER_METADATA;
    if (!strcmp(g->category,"gp") || !strcmp(g->category,"gdp"))
        return UI_AR_PATCH_METADATA;
    if (!strcmp(g->category,"ac")) return UI_AR_DLC_METADATA;
    if (!strcmp(g->category,"gd") || !strcmp(g->category,"gda") ||
        !strcmp(g->category,"gdo") || !strcmp(g->category,"gde"))
        return g->install_state==1 ? UI_AR_GAME_INSTALLED :
               g->install_state==0 ? UI_AR_GAME_NOT_INSTALLED :
               UI_AR_BASE_UNKNOWN;
    return UI_AR_OTHER_METADATA;
}
static void game_title(uint32_t *p,int x,int y,int max_width,
                       const ScannerUiEntry *g,uint32_t color,int scale) {
    if(g && g->title && g->title[0])
        ui_title(p,x,y,max_width,g->title,color,scale);
    else
        arabic(p,x,y,UI_AR_GAME_NAME_UNAVAILABLE,AMBER);
}
static const char *fallback_version(const ScannerUiEntry *g) {
    return g && g->version && g->version[0] ? g->version : "--";
}
static void placeholder(uint32_t *p, int x, int y, int w, int h) {
    panel(p,x,y,w,h,0xFF163860u);
    rect(p,x+15,y+15,w-30,4,CYAN);
    ascii(p,x+19,y+h/2-8,"PS4",WHITE,3,3);
}
/* Decode outside draw(). The 128px local thumbnail is scaled on demand. */
static void cover(uint32_t *p,int x,int y,int w,int h,const char *cusa) {
    const uint32_t *thumb=ui_icon_cache_find(cusa);
    if(!thumb) { placeholder(p,x,y,w,h); return; }
    for(int dy=0;dy<h;++dy) {
        unsigned sy=(unsigned)((uint64_t)dy*UI_ICON_SIDE/(unsigned)h);
        for(int dx=0;dx<w;++dx) {
            unsigned sx=(unsigned)((uint64_t)dx*UI_ICON_SIDE/(unsigned)w);
            p[(size_t)(y+dy)*WIDTH+x+dx]=thumb[sy*UI_ICON_SIDE+sx];
        }
    }
}
static void header(uint32_t *p, UiPage page) {
    rect(p,0,0,WIDTH,115,0xFF0A1E3Eu);
    rect(p,0,112,WIDTH,3,ACCENT);
    panel(p,68,27,62,62,ACCENT);
    ascii(p,84,49,"PS4",WHITE,2,3);
    arabic(p,154,29,UI_AR_APP_NAME,WHITE);
    arabic(p,156,73,UI_AR_LOCAL_READ_ONLY,MUTED);

    const int positions[4]={920,1100,1335,1580};
    const int widths[4]={165,215,230,240};
    const UiArabicLabel labels[4]={
        UI_AR_HOME,UI_AR_DETAILS,UI_AR_DOWNLOADS,UI_AR_SETTINGS
    };
    const UiPage sections[4]={
        UI_PAGE_HOME,UI_PAGE_DETAILS,UI_PAGE_DOWNLOADS,UI_PAGE_SETTINGS
    };
    for(int i=0;i<4;++i) {
        const int highlighted=page==sections[i];
        panel(p,positions[i],31,widths[i],58,
              highlighted?ACCENT:0xFF102D50u);
        arabic(p,positions[i]+18,40,labels[i],highlighted?WHITE:MUTED);
    }
}
/* A blurred 96x54 background is computed ONLY when the selected local
 * icon changes. It uses the cached 128x128 thumbnail and 3x3 box sampling,
 * never the raw full-resolution PNG or a GPU shader. */
#define BLUR_WIDTH 96
#define BLUR_HEIGHT 54
static uint32_t blur_image[BLUR_WIDTH*BLUR_HEIGHT];
static char blurred_for[10];
static int blur_available=0;
static void prepare_blurred_background(const ScannerUiEntry *entries,
                                       unsigned count) {
    unsigned idx=ui_nav_resolve(&navigation,navigation.selected);
    if(!entries || idx>=count || !entries[idx].cusa) {
        blurred_for[0]=0;blur_available=0;return;
    }
    const char *id=entries[idx].cusa;
    if(blurred_for[0] && strcmp(blurred_for,id)==0)return;
    const uint32_t *src=ui_icon_cache_find(id);
    if(!src) {
        blurred_for[0]=0;
        blur_available=0;
        return;
    }
    for(int y=0;y<BLUR_HEIGHT;++y) {
        for(int x=0;x<BLUR_WIDTH;++x) {
            int sx=x*(int)UI_ICON_SIDE/BLUR_WIDTH;
            int sy=y*(int)UI_ICON_SIDE/BLUR_HEIGHT;
            unsigned r=0,g=0,b=0,samples=0;
            for(int oy=-4;oy<=4;oy+=2) {
                int py=sy+oy;
                if(py<0)py=0;
                if(py>=(int)UI_ICON_SIDE)py=(int)UI_ICON_SIDE-1;
                for(int ox=-4;ox<=4;ox+=2) {
                    int px=sx+ox;
                    if(px<0)px=0;
                    if(px>=(int)UI_ICON_SIDE)px=(int)UI_ICON_SIDE-1;
                    uint32_t pixel=src[py*UI_ICON_SIDE+px];
                    r+=(pixel>>16)&255u;
                    g+=(pixel>>8)&255u;
                    b+=pixel&255u;
                    ++samples;
                }
            }
            /* Precompute darkened channel values once per selected icon.
             * Avoid three color divisions per framebuffer pixel and
             * per repaint, keeping 1080p CPU rendering responsive. */
            const unsigned out_r=((r/samples)+6u*3u)/4u;
            const unsigned out_g=((g/samples)+17u*3u)/4u;
            const unsigned out_b=((b/samples)+38u*3u)/4u;
            blur_image[y*BLUR_WIDTH+x]=0xFF000000u|
                (out_r<<16)|(out_g<<8)|out_b;
        }
    }
    memcpy(blurred_for,id,10);
    blur_available=1;
}
static void background(uint32_t *p) {
    if(blur_available) {
        /* Avoid clearing two million pixels that the opaque artwork
         * immediately replaces. Header/footer still get a clean base. */
        rect(p,0,0,WIDTH,116,BG);
        rect(p,0,1000,WIDTH,HEIGHT-1000,BG);
        /* The blurred pixels already include the dark text-safe tint. */
        static_assert(WIDTH % BLUR_WIDTH == 0 &&
                      HEIGHT % BLUR_HEIGHT == 0,
                      "Backdrop must fit integer pixel blocks");
        const int block_w=WIDTH/BLUR_WIDTH;
        const int block_h=HEIGHT/BLUR_HEIGHT;
        for(int y=116;y<1000;++y) {
            uint32_t *line=p+(size_t)y*WIDTH;
            const uint32_t *src=blur_image+(y/block_h)*BLUR_WIDTH;
            for(int tile=0;tile<BLUR_WIDTH;++tile) {
                const uint32_t pixel=src[tile];
                uint32_t *dst=line+tile*block_w;
                for(int dx=0;dx<block_w;++dx) dst[dx]=pixel;
            }
        }
    } else {
        rect(p,0,0,WIDTH,HEIGHT,BG);
        for(int y=128;y<1030;y+=48)
            rect(p,0,y,WIDTH,16,(y/48)%2 ? 0xFF07172Fu:0xFF081A35u);
    }
}
/* Thin PS4 controller navigation rail. Draw once per state change with
 * small geometric icons rather than decoding SVGs or GPU textures. The
 * wide content panels still have their full original 1080p layout. */
static void rail_item(uint32_t *p,int y,UiPage section,UiPage current) {
    const int active=section==current;
    const uint32_t ink=active?WHITE:MUTED;
    if(active) {
        panel(p,5,y,58,69,0xFF174F92u);
        rect(p,4,y+14,3,43,CYAN);
    }
    const int x=20, cy=y+17;
    if(section==UI_PAGE_HOME) {
        rect(p,x+12,cy,8,5,ink);
        rect(p,x+8,cy+5,16,5,ink);
        rect(p,x+4,cy+10,24,5,ink);
        rect(p,x+8,cy+15,16,16,ink);
        rect(p,x+14,cy+21,4,10,0xFF081D39u);
    } else if(section==UI_PAGE_DETAILS) {
        rect(p,x+1,cy+7,30,23,ink);
        rect(p,x+4,cy+10,24,17,0xFF081D39u);
        rect(p,x+8,cy+13,4,11,ink);
        rect(p,x+5,cy+16,10,4,ink);
        rect(p,x+21,cy+15,4,4,ink);
        rect(p,x+25,cy+21,4,4,ink);
    } else if(section==UI_PAGE_DOWNLOADS) {
        rect(p,x+14,cy+1,4,19,ink);
        rect(p,x+9,cy+17,14,5,ink);
        rect(p,x+12,cy+22,8,5,ink);
        rect(p,x+4,cy+29,24,3,ink);
    } else if(section==UI_PAGE_SEARCH) {
        rect(p,x+4,cy+4,21,21,ink);
        rect(p,x+8,cy+8,13,13,0xFF081D39u);
        rect(p,x+23,cy+23,5,5,ink);
        rect(p,x+27,cy+27,5,5,ink);
    } else if(section==UI_PAGE_SETTINGS) {
        rect(p,x+4,cy+6,24,24,ink);
        rect(p,x+8,cy+10,16,16,0xFF081D39u);
        rect(p,x+12,cy+14,8,8,ink);
        rect(p,x+13,cy,6,5,ink);
        rect(p,x+13,cy+30,6,5,ink);
        rect(p,x,cy+15,5,6,ink);
        rect(p,x+27,cy+15,5,6,ink);
    }
}
static void rail(uint32_t *p,UiPage page) {
    rect(p,0,116,69,884,0xFF081D39u);
    rect(p,66,116,2,884,0xFF1A5189u);
    rail_item(p,199,UI_PAGE_HOME,page);
    rail_item(p,297,UI_PAGE_DETAILS,page);
    rail_item(p,395,UI_PAGE_DOWNLOADS,page);
    rail_item(p,493,UI_PAGE_SEARCH,page);
    rail_item(p,591,UI_PAGE_SETTINGS,page);
}
static void footer(uint32_t *p, UiArabicLabel hint) {
    rect(p,0,1000,WIDTH,80,0xFF081A35u);
    rect(p,0,1000,WIDTH,2,0xFF1F5C9Eu);
    arabic(p,85,1015,hint,MUTED);
}
static void summary(uint32_t *p, int x, int w, UiArabicLabel name,
                    const char *value, uint32_t color) {
    panel(p,x,145,w,110,PANEL);
    rect(p,x+16,166,4,66,color);
    arabic(p,x+36,151,name,MUTED);
    ascii(p,x+36,208,value,WHITE,3,28);
}
static void game_item(uint32_t *p, const ScannerUiEntry *g,
                      int y,int is_selected) {
    panel(p,76,y,1090,88,is_selected?0xFF164881u:PANEL);
    if(is_selected) rect(p,76,y+10,5,68,CYAN);
    cover(p,92,y+11,68,66,g?g->cusa:nullptr);
    game_title(p,187,y+5,630,g,WHITE,2);
    ascii(p,187,y+53,g->cusa?g->cusa:"NO CUSA",MUTED,2,12);
    ascii(p,877,y+16,fallback_version(g),CYAN,2,16);
    arabic(p,877,y+37,category_label(g),
           g->metadata_verified?MUTED:AMBER);
}
static void dashboard(uint32_t *p, const ScannerUiEntry *entries,
                      unsigned count,unsigned good,unsigned errors,
                      int complete,int saved) {
    char number[64];
    snprintf(number,sizeof(number),"%u",good);
    summary(p,76,335,UI_AR_BASE_GAMES,number,CYAN);
    snprintf(number,sizeof(number),"%u",count);
    summary(p,426,335,UI_AR_CUSA_FOLDERS,number,ACCENT);
    snprintf(number,sizeof(number),"%u",errors);
    summary(p,776,390,UI_AR_SCAN_ERRORS,number,errors?AMBER:0xFF6CCFACu);

    arabic(p,86,265,UI_AR_LIBRARY,WHITE);
    arabic(p,854,268,complete?UI_AR_SCAN_COMPLETE:UI_AR_SCAN_INCOMPLETE,
          complete?0xFF6CCFACu:AMBER);
    unsigned visible=navigation.available>navigation.scroll?
                     navigation.available-navigation.scroll:0;
    if(visible>ROWS)visible=ROWS;
    for(unsigned i=0;i<visible;++i) {
        unsigned resolved=ui_nav_resolve(&navigation,navigation.scroll+i);
        if(resolved>=count)continue;
        game_item(p,&entries[resolved],332+(int)i*99,
                  navigation.selected==navigation.scroll+i);
    }
    if(!navigation.available)
        arabic(p,124,397,count?UI_AR_NO_MATCHING_GAME:UI_AR_NO_GAME_METADATA,AMBER);
    if(!saved)
        arabic(p,112,925,UI_AR_MISSING_INVENTORY,AMBER);

    panel(p,1193,145,658,814,0xFF0B2142u);
    arabic(p,1230,158,UI_AR_SELECTED_GAME,CYAN);
    if(navigation.available && entries) {
        unsigned idx=ui_nav_resolve(&navigation,navigation.selected);
        if(idx<count) {
            const ScannerUiEntry *g=&entries[idx];
            cover(p,1232,240,208,231,g->cusa);
            game_title(p,1472,252,340,g,WHITE,2);
            ascii(p,1472,309,g->cusa?g->cusa:"NO CUSA",CYAN,2,15);
            arabic(p,1472,337,UI_AR_VERSION,MUTED);
            ascii(p,1472,377,fallback_version(g),WHITE,3,14);
            arabic(p,1235,490,UI_AR_CATEGORY,MUTED);
            arabic(p,1235,535,category_label(g),WHITE);
            rect(p,1234,598,568,2,0xFF244769u);
            arabic(p,1235,613,UI_AR_NO_TRUSTED_UPDATE,AMBER);
            arabic(p,1235,653,UI_AR_DOWNLOAD_BLOCKED,MUTED);
            panel(p,1231,764,562,71,0xFF15589Du);
            arabic(p,1270,775,UI_AR_FULL_DETAILS,WHITE);
        }
    } else {
        arabic(p,1240,362,UI_AR_NO_SELECTED_GAME,MUTED);
    }
    arabic(p,1240,875,UI_AR_DATA_NOT_PROOF,MUTED);
    snprintf(number,sizeof(number),"ITEM %u / %u",
             navigation.available?navigation.selected+1:0,
             navigation.available);
    ascii(p,89,960,number,MUTED,2,28);
    footer(p,UI_AR_FOOTER_HOME);
}
static void details(uint32_t *p,const ScannerUiEntry *entries,
                    unsigned count,int complete) {
    unsigned idx=ui_nav_resolve(&navigation,navigation.selected);
    if(!entries || idx>=count) {
        arabic(p,120,337,UI_AR_NO_SELECTED_GAME,AMBER);
        footer(p,UI_AR_FOOTER_DETAILS);
        return;
    }
    const ScannerUiEntry *g=&entries[idx];
    arabic(p,93,137,UI_AR_DETAILS,WHITE);
    panel(p,90,225,1730,705,PANEL);
    cover(p,1465,271,285,317,g->cusa);
    game_title(p,135,251,1275,g,WHITE,4);
    ascii(p,140,348,g->cusa?g->cusa:"UNKNOWN",CYAN,3,18);
    arabic(p,143,405,UI_AR_VERSION,MUTED);
    ascii(p,143,452,fallback_version(g),WHITE,4,15);
    arabic(p,625,405,UI_AR_PACKAGE,MUTED);
    arabic(p,625,440,category_label(g),WHITE);
    arabic(p,143,525,UI_AR_REGION,MUTED);
    arabic(p,400,525,UI_AR_GAME_SIZE,MUTED);
    arabic(p,680,525,UI_AR_FIRMWARE,MUTED);
    arabic(p,143,564,UI_AR_UNAVAILABLE,AMBER);
    rect(p,130,641,1270,2,0xFF22527Bu);
    arabic(p,142,660,UI_AR_AVAILABLE_UPDATES,CYAN);
    /* Strictly view-only metadata. A supplied UI_UPDATE_VERIFIED value
     * does not grant install/download permission: the independent
     * compatibility and authorization checks are not connected yet. */
    const UiUpdateInfo *first=ui_model_update_for(g->cusa,0);
    if(!first) {
        arabic(p,143,714,UI_AR_NO_TRUSTED_UPDATE,AMBER);
        arabic(p,143,766,UI_AR_RETAIL_FPKG_WARNING,MUTED);
    } else {
        for(unsigned i=0;i<3u;++i) {
            const UiUpdateInfo *row=ui_model_update_for(g->cusa,i);
            if(!row)break;
            const int y=700+(int)i*52;
            panel(p,140,y,1260,47,0xFF102D50u);
            ascii(p,167,y+7,row->version,WHITE,2,15);
            if(row->minimum_fw[0])
                ascii(p,430,y+7,row->minimum_fw,CYAN,2,15);
            if(row->size_bytes) {
                char size[40];
                (void)snprintf(size,sizeof size,"%llu MB",
                    (unsigned long long)(row->size_bytes/1048576u));
                ascii(p,680,y+7,size,MUTED,2,18);
            }
            arabic(p,970,y+5,row->verdict==UI_UPDATE_BLOCKED ?
                UI_AR_UPDATE_BLOCKED:UI_AR_UPDATE_REVIEW,
                row->verdict==UI_UPDATE_BLOCKED?AMBER:MUTED);
        }
        arabic(p,143,861,UI_AR_RETAIL_FPKG_WARNING,MUTED);
    }
    panel(p,1464,646,290,96,0xFF283648u);
    arabic(p,1490,667,UI_AR_INSTALL_BLOCKED,MUTED);
    arabic(p,141,861,complete?UI_AR_DATA_NOT_PROOF:
          UI_AR_SCAN_INCOMPLETE,MUTED);
    footer(p,UI_AR_FOOTER_DETAILS);
}
/* Render only status records supplied by a verified higher-level owner.
 * This module never starts networking, writes PKGs or authorizes install.
 * Zero jobs means zero displayed progress, not a simulated queue. */
static UiArabicLabel job_status_label(UiJobStatus status) {
    switch(status) {
        case UI_JOB_QUEUED: return UI_AR_JOB_QUEUED;
        case UI_JOB_DOWNLOADING: return UI_AR_JOB_DOWNLOADING;
        case UI_JOB_PAUSED: return UI_AR_JOB_PAUSED;
        case UI_JOB_COMPLETED: return UI_AR_JOB_COMPLETED;
        case UI_JOB_FAILED: return UI_AR_JOB_FAILED;
        default: return UI_AR_JOB_BLOCKED;
    }
}
static uint32_t job_status_color(UiJobStatus status) {
    switch(status) {
        case UI_JOB_DOWNLOADING: return CYAN;
        case UI_JOB_COMPLETED: return 0xFF6CCFACu;
        case UI_JOB_BLOCKED:
        case UI_JOB_FAILED: return AMBER;
        default: return MUTED;
    }
}
static void downloads(uint32_t *p) {
    arabic(p,90,138,UI_AR_DOWNLOADS,WHITE);
    const unsigned total=ui_model_download_count();
    unsigned running=0,queued=0,finished=0;
    for(unsigned i=0;i<total;++i) {
        const UiDownloadInfo *r=ui_model_download(i);
        if(!r)continue;
        if(r->status==UI_JOB_DOWNLOADING)++running;
        if(r->status==UI_JOB_QUEUED)++queued;
        if(r->status==UI_JOB_COMPLETED)++finished;
    }
    char n[24];
    (void)snprintf(n,sizeof n,"%u",running);
    summary(p,90,540,UI_AR_ACTIVE_DOWNLOADS,n,CYAN);
    (void)snprintf(n,sizeof n,"%u",queued);
    summary(p,645,540,UI_AR_QUEUED_DOWNLOADS,n,ACCENT);
    (void)snprintf(n,sizeof n,"%u",finished);
    summary(p,1200,540,UI_AR_FINISHED_DOWNLOAD,n,0xFF6CCFACu);
    panel(p,90,288,1655,640,PANEL);
    if(!total) {
        arabic(p,151,330,UI_AR_NO_DOWNLOAD_TASKS,WHITE);
        arabic(p,151,393,UI_AR_NO_NETWORK_DOWNLOADS,MUTED);
        rect(p,150,475,1510,2,0xFF22527Bu);
        arabic(p,151,529,UI_AR_INSTALL_BLOCKED,AMBER);
        arabic(p,151,588,UI_AR_NO_TRUSTED_UPDATE,MUTED);
        footer(p,UI_AR_FOOTER_DOWNLOADS);
        return;
    }
    /* Bounded list: at most five jobs visible; Up/Down scrolls all 16.
     * Percent and downloaded bytes are derived from measured model data. */
    for(unsigned i=0;i<5u && job_scroll+i<total;++i) {
        const unsigned idx=job_scroll+i;
        const UiDownloadInfo *r=ui_model_download(idx);
        if(!r)continue;
        const int y=307+(int)i*120;
        panel(p,118,y,1578,108,idx==job_selected?
              0xFF164881u:0xFF102D50u);
        if(idx==job_selected)rect(p,120,y+12,5,84,CYAN);
        ui_title(p,158,y+7,678,r->title[0]?r->title:r->cusa,WHITE,2);
        ascii(p,158,y+45,r->cusa,MUTED,2,12);
        ascii(p,490,y+45,r->version,CYAN,2,15);
        arabic(p,1230,y+12,job_status_label(r->status),
               job_status_color(r->status));
        rect(p,158,y+82,1015,10,0xFF254465u);
        if(r->bytes_total>0u) {
            /* The size is bounded before multiplication to avoid
             * 64-bit overflow even for malformed provider metadata. */
            const double ratio=(double)r->bytes_done/(double)r->bytes_total;
            unsigned width=(unsigned)(ratio*1015.0);
            if(width>1015u)width=1015u;
            if(width)rect(p,158,y+82,(int)width,10,CYAN);
            const unsigned percent=(unsigned)(ratio*100.0);
            char pcent[16];
            (void)snprintf(pcent,sizeof pcent,"%u%%",percent>100u?100u:percent);
            ascii(p,1190,y+77,pcent,WHITE,2,8);
        } else {
            ascii(p,1190,y+77,"--",MUTED,2,4);
        }
        char bytes[64];
        (void)snprintf(bytes,sizeof bytes,"%llu / %llu MB",
            (unsigned long long)(r->bytes_done/1048576u),
            (unsigned long long)(r->bytes_total/1048576u));
        ascii(p,1380,y+75,bytes,MUTED,2,25);
    }
    (void)snprintf(n,sizeof n,"%u / %u",job_selected+1,total);
    ascii(p,1490,936,n,MUTED,2,20);
    footer(p,UI_AR_FOOTER_JOBS);
}
static void settings_screen(uint32_t *p) {
    arabic(p,100,143,UI_AR_SETTINGS_HEADER,WHITE);
    panel(p,90,222,1655,690,PANEL);
    const UiArabicLabel labels[5]={
        UI_AR_LANGUAGE_SETTING, UI_AR_THEME_SETTING,
        UI_AR_STORAGE_SETTING, UI_AR_SOURCE_SETTING,
        UI_AR_INSTALL_SETTING
    };
    const UiArabicLabel values[5]={
        UI_AR_ARABIC_SETTING, UI_AR_ROYAL_BLUE_SETTING,
        UI_AR_INTERNAL_SETTING, UI_AR_TRUSTED_SETTING,
        UI_AR_INSTALL_OFF_SETTING
    };
    for(int i=0;i<5;++i) {
        const int y=268+i*112;
        panel(p,132,y,1558,91,0xFF123158u);
        rect(p,155,y+16,4,55,i==4?AMBER:CYAN);
        arabic(p,185,y+20,labels[i],MUTED);
        arabic(p,620,y+20,values[i],i==4?AMBER:WHITE);
    }
    arabic(p,159,860,UI_AR_SETTINGS_NOTE,MUTED);
    footer(p,UI_AR_FOOTER_SETTINGS);
}
static void search_screen(uint32_t *p) {
    arabic(p,92,143,UI_AR_SEARCH_HINT,WHITE);
    panel(p,90,230,1655,104,0xFF0E2A51u);
    if(navigation.query[0])
        ui_title(p,130,240,1550,navigation.query,CYAN,3);
    else
        arabic(p,130,243,UI_AR_SEARCH_BY_NAME,MUTED);
    arabic(p,109,353,navigation.keyboard_arabic ?
           UI_AR_KEYBOARD_ARABIC:UI_AR_KEYBOARD_ENGLISH,CYAN);
    char line[64];
    snprintf(line,sizeof(line),"%u",navigation.available);
    arabic(p,1415,354,UI_AR_SEARCH_RESULTS,MUTED);
    ascii(p,1715,370,line,WHITE,3,10);

    const unsigned max=ui_nav_keyboard_count(&navigation);
    for(unsigned i=0;i<max;++i) {
        int col=(int)(i%10), row=(int)(i/10);
        int x=128+col*163, y=410+row*129;
        panel(p,x,y,140,105,
              i==navigation.keyboard_index?ACCENT:0xFF112E51u);
        const char *key=ui_nav_keyboard_key(&navigation,i);
        if(!key)continue;
        if(navigation.keyboard_arabic) {
            ui_title(p,x+18,y+17,104,key,WHITE,3);
        } else {
            if(key[0]==' ') {
                ascii(p,x+28,y+42,"SPACE",MUTED,2,8);
            } else {
                ascii(p,x+54,y+32,key,WHITE,4,1);
            }
        }
    }
    footer(p,UI_AR_FOOTER_SEARCH);
}

static void draw(const ScannerUiEntry *entries,
                 unsigned count, unsigned good, unsigned errors,
                 int complete, int saved, unsigned start, int scanning) {
    (void)start;
    if(!usable) return;
    uint32_t *p=surface[next_buffer];
    if(!scanning && navigation.page!=UI_PAGE_DOWNLOADS &&
       navigation.page!=UI_PAGE_SEARCH)
        prepare_blurred_background(entries,count);
    background(p);
    rail(p,scanning?UI_PAGE_HOME:navigation.page);
    header(p,scanning?UI_PAGE_HOME:navigation.page);
    if(scanning) {
        arabic(p,118,218,UI_AR_READING_METADATA,WHITE);
        panel(p,108,328,1700,344,PANEL);
        ascii(p,150,408,progress_line,CYAN,4,75);
        arabic(p,150,525,UI_AR_DOWNLOAD_BLOCKED,MUTED);
        footer(p,UI_AR_FOOTER_SCANNING);
    }else if(navigation.page==UI_PAGE_SEARCH) {
        search_screen(p);
    }else if(navigation.page==UI_PAGE_SETTINGS) {
        settings_screen(p);
    }else if(navigation.page==UI_PAGE_DETAILS) {
        details(p,entries,count,complete);
    }else if(navigation.page==UI_PAGE_DOWNLOADS) {
        downloads(p);
    }else {
        dashboard(p,entries,count,good,errors,complete,saved);
    }
    /* Continue using the video flip completion protocol verified by
     * previous hardware tests; teardown remains unchanged. */
    const int64_t submitted=++frame_id;
    int32_t submitted_ok=sceVideoOutSubmitFlip(
        video,next_buffer,ORBIS_VIDEO_OUT_FLIP_VSYNC,submitted
    );
    if(submitted_ok==0) {
        for(unsigned attempt=0;attempt<160;++attempt) {
            OrbisVideoOutFlipStatus state={};
            if(sceVideoOutGetFlipStatus(video,&state)!=0 ||
               state.flipArg==submitted) break;
            (void)sceKernelUsleep(2500);
        }
        next_buffer=(next_buffer+1)%2;
    }
}

extern "C" int scanner_ui_offer_icon(
    const char *cusa, const unsigned char *png, unsigned png_bytes
) {
    return ui_icon_cache_offer(cusa,png,png_bytes);
}

extern "C" int scanner_ui_begin(void) {
    ui_icon_cache_reset();
    ui_model_reset();
    job_selected=0;
    job_scroll=0;
    blurred_for[0]=0;
    blur_available=0;
    OrbisUserServiceInitializeParams init = {};
    init.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    (void)sceUserServiceInitialize(&init);
    int32_t user_id = ORBIS_USER_SERVICE_USER_ID_INVALID;
    (void)sceUserServiceGetInitialUser(&user_id);

    video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN,
                            ORBIS_VIDEO_OUT_BUS_MAIN, 0, nullptr);
    if (video < 0) return 0;

    mem_size = ((2 * BUFFER_SIZE + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT;
    if (sceKernelAllocateDirectMemory(
            0, (off_t)sceKernelGetDirectMemorySize(),
            mem_size, ALIGNMENT, 3, &mem_off) != 0) {
        (void)sceVideoOutClose(video);
        video = -1;
        return 0;
    }
    if (sceKernelMapDirectMemory(
            &mapped, mem_size, 0x33, 0, mem_off, ALIGNMENT) != 0) {
        (void)sceKernelReleaseDirectMemory(mem_off, mem_size);
        (void)sceVideoOutClose(video);
        video = -1;
        mem_size = 0;
        return 0;
    }

    surface[0] = static_cast<uint32_t *>(mapped);
    surface[1] = surface[0] + PIXELS;
    void *buffers[2] = {surface[0], surface[1]};
    OrbisVideoOutBufferAttribute attr = {};
    sceVideoOutSetBufferAttribute(&attr, 0x80000000,
                                  1, 0, WIDTH, HEIGHT, WIDTH);
    if (sceVideoOutRegisterBuffers(video, 0, buffers, 2, &attr) != 0) {
        (void)sceKernelMunmap(mapped, mem_size);
        (void)sceKernelReleaseDirectMemory(mem_off, mem_size);
        (void)sceVideoOutClose(video);
        video = -1;
        mapped = nullptr;
        mem_size = 0;
        return 0;
    }
    buffers_registered = 1;
    (void)sceVideoOutSetFlipRate(video, 0);
    if (scePadInit() == 0 && user_id >= 0)
        pad = scePadOpen(user_id, 0, 0, nullptr);
    usable = 1;
    draw(nullptr, 0, 0, 0, 0, 0, 0, 1);
    return 1;
}

extern "C" void scanner_ui_progress(
    const char *cusa, unsigned current, unsigned total
) {
    if (!usable) return;
    if (total > 0 && cusa)
        snprintf(progress_line, sizeof(progress_line),
                 "READING %u OF %u - %s", current, total, cusa);
    else
        snprintf(progress_line, sizeof(progress_line),
                 "%s", cusa ? cusa : "WORKING");
    draw(nullptr, 0, 0, 0, 0, 0, 0, 1);
}

extern "C" void scanner_ui_results(
    const ScannerUiEntry *entries, unsigned count,
    unsigned metadata_count, unsigned errors,
    int scan_complete, int inventory_saved
) {
    if (!usable) return;
    if (!entries) count=0;
    if (count>UI_MAX_TITLES) count=UI_MAX_TITLES;
    for(unsigned i=0;i<count;++i) {
        navigation_records[i].cusa=entries[i].cusa;
        navigation_records[i].name=entries[i].title;
    }
    ui_nav_init(&navigation,navigation_records,count);
    draw(entries,count,metadata_count,errors,
         scan_complete,inventory_saved,0,0);
    uint32_t prior=0;
    unsigned no_pad_wait=0;
    unsigned pad_read_failures=0;
    for(;;) {
        (void)sceKernelUsleep(100000);
        if(pad<0) {
            /* No controller fallback: do not trap the app indefinitely. */
            if(++no_pad_wait>=300)break;
            continue;
        }
        OrbisPadData state={};
        if(scePadReadState(pad,&state)<0) {
            if(++pad_read_failures>=100)break;
            continue;
        }
        pad_read_failures=0;
        uint32_t pressed=state.buttons & ~prior;
        prior=state.buttons;
        UiAction action;
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE) action=UI_ACT_BACK;
        else if(pressed&ORBIS_PAD_BUTTON_CROSS) action=UI_ACT_OPEN;
        else if(pressed&ORBIS_PAD_BUTTON_TRIANGLE) action=
            navigation.page==UI_PAGE_SEARCH?UI_ACT_TOGGLE_KEYBOARD:UI_ACT_SEARCH;
        else if(pressed&ORBIS_PAD_BUTTON_SQUARE) action=
            navigation.page==UI_PAGE_SEARCH?UI_ACT_REMOVE:UI_ACT_DOWNLOADS;
        else if(pressed&ORBIS_PAD_BUTTON_OPTIONS) action=UI_ACT_SETTINGS;
        else if(pressed&ORBIS_PAD_BUTTON_R1) action=UI_ACT_NEXT_TAB;
        else if(pressed&ORBIS_PAD_BUTTON_L1) action=UI_ACT_PREVIOUS_TAB;
        else if(pressed&ORBIS_PAD_BUTTON_DOWN) action=UI_ACT_DOWN;
        else if(pressed&ORBIS_PAD_BUTTON_UP) action=UI_ACT_UP;
        else if(pressed&ORBIS_PAD_BUTTON_LEFT) action=UI_ACT_LEFT;
        else if(pressed&ORBIS_PAD_BUTTON_RIGHT) action=UI_ACT_RIGHT;
        else continue;
        if(navigation.page==UI_PAGE_DOWNLOADS &&
           (action==UI_ACT_UP || action==UI_ACT_DOWN)) {
            const unsigned n=ui_model_download_count();
            const unsigned prev=job_selected;
            if(action==UI_ACT_UP && job_selected>0)--job_selected;
            if(action==UI_ACT_DOWN && job_selected+1<n)++job_selected;
            if(prev==job_selected)continue;
            if(job_selected<job_scroll)job_scroll=job_selected;
            if(job_selected>=job_scroll+5u)job_scroll=job_selected-4u;
            draw(entries,count,metadata_count,errors,
                 scan_complete,inventory_saved,navigation.scroll,0);
            continue;
        }
        if(!ui_nav_action(&navigation,action))continue;
        if(navigation.exit_requested)break;
        draw(entries,count,metadata_count,errors,
             scan_complete,inventory_saved,navigation.scroll,0);
    }
}

/*
 * Real PS4 exit path. The order is important:
 * 1. Stop input, 2. wait for pending flips, 3. unregister VideoOut
 * buffers, 4. close VideoOut, 5. unmap/release direct memory.
 * Never free memory still potentially referenced by the display.
 * trace() is optional and is intended to write flushed checkpoints
 * to the scanner's existing diagnostic report.
 */
extern "C" void scanner_ui_shutdown(
    void (*trace)(const char *, int)
) {
    if (!usable && video < 0 && !mapped && !mem_size) return;
    if (trace) trace("UI_SHUTDOWN_ENTER", 1);

    if (pad >= 0) {
        if (trace) trace("UI_PAD_CLOSE_BEGIN", 1);
        const int result = scePadClose(pad);
        if (trace) trace("UI_PAD_CLOSE_RC", result);
        pad = -1;
    }

    int safe_to_release = 1;
    if (video >= 0 && buffers_registered) {
        /* The flip completion check prevents unsafe teardown during
         * a pending asynchronous display operation. */
        int flips_drained = 0;
        if (trace) trace("UI_FLIP_DRAIN_BEGIN", 1);
        for (unsigned i = 0; i < 100; ++i) {
            OrbisVideoOutFlipStatus status = {};
            const int rc = sceVideoOutGetFlipStatus(video, &status);
            if (rc != 0) {
                if (trace) trace("UI_FLIP_STATUS_FAILED", rc);
                break;
            }
            if (status.numFlipPending == 0 &&
                status.numGpuFlipPending == 0 &&
                status.flipArg >= frame_id) {
                flips_drained = 1;
                break;
            }
            (void)sceKernelUsleep(10000);
        }
        if (trace) trace("UI_FLIP_DRAIN_OK", flips_drained);
        if (!flips_drained)
            safe_to_release = 0;

        if (trace) trace("UI_UNREGISTER_BEGIN", 1);
        const int rc = sceVideoOutUnregisterBuffers(video, 0);
        if (trace) trace("UI_UNREGISTER_RC", rc);
        if (rc == 0)
            buffers_registered = 0;
        else
            safe_to_release = 0;
    }

    if (video >= 0) {
        if (trace) trace("UI_VIDEO_CLOSE_BEGIN", 1);
        const int rc = sceVideoOutClose(video);
        if (trace) trace("UI_VIDEO_CLOSE_RC", rc);
        if (rc != 0)
            safe_to_release = 0;
        video = -1;
    }

    if (safe_to_release && mapped && mem_size) {
        if (trace) trace("UI_MUNMAP_BEGIN", 1);
        const int rc = sceKernelMunmap(mapped, mem_size);
        if (trace) trace("UI_MUNMAP_RC", rc);
        if (rc != 0)
            safe_to_release = 0;
        else
            mapped = nullptr;
    }
    if (safe_to_release && mem_size) {
        if (trace) trace("UI_RELEASE_MEM_BEGIN", 1);
        const int rc = sceKernelReleaseDirectMemory(mem_off, mem_size);
        if (trace) trace("UI_RELEASE_MEM_RC", rc);
        if (rc == 0)
            mem_size = 0;
    }
    if (!safe_to_release && trace)
        trace("UI_MEMORY_RETAINED_FOR_SAFETY", 1);

    usable = 0;
    next_buffer = 0;
    frame_id = 0;
    if (trace) trace("UI_SHUTDOWN_DONE", 1);
}
