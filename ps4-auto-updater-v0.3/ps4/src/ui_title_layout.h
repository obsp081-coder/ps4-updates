/* Bounded, allocation-free title layout for native PS4 framebuffer.
 * This is deliberate mixed Arabic/Latin run ordering, NOT UAX #9 Bidi. */
#ifndef PS4_UI_TITLE_LAYOUT_H
#define PS4_UI_TITLE_LAYOUT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define UI_TITLE_MAX_CODEPOINTS 191u
#define UI_TITLE_MAX_RUNS 191u
typedef enum UiTitleDirection {
    UI_TITLE_LTR = 0, UI_TITLE_RTL = 1
} UiTitleDirection;
typedef struct UiTitleRun {
    unsigned start;
    unsigned length;
    UiTitleDirection direction;
} UiTitleRun;
typedef struct UiTitleLayout {
    UiTitleRun runs[UI_TITLE_MAX_RUNS];
    unsigned count;
    UiTitleDirection paragraph;
} UiTitleLayout;
/* First strong character sets paragraph direction. Adjacent RTL/LTR
 * segments retain logical order and neutrals follow the preceding segment.
 * Invalid sizes/pointers return -1 and leave no partial plan. */
int ui_title_layout(const uint32_t *codepoints, size_t length,
                    UiTitleLayout *out);
#ifdef __cplusplus
}
#endif
#endif
