/*
 * Minimal PS4 native read-only scanner UI.
 * CPU framebuffer via OpenOrbis VideoOut; DualShock input via ScePad.
 * No installation, download, or game-file writes.
 */
#include "scanner_ui.h"
#include <orbis/VideoOut.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 1920
#define HEIGHT 1080
#define PIXELS ((size_t)WIDTH * HEIGHT)
#define BUFFER_SIZE (PIXELS * 4)
#define ALIGNMENT 0x200000UL
#define ROWS 12

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
static off_t mem_off = 0;
static size_t mem_size = 0;
static void *mapped = nullptr;
static uint32_t *surface[2] = {nullptr, nullptr};
static int64_t frame_id = 0;

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

static void draw(const ScannerUiEntry *entries,
                 unsigned count, unsigned good, unsigned errors,
                 int complete, int saved, unsigned start, int scanning) {
    if (!usable) return;
    uint32_t *p = surface[next_buffer];
    rect(p, 0, 0, WIDTH, HEIGHT, 0xFF0F1D30u);
    rect(p, 0, 0, WIDTH, 142, 0xFF243E61u);
    rect(p, 0, 139, WIDTH, 4, 0xFF46BFD6u);
    text(p, 90, 48, "PS4 GAME UPDATE MANAGER", 0xFFF2FBFFu, 4, 45);
    text(p, 93, 166, "READ ONLY - DOWNLOAD AND INSTALL DISABLED",
         0xFF46BFD6u, 2, 66);

    if (scanning) {
        text(p, 120, 360, "SCANNING LOCAL METADATA...",
             0xFFF2FBFFu, 4, 60);
        text(p, 120, 450, "GOLDHEN FTP MUST BE ENABLED",
             0xFFBFD7F0u, 3, 60);
    } else {
        char line[160];
        snprintf(line, sizeof(line),
                 "CANDIDATES %u   METADATA %u   ERRORS %u",
                 count, good, errors);
        text(p, 95, 219, line, 0xFFF2FBFFu, 3, 78);
        text(p, 95, 270, complete ? "SCAN COMPLETE" :
             "SCAN INCOMPLETE - READ DIAGNOSTIC",
             complete ? 0xFF82E1AEu : 0xFFFFBA78u, 2, 73);
        if (!saved)
            text(p, 95, 308, "ERROR: INVENTORY NOT SAVED",
                 0xFFFF8181u, 2, 75);
        unsigned visible = count > start ? count - start : 0;
        if (visible > ROWS) visible = ROWS;
        for (unsigned i = 0; i < visible; ++i) {
            const ScannerUiEntry *g = &entries[start + i];
            int y = 347 + (int)i * 53;
            if (i % 2 == 0) rect(p, 76, y - 8, 1750, 47, 0xFF24364Cu);
            text(p, 94, y, g->cusa, 0xFF6EE3FFu, 2, 11);
            if (!g->metadata_verified) {
                text(p, 360, y, "UNVERIFIED METADATA",
                     0xFFFFBD7Au, 2, 42);
                continue;
            }
            text(p, 360, y, g->title && g->title[0] ?
                 g->title : "UNTITLED", 0xFFEBF7FFu, 2, 42);
            text(p, 1170, y, g->version && g->version[0] ?
                 g->version : "UNKNOWN", 0xFFBFD7F0u, 2, 16);
            text(p, 1460, y,
                 g->install_state == 1 ? "INSTALLED" :
                 g->install_state == 0 ? "NOT FOUND" : "UNKNOWN",
                 0xFFFFBD7Au, 2, 21);
        }
        if (count == 0)
            text(p, 100, 400, "NO GAME METADATA FOUND",
                 0xFFFFBD7Au, 3, 45);
        if (count > ROWS) {
            snprintf(line, sizeof(line), "ROWS %u - %u OF %u",
                     start + 1, start + visible, count);
            text(p, 93, 992, line, 0xFFBFD7F0u, 2, 50);
        }
    }

    rect(p, 0, 1036, WIDTH, 44, 0xFF26415Eu);
    text(p, 89, 1046,
         scanning ? "PLEASE WAIT" :
         "UP/DOWN: SCROLL    CIRCLE: EXIT    METADATA IS NOT INSTALL PROOF",
         0xFFD8E8F9u, 2, 90);
    (void)sceVideoOutSubmitFlip(video, next_buffer,
                                ORBIS_VIDEO_OUT_FLIP_VSYNC, frame_id++);
    next_buffer = (next_buffer + 1) % 2;
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
    (void)sceVideoOutSetFlipRate(video, 0);
    if (scePadInit() == 0 && user_id >= 0)
        pad = scePadOpen(user_id, 0, 0, nullptr);
    usable = 1;
    draw(nullptr, 0, 0, 0, 0, 0, 0, 1);
    return 1;
}

extern "C" void scanner_ui_results(
    const ScannerUiEntry *entries, unsigned count,
    unsigned metadata_count, unsigned errors,
    int scan_complete, int inventory_saved
) {
    if (!usable) return;
    unsigned start = 0;
    draw(entries, count, metadata_count, errors,
         scan_complete, inventory_saved, start, 0);
    uint32_t prior = 0;
    unsigned no_pad_wait = 0;
    for (;;) {
        (void)sceKernelUsleep(100000);
        if (pad < 0) {
            if (++no_pad_wait >= 1200) break;
            continue;
        }
        OrbisPadData state = {};
        if (scePadReadState(pad, &state) < 0) continue;
        uint32_t pressed = state.buttons & ~prior;
        prior = state.buttons;
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE) break;
        if ((pressed & ORBIS_PAD_BUTTON_DOWN) && start + ROWS < count)
            ++start;
        else if ((pressed & ORBIS_PAD_BUTTON_UP) && start > 0)
            --start;
        else
            continue;
        draw(entries, count, metadata_count, errors,
             scan_complete, inventory_saved, start, 0);
    }
}

extern "C" void scanner_ui_shutdown(void) {
    if (pad >= 0) {
        (void)scePadClose(pad);
        pad = -1;
    }
    if (video >= 0) {
        (void)sceVideoOutClose(video);
        video = -1;
    }
    if (mapped && mem_size)
        (void)sceKernelMunmap(mapped, mem_size);
    if (mem_size)
        (void)sceKernelReleaseDirectMemory(mem_off, mem_size);
    mapped = nullptr;
    mem_size = 0;
    usable = 0;
}
