#ifndef PS4_SCANNER_UI_H
#define PS4_SCANNER_UI_H
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
void scanner_ui_progress(const char *cusa, unsigned current, unsigned total);
void scanner_ui_results(
    const ScannerUiEntry *items, unsigned entries,
    unsigned metadata_count, unsigned errors,
    int scan_complete, int inventory_saved
);
void scanner_ui_shutdown(void (*trace)(const char *, int));
/* Source is local console metadata only, bytes read by scanner; no network. */
int scanner_ui_offer_icon(const char *cusa, const unsigned char *png,
                          unsigned png_bytes);

#ifdef __cplusplus
}
#endif
#endif
