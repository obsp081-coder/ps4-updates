#ifndef PS4_UI_COMPATIBILITY_H
#define PS4_UI_COMPATIBILITY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Candidate-only, read-only native compatibility review. These decisions
 * NEVER authorize downloading, installing or claiming FPKG compatibility. */
typedef enum UiCompatDecision {
    UI_COMPAT_BLOCKED=0, UI_COMPAT_UNVERIFIED=1, UI_COMPAT_REVIEW_ONLY=2
} UiCompatDecision;
typedef enum UiCompatKind {
    UI_COMPAT_KIND_UNKNOWN=0, UI_COMPAT_RETAIL=1, UI_COMPAT_FPKG=2
} UiCompatKind;
typedef enum UiCompatInstalled {
    UI_COMPAT_INSTALL_UNKNOWN=0, UI_COMPAT_INSTALL_YES=1,
    UI_COMPAT_INSTALL_NO=2
} UiCompatInstalled;
enum UiCompatReason {
    UI_COMPAT_BAD_TITLE=1u<<0, UI_COMPAT_TITLE_MISMATCH=1u<<1,
    UI_COMPAT_BAD_CID=1u<<2, UI_COMPAT_CID_TITLE_MISMATCH=1u<<3,
    UI_COMPAT_REGION_MISMATCH=1u<<4, UI_COMPAT_PUBLISHER_MISMATCH=1u<<5,
    UI_COMPAT_KIND_MISMATCH=1u<<6, UI_COMPAT_NOT_INSTALLED=1u<<7,
    UI_COMPAT_INVALID_VERSION=1u<<8, UI_COMPAT_NOT_NEWER=1u<<9,
    UI_COMPAT_BASE_MISMATCH=1u<<10, UI_COMPAT_FIRMWARE_TOO_LOW=1u<<11,
    UI_COMPAT_CID_UNKNOWN=1u<<12, UI_COMPAT_INSTALL_UNCONFIRMED=1u<<13,
    UI_COMPAT_KIND_UNCONFIRMED=1u<<14, UI_COMPAT_VERSION_UNKNOWN=1u<<15,
    UI_COMPAT_FIRMWARE_UNKNOWN=1u<<16, UI_COMPAT_BASE_REQUIRED_UNKNOWN=1u<<17,
    UI_COMPAT_BINDING_UNKNOWN=1u<<18, UI_COMPAT_SOURCE_UNKNOWN=1u<<19,
    UI_COMPAT_INTEGRITY_UNKNOWN=1u<<20, UI_COMPAT_METADATA_UNKNOWN=1u<<21,
    UI_COMPAT_SIZE_UNKNOWN=1u<<22,
    UI_COMPAT_RETAIL_UNSUPPORTED=1u<<23
};
typedef struct UiCompatFacts {
    const char *base_cusa, *patch_cusa;
    const char *base_content_id, *patch_content_id;
    const char *base_version, *patch_version, *required_base_version;
    const char *console_firmware, *minimum_firmware;
    uint64_t patch_size_bytes;
    UiCompatInstalled installed;
    UiCompatKind base_kind, patch_kind;
    unsigned base_metadata_verified;
    /* These caller claims do not themselves establish cryptographic proof. */
    unsigned identity_binding_verified, source_authorized, integrity_verified;
} UiCompatFacts;
typedef struct UiCompatAssessment {
    UiCompatDecision decision;
    uint32_t blockers, unknowns;
    unsigned download_allowed, install_allowed; /* both always zero */
} UiCompatAssessment;
UiCompatAssessment ui_compat_assess(const UiCompatFacts *facts);
const char *ui_compat_reason_key(uint32_t single_reason);
#ifdef __cplusplus
}
#endif
#endif
