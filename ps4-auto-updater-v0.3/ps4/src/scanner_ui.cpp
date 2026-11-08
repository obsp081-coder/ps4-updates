/*
 * Minimal PS4 native read-only scanner UI.
 * CPU framebuffer via OpenOrbis VideoOut; DualShock input via ScePad.
 * No installation, download, or game-file writes.
 */
#include "scanner_ui.h"
#include "ui_navigation.h"
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

static void text(uint32_t *pixels, int x, int y, const char *s,
                 uint32_t color, int scale, int max_chars) {
    if (!s) return;
    for (int i = 0; s[i] && i < max_chars; ++i) {
        int bx = x + i * 6 * scale;
        if (bx + 5 * scale >= WIDTH) break;
        const uint8_t *rows = glyph(s[i]);
        for (int row = 0; row < 7; ++row)
            for (int bit = 0; bit < 5; ++bit)
                if (rows[row] & (1u << (4 - bit)))
                    rect(pixels, bx + bit * scale, y + row * scale,
                         scale, scale, color);
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

static void panel(uint32_t *p, int x, int y, int w, int h,
                  uint32_t color) {
    rect(p, x+8, y, w-16, h, color);
    rect(p, x, y+8, w, h-16, color);
}
static void ascii(uint32_t *p, int x, int y, const char *s,
                  uint32_t color, int scale, int max_chars) {
    text(p,x,y,s,color,scale,max_chars);
}
static const char *category_label(const ScannerUiEntry *g) {
    if (!g || !g->metadata_verified) return "NOT VERIFIED";
    if (!g->category) return "METADATA";
    if (!strcmp(g->category,"gp") || !strcmp(g->category,"gdp"))
        return "PATCH META";
    if (!strcmp(g->category,"ac")) return "ADD-ON META";
    if (!strcmp(g->category,"gd") || !strcmp(g->category,"gda") ||
        !strcmp(g->category,"gdo") || !strcmp(g->category,"gde"))
        return g->install_state==1 ? "INSTALLED" :
               g->install_state==0 ? "NOT INSTALLED" : "BASE - UNKNOWN";
    return "OTHER META";
}
static const char *fallback_name(const ScannerUiEntry *g) {
    return g && g->title && g->title[0] ? g->title : "NAME NOT AVAILABLE";
}
static const char *fallback_version(const ScannerUiEntry *g) {
    return g && g->version && g->version[0] ? g->version : "UNKNOWN";
}
static void placeholder(uint32_t *p, int x, int y, int w, int h) {
    panel(p,x,y,w,h,0xFF163860u);
    rect(p,x+15,y+15,w-30,4,CYAN);
    ascii(p,x+19,y+h/2-8,"PS4",WHITE,3,3);
}
static void header(uint32_t *p, UiPage page) {
    rect(p,0,0,WIDTH,115,0xFF0A1E3Eu);
    rect(p,0,112,WIDTH,3,ACCENT);
    panel(p,68,27,62,62,ACCENT);
    ascii(p,84,49,"PS4",WHITE,2,3);
    ascii(p,154,29,"GAME UPDATE MANAGER",WHITE,4,32);
    ascii(p,156,73,"ROYAL BLUE  |  LOCAL LIBRARY",MUTED,2,38);

    const int positions[3]={1110,1370,1610};
    const char *labels[3]={"HOME","GAME DETAILS","DOWNLOADS"};
    for (int i=0;i<3;++i) {
        int highlighted=(page==UI_PAGE_HOME&&i==0) ||
                        (page==UI_PAGE_DETAILS&&i==1) ||
                        (page==UI_PAGE_DOWNLOADS&&i==2);
        panel(p,positions[i],31,i==1?225:190,48,
              highlighted?ACCENT:0xFF102D50u);
        ascii(p,positions[i]+20,48,labels[i],
              highlighted?WHITE:MUTED,2,20);
    }
}
static void background(uint32_t *p) {
    rect(p,0,0,WIDTH,HEIGHT,BG);
    for(int y=128;y<1030;y+=48)
        rect(p,0,y,WIDTH,16,(y/48)%2 ? 0xFF07172Fu:0xFF081A35u);
}
static void footer(uint32_t *p, const char *hint) {
    rect(p,0,1000,WIDTH,80,0xFF081A35u);
    rect(p,0,1000,WIDTH,2,0xFF1F5C9Eu);
    ascii(p,85,1028,hint,MUTED,2,91);
}
static void summary(uint32_t *p, int x, int w, const char *name,
                    const char *value, uint32_t color) {
    panel(p,x,145,w,110,PANEL);
    rect(p,x+16,166,4,66,color);
    ascii(p,x+36,168,name,MUTED,2,28);
    ascii(p,x+36,203,value,WHITE,3,28);
}
static void game_item(uint32_t *p, const ScannerUiEntry *g,
                      int y,int is_selected) {
    panel(p,76,y,1090,88,is_selected?0xFF164881u:PANEL);
    if(is_selected) rect(p,76,y+10,5,68,CYAN);
    placeholder(p,92,y+11,68,66);
    ascii(p,187,y+15,fallback_name(g),WHITE,2,39);
    ascii(p,187,y+53,g->cusa?g->cusa:"NO CUSA",MUTED,2,12);
    ascii(p,877,y+16,fallback_version(g),CYAN,2,16);
    ascii(p,877,y+52,category_label(g),
          g->metadata_verified?MUTED:AMBER,2,21);
}
static void dashboard(uint32_t *p, const ScannerUiEntry *entries,
                      unsigned count,unsigned good,unsigned errors,
                      int complete,int saved) {
    char number[64];
    snprintf(number,sizeof(number),"%u",good);
    summary(p,76,335,"BASE-GAME METADATA",number,CYAN);
    snprintf(number,sizeof(number),"%u",count);
    summary(p,426,335,"CUSA DIRECTORIES",number,ACCENT);
    snprintf(number,sizeof(number),"%u",errors);
    summary(p,776,390,"SCAN ERRORS",number,errors?AMBER:0xFF6CCFACu);

    ascii(p,86,286,"GAME LIBRARY",WHITE,3,30);
    ascii(p,854,296,complete?"SCAN COMPLETE":"PARTIAL SCAN",
          complete?0xFF6CCFACu:AMBER,2,17);
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
        ascii(p,124,411,count?"NO MATCHING TITLES":"NO LOCAL GAME METADATA",
              AMBER,3,37);
    if(!saved)
        ascii(p,112,949,"WARNING: INVENTORY FILE NOT SAVED",AMBER,2,45);

    panel(p,1193,145,658,814,0xFF0B2142u);
    ascii(p,1230,176,"SELECTED GAME",CYAN,3,24);
    if(navigation.available && entries) {
        unsigned idx=ui_nav_resolve(&navigation,navigation.selected);
        if(idx<count) {
            const ScannerUiEntry *g=&entries[idx];
            placeholder(p,1232,240,208,231);
            ascii(p,1472,262,fallback_name(g),WHITE,2,23);
            ascii(p,1472,309,g->cusa?g->cusa:"NO CUSA",CYAN,2,15);
            ascii(p,1472,348,"VERSION",MUTED,2,12);
            ascii(p,1472,377,fallback_version(g),WHITE,3,14);
            ascii(p,1235,511,"METADATA CLASS",MUTED,2,25);
            ascii(p,1235,547,category_label(g),WHITE,3,23);
            rect(p,1234,598,568,2,0xFF244769u);
            ascii(p,1235,635,"NO VERIFIED UPDATE SOURCE",AMBER,2,33);
            ascii(p,1235,674,"DOWNLOAD / INSTALL LOCKED",MUTED,2,33);
            panel(p,1231,764,562,71,0xFF15589Du);
            ascii(p,1270,790,"CROSS: OPEN FULL DETAILS",WHITE,2,40);
        }
    } else {
        ascii(p,1240,387,"NO GAME SELECTED",MUTED,3,25);
    }
    ascii(p,1240,899,"METADATA DOES NOT PROVE INSTALL",MUTED,2,42);
    snprintf(number,sizeof(number),"ITEM %u / %u",
             navigation.available?navigation.selected+1:0,
             navigation.available);
    ascii(p,89,960,number,MUTED,2,28);
    footer(p,"UP/DOWN: SELECT  CROSS: DETAILS  SQUARE/R1: DOWNLOADS  CIRCLE: EXIT");
}
static void details(uint32_t *p,const ScannerUiEntry *entries,
                    unsigned count,int complete) {
    unsigned idx=ui_nav_resolve(&navigation,navigation.selected);
    if(!entries || idx>=count) {
        ascii(p,120,337,"NO GAME SELECTED",AMBER,4,30);
        footer(p,"CIRCLE: BACK");
        return;
    }
    const ScannerUiEntry *g=&entries[idx];
    ascii(p,93,152,"GAME DETAILS",WHITE,4,35);
    panel(p,90,225,1730,705,PANEL);
    placeholder(p,1465,271,285,317);
    ascii(p,135,271,fallback_name(g),WHITE,4,35);
    ascii(p,140,348,g->cusa?g->cusa:"UNKNOWN",CYAN,3,18);
    ascii(p,143,415,"CURRENT APP VERSION",MUTED,2,30);
    ascii(p,143,452,fallback_version(g),WHITE,4,15);
    ascii(p,625,415,"PACKAGE CATEGORY",MUTED,2,29);
    ascii(p,625,455,category_label(g),WHITE,3,30);
    ascii(p,143,538,"REGION / GAME SIZE / REQUIRED FW",MUTED,2,55);
    ascii(p,143,575,"NOT AVAILABLE - DO NOT ASSUME",AMBER,2,55);
    rect(p,130,641,1270,2,0xFF22527Bu);
    ascii(p,142,674,"AVAILABLE UPDATE VERSIONS",CYAN,3,45);
    ascii(p,143,727,"NO VERIFIED AUTHORIZED SOURCE",AMBER,2,55);
    ascii(p,143,779,"RETAIL AND FPKG PATCHES ARE NOT INTERCHANGEABLE",
          MUTED,2,60);
    panel(p,1464,646,290,96,0xFF283648u);
    ascii(p,1490,678,"INSTALL BLOCKED",MUTED,2,23);
    ascii(p,141,873,complete?"SCAN METADATA: PARTIAL INSTALL PROOF":
          "SCAN INCOMPLETE: CHECK DIAGNOSTIC",MUTED,2,55);
    footer(p,"CIRCLE: BACK  R1: DOWNLOADS  NO DOWNLOAD/INSTALL API ENABLED");
}
static void downloads(uint32_t *p) {
    ascii(p,90,157,"DOWNLOAD MANAGER",WHITE,4,35);
    summary(p,90,540,"ACTIVE DOWNLOADS","0",CYAN);
    summary(p,645,540,"QUEUED DOWNLOADS","0",ACCENT);
    summary(p,1200,540,"INSTALL JOBS","0",AMBER);
    panel(p,90,288,1655,640,PANEL);
    ascii(p,151,350,"NO AUTHORIZED DOWNLOAD TASKS",WHITE,4,53);
    ascii(p,151,416,"UPDATES WILL APPEAR ONLY AFTER TRUST AND FW CHECKS",
          MUTED,2,75);
    rect(p,150,475,1510,2,0xFF22527Bu);
    ascii(p,151,553,"INSTALL AND AUTO-INSTALL ARE NOT ENABLED",AMBER,3,54);
    ascii(p,151,612,"NO FAKE PROGRESS AND NO UNVERIFIED PACKAGE LINKS",MUTED,2,75);
    footer(p,"CIRCLE/L1: BACK  DOWNLOADS ARE DISABLED UNTIL VERIFIED");
}
static void draw(const ScannerUiEntry *entries,
                 unsigned count, unsigned good, unsigned errors,
                 int complete, int saved, unsigned start, int scanning) {
    (void)start;
    if(!usable) return;
    uint32_t *p=surface[next_buffer];
    background(p);
    header(p,scanning?UI_PAGE_HOME:navigation.page);
    if(scanning) {
        ascii(p,118,234,"READING LOCAL GAME METADATA",WHITE,4,50);
        panel(p,108,328,1700,344,PANEL);
        ascii(p,150,408,progress_line,CYAN,4,75);
        ascii(p,150,546,"NO DOWNLOAD OR INSTALL OPERATIONS",MUTED,3,54);
        footer(p,"SCANNER RUNNING  |  READ ONLY");
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

extern "C" int scanner_ui_begin(void) {
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
        else if(pressed&ORBIS_PAD_BUTTON_SQUARE) action=UI_ACT_DOWNLOADS;
        else if(pressed&ORBIS_PAD_BUTTON_R1) action=UI_ACT_NEXT_TAB;
        else if(pressed&ORBIS_PAD_BUTTON_L1) action=UI_ACT_PREVIOUS_TAB;
        else if(pressed&ORBIS_PAD_BUTTON_DOWN) action=UI_ACT_DOWN;
        else if(pressed&ORBIS_PAD_BUTTON_UP) action=UI_ACT_UP;
        else continue;
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
