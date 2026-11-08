#ifndef PS4_UI_ICON_CACHE_H
#define PS4_UI_ICON_CACHE_H
#include <stddef.h>
#include <stdint.h>
#include "ui_image.h"
#ifdef __cplusplus
extern "C" {
#endif
#define UI_ICON_CACHE_SLOTS 8u
void ui_icon_cache_reset(void);
/* Always returns nullptr/NULL for absent or unsafe title identifiers.
 * The icon cache never opens files, starts network requests, or mutates games. */
const uint32_t *ui_icon_cache_find(const char *cusa);
/* 0 on success, negative when a PNG is unsupported/malformed or CUSA invalid.
 * Data must originate from an already-authorized read-only local icon source. */
int ui_icon_cache_offer(const char *cusa, const unsigned char *png, size_t bytes);
#ifdef __cplusplus
}
#endif
#endif
