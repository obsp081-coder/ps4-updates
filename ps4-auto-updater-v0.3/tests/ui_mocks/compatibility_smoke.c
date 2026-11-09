/* Offline-only unit test for the candidate compatibility assessor.
 * This deliberately cannot prove authenticity, installation or PS4 behavior. */
#include "../../ps4/src/ui_compatibility.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\\n", __LINE__, #expr); \
    return 1; \
} } while (0)

static UiCompatFacts example(void) {
    UiCompatFacts f = {0};
    f.base_cusa = "CUSA02343";
    f.patch_cusa = "CUSA02343";
    f.base_content_id = "IV0000-CUSA02343_00-SCANNER000000000";
    f.patch_content_id = "IV0000-CUSA02343_00-SCANNER000000000";
    f.base_version = "01.00";
    f.patch_version = "01.05";
    f.required_base_version = "01.00";
    f.console_firmware = "14.00";
    f.minimum_firmware = "09.00";
    f.patch_size_bytes = 4096;
    f.installed = UI_COMPAT_INSTALL_YES;
    f.base_kind = UI_COMPAT_FPKG;
    f.patch_kind = UI_COMPAT_FPKG;
    f.base_metadata_verified = 1;
    f.identity_binding_verified = 1;
    f.source_authorized = 1;
    f.integrity_verified = 1;
    return f;
}

static int forbidden(UiCompatAssessment a) {
    return a.download_allowed == 0 && a.install_allowed == 0;
}

int main(void) {
    UiCompatAssessment a = ui_compat_assess(NULL);
    CHECK(a.decision == UI_COMPAT_BLOCKED);
    CHECK(a.blockers & UI_COMPAT_BAD_TITLE);
    CHECK(forbidden(a));

    UiCompatFacts f = example();
    a = ui_compat_assess(&f);
    /* REVIEW_ONLY is NOT an install or compatibility authorization. */
    CHECK(a.decision == UI_COMPAT_REVIEW_ONLY);
    CHECK(a.blockers == 0 && a.unknowns == 0);
    CHECK(forbidden(a));

    f = example();
    f.patch_cusa = "CUSA00411";
    a = ui_compat_assess(&f);
    CHECK(a.decision == UI_COMPAT_BLOCKED);
    CHECK(a.blockers & UI_COMPAT_TITLE_MISMATCH);
    CHECK(forbidden(a));

    f = example();
    f.patch_content_id = "IV0000-CUSA00411_00-SCANNER000000000";
    a = ui_compat_assess(&f);
    CHECK(a.blockers & UI_COMPAT_CID_TITLE_MISMATCH);
    CHECK(forbidden(a));

    f = example();
    f.patch_content_id = "EP0000-CUSA02343_00-SCANNER000000000";
    a = ui_compat_assess(&f);
    CHECK(a.blockers & UI_COMPAT_REGION_MISMATCH);
    CHECK(forbidden(a));

    f = example();
    f.base_kind = UI_COMPAT_RETAIL;
    a = ui_compat_assess(&f);
    CHECK(a.blockers & UI_COMPAT_KIND_MISMATCH);
    CHECK(forbidden(a));

    f = example();
    f.patch_version = "00.99";
    a = ui_compat_assess(&f);
    CHECK(a.blockers & UI_COMPAT_NOT_NEWER);
    CHECK(forbidden(a));

    f = example();
    f.required_base_version = "01.02";
    a = ui_compat_assess(&f);
    CHECK(a.blockers & UI_COMPAT_BASE_MISMATCH);
    CHECK(forbidden(a));

    f = example();
    f.minimum_firmware = "15.00";
    a = ui_compat_assess(&f);
    CHECK(a.blockers & UI_COMPAT_FIRMWARE_TOO_LOW);
    CHECK(forbidden(a));

    f = example();
    f.installed = UI_COMPAT_INSTALL_UNKNOWN;
    a = ui_compat_assess(&f);
    CHECK(a.decision == UI_COMPAT_UNVERIFIED);
    CHECK(a.unknowns & UI_COMPAT_INSTALL_UNCONFIRMED);
    CHECK(forbidden(a));

    f = example();
    f.installed = UI_COMPAT_INSTALL_NO;
    a = ui_compat_assess(&f);
    CHECK(a.blockers & UI_COMPAT_NOT_INSTALLED);
    CHECK(forbidden(a));

    f = example();
    f.base_kind = UI_COMPAT_KIND_UNKNOWN;
    a = ui_compat_assess(&f);
    CHECK(a.unknowns & UI_COMPAT_KIND_UNCONFIRMED);
    CHECK(forbidden(a));

    f = example();
    f.source_authorized = 0;
    a = ui_compat_assess(&f);
    CHECK(a.unknowns & UI_COMPAT_SOURCE_UNKNOWN);
    CHECK(forbidden(a));

    f = example();
    f.integrity_verified = 0;
    a = ui_compat_assess(&f);
    CHECK(a.unknowns & UI_COMPAT_INTEGRITY_UNKNOWN);
    CHECK(forbidden(a));

    f = example();
    f.patch_content_id = "IV0000-CUSA02343_00-SCANNER000000001";
    f.identity_binding_verified = 0;
    a = ui_compat_assess(&f);
    CHECK(a.unknowns & UI_COMPAT_BINDING_UNKNOWN);
    CHECK(forbidden(a));

    f = example();
    f.console_firmware = NULL;
    a = ui_compat_assess(&f);
    CHECK(a.unknowns & UI_COMPAT_FIRMWARE_UNKNOWN);
    CHECK(forbidden(a));

    f = example();
    f.patch_size_bytes = 0;
    a = ui_compat_assess(&f);
    CHECK(a.unknowns & UI_COMPAT_SIZE_UNKNOWN);
    CHECK(forbidden(a));

    f = example();
    f.base_version = "bad version";
    a = ui_compat_assess(&f);
    CHECK(a.blockers & UI_COMPAT_INVALID_VERSION);
    CHECK(forbidden(a));

    CHECK(strcmp(ui_compat_reason_key(UI_COMPAT_KIND_MISMATCH),
                 "RETAIL_FPKG_MISMATCH") == 0);
    puts("UI_COMPATIBILITY_HOST_OK");
    return 0;
}
