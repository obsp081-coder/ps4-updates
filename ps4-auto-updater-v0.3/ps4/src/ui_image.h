#ifndef PS4_UI_IMAGE_H
#define PS4_UI_IMAGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define UI_ICON_SIDE 128u
#define UI_ICON_PIXELS (UI_ICON_SIDE * UI_ICON_SIDE)
#define UI_ICON_MAX_PNG (2u * 1024u * 1024u)
/* Decode one local PNG from already acquired memory; zero network and disk
 * access, zero ownership transfer. Destination must hold UI_ICON_PIXELS u32.
 * Returns 0 success, negative error. Decode outside the VideoOut render loop. */
int ui_image_thumbnail(const unsigned char *png, size_t bytes,
                       uint32_t *argb, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
