/* Bounded read-only metadata snapshot; NOT an installed-games database. */
#ifndef PS4_UI_SNAPSHOT_H
#define PS4_UI_SNAPSHOT_H
#ifdef __cplusplus
extern "C" {
#endif

#define UI_SNAPSHOT_LIMIT 256u
typedef struct UiSnapshotGame {
    char cusa[10];
    char title[192];
    char version[20];
    char category[12];
} UiSnapshotGame;

/* 1 = valid snapshot (including empty), 0 = absent, -1 = invalid / IO error.
 * On error: *count=0 and out[] is unchanged.
 * Never use cache records as compatibility or installation proof. */
int ui_snapshot_load(const char *path, UiSnapshotGame *out,
                     unsigned capacity, unsigned *count);
/* 0 = atomically renamed to path, negative = failure.
 * Writes only app-owned cache path supplied by caller, not game files. */
int ui_snapshot_save(const char *path, const UiSnapshotGame *rows,
                     unsigned count);

/* Fail-closed cache replacement policy:
 *  - a complete, verified scan may replace an existing snapshot;
 *  - partial internal metadata may seed a cache only if no valid snapshot
 *    exists (missing/corrupt) and at least one base game's SFO is valid;
 *  - an incomplete scan must never erase a previous complete library.
 *  A valid empty snapshot counts as existing and is also preserved.
 *  Returns 1 if saving is allowed, 0 if the old snapshot must be kept. */
int ui_snapshot_should_save(int complete_scan,
                            int verified_internal_base_metadata,
                            int valid_previous_snapshot);

#ifdef __cplusplus
}
#endif
#endif
