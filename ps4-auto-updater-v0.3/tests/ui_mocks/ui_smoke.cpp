#include "scanner_ui_host_mock.h"
#include "../../ps4/src/scanner_ui.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>

static int video_opened = 0;
static int video_closed = 0;
static int pad_closed = 0;
static int flip_count = 0;
static int pad_reads = 0;
static int allocated = 0;
static int sleep_count = 0;
static int64_t last_flip = 0;
static const uint32_t *display_buffers[2] = {nullptr, nullptr};
static bool saw_blurred_background = false;
static bool capture_live_ui = false;
static unsigned captured_pages = 0;
/* Visual-regression snapshots use the SAME native framebuffer output that
 * is submitted to the host VideoOut mock; this is not a browser mock. */
static void capture_ppm(const uint32_t *frame, const char *page,
                        unsigned page_bit) {
    const char *root = std::getenv("PS4_UI_PREVIEW_DIR");
    if (!root || !*root || (captured_pages & page_bit)) return;
    char path[768];
    const int n = std::snprintf(path, sizeof path, "%s/%s.ppm", root, page);
    if(n<0 || (size_t)n>=sizeof path) return;
    FILE *out = std::fopen(path, "wb");
    if(!out) return;
    const unsigned width=1920u, height=1080u;
    if(std::fprintf(out, "P6\n%u %u\n255\n", width, height) < 0) {
        std::fclose(out); return;
    }
    unsigned char rgb[1920u*3u];
    for(unsigned y=0;y<height;++y) {
        for(unsigned x=0;x<width;++x) {
            const uint32_t pixel=frame[(size_t)y*width+x];
            rgb[3*x]=(unsigned char)(pixel>>16);
            rgb[3*x+1]=(unsigned char)(pixel>>8);
            rgb[3*x+2]=(unsigned char)pixel;
        }
        if(std::fwrite(rgb,1,sizeof rgb,out)!=sizeof rgb) {
            std::fclose(out); return;
        }
    }
    if(std::fclose(out)==0) captured_pages|=page_bit;
}

static constexpr unsigned SCREEN_WIDTH = 1920u;
static const unsigned char icon_png_sample[] = {
0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x08,0x06,0x00,0x00,0x00,0x72,0xb6,0x0d,0x24,0x00,0x00,0x00,0x14,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0x94,0xf4,0xde,0xf2,0x9f,0x81,0x81,0x81,0x81,0x89,0x01,0x0a,0x00,0x1f,0x0d,0x02,0x1b,0x4c,0x07,0x75,0x81,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
};
static bool fail_video = false;
static bool fail_memory = false;
static bool fail_pad = false;
static bool stall_flip = false;
static bool registered = false;
static int unregister_count = 0;
static int trace_count = 0;
static int shutdown_done = 0;
static int retained_warning = 0;
static void trace(const char *name, int) {
    ++trace_count;
    if (std::strcmp(name, "UI_SHUTDOWN_DONE") == 0) ++shutdown_done;
    if (std::strcmp(name, "UI_MEMORY_RETAINED_FOR_SAFETY") == 0) ++retained_warning;
}

