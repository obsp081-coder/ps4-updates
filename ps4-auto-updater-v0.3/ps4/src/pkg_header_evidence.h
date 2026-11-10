#ifndef PS4_PKG_HEADER_EVIDENCE_H
#define PS4_PKG_HEADER_EVIDENCE_H

#include <stddef.h>
#include <stdint.h>

/* PS4 PKG header bytes are an untrusted HINT, not authenticated evidence.
 * We do not verify package signatures, FPKG rights, or base/patch binding.
 * The offsets/patterns are based on observed publicly documented PKG
 * headers (e.g. ItemzFlow's installed-app.pkg classification), NOT on
 * a successful test on PS4 FW14. No code from that GPLv3 project is used.
 *
 * This parser never performs I/O. A caller may read EXACTLY 8 bytes from
 * a locally owned, seekable installed app.pkg in diagnostic-only mode.
 */
typedef enum PkgHeaderObservation {
    PKG_OBS_NOT_PROBED = 0,
    PKG_OBS_OPEN_FAILED,
    PKG_OBS_NOFOLLOW_UNAVAILABLE,
    PKG_OBS_NOT_REGULAR,
    PKG_OBS_SHORT_READ,
    PKG_OBS_BAD_MAGIC,
    PKG_OBS_FAKE_FLAG_CANDIDATE,
    PKG_OBS_NONFAKE_FLAG_CANDIDATE,
    PKG_OBS_OTHER_FLAGS
} PkgHeaderObservation;

typedef struct PkgHeaderEvidence {
    PkgHeaderObservation observation;
    uint32_t flags_be;
    unsigned header_read;
} PkgHeaderEvidence;

static inline PkgHeaderEvidence pkg_header_observe(
    const unsigned char *data, size_t length
) {
    PkgHeaderEvidence evidence = {PKG_OBS_SHORT_READ, 0u, 0u};
    if (!data || length != 8u)
        return evidence;
    if (data[0] != 0x7fu || data[1] != 'C' ||
        data[2] != 'N' || data[3] != 'T') {
        evidence.observation = PKG_OBS_BAD_MAGIC;
        return evidence;
    }
    evidence.header_read = 1u;
    evidence.flags_be =
        ((uint32_t)data[4] << 24) |
        ((uint32_t)data[5] << 16) |
        ((uint32_t)data[6] << 8) |
        (uint32_t)data[7];

    /* Exact patterns only. 0x00000001 is a *candidate* fake-package
     * flag pattern; 0x83000001 and 0x81000001 are non-fake candidates.
     * Every other pattern stays unknown; no bitmask heuristic or
     * certificate/PKG authenticity claim is allowed. */
    if (evidence.flags_be == 0x00000001u)
        evidence.observation = PKG_OBS_FAKE_FLAG_CANDIDATE;
    else if (evidence.flags_be == 0x83000001u ||
             evidence.flags_be == 0x81000001u)
        evidence.observation = PKG_OBS_NONFAKE_FLAG_CANDIDATE;
    else
        evidence.observation = PKG_OBS_OTHER_FLAGS;
    return evidence;
}

static inline const char *pkg_header_observation_name(
    PkgHeaderObservation value
) {
    switch(value) {
        case PKG_OBS_NOT_PROBED: return "NOT_PROBED";
        case PKG_OBS_OPEN_FAILED: return "OPEN_FAILED";
        case PKG_OBS_NOFOLLOW_UNAVAILABLE: return "NOFOLLOW_UNAVAILABLE";
        case PKG_OBS_NOT_REGULAR: return "NOT_REGULAR";
        case PKG_OBS_SHORT_READ: return "SHORT_READ";
        case PKG_OBS_BAD_MAGIC: return "BAD_MAGIC";
        case PKG_OBS_FAKE_FLAG_CANDIDATE: return "FAKE_FLAG_CANDIDATE";
        case PKG_OBS_NONFAKE_FLAG_CANDIDATE: return "NONFAKE_FLAG_CANDIDATE";
        case PKG_OBS_OTHER_FLAGS: return "OTHER_FLAGS";
        default: return "UNKNOWN";
    }
}

#endif
