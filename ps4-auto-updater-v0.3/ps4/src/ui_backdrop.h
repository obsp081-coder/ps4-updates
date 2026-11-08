#ifndef PS4_UI_BACKDROP_H
#define PS4_UI_BACKDROP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define UI_BACKDROP_COLS 64u
#define UI_BACKDROP_ROWS 36u
#define UI_BACKDROP_CELLS (UI_BACKDROP_COLS * UI_BACKDROP_ROWS)
/* Cache one pre-darkened, low-resolution blurred thumbnail.
 * No image decoding, heap allocation or GPU calls on the render path.
 * Returns 1 for a real local icon, 0 for fallback, negative for invalid CUSA.
 */
int ui_backdrop_select(const char *cusa);
void ui_backdrop_reset(void);
/* Returns zero when there is no selected local art, so the caller can draw
 * a stable dark gradient instead. Framebuffer is owned by caller.
 */
int ui_backdrop_render(uint32_t *framebuffer, size_t capacity,
                       unsigned width, unsigned height);
#ifdef __cplusplus
}
#endif
#endif
