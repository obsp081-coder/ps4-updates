#ifndef PS4_UI_SOURCE_BRIDGE_H
#define PS4_UI_SOURCE_BRIDGE_H
#include "ui_models.h"
#include "ui_compatibility.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Native candidate boundary: parsed metadata only, on UI thread.
 * No URL, download job, license verification, file or network operation.
 * The optional compatibility assessment never enables an action. */
typedef struct UiSourceCandidate {
    char cusa[10];
    char content_id[37];
    char version[16];
    UiUpdateSourceKind source_kind;
} UiSourceCandidate;
/* Structurally valid only. ALL published updates are UNKNOWN, never
 * compatible, installable, trusted or downloadable. Entirely transactional. */
int ui_source_publish_candidates(const UiSourceCandidate *rows,unsigned count);
/* Assessment is ALWAYS review-only. For each row, facts must bind to the
 * exact candidate CUSA/Content ID/version; mismatches reject the ENTIRE
 * batch without mutating the UI model. BLOCKED gets a reason mask;
 * REVIEW_ONLY and UNVERIFIED both remain UNKNOWN in the UI.
 * The facts can come only from trusted internal readers; caller claims
 * (such as source_authorized) never grant download/install permission. */
int ui_source_publish_reviews(const UiSourceCandidate *rows,
                              const UiCompatFacts *facts,unsigned count);
#ifdef __cplusplus
}
#endif
#endif
