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
    registered = true;
    return 0;
}
int32_t sceVideoOutSetFlipRate(int32_t, int32_t) { return 0; }
int32_t sceVideoOutSubmitFlip(int32_t, int32_t, uint32_t, int64_t tag) {
    ++flip_count;
    last_flip = tag;
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
    if (pad_reads == 2) data->buttons = ORBIS_PAD_BUTTON_DOWN;
    if (pad_reads == 4) data->buttons = ORBIS_PAD_BUTTON_CIRCLE;
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
    scanner_ui_progress("CUSA02343", 1, 3);
    scanner_ui_results(entries, 3, 2, 1, 0, 1);
    scanner_ui_shutdown(trace);
    assert(flip_count >= 3);
    assert(pad_reads >= 4);
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
