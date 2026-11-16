/* Verify a whole PNG's chunk CRCs before showing an FTP-transferred cover.
 * FTP text mode may CRLF-expand binary data, so accept recovery only when
 * CRCs of all PNG chunks subsequently validate. No decode, file or network. */
#ifndef PS4_UI_PNG_TRANSPORT_H
#define PS4_UI_PNG_TRANSPORT_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Returns 1 for a structurally valid PNG with intact per-chunk CRCs. */
int ui_png_crc_valid(const unsigned char *buf,size_t size);
/* Mutable bounded buffer. 0=original verified, 1=CRC-verified CRLF
 * recovery, negative=unsupported/corrupt. No unverified bytes pass. */
int ui_png_ftp_validate(unsigned char *buf,size_t *size);
#ifdef __cplusplus
}
#endif
#endif
