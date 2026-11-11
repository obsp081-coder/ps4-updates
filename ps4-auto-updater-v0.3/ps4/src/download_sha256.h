#ifndef PS4_DOWNLOAD_SHA256_H
#define PS4_DOWNLOAD_SHA256_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* SHA-256 (FIPS 180-4), standalone C99 implementation for testing a
 * stored, independently authorized fixture's integrity. No filesystem,
 * networking, PS4 API or update authorization is performed here.
 * SHA-256 alone does not prove a PKG's origin or authenticity. */
#define DS_SHA256_DIGEST_BYTES 32u
#define DS_SHA256_BLOCK_BYTES 64u

typedef struct DsSha256 {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[DS_SHA256_BLOCK_BYTES];
    size_t buffered;
    unsigned failed;
} DsSha256;

void ds_sha256_init(DsSha256 *ctx);
/* 1 on success, 0 on invalid arguments, finalized state, or byte overflow. */
int ds_sha256_update(DsSha256 *ctx,const uint8_t *data,size_t count);
int ds_sha256_final(DsSha256 *ctx,uint8_t out[DS_SHA256_DIGEST_BYTES]);
/* Fixed-length byte comparison, returns 1 when equal. */
int ds_sha256_digest_equal(const uint8_t a[32],const uint8_t b[32]);

/* Host tests provide a read-only file backend. Production backend MUST open
 * only an app-owned, symlink-safe, immutable file; protect it from changes
 * between hashing and exclusive publication. A successful close is required.
 * read_next returns 0 for success; got==0 is EOF; got must be <= capacity.
 */
typedef struct DsSha256Reader {
    void *context;
    int (*open_read)(void *,const char *part_leaf);
    int (*read_next)(void *,uint8_t *data,size_t capacity,size_t *got);
    int (*close_read)(void *);
} DsSha256Reader;

/* Verifies actual bytes of the stored file: exact expected length + SHA256.
 * Returns 1 only for a clean read, clean close, exact byte count and digest.
 * No existing file is written, renamed or deleted. */
int ds_sha256_verify_stored(const DsSha256Reader *reader,
                            const char *part_leaf,uint64_t expected_bytes,
                            const uint8_t expected_digest[32]);

#ifdef __cplusplus
}
#endif
#endif
