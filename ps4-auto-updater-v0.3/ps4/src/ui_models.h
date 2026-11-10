#ifndef PS4_UI_MODELS_H
#define PS4_UI_MODELS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define UI_MODEL_MAX_UPDATES 24u
#define UI_MODEL_MAX_JOBS 16u
/* Display metadata only. No value in these structs can authorize an
 * installer or downloader: all side effects live outside this module. */
typedef enum UiUpdateVerdict {
    UI_UPDATE_UNKNOWN=0,
    UI_UPDATE_BLOCKED=1,
    UI_UPDATE_VERIFIED=2
} UiUpdateVerdict;
/* Source kind is provenance, not authorization or compatibility. */
typedef enum UiUpdateSourceKind {
    UI_SOURCE_UNSPECIFIED=0, UI_SOURCE_RETAIL_HTTP=1,
    UI_SOURCE_FPKG_SUBMITTED=2
} UiUpdateSourceKind;
typedef struct UiUpdateInfo {
    char cusa[10];
    char version[16];
    char minimum_fw[16];
    uint64_t size_bytes; /* zero = not known */
    UiUpdateVerdict verdict;
    UiUpdateSourceKind source_kind;
    char content_id[37];
    /* Candidate-only explanation. No bitmask ever authorizes an action.
     * Blockers are mutually exclusive with UNKNOWN verdict; missing proof
     * is retained in unknowns for audit/Details. */
    uint32_t compat_blockers;
    uint32_t compat_unknowns;
} UiUpdateInfo;
typedef enum UiJobStatus {
    UI_JOB_QUEUED=0,
    UI_JOB_DOWNLOADING=1,
    UI_JOB_PAUSED=2,
    UI_JOB_COMPLETED=3,
    UI_JOB_FAILED=4,
    UI_JOB_BLOCKED=5
} UiJobStatus;
typedef struct UiDownloadInfo {
    char cusa[10];
    char title[128];
    char version[16];
    uint64_t bytes_done;
    uint64_t bytes_total; /* zero = not known */
    UiJobStatus status;
} UiDownloadInfo;
void ui_model_reset(void);
/* Transactional copy: bad input cannot replace the previous valid list.
 * No caller-owned pointer is retained, and no heap/files/network are used. */
int ui_model_set_updates(const UiUpdateInfo *rows,unsigned count);
int ui_model_set_downloads(const UiDownloadInfo *rows,unsigned count);
unsigned ui_model_update_count(void);
unsigned ui_model_download_count(void);
const UiUpdateInfo *ui_model_update(unsigned index);
const UiDownloadInfo *ui_model_download(unsigned index);
/* Read-only list filtering. Returns the i-th matching update or NULL. */
const UiUpdateInfo *ui_model_update_for(const char *cusa,unsigned index);
#ifdef __cplusplus
}
#endif
#endif