static void reset() {
    video_opened = video_closed = pad_closed = 0;
    flip_count = pad_reads = allocated = sleep_count = 0;
    last_flip = 0;
    display_buffers[0] = display_buffers[1] = nullptr;
    saw_blurred_background = false;
    captured_pages = 0;
    capture_live_ui = false;
    fail_video = fail_memory = fail_pad = stall_flip = false;
    registered = false;
    unregister_count = trace_count = shutdown_done = retained_warning = 0;
}
extern "C" {
int32_t sceUserServiceInitialize(void *) {return 0;}
int32_t sceUserServiceGetInitialUser(int32_t *id) {
    *id = 1;
    return 0;
}
int32_t sceVideoOutOpen(int32_t, int32_t, int32_t, const void *) {
    if (fail_video) return -1;
    ++video_opened;
    return 7;
}
int32_t sceVideoOutClose(int32_t) {
    assert(!registered);  // registered buffers must be unregistered first
    ++video_closed;
    return 0;
}
int32_t sceVideoOutUnregisterBuffers(int32_t, int32_t idx) {
    assert(idx == 0 && registered);
    ++unregister_count;
    registered = false;
    return 0;
}
void sceVideoOutSetBufferAttribute(void *, uint32_t, uint32_t, uint32_t,
                                    uint32_t, uint32_t, uint32_t) {}
int32_t sceVideoOutRegisterBuffers(int32_t, int32_t, void * const *p,
                                    int32_t count, const OrbisVideoOutBufferAttribute *) {
    if (count != 2 || !p || !p[0] || !p[1]) return -1;
    display_buffers[0] = static_cast<uint32_t *>(p[0]);
    display_buffers[1] = static_cast<uint32_t *>(p[1]);
    registered = true;
    return 0;
}
int32_t sceVideoOutSetFlipRate(int32_t, int32_t) { return 0; }
int32_t sceVideoOutSubmitFlip(int32_t, int32_t index, uint32_t, int64_t tag) {
    ++flip_count;
    last_flip = tag;
    if(index >= 0 && index < 2 && display_buffers[index]) {
        /* Source is the synthetic 2x2 PNG in icon_smoke.c, decoded to
         * 0xFF194BB4. The selected-game blurred backdrop is 25% icon and
         * 75% royal-blue with blending performed once, not per pixel. */
        /* x=72 is outside the 69px sidebar rail and outside
         * content panels, so it genuinely measures the blurred art. */
        const uint32_t sample =
            display_buffers[index][500u*SCREEN_WIDTH+72u];
        if(sample == 0xFF0A1F49u) saw_blurred_background = true;
        if(capture_live_ui) {
            const uint32_t *frame=display_buffers[index];
            const size_t row=50u*SCREEN_WIDTH;
            const uint32_t active=0xFF116BCEu;
            const int home=frame[row+932u]==active;
            const int details=frame[row+1112u]==active;
            const int downloads=frame[row+1347u]==active;
            const int settings=frame[row+1592u]==active;
            if(home) capture_ppm(frame,"home",1u);
            else if(details) capture_ppm(frame,"details",2u);
            else if(downloads) capture_ppm(frame,"downloads",4u);
            else if(settings) capture_ppm(frame,"settings",16u);
            else if(pad_reads>=16 && pad_reads<18)
                capture_ppm(frame,"search_ar",32u);
            else capture_ppm(frame,"search",8u);
        }
    }
    return 0;
}
int32_t sceVideoOutGetFlipStatus(int32_t, OrbisVideoOutFlipStatus *out) {
    std::memset(out, 0, sizeof(*out));
    out->flipArg = stall_flip ? 0 : last_flip;
    return 0;
}
int32_t scePadInit(void) { return 0; }
int32_t scePadOpen(int32_t, int32_t, int32_t, void *) {
    return fail_pad ? -1 : 3;
}
int32_t scePadClose(int32_t) {
    ++pad_closed;
    return 0;
}
int32_t scePadReadState(int32_t, OrbisPadData *data) {
    data->buttons = 0;
    ++pad_reads;
    /* Exercise actual controller flow: home -> details -> home ->
     * downloads -> home -> Arabic/English search -> home ->
     * settings -> home -> exit.
     * These are host input mocks, not simulated success of networking. */
    if (pad_reads == 2) data->buttons = ORBIS_PAD_BUTTON_CROSS;
    if (pad_reads == 4) data->buttons = ORBIS_PAD_BUTTON_CIRCLE;
    if (pad_reads == 6) data->buttons = ORBIS_PAD_BUTTON_SQUARE;
    if (pad_reads == 8) data->buttons = ORBIS_PAD_BUTTON_CIRCLE;
    if (pad_reads == 10) data->buttons = ORBIS_PAD_BUTTON_TRIANGLE;
    if (pad_reads == 12) data->buttons = ORBIS_PAD_BUTTON_CROSS;
    if (pad_reads == 14) data->buttons = ORBIS_PAD_BUTTON_SQUARE;
    if (pad_reads == 16) data->buttons = ORBIS_PAD_BUTTON_TRIANGLE;
    if (pad_reads == 18) data->buttons = ORBIS_PAD_BUTTON_CIRCLE;
    if (pad_reads == 20) data->buttons = ORBIS_PAD_BUTTON_OPTIONS;
    if (pad_reads == 22) data->buttons = ORBIS_PAD_BUTTON_CIRCLE;
    if (pad_reads == 24) data->buttons = ORBIS_PAD_BUTTON_CIRCLE;
    return 0;
}
size_t sceKernelGetDirectMemorySize(void) { return 512u * 1024u * 1024u; }
int32_t sceKernelAllocateDirectMemory(off_t, off_t, size_t, size_t,
                                       int32_t, off_t *offset) {
    if (fail_memory) return -1;
    *offset = 0x10000;
    return 0;
}
int32_t sceKernelMapDirectMemory(void **ptr, size_t size, int32_t, int32_t,
                                  off_t, size_t) {
    *ptr = std::malloc(size);
    if (!*ptr) return -1;
    ++allocated;
    return 0;
}
int32_t sceKernelMunmap(void *ptr, size_t) {
    assert(!registered);
    assert(allocated == 1);
    --allocated;
    std::free(ptr);
    return 0;
}
int32_t sceKernelReleaseDirectMemory(off_t, size_t) { return 0; }
int32_t sceKernelUsleep(uint32_t) {
    ++sleep_count;
    return 0;
}
} // extern "C"

