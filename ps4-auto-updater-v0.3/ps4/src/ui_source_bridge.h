#ifndef PS4_UI_SOURCE_BRIDGE_H
#define PS4_UI_SOURCE_BRIDGE_H
#include "ui_models.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Native Phase-3 boundary: input is parsed metadata ONLY, on UI thread.
 * No URL, download job, license verification, file or network operation.
 * This does NOT depend on the paused Stage-4 Compatibility Engine. */
typedef struct UiSourceCandidate {
    char cusa[10];
    char content_id[37];
    char version[16];
    UiUpdateSourceKind source_kind;
} UiSourceCandidate;
/* Structurally valid only. ALL published updates are UNKNOWN, never
 * compatible, installable, trusted or downloadable. Entirely transactional. */
int ui_source_publish_candidates(const UiSourceCandidate *rows,unsigned count);
#ifdef __cplusplus
}
#endif
#endif
