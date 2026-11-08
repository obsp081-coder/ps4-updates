#ifndef PS4_SCANNER_UI_H
#define PS4_SCANNER_UI_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ScannerUiEntry {
    const char *cusa;
    const char *title;
    const char *version;
    int metadata_verified;
    int install_state;
    const char *category;
} ScannerUiEntry;

int scanner_ui_begin(void);
/* Paint unverified cached metadata immediately, before background scanning.
 * A cached entry is never installation or compatibility proof. */
int scanner_ui_preview_snapshot(const ScannerUiEntry *items,unsigned entries);
void scanner_ui_progress(const char *cusa, unsigned current, unsigned total);
/* Worker publishes only a packed atomic status: bit31 DONE,
 * bits16..30 current, bits0..15 total. Renderer/Pad stay on the UI thread.
 * Returns 1 for user-requested exit; 0 if worker finished normally. */
#define SCANNER_UI_SCAN_DONE 0x80000000u
int scanner_ui_wait_for_scan(const volatile uint32_t *atomic_status);
void scanner_ui_results(
    const ScannerUiEntry *items, unsigned entries,
    unsigned metadata_count, unsigned errors,
    int scan_complete, int inventory_saved
);
void scanner_ui_shutdown(void (*trace)(const char *, int));
/* Source is local console metadata only, bytes read by scanner; no network. */
int scanner_ui_offer_icon(const char *cusa, const unsigned char *png,
                          unsigned png_bytes);
/* UI-thread handoff for an already-validated worker-decoded thumbnail. */
int scanner_ui_offer_thumbnail(const char *cusa,const uint32_t *pixels,
                               size_t pixel_count);
/* Optional read-only cover service callback, invoked ONLY on UI thread.
 * Called for selected and visible base games; returns 1 if an image was
 * committed to the UI's cache, 0 otherwise. No network on render thread. */
void scanner_ui_set_cover_tick(int (*tick)(const char *cusa));

#ifdef __cplusplus
}
#endif
#endif
