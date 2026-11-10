#ifndef PS4_DOWNLOAD_STORAGE_H
#define PS4_DOWNLOAD_STORAGE_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stage 5 storage contract, portable C99 and platform-independent.
 * NO filesystem syscalls, socket calls, PS4 permissions, or PKG install.
 * Callbacks must be implemented by an independently verified backend.
 * The host test backend uses synthetic fixture bytes only.
 */
#define DS_NAME_CAP 64u
#define DS_WRITE_CAP 65536u

typedef enum DsState {
    DS_EMPTY=0,
    DS_WRITING=1,
    DS_SEALED=2,
    DS_PUBLISHED=3,
    DS_FAILED=4,
    DS_ABORTED=5
} DsState;

typedef enum DsResult {
    DS_OK=0,
    DS_INVALID=-1,
    DS_BAD_STATE=-2,
    DS_OPEN_FAILED=-3,
    DS_WRITE_FAILED=-4,
    DS_SIZE_EXCEEDED=-5,
    DS_FLUSH_FAILED=-6,
    DS_VERIFY_FAILED=-7,
    DS_PUBLISH_FAILED=-8,
    DS_CLEANUP_FAILED=-9
} DsResult;

/* Callbacks must be confined to an app-owned directory. Every leaf name
 * comes from a validated Content ID and version; callbacks must reject
 * symlinks/path escapes and MUST NEVER overwrite a pre-existing path.
 *
 * create_exclusive must be atomic: success means this session owns .part;
 * failure means it has not created a file or left an open descriptor.
 * write_all returns 0 only when ALL bytes were written. A failed write
 * may still have written bytes; the session will enter FAILED and reject
 * finalization. flush/close are distinct, and close MUST release the fd.
 * verify_sha256 must hash the STORED file, not an untrusted header/buffer.
 * publish_exclusive must atomically create the final file ONLY IF ABSENT.
 * unlink_part may delete ONLY the .part leaf previously created by this
 * session; it must never delete any final file.
 */
typedef struct DsPort {
    void *context;
    int (*create_exclusive)(void *,const char *part_leaf);
    int (*write_all)(void *,const uint8_t *bytes,size_t count);
    int (*flush)(void *);
    int (*close)(void *);
    int (*verify_sha256)(void *,const char *part_leaf,
                         const uint8_t expected_digest[32]);
    int (*publish_exclusive)(void *,const char *part_leaf,
                             const char *final_leaf);
    int (*unlink_part)(void *,const char *part_leaf);
} DsPort;

typedef struct DsSession {
    DsPort port;
    char part_leaf[DS_NAME_CAP];
    char final_leaf[DS_NAME_CAP];
    uint64_t expected_size;
    uint64_t written_size;
    uint8_t expected_sha256[32];
    DsState state;
    unsigned owns_part;
    unsigned file_open;
} DsSession;

void ds_init(DsSession *session);
DsResult ds_begin(DsSession *session,const DsPort *port,
                  const char *content_id,const char *version,
                  uint64_t expected_size,
                  const uint8_t expected_sha256[32]);
DsResult ds_write(DsSession *session,const uint8_t *bytes,size_t count);
/* Requires a full byte count; calls flush, close, stored-file SHA-256
 * verification and exclusive publication, in that order. */
DsResult ds_finalize(DsSession *session);
/* Only cleans up a partial file the session created. If final is already
 * published, cleanup may remove a leftover owned .part, NEVER final. */
DsResult ds_abort(DsSession *session);

#ifdef __cplusplus
}
#endif
#endif