int main() {
    ScannerUiEntry entries[] = {
        {"CUSA02343", "Verified Game", "01.07", 1, -1, "gd"},
        {"CUSA00411", "", "", 0, -1, nullptr},
        {"CUSA08032", "Other Patch", "02.04", 1, -1, "gp"},
    };

    reset();
    assert(scanner_ui_begin() == 1);
    assert(scanner_ui_offer_icon("CUSA02343",
                                 icon_png_sample,sizeof icon_png_sample) == 0);
    scanner_ui_progress("CUSA02343", 1, 3);
    capture_live_ui = true;
    scanner_ui_results(entries, 3, 2, 1, 0, 1);
    capture_live_ui = false;
    assert(saw_blurred_background);
    scanner_ui_shutdown(trace);
    assert(flip_count >= 8);
    assert(pad_reads >= 24);
    assert(allocated == 0 && video_closed == 1 && pad_closed == 1);
    assert(unregister_count == 1 && trace_count >= 7 && shutdown_done == 1);
    assert(retained_warning == 0);

    reset();
    fail_video = true;
    assert(scanner_ui_begin() == 0);
    scanner_ui_results(entries, 3, 2, 1, 0, 0);
    scanner_ui_shutdown(trace);
    assert(allocated == 0 && video_closed == 0);

    reset();
    fail_memory = true;
    assert(scanner_ui_begin() == 0);
    scanner_ui_shutdown(trace);
    assert(allocated == 0 && video_closed == 1);

    reset();
    fail_pad = true;
    assert(scanner_ui_begin() == 1);
    scanner_ui_results(entries, 3, 2, 0, 1, 1);
    scanner_ui_shutdown(trace);
    assert(allocated == 0 && pad_closed == 0);
    assert(sleep_count >= 300);

    reset();
    stall_flip = true;
    assert(scanner_ui_begin() == 1);
    scanner_ui_progress("CUSA02343", 2, 3);
    scanner_ui_results(entries, 3, 2, 0, 1, 1);
    /* Flip latency in draw() must not force unsafe memory cleanup. */
    stall_flip = false;
    scanner_ui_shutdown(trace);
    assert(allocated == 0 && pad_closed == 1);
    assert(sleep_count >= 160);
    assert(unregister_count == 1 && retained_warning == 0);

    std::puts("UI_HOST_SMOKE_OK");
    return 0;
}
