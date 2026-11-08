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

#ifdef __cplusplus
}
#endif
#endif
