/*
 * Minimal PS4 native read-only scanner UI.
 * CPU framebuffer via OpenOrbis VideoOut; DualShock input via ScePad.
 * No installation, download, or game-file writes.
 */
#include "scanner_ui.h"
#include "ui_navigation.h"
#include "ui_title_layout.h"
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
static char progress_line[128] = "...";
static UiNavigation navigation;
/* Borrowed static metadata from the scanner, never treated as installed. */
static ScannerUiEntry previous_scan[UI_MAX_TITLES];
static unsigned previous_scan_count;
/* True only when the current scan failed and stale data is still shown. */
static int final_view_is_stale;
static unsigned job_selected=0;
static unsigned job_scroll=0;
static UiRecord navigation_records[UI_MAX_TITLES];
/* A read-only async cover worker may be polled, but never invoked in draw(). */
static int (*cover_tick)(const char *cusa)=nullptr;

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
        /* max_chars limits text length, not the width of one glyph.
         * Wide single Latin glyphs (e.g. 'm') must never disappear.
         * Single-glyph callers reserve visual space, and ui_title_paint()
         * separately clips to the title column before calling text(). */
        const int cell_width=max_chars<=4 ? 12*scale : 6*scale;
        const int cap=max_chars==1 ? WIDTH :
            (x+max_chars*cell_width<WIDTH ?
             x+max_chars*cell_width:WIDTH);
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
/* UTF-8 byte validation and build-time Arabic glyph atlas.
 * Directional run ordering is delegated to ui_title_layout. */
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
                                uint16_t form,uint32_t color,
                                int clip_left,int clip_right) {
    if(form>=UI_AR_DYNAMIC_COUNT)return;
    const UiArabicDynamicGlyph g=UI_AR_DYNAMIC_GLYPHS[form];
    if(x<0||y<0||x+(int)g.width>WIDTH||y+(int)g.height>HEIGHT)return;
    for(unsigned row=0;row<g.height;++row)for(unsigned col=0;col<g.width;++col) {
        /* The source atlas has transparent side margins. Compact natural
         * advances let joined letter strokes meet; clip any margin pixels
         * outside the allocated text column before writing framebuffer. */
        if(x+(int)col<clip_left || x+(int)col>=clip_right)continue;
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
/* Dynamic title renderer: Arabic presentation-form joining plus bounded
 * Latin runs. ui_title_layout() chooses LTR or RTL by first strong letter.
 * Explicitly limited: no full UAX #9 bidi, ligatures, Indic/CJK shaping,
 * Arabic harakat rendering, or general Unicode line breaking. */
static int ui_title_mark(uint32_t cp) {
    return (cp>=0x064Bu && cp<=0x065Fu) || cp==0x0670u ||
           (cp>=0x06D6u && cp<=0x06EDu);
}
static uint16_t ui_title_shape(const uint32_t *cp,size_t n,size_t i) {
    const UiArabicJoining *curr=ui_joining(cp[i]);
    if(!curr)return 65535u;
    size_t before=i,after=i+1;
    while(before>0 && ui_title_mark(cp[before-1]))--before;
    while(after<n && ui_title_mark(cp[after]))++after;
    const UiArabicJoining *prev=before?ui_joining(cp[before-1]):nullptr;
    const UiArabicJoining *next=after<n?ui_joining(cp[after]):nullptr;
    const int connected_prev=prev && curr->final!=65535u &&
                             prev->initial!=65535u;
    const int connected_next=next && curr->initial!=65535u &&
                             next->final!=65535u;
    uint16_t shape=curr->isolated;
    if(connected_prev && connected_next && curr->medial!=65535u)
        shape=curr->medial;
    else if(connected_prev && curr->final!=65535u)
        shape=curr->final;
    else if(connected_next && curr->initial!=65535u)
        shape=curr->initial;
    return shape;
}
static char ui_title_ascii(uint32_t cp) {
    return cp>=32u && cp<=126u ? (char)cp : '?';
}
static int ui_title_advance(const uint32_t *cp,size_t n,size_t i,
                            int scale) {
    if(ui_title_mark(cp[i]))return 0;
    const uint16_t shape=ui_title_shape(cp,n,i);
    if(shape<UI_AR_DYNAMIC_COUNT)
        return (int)UI_AR_DYNAMIC_GLYPHS[shape].advance;
    const char c[2]={ui_title_ascii(cp[i]),0};
    return latin_width(c,scale,1);
}
/* Measure actual build-time advances, never estimate from UTF-8 byte count.
 * The max 191 codepoint title remains bounded and allocation-free. */
static int ui_title_measure(const uint32_t *cp,size_t n,int scale) {
    int width=0;
    for(size_t i=0;i<n;++i) {
        const int step=ui_title_advance(cp,n,i,scale);
        if(step<0 || step>WIDTH-width)return WIDTH+1;
        width+=step;
    }
    return width;
}
/* Caller ensures its measured run fits. We still clip the complete glyph
 * to the allocated title column to avoid painting neighbouring panels. */
static void ui_title_paint(uint32_t *p,int pen,int y,int left,int right,
                           uint32_t cp,const uint32_t *codepoints,size_t n,
                           size_t index,uint32_t color,int scale) {
    if(ui_title_mark(cp))return;
    const uint16_t shape=ui_title_shape(codepoints,n,index);
    if(shape<UI_AR_DYNAMIC_COUNT) {
        const int w=(int)UI_AR_DYNAMIC_GLYPHS[shape].width;
        const int paint_x=pen-3;
        /* Only transparent glyph margins may cross the column edge.
         * Rasterization itself is clipped to [left, right). */
        if(paint_x>=left-3 && paint_x+w<=right+3)
            ui_draw_arabic_form(p,paint_x,y-4,shape,color,left,right);
    } else {
        const char single[2]={ui_title_ascii(cp),0};
        const unsigned c=(unsigned char)single[0];
        const int w=(int)UI_LATIN_GLYPHS[scale-2][c-32u].width;
        if(pen>=left && pen+w<=right)
            ascii(p,pen,y+18,single,color,scale,1);
    }
}
/* Truncate by glyph advance, not UTF-8 bytes and not whole-direction
 * runs. Previously a single long LTR or Arabic RTL run was silently
 * discarded when its full width exceeded the title column. The logical
 * prefix stays visible; a visual trailing "..." indicates omitted data.
 * This is a bounded UI convention, not full Unicode bidi/line breaking. */
static void ui_title(uint32_t *p,int x,int y,int width,
                      const char *string,uint32_t color,int ascii_scale) {
    if(!p || !string || width<=0 || ascii_scale<2 || ascii_scale>4 ||
       x<0 || x>=WIDTH)return;
    if(width>WIDTH-x)width=WIDTH-x;
    uint32_t cp[192]={};
    const size_t n=utf8_title(string,cp);
    if(!n)return;
    UiTitleLayout full={};
    if(ui_title_layout(cp,n,&full)!=0)return;

    const int dots_width=latin_width("...",ascii_scale,3);
    const int too_wide=ui_title_measure(cp,n,ascii_scale)>width;
    size_t visible=n;
    if(too_wide) {
        const int budget=width-dots_width;
        if(budget<0)return; /* Caller supplied too narrow a column. */
        int consumed=0;
        visible=0;
        for(size_t i=0;i<n;++i) {
            const int step=ui_title_advance(cp,n,i,ascii_scale);
            if(step<0 || step>budget-consumed)break;
            consumed+=step;
            ++visible;
        }
        /* Contextual Arabic presentation forms can change advance when
         * a truncated letter becomes the final connected glyph. */
        while(visible &&
              ui_title_measure(cp,visible,ascii_scale)>budget)
            --visible;
    }

    UiTitleLayout layout={};
    if(ui_title_layout(cp,visible,&layout)!=0)return;
    /* Keep original paragraph direction when a very narrow title budget
     * contains only leading neutral punctuation. */
    layout.paragraph=full.paragraph;
    const int right=x+width;
    int cursor=layout.paragraph==UI_TITLE_RTL?right:x;
    for(unsigned r=0;r<layout.count;++r) {
        const UiTitleRun run=layout.runs[r];
        int advance=0;
        for(unsigned j=run.start;j<run.start+run.length;++j) {
            const int cell=ui_title_advance(cp,visible,j,ascii_scale);
            if(cell<0 || advance>width-cell) {
                advance=width+1;
                break;
            }
            advance+=cell;
        }
        if(advance>width)break;
        const int left=layout.paragraph==UI_TITLE_RTL?
                       cursor-advance:cursor;
        if(left<x || left+advance>right)break;
        int pen=run.direction==UI_TITLE_RTL?left+advance:left;
        for(unsigned j=run.start;j<run.start+run.length;++j) {
            const int step=ui_title_advance(cp,visible,j,ascii_scale);
            if(run.direction==UI_TITLE_RTL)pen-=step;
            ui_title_paint(p,pen,y,x,right,cp[j],cp,visible,j,color,
                           ascii_scale);
            if(run.direction==UI_TITLE_LTR)pen+=step;
        }
        cursor=layout.paragraph==UI_TITLE_RTL?left:left+advance;
    }
    if(too_wide && dots_width<=width) {
        const int dots_x=layout.paragraph==UI_TITLE_RTL ?
                         cursor-dots_width:cursor;
        if(dots_x>=x && dots_x+dots_width<=right)
            ascii(p,dots_x,y+18,"...",color,ascii_scale,3);
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
static void header(uint32_t *p) {
    rect(p,0,0,WIDTH,115,0xFF0A1E3Eu);
    rect(p,0,112,WIDTH,3,ACCENT);
    panel(p,68,27,62,62,ACCENT);
    ascii(p,84,49,"PS4",WHITE,2,3);
    /* Royal-blue dual-language masthead. Never claim network access,
     * installed status or verified firmware in preview-only mode. */
    /* Bounds describe available pixel space, not only character count.
      * The proportional Noto title is wider than the old bitmap cells. */
    ascii(p,156,12,"PS4 GAME UPDATE MANAGER",CYAN,2,48);
    arabic(p,154,36,UI_AR_APP_NAME,WHITE);
    arabic(p,156,80,UI_AR_LOCAL_READ_ONLY,MUTED);

    /* The 252px Arabic sidebar is the one navigation surface.
     * Repeating clickable-looking tabs in the header wasted space and
     * conflicted with the approved left-sidebar layout. Do NOT invent
     * online status or claim firmware 14.00 has been hardware-tested. */
    panel(p,1300,29,248,56,0xFF102D50u);
    rect(p,1316,45,5,26,CYAN);
    ascii(p,1334,44,"READ ONLY",WHITE,2,20);
    panel(p,1564,29,294,56,0xFF102D50u);
    rect(p,1578,45,5,26,AMBER);
    ascii(p,1596,44,"FW14 TARGET",MUTED,2,23);
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
        /* Without a locally available game icon, the approved mockup's
         * soft dark Royal Blue is more faithful than the old 48px debug-like
         * horizontal stripes. A smooth scanline gradient costs only the
         * same full-frame writes as the previous opaque fallback. */
        rect(p,0,0,WIDTH,116,BG);
        rect(p,0,1000,WIDTH,HEIGHT-1000,BG);
        for(int y=116;y<1000;++y) {
            const unsigned t=(unsigned)(y-116);
            const unsigned rr=9u-(t*5u)/884u;
            const unsigned gg=30u-(t*17u)/884u;
            const unsigned bb=65u-(t*35u)/884u;
            rect(p,0,y,WIDTH,1,0xFF000000u|
                 (rr<<16)|(gg<<8)|bb);
        }
        rect(p,252,116,1125,2,0xFF1368C1u);
        rect(p,252,118,1125,1,0xFF0D417Bu);
    }
}
/* Wide 252px Arabic navigation rail. Reuses existing geometrical icons
 * and build-time shaped Arabic atlas: no runtime SVG/font or GPU textures.
 * Only pages with real in-app navigation are displayed, not nonfunctional
 * mockup sections for installer/available updates. */
#define RAIL_W 252
#define CONTENT_LEFT 266
static UiArabicLabel rail_label(UiPage page) {
    switch(page) {
        case UI_PAGE_HOME: return UI_AR_HOME;
        case UI_PAGE_GAMES: return UI_AR_GAMES_MENU;
        case UI_PAGE_AVAILABLE: return UI_AR_AVAILABLE_MENU;
        case UI_PAGE_DOWNLOADS: return UI_AR_DOWNLOADS;
        case UI_PAGE_QUEUE: return UI_AR_QUEUE_MENU;
        case UI_PAGE_INSTALLED: return UI_AR_INSTALLED_MENU;
        case UI_PAGE_ABOUT: return UI_AR_ABOUT_MENU;
        case UI_PAGE_SETTINGS: return UI_AR_SETTINGS;
        default: return UI_AR_GAMES_MENU;
    }
}
static void rail_item(uint32_t *p,int y,UiPage section,UiPage current) {
    const int active=section==current;
    const uint32_t ink=active?WHITE:MUTED;
    if(active) {
        panel(p,9,y,233,74,0xFF15589Du);
        rect(p,9,y+12,4,50,CYAN);
        /* A bounded three-stroke electric-blue focus rim is closer to the
         * approved mockup than a flat fill. At most ~650 extra pixels;
         * no shadow bitmap, animation timer or GPU dependency. */
        rect(p,18,y+3,215,2,CYAN);
        rect(p,18,y+5,215,2,0xFF175CA5u);
        rect(p,18,y+68,215,2,0xFF2694EDu);
    }
    const int x=28, cy=y+19;
    if(section==UI_PAGE_HOME) {
        rect(p,x+12,cy,8,5,ink);
        rect(p,x+8,cy+5,16,5,ink);
        rect(p,x+4,cy+10,24,5,ink);
        rect(p,x+8,cy+15,16,16,ink);
        rect(p,x+14,cy+21,4,10,0xFF081D39u);
    } else if(section==UI_PAGE_GAMES) {
        rect(p,x+1,cy+7,30,23,ink);
        rect(p,x+4,cy+10,24,17,0xFF081D39u);
        rect(p,x+8,cy+13,4,11,ink);
        rect(p,x+5,cy+16,10,4,ink);
        rect(p,x+21,cy+15,4,4,ink);
        rect(p,x+25,cy+21,4,4,ink);
    } else if(section==UI_PAGE_AVAILABLE ||
              section==UI_PAGE_DOWNLOADS) {
        rect(p,x+14,cy+1,4,19,ink);
        rect(p,x+9,cy+17,14,5,ink);
        rect(p,x+12,cy+22,8,5,ink);
        rect(p,x+4,cy+29,24,3,ink);
        if(section==UI_PAGE_AVAILABLE)
            rect(p,x+24,cy+3,6,6,CYAN);
    } else if(section==UI_PAGE_QUEUE) {
        for(int i=0;i<3;++i) {
            rect(p,x+3,cy+6+i*10,4,4,ink);
            rect(p,x+12,cy+6+i*10,19,3,ink);
        }
    } else if(section==UI_PAGE_INSTALLED) {
        rect(p,x+4,cy+9,25,20,ink);
        rect(p,x+8,cy+13,17,12,0xFF081D39u);
        rect(p,x+8,cy+2,17,5,ink);
        rect(p,x+14,cy+17,6,4,CYAN);
    } else if(section==UI_PAGE_SETTINGS) {
        rect(p,x+4,cy+6,24,24,ink);
        rect(p,x+8,cy+10,16,16,0xFF081D39u);
        rect(p,x+12,cy+14,8,8,ink);
        rect(p,x+13,cy,6,5,ink);
        rect(p,x+13,cy+30,6,5,ink);
        rect(p,x,cy+15,5,6,ink);
        rect(p,x+27,cy+15,5,6,ink);
    } else if(section==UI_PAGE_ABOUT) {
        rect(p,x+4,cy+4,24,28,ink);
        rect(p,x+8,cy+8,16,20,0xFF081D39u);
        rect(p,x+14,cy+11,4,4,ink);
        rect(p,x+14,cy+18,4,8,ink);
    }
    arabic(p,78,y+20,rail_label(section),ink);
}
static void rail(uint32_t *p,UiPage page) {
    rect(p,0,116,RAIL_W,884,0xFF081D39u);
    rect(p,RAIL_W-3,116,3,884,0xFF1A5189u);
    rect(p,16,145,219,2,0xFF1B518Au);
    const UiPage sections[8]={
        UI_PAGE_HOME,UI_PAGE_GAMES,UI_PAGE_AVAILABLE,UI_PAGE_DOWNLOADS,
        UI_PAGE_QUEUE,UI_PAGE_INSTALLED,UI_PAGE_ABOUT,UI_PAGE_SETTINGS
    };
    /* All tabs visible without scroll; Settings + R1 always wraps Home. */
    for(unsigned i=0;i<8;++i)
        rail_item(p,155+(int)i*92,sections[i],
                  page==UI_PAGE_DETAILS ? navigation.details_origin:page);
    /* Honest preview/read-only footer inside the native sidebar. */
    rect(p,24,906,204,2,0xFF245885u);
    panel(p,25,919,200,53,0xFF102D50u);
    rect(p,34,935,4,23,AMBER);
    /* Split the label into two bounded runs: Noto's proportional glyphs
     * otherwise exceed the legacy 9-character cell and truncate "ONLY". */
    ascii(p,45,936,"READ",MUTED,2,24);
    ascii(p,45+latin_width("READ",2,24)+12,936,"ONLY",MUTED,2,24);
}
static void footer(uint32_t *p, UiArabicLabel hint) {
    rect(p,0,1000,WIDTH,80,0xFF081A35u);
    rect(p,0,1000,WIDTH,2,0xFF1F5C9Eu);
    arabic(p,85,1015,hint,MUTED);
    /* Always describe the real shoulder-button behavior, even in Search
     * and on an empty library. Rendered from a pre-shaped static atlas. */
    /* The fixed Arabic atlas intentionally contains no Latin glyphs.
     * Two Noto Latin button labels and two short Arabic captions stay
     * independent, avoiding mixed-script shaping or clipping. */
    ascii(p,1485,1027,"R1",CYAN,2,2);
    arabic(p,1535,1015,UI_AR_NEXT_MENU,CYAN);
    ascii(p,1690,1027,"L1",CYAN,2,2);
    arabic(p,1740,1015,UI_AR_PREV_MENU,CYAN);
}
static void summary_at(uint32_t *p,int x,int y,int w,UiArabicLabel name,
                       const char *value,uint32_t color) {
    panel(p,x,y,w,110,PANEL);
    rect(p,x+16,y+21,4,66,color);
    arabic(p,x+36,y+6,name,MUTED);
    ascii(p,x+36,y+63,value,WHITE,3,28);
}
static void game_item(uint32_t *p, const ScannerUiEntry *g,
                      int y,int is_selected) {
    /* The approved composition gives the game list most of the width.
     * Keep all six rows and enlarge only their horizontal layout. */
    panel(p,269,y,1109,88,is_selected?0xFF164881u:PANEL);
    if(is_selected) {
        rect(p,269,y+10,5,68,CYAN);
        /* Thin two-tone focus rim; painting a whole-row gradient on CPU
         * would cost considerably more than these three bounded strokes. */
        rect(p,281,y+3,1067,2,CYAN);
        rect(p,281,y+5,1067,2,0xFF1765ABu);
        rect(p,281,y+81,1067,2,0xFF268DD7u);
    }
    cover(p,285,y+11,68,66,g?g->cusa:nullptr);
    game_title(p,374,y+5,615,g,WHITE,2);
    ascii(p,374,y+53,g->cusa?g->cusa:"NO CUSA",MUTED,2,12);
    /* Only metadata-verified and unknown-install markers are shown.
     * Game presence in appmeta is never called proof of installation. */
    ascii(p,1040,y+30,fallback_version(g),CYAN,2,14);
    /* Short row status stays inside the expanded 1109px list without
     * misrepresenting SFO metadata as an installed package. */
    const int base_meta=g && g->category &&
        (!strcmp(g->category,"gd") || !strcmp(g->category,"gda") ||
         !strcmp(g->category,"gdo") || !strcmp(g->category,"gde"));
    const UiArabicLabel row_status =
        base_meta && g->install_state<0 ? UI_AR_INSTALL_UNKNOWN :
        category_label(g);
    arabic(p,1164,y+25,row_status,
           g->metadata_verified?MUTED:AMBER);
}
static void dashboard(uint32_t *p, const ScannerUiEntry *entries,
                      unsigned count,unsigned good,unsigned errors,
                      int complete,int saved) {

    char number[64];
    /* The approved mockup has a welcome banner above four compact cards.
     * Display only real read-only scanner and job data, no invented updates,
     * network connection, installed packages or compatibility verdicts. */
    panel(p,269,125,1109,75,0xFF0C2850u);
    rect(p,279,143,5,39,CYAN);
    panel(p,299,139,60,43,0xFF164881u);
    rect(p,310,154,15,4,WHITE);
    rect(p,315,149,5,14,WHITE);
    rect(p,340,152,5,5,CYAN);
    rect(p,347,159,5,5,CYAN);
    arabic(p,375,129,UI_AR_WELCOME_TITLE,WHITE);
    arabic(p,375,167,UI_AR_WELCOME_NOTE,MUTED);

    const int metric_x[4]={269,548,827,1106};
    const UiArabicLabel metric_labels[4]={
        UI_AR_BASE_GAMES,UI_AR_INSTALL_UNKNOWN,UI_AR_SCAN_ERRORS,
        UI_AR_ACTIVE_DOWNLOADS
    };
    const uint32_t metric_colors[4]={
        CYAN,ACCENT,errors?AMBER:0xFF6CCFACu,CYAN
    };
    unsigned install_unknown=0;
    for(unsigned i=0;i<count && entries;++i)
        if(entries[i].install_state<0)++install_unknown;
    unsigned active_downloads=0;
    for(unsigned i=0;i<ui_model_download_count();++i) {
        const UiDownloadInfo *job=ui_model_download(i);
        if(job && job->status==UI_JOB_DOWNLOADING)++active_downloads;
    }
    const unsigned metric_values[4]={
        good,install_unknown,errors,active_downloads
    };
    for(unsigned i=0;i<4;++i) {
        panel(p,metric_x[i],201,270,95,PANEL);
        rect(p,metric_x[i]+15,213,4,71,metric_colors[i]);
        arabic(p,metric_x[i]+28,205,metric_labels[i],MUTED);
        snprintf(number,sizeof number,"%u",metric_values[i]);
        ascii(p,metric_x[i]+34,257,number,WHITE,3,6);
    }
    arabic(p,282,301,UI_AR_LIBRARY,WHITE);
    arabic(p,1035,301,complete?UI_AR_SCAN_COMPLETE:
           UI_AR_SCAN_INCOMPLETE,complete?0xFF6CCFACu:AMBER);
    panel(p,269,336,1109,39,0xFF102D50u);
    arabic(p,373,340,UI_AR_GAME_COLUMN,MUTED);
    arabic(p,1039,340,UI_AR_VERSION,MUTED);
    arabic(p,1161,340,UI_AR_STATE_COLUMN,MUTED);
    rect(p,286,374,1074,2,0xFF245D98u);
    unsigned visible=navigation.available>navigation.scroll?
                     navigation.available-navigation.scroll:0;
    if(visible>ROWS)visible=ROWS;
    for(unsigned i=0;i<visible;++i) {
        unsigned resolved=ui_nav_resolve(&navigation,navigation.scroll+i);
        if(resolved>=count)continue;
        game_item(p,&entries[resolved],380+(int)i*92,
                  navigation.selected==navigation.scroll+i);
    }
    if(!navigation.available) {
        /* The empty library is NOT an empty update catalog and does not
         * imply that there are no installed games. Show an explicit
         * read-only diagnostic instead of a tiny label on a blank grid.
         * No external assets and no allocations in the renderer. */
        panel(p,285,371,1075,390,0xFF102D50u);
        rect(p,301,388,5,350,AMBER);
        panel(p,331,410,167,163,0xFF123C74u);
        rect(p,350,427,125,4,CYAN);
        rect(p,350,552,125,4,0xFF245F9Bu);
        ascii(p,361,472,"PS4",WHITE,3,3);
        arabic(p,529,411,UI_AR_NO_GAME_METADATA,AMBER);
        /* The scan status is already shown above the table.
         * Offer useful FTP guidance in both complete/partial states. */
        arabic(p,529,469,UI_AR_EMPTY_LIBRARY_HELP,MUTED);
        arabic(p,529,526,UI_AR_DOWNLOAD_BLOCKED,MUTED);
    }
    /* An incomplete scan or a zero-record library does not prove a
     * persistence error. Avoid the false "could not save" message.
     * Only completed scans containing useful metadata may report it. */
    if(!saved && complete && count>0)
        arabic(p,455,955,UI_AR_MISSING_INVENTORY,AMBER);

    /* Main layout matches the approved mockup's wide library and narrow
     * right artwork rail. No remote artwork or invented update status. */
    panel(p,1390,145,460,814,0xFF0B2142u);
    rect(p,1394,162,4,776,0xFF1664B3u);
    arabic(p,1420,157,UI_AR_SELECTED_GAME,CYAN);
    panel(p,1413,205,414,326,0xFF112C50u);
    rect(p,1428,218,382,2,0xFF2C77C0u);
    rect(p,1428,514,382,2,0xFF205A91u);
    if(navigation.available && entries) {
        unsigned idx=ui_nav_resolve(&navigation,navigation.selected);
        if(idx<count) {
            const ScannerUiEntry *g=&entries[idx];
            panel(p,1453,223,334,286,0xFF0C244Au);
            cover(p,1473,236,266,266,g->cusa);
            panel(p,1424,434,120,55,0xFF123C74u);
            ascii(p,1448,441,"PS4",WHITE,3,3);
            game_title(p,1423,542,400,g,WHITE,2);
            ascii(p,1424,601,g->cusa?g->cusa:"NO CUSA",CYAN,2,15);
            rect(p,1423,646,399,2,0xFF275980u);
            arabic(p,1423,654,UI_AR_VERSION,MUTED);
            ascii(p,1689,663,fallback_version(g),WHITE,2,12);
            rect(p,1423,701,399,1,0xFF204568u);
            arabic(p,1423,708,UI_AR_CATEGORY,MUTED);
            /* Category can be long in Arabic. Give it its own line. */
            arabic(p,1432,745,category_label(g),
                   g->metadata_verified?WHITE:AMBER);
            rect(p,1423,786,399,1,0xFF204568u);
            arabic(p,1423,794,UI_AR_NO_TRUSTED_UPDATE,AMBER);
            panel(p,1423,849,397,57,0xFF15589Du);
            rect(p,1437,857,5,40,CYAN);
            arabic(p,1460,856,UI_AR_FULL_DETAILS,WHITE);
        }
    } else {
        arabic(p,1423,362,UI_AR_NO_SELECTED_GAME,MUTED);
    }
    arabic(p,1421,912,UI_AR_DATA_NOT_PROOF,MUTED);
    snprintf(number,sizeof(number),"%u / %u",
             navigation.available?navigation.selected+1:0,
             navigation.available);
    ascii(p,284,959,number,MUTED,2,28);
    footer(p,UI_AR_FOOTER_HOME);
}
static void details(uint32_t *p,const ScannerUiEntry *entries,
                    unsigned count,int complete) {
    const unsigned idx=ui_nav_resolve(&navigation,navigation.selected);
    if(!entries || idx>=count) {
        arabic(p,298,337,UI_AR_NO_SELECTED_GAME,AMBER);
        footer(p,UI_AR_FOOTER_DETAILS);
        return;
    }
    const ScannerUiEntry *g=&entries[idx];
    arabic(p,280,137,UI_AR_DETAILS,WHITE);

    /* Content side: a visually grouped read-only inspection page.
     * SFO metadata is NOT evidence of game installation, package type,
     * update compatibility, firmware requirements, or authorized source. */
    panel(p,266,225,1109,705,0xFF0C244Au);
    rect(p,286,242,4,111,CYAN);
    game_title(p,303,251,1047,g,WHITE,4);
    ascii(p,303,344,g->cusa?g->cusa:"UNKNOWN",CYAN,3,18);

    panel(p,287,393,343,103,0xFF102D50u);
    arabic(p,307,398,UI_AR_VERSION,MUTED);
    ascii(p,307,445,fallback_version(g),WHITE,3,15);

    panel(p,643,393,344,103,0xFF102D50u);
    arabic(p,664,398,UI_AR_CATEGORY,MUTED);
    arabic(p,664,439,category_label(g),
           g->metadata_verified?WHITE:AMBER);

    panel(p,1000,393,350,103,0xFF102D50u);
    arabic(p,1021,398,UI_AR_REGION,MUTED);
    arabic(p,1021,439,UI_AR_UNAVAILABLE,AMBER);

    panel(p,287,510,343,103,0xFF102D50u);
    arabic(p,307,515,UI_AR_GAME_SIZE,MUTED);
    arabic(p,307,554,UI_AR_UNAVAILABLE,AMBER);

    panel(p,643,510,344,103,0xFF102D50u);
    arabic(p,664,515,UI_AR_FIRMWARE,MUTED);
    arabic(p,664,554,UI_AR_UNAVAILABLE,AMBER);

    panel(p,1000,510,350,103,0xFF102D50u);
    arabic(p,1021,515,UI_AR_SOURCE,MUTED);
    arabic(p,1021,554,UI_AR_UNAVAILABLE,AMBER);

    rect(p,288,640,1063,2,0xFF255B8Du);
    arabic(p,303,654,UI_AR_AVAILABLE_UPDATES,CYAN);
    /* Preview-only: a provider's displayed verdict never enables
     * download or install without separate authorization and PKG checks. */
    const UiUpdateInfo *first=ui_model_update_for(g->cusa,0);
    if(!first) {
        panel(p,289,709,1062,122,0xFF102D50u);
        rect(p,290,720,5,95,AMBER);
        arabic(p,309,722,UI_AR_NO_TRUSTED_UPDATE,AMBER);
        arabic(p,309,772,UI_AR_RETAIL_FPKG_WARNING,MUTED);
    } else {
        for(unsigned i=0;i<3u;++i) {
            const UiUpdateInfo *row=ui_model_update_for(g->cusa,i);
            if(!row)break;
            const int y=700+(int)i*52;
            panel(p,289,y,1062,47,0xFF102D50u);
            ascii(p,308,y+7,row->version,WHITE,2,15);
            if(row->minimum_fw[0])
                ascii(p,510,y+7,row->minimum_fw,CYAN,2,15);
            if(row->size_bytes) {
                char size[40];
                (void)snprintf(size,sizeof size,"%llu MB",
                    (unsigned long long)(row->size_bytes/1048576u));
                ascii(p,745,y+7,size,MUTED,2,18);
            }
            arabic(p,1040,y+5,row->verdict==UI_UPDATE_BLOCKED ?
                UI_AR_UPDATE_BLOCKED:UI_AR_UPDATE_REVIEW,
                row->verdict==UI_UPDATE_BLOCKED?AMBER:MUTED);
        }
    }
    arabic(p,304,863,complete?UI_AR_DATA_NOT_PROOF:
          UI_AR_SCAN_INCOMPLETE,MUTED);

    /* Right side mirrors the approved artwork-first detail panel, using
     * only an icon read locally through GoldHEN FTP. Square icon0 assets
     * retain their proportions; missing artwork shows an honest placeholder. */
    panel(p,1390,225,460,705,0xFF0B2142u);
    panel(p,1413,248,414,329,0xFF123158u);
    rect(p,1428,256,382,2,CYAN);
    cover(p,1473,270,290,290,g->cusa);
    game_title(p,1422,595,403,g,WHITE,2);
    ascii(p,1428,654,g->cusa?g->cusa:"NO CUSA",CYAN,2,15);
    rect(p,1423,697,399,2,0xFF275980u);
    panel(p,1421,718,399,79,0xFF283648u);
    rect(p,1432,730,5,55,AMBER);
    arabic(p,1445,737,UI_AR_INSTALL_BLOCKED,MUTED);
    arabic(p,1435,831,UI_AR_NO_TRUSTED_UPDATE,AMBER);
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
    arabic(p,285,138,UI_AR_DOWNLOADS,WHITE);
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
    summary_at(p,270,183,495,UI_AR_ACTIVE_DOWNLOADS,n,CYAN);
    (void)snprintf(n,sizeof n,"%u",queued);
    summary_at(p,787,183,495,UI_AR_QUEUED_DOWNLOADS,n,ACCENT);
    (void)snprintf(n,sizeof n,"%u",finished);
    summary_at(p,1304,183,500,UI_AR_FINISHED_DOWNLOAD,n,0xFF6CCFACu);
    panel(p,268,310,1579,640,PANEL);
    if(!total) {
        /* Empty means genuinely zero active jobs. Render a deliberate
         * read-only empty state instead of synthetic progress/status.
         * No fake source, download speed, resume or completed PKG. */
        panel(p,284,327,1540,209,0xFF102D50u);
        rect(p,302,345,5,172,AMBER);
        panel(p,336,352,147,153,0xFF123C74u);
        /* Compact geometric padlock, no external image or font runtime. */
        rect(p,376,373,67,7,CYAN);
        rect(p,376,380,8,42,CYAN);
        rect(p,435,380,8,42,CYAN);
        rect(p,365,421,89,64,0xFF14315Bu);
        rect(p,365,421,89,4,AMBER);
        rect(p,365,481,89,4,AMBER);
        rect(p,405,442,8,24,AMBER);
        arabic(p,523,350,UI_AR_NO_DOWNLOAD_TASKS,WHITE);
        arabic(p,523,405,UI_AR_NO_NETWORK_DOWNLOADS,MUTED);
        arabic(p,523,468,UI_AR_NO_TRUSTED_UPDATE,AMBER);

        /* Side-by-side capability gates, explicitly unconfigured. */
        panel(p,285,555,749,279,0xFF102D50u);
        rect(p,304,575,5,232,CYAN);
        arabic(p,330,572,UI_AR_SOURCE,MUTED);
        rect(p,327,632,674,2,0xFF22527Bu);
        arabic(p,330,645,UI_AR_TRUSTED_SETTING,WHITE);
        arabic(p,330,722,UI_AR_RETAIL_FPKG_WARNING,MUTED);

        panel(p,1050,555,774,279,0xFF102D50u);
        rect(p,1070,575,5,232,AMBER);
        arabic(p,1095,572,UI_AR_INSTALL_SETTING,MUTED);
        rect(p,1092,632,696,2,0xFF22527Bu);
        arabic(p,1095,645,UI_AR_INSTALL_OFF_SETTING,AMBER);
        arabic(p,1095,722,UI_AR_DATA_NOT_PROOF,MUTED);
        footer(p,UI_AR_FOOTER_DOWNLOADS);
        return;
    }
    /* Bounded list: at most five jobs visible; Up/Down scrolls all 16.
     * Percent and downloaded bytes are derived from measured model data. */
    for(unsigned i=0;i<5u && job_scroll+i<total;++i) {
        const unsigned idx=job_scroll+i;
        const UiDownloadInfo *r=ui_model_download(idx);
        if(!r)continue;
        const int y=327+(int)i*120;
        panel(p,289,y,1522,108,idx==job_selected?
              0xFF164881u:0xFF102D50u);
        if(idx==job_selected)rect(p,290,y+12,5,84,CYAN);
        ui_title(p,324,y+7,638,r->title[0]?r->title:r->cusa,WHITE,2);
        ascii(p,324,y+45,r->cusa,MUTED,2,12);
        ascii(p,600,y+45,r->version,CYAN,2,15);
        arabic(p,1230,y+12,job_status_label(r->status),
               job_status_color(r->status));
        rect(p,324,y+82,936,10,0xFF254465u);
        if(r->bytes_total>0u) {
            /* The size is bounded before multiplication to avoid
             * 64-bit overflow even for malformed provider metadata. */
            const double ratio=(double)r->bytes_done/(double)r->bytes_total;
            unsigned width=(unsigned)(ratio*936.0);
            if(width>936u)width=936u;
            if(width)rect(p,324,y+82,(int)width,10,CYAN);
            const unsigned percent=(unsigned)(ratio*100.0);
            char pcent[16];
            (void)snprintf(pcent,sizeof pcent,"%u%%",percent>100u?100u:percent);
            ascii(p,1288,y+77,pcent,WHITE,2,8);
        } else {
            ascii(p,1288,y+77,"--",MUTED,2,4);
        }
        char bytes[64];
        (void)snprintf(bytes,sizeof bytes,"%llu / %llu MB",
            (unsigned long long)(r->bytes_done/1048576u),
            (unsigned long long)(r->bytes_total/1048576u));
        ascii(p,1450,y+75,bytes,MUTED,2,25);
    }
    (void)snprintf(n,sizeof n,"%u / %u",job_selected+1,total);
    ascii(p,1490,922,n,MUTED,2,20);
    footer(p,UI_AR_FOOTER_JOBS);
}

/* Reference-section shells. Display state only; no install or downloader
 * permission follows from SFO metadata, queue rows or provider hints. */
static void readonly_section(uint32_t *p,UiPage page) {
    UiArabicLabel heading=UI_AR_ABOUT_MENU;
    UiArabicLabel message=UI_AR_ABOUT_NOTE;
    if(page==UI_PAGE_AVAILABLE) {
        heading=UI_AR_AVAILABLE_MENU;
        message=UI_AR_NO_TRUSTED_UPDATE;
    } else if(page==UI_PAGE_QUEUE) {
        heading=UI_AR_QUEUE_MENU;
        message=UI_AR_QUEUE_EMPTY;
    } else if(page==UI_PAGE_INSTALLED) {
        heading=UI_AR_INSTALLED_MENU;
        message=UI_AR_INSTALLED_UNKNOWN;
    }
    arabic(p,281,143,heading,WHITE);
    panel(p,269,222,1580,721,PANEL);
    rect(p,289,242,5,80,CYAN);
    panel(p,311,246,1503,132,0xFF102D50u);
    rect(p,327,261,5,89,AMBER);
    arabic(p,356,264,message,
           page==UI_PAGE_ABOUT?WHITE:AMBER);
    if(page==UI_PAGE_AVAILABLE) {
        arabic(p,356,321,UI_AR_UPDATES_UNVERIFIED,MUTED);
        panel(p,311,407,1503,137,0xFF112C50u);
        arabic(p,341,421,UI_AR_SOURCE,MUTED);
        arabic(p,341,478,UI_AR_TRUSTED_SETTING,WHITE);
        panel(p,311,571,1503,190,0xFF112C50u);
        arabic(p,341,593,UI_AR_RETAIL_FPKG_WARNING,MUTED);
        arabic(p,341,658,UI_AR_DOWNLOAD_BLOCKED,AMBER);
    } else if(page==UI_PAGE_QUEUE) {
        /* All entries are model-backed, not the console system queue. */
        unsigned queued=0;
        int y=429;
        for(unsigned i=0;i<ui_model_download_count();++i) {
            const UiDownloadInfo *job=ui_model_download(i);
            if(!job || job->status!=UI_JOB_QUEUED)continue;
            ++queued;
            if(y>794)continue;
            panel(p,311,y,1503,92,0xFF112C50u);
            ui_title(p,340,y+6,950,
                     job->title[0]?job->title:job->cusa,WHITE,2);
            ascii(p,340,y+47,job->cusa,CYAN,2,12);
            arabic(p,1430,y+20,UI_AR_JOB_QUEUED,MUTED);
            y+=108;
        }
        if(!queued) {
            panel(p,311,427,1503,134,0xFF112C50u);
            arabic(p,341,446,UI_AR_NO_QUEUED_DOWNLOAD,MUTED);
        }
        arabic(p,341,865,UI_AR_NO_NETWORK_DOWNLOADS,AMBER);
    } else if(page==UI_PAGE_INSTALLED) {
        panel(p,311,424,1503,158,0xFF112C50u);
        arabic(p,341,445,UI_AR_DATA_NOT_PROOF,MUTED);
        arabic(p,341,506,UI_AR_INSTALL_OFF_SETTING,AMBER);
    } else {
        panel(p,311,423,1503,258,0xFF112C50u);
        arabic(p,341,449,UI_AR_APP_NAME,WHITE);
        arabic(p,341,508,UI_AR_ABOUT_SAFETY,MUTED);
        arabic(p,341,573,UI_AR_NO_TRUSTED_UPDATE,AMBER);
    }
    footer(p,UI_AR_FOOTER_SETTINGS);
}
static void settings_screen(uint32_t *p) {
    arabic(p,281,143,UI_AR_SETTINGS_HEADER,WHITE);
    panel(p,270,222,1579,690,PANEL);
    /* Static read-only configuration preview. Never present a toggle as
     * writable until settings have a verified, reversible implementation. */
    rect(p,305,247,1060,2,0xFF2C77C0u);
    panel(p,1398,232,286,47,0xFF123C74u);
    ascii(p,1435,239,"READ ONLY",CYAN,2,12);
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
        panel(p,305,y,1510,91,0xFF123158u);
        rect(p,328,y+16,4,55,i==4?AMBER:CYAN);
        arabic(p,355,y+20,labels[i],MUTED);
        arabic(p,810,y+20,values[i],i==4?AMBER:WHITE);
    }
    arabic(p,328,860,UI_AR_SETTINGS_NOTE,MUTED);
    footer(p,UI_AR_FOOTER_SETTINGS);
}
static void search_screen(uint32_t *p,const ScannerUiEntry *entries,
                          unsigned count) {
    arabic(p,285,143,UI_AR_SEARCH_HINT,WHITE);
    panel(p,270,230,1579,104,0xFF0E2A51u);
    if(navigation.query[0])
        ui_title(p,305,240,1470,navigation.query,CYAN,3);
    else
        arabic(p,305,243,UI_AR_SEARCH_BY_NAME,MUTED);
    arabic(p,300,353,navigation.keyboard_arabic ?
           UI_AR_KEYBOARD_ARABIC:UI_AR_KEYBOARD_ENGLISH,CYAN);
    char line[64];
    snprintf(line,sizeof(line),"%u",navigation.available);
    arabic(p,1415,354,UI_AR_SEARCH_RESULTS,MUTED);
    ascii(p,1715,370,line,WHITE,3,10);

    const unsigned max=ui_nav_keyboard_count(&navigation);
    for(unsigned i=0;i<max;++i) {
        int col=(int)(i%10), row=(int)(i/10);
        int x=290+col*152, y=410+row*129;
        panel(p,x,y,140,105,
              !navigation.search_result_focus &&
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
    /* One read-only match preview fits below the four keyboard rows.
     * Down from the last keyboard row focuses this preview; Left/Right
     * cycles matches, Cross opens real game metadata in Details. */
    panel(p,270,910,1579,83,navigation.search_result_focus ?
          0xFF164881u:0xFF102D50u);
    if(navigation.available && entries) {
        const unsigned idx=ui_nav_resolve(&navigation,navigation.selected);
        if(idx<count) {
            const ScannerUiEntry *g=&entries[idx];
            cover(p,291,925,57,55,g->cusa);
            game_title(p,360,917,847,g,WHITE,2);
            ascii(p,361,957,g->cusa?g->cusa:"NO CUSA",CYAN,2,13);
            char selection[48];
            (void)snprintf(selection,sizeof selection,"%u / %u",
                navigation.selected+1,navigation.available);
            ascii(p,1450,945,selection,MUTED,3,12);
            if(navigation.search_result_focus)
                rect(p,270,918,5,66,CYAN);
        }
    } else {
        arabic(p,303,933,UI_AR_NO_MATCHING_GAME,AMBER);
    }
    footer(p,UI_AR_FOOTER_SEARCH);
}

/* Cache is unverified metadata, but the warning must never obscure the
 * selected game's read-only Details control. The sixth game row ends at
 * y=931; this status strip lives only in the gap before the footer. */
static void cache_notice(uint32_t *p,int scanning) {
    /* One legible sentence, intentionally above the y=1000 footer.
     * Two stacked 30px Arabic lines were clipped at 1080p and could
     * falsely say "checking" after a definitive FTP failure. */
    panel(p,269,937,1109,59,0xFF102D50u);
    rect(p,279,947,5,39,AMBER);
    arabic(p,304,947,scanning?UI_AR_CACHE_PREVIEW:UI_AR_CACHE_STALE,
           AMBER);
    if(scanning) {
        /* This is scanner progress, not a download progress bar. */
        ascii(p,924,953,progress_line,CYAN,2,16);
    }
}
static void draw(const ScannerUiEntry *entries,
                 unsigned count, unsigned good, unsigned errors,
                 int complete, int saved, unsigned start, int scanning) {
    (void)start;
    if(!usable) return;
    uint32_t *p=surface[next_buffer];
    if(!scanning &&
       (navigation.page==UI_PAGE_HOME ||
        navigation.page==UI_PAGE_GAMES ||
        navigation.page==UI_PAGE_DETAILS))
        prepare_blurred_background(entries,count);
    background(p);
    rail(p,navigation.page);
    header(p);
    if(scanning && previous_scan_count &&
       (navigation.page==UI_PAGE_HOME || navigation.page==UI_PAGE_GAMES)) {
        /* Make the library visible immediately, but keep this strictly
         * non-interactive until the synchronous scanner finishes.
         * Cached names/versions are ALWAYS unverified and stale. */
        dashboard(p,previous_scan,previous_scan_count,
                  previous_scan_count,0,0,1);
        /* Do not paint over the selected game's Details control.
         * Cache warnings stay below the sixth library row. */
        cache_notice(p,1);
    }else if(scanning &&
             (navigation.page==UI_PAGE_HOME ||
              navigation.page==UI_PAGE_GAMES)) {
        arabic(p,293,218,UI_AR_READING_METADATA,WHITE);
        panel(p,280,328,1540,344,PANEL);
        ascii(p,315,408,progress_line,CYAN,4,70);
        arabic(p,315,525,UI_AR_DOWNLOAD_BLOCKED,MUTED);
        footer(p,UI_AR_FOOTER_SCANNING);
    }else if(navigation.page==UI_PAGE_SEARCH) {
        search_screen(p,entries,count);
    }else if(navigation.page==UI_PAGE_SETTINGS) {
        settings_screen(p);
    }else if(navigation.page==UI_PAGE_DETAILS) {
        details(p,entries,count,complete);
        if(final_view_is_stale || scanning) {
            /* Keep the Details warning accurate too: failed/partial FTP
             * is NOT still "checking". The same one-line 1080p-safe copy
             * is used on Home, and neither warning enables installation. */
            panel(p,270,937,1548,62,0xFF102D50u);
            rect(p,280,947,5,39,AMBER);
            arabic(p,304,947,scanning?UI_AR_CACHE_PREVIEW:
                   UI_AR_CACHE_STALE,AMBER);
        }
    }else if(navigation.page==UI_PAGE_DOWNLOADS) {
        downloads(p);
    }else if(navigation.page==UI_PAGE_AVAILABLE ||
             navigation.page==UI_PAGE_QUEUE ||
             navigation.page==UI_PAGE_INSTALLED ||
             navigation.page==UI_PAGE_ABOUT) {
        readonly_section(p,navigation.page);
    }else {
        dashboard(p,entries,count,good,errors,complete,saved);
        if(final_view_is_stale)
            cache_notice(p,0);
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

extern "C" int scanner_ui_preview_snapshot(
    const ScannerUiEntry *items,unsigned entries
) {
    if(!usable || !items || entries==0u || entries>UI_MAX_TITLES)
        return 0;
    for(unsigned i=0;i<entries;++i) {
        if(!items[i].cusa)return 0;
        previous_scan[i]=items[i];
        /* Cache contents can never confirm installation or SFO identity. */
        previous_scan[i].metadata_verified=0;
        previous_scan[i].install_state=-1;
        navigation_records[i].cusa=items[i].cusa;
        navigation_records[i].name=items[i].title;
    }
    previous_scan_count=entries;
    ui_nav_init(&navigation,navigation_records,entries);
    draw(previous_scan,entries,entries,0,0,1,0,1);
    return 1;
}

extern "C" int scanner_ui_offer_icon(
    const char *cusa, const unsigned char *png, unsigned png_bytes
) {
    /* Like offer_thumbnail(), this API must be called from the UI owner
     * thread. An updated PNG with the SAME CUSA still changes the artwork:
     * invalidate the cached blurred backdrop only after successful decode. */
    const int result=ui_icon_cache_offer(cusa,png,png_bytes);
    if(result==0 && cusa && blurred_for[0] &&
       strcmp(blurred_for,cusa)==0)
        blurred_for[0]=0;
    return result;
}

extern "C" int scanner_ui_offer_thumbnail(
    const char *cusa, const uint32_t *pixels, size_t pixel_count
) {
    /* Render thread only: worker never modifies the UI's LRU cache. */
    const int result=ui_icon_cache_offer_pixels(cusa,pixels,pixel_count);
    /* Only invalidate when the changed cover is actually the selected
     * backdrop; unrelated list covers must not recompute the blur. */
    if(result==0 && cusa && blurred_for[0] &&
       strcmp(blurred_for,cusa)==0)
        blurred_for[0]=0;
    return result;
}

extern "C" void scanner_ui_set_cover_tick(int (*tick)(const char *)) {
    cover_tick=tick;
}

extern "C" int scanner_ui_begin(void) {
    cover_tick=nullptr;
    ui_icon_cache_reset();
    previous_scan_count=0;
    final_view_is_stale=0;
    ui_nav_init(&navigation,nullptr,0);
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
                 "%u / %u - %s", current, total, cusa);
    else if(total>0)
        snprintf(progress_line, sizeof(progress_line),
                 "%u / %u", current, total);
    else
        snprintf(progress_line, sizeof(progress_line),
                 "%s", cusa ? cusa : "...");
    draw(previous_scan_count?previous_scan:nullptr,previous_scan_count,
         previous_scan_count,0,0,1,navigation.scroll,1);
}

/* Called on the VideoOut/Pad owner thread while the read-only scanner
 * runs on a separate scePthread. The scanner publishes a single atomic
 * progress word and never touches framebuffer, navigation, or icon cache.
 * Navigation remains responsive even when FTP commands are blocking. */
extern "C" int scanner_ui_wait_for_scan(
    const volatile uint32_t *atomic_status
) {
    if(!usable || !atomic_status)return 0;
    uint32_t last_progress=0;
    uint32_t prior=0;
    unsigned no_pad_wait=0, pad_failures=0;
    draw(previous_scan_count?previous_scan:nullptr,previous_scan_count,
         previous_scan_count,0,0,1,navigation.scroll,1);
    for(;;) {
        const uint32_t status=__atomic_load_n(atomic_status,__ATOMIC_ACQUIRE);
        if(status&SCANNER_UI_SCAN_DONE)return 0;
        if((status&~SCANNER_UI_SCAN_DONE)!=last_progress) {
            last_progress=status&~SCANNER_UI_SCAN_DONE;
            scanner_ui_progress(nullptr,(status>>16)&0x7FFFu,status&0xFFFFu);
        }
        (void)sceKernelUsleep(33000);
        if(pad<0) {
            if(++no_pad_wait>=900)return 1;
            continue;
        }
        OrbisPadData state={};
        if(scePadReadState(pad,&state)<0) {
            if(++pad_failures>=300)return 1;
            continue;
        }
        pad_failures=0;
        const uint32_t pressed=state.buttons & ~prior;
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
            const unsigned n=ui_model_download_count(),prev=job_selected;
            if(action==UI_ACT_UP && job_selected>0)--job_selected;
            if(action==UI_ACT_DOWN && job_selected+1<n)++job_selected;
            if(prev==job_selected)continue;
            if(job_selected<job_scroll)job_scroll=job_selected;
            if(job_selected>=job_scroll+5u)job_scroll=job_selected-4u;
        }else if(!ui_nav_action(&navigation,action))continue;
        if(navigation.exit_requested)return 1;
        draw(previous_scan_count?previous_scan:nullptr,previous_scan_count,
             previous_scan_count,0,0,1,navigation.scroll,1);
    }
}

extern "C" void scanner_ui_results(
    const ScannerUiEntry *entries, unsigned count,
    unsigned metadata_count, unsigned errors,
    int scan_complete, int inventory_saved
) {
    if (!usable) return;
    if (!entries) count=0;
    /* Even a nonempty partial FTP result is not a complete inventory.
     * Preserve the previous read-only library until a complete scan succeeds.
     * Cache entries always remain unverified and do not grant permissions. */
    final_view_is_stale=(!scan_complete && previous_scan_count>0u);
    if(final_view_is_stale) {
        entries=previous_scan;
        count=previous_scan_count;
        /* The primary summary describes the visible cached games, not the
         * partial SFO count from this failed scan. */
        metadata_count=count;
    }
    if(count>UI_MAX_TITLES)count=UI_MAX_TITLES;
    /* The summary must never claim more verified base records than the
     * bounded UI library can actually represent. An oversized provider
     * count is not evidence of additional installed games. */
    if(metadata_count>count)metadata_count=count;
    /* Navigation records are about to be replaced. Save the selected ID
     * first so a completed rescan cannot jump Details to a different game. */
    char selected_cusa[10]={0};
    const unsigned old_idx=ui_nav_resolve(&navigation,navigation.selected);
    if(navigation.items && old_idx<navigation.item_count &&
       navigation.items[old_idx].cusa)
        (void)snprintf(selected_cusa,sizeof selected_cusa,"%s",
                       navigation.items[old_idx].cusa);
    for(unsigned i=0;i<count;++i) {
        navigation_records[i].cusa=entries[i].cusa;
        navigation_records[i].name=entries[i].title;
    }
    if(!final_view_is_stale)previous_scan_count=0;
    ui_nav_rebind(&navigation,navigation_records,count,selected_cusa);
    draw(entries,count,metadata_count,errors,
         scan_complete,inventory_saved,0,0);
    uint32_t prior=0;
    unsigned no_pad_wait=0;
    unsigned pad_read_failures=0;
    for(;;) {
        /* Poll Pad at ~30 Hz, but draw only on input or new icon data.
         * This is NOT proof of 30 rendered FPS on physical hardware. */
        (void)sceKernelUsleep(33000);
        /* Icon requests are nonblocking atomic mailbox operations.
         * Poll selected title first, then six visible rows; never decode
         * images or access FTP on the VideoOut/Pad thread. */
        int cover_updated=0;
        if(cover_tick && entries &&
           (navigation.page==UI_PAGE_HOME ||
            navigation.page==UI_PAGE_GAMES ||
            navigation.page==UI_PAGE_DETAILS ||
            (navigation.page==UI_PAGE_SEARCH &&
             navigation.search_result_focus))) {
            const unsigned selected=ui_nav_resolve(
                &navigation,navigation.selected);
            if(selected<count && entries[selected].cusa)
                cover_updated |= cover_tick(entries[selected].cusa);
            const unsigned end=navigation.scroll+UI_VISIBLE_ROWS;
            for(unsigned i=navigation.scroll;
                i<end && i<navigation.available;++i) {
                const unsigned index=ui_nav_resolve(&navigation,i);
                if(index<count && index!=selected && entries[index].cusa)
                    cover_updated |= cover_tick(entries[index].cusa);
            }
        }
        if(cover_updated)
            draw(entries,count,metadata_count,errors,
                 scan_complete,inventory_saved,navigation.scroll,0);
        if(pad<0) {
            /* ~30 seconds, preserving the previous no-Pad safety window. */
            if(++no_pad_wait>=900)break;
            continue;
        }
        OrbisPadData state={};
        if(scePadReadState(pad,&state)<0) {
            /* ~10 seconds, independent of the faster input polling rate. */
            if(++pad_read_failures>=300)break;
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
