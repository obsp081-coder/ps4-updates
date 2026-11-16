#ifndef PS4_DOWNLOAD_FILE_BACKEND_H
#define PS4_DOWNLOAD_FILE_BACKEND_H
#include "download_storage.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Experimental platform adapter. Only the Host POSIX implementation is
 * active. Native PS4 deliberately fails closed: musl PS4 linkat/unlinkat
 * may return ENOSYS and no safe publish-if-absent path is verified.
 *
 * Directory descriptors are injected by a trusted caller, not arbitrary
 * network metadata. The adapter never creates directories or handles PKGs.
 */
typedef struct DsFileBackend {
    int directory_fd;
    int writer_fd;
    int reader_fd;
    uint64_t owned_device;
    uint64_t owned_inode;
    char owned_part[DS_NAME_CAP];
    unsigned owns_partial;
} DsFileBackend;

/* Returns 0 only when POSIX backend is enabled on Host and directory fd
 * is a writable directory owned by the current effective uid.
 * Never enabled by default on __ORBIS__ (returns -1).
 * Caller retains ownership of the supplied directory fd.
 */
int ds_file_backend_attach(DsFileBackend *backend,int trusted_directory_fd);
void ds_file_backend_detach(DsFileBackend *backend);
/* The returned callbacks are available only after successful attach.
 * Returns zeroed callbacks otherwise, forcing ds_begin to reject it. */
DsPort ds_file_backend_port(DsFileBackend *backend);

#ifdef __cplusplus
}
#endif
#endif
