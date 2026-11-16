# إثباتات PS4 Firmware 14.00 — الجولة الأولى من Diagnostic
**التاريخ:** 11 نوفمبر 2026
**المنصة:** PS4 مع GoldHEN FTP Server v2.2 — معلومات مصدرها ملفات Debug رفعها المستخدم بعد تشغيل التطبيق.
**البرنامج:** Diagnostic مستقل BREW01998 / FW14 Header Probe — Read Only
**الحالة:** PARTIALLY VERIFIED / NEEDS HARDWARE FOLLOW-UP

## Verified on real hardware (user log, not independent lab reproduction)

- Native UI launched and rendered, per user screenshot.
- Scanner initialized network with rc=0, connected to GoldHEN FTP loopback, obtained 220 welcome, used passive transfers, read and recovered nine `param.sfo` records successfully; `SFO invalid: 0`.
- Internal `LIST /system_data/priv/appmeta`: 9 candidate directories. Identified six metadata entries with base-game SFO categories (`gd` or `gdo`), and three update/app metadata records (`gp` or `gpe`). Note this does NOT confirm actual installed state or FPKG type.
- Local UI inventory saved: `SCAN_STAGE_INVENTORY_SAVE_RETURNED: 0`; six game metadata rows.
- `PS4_SHELL_EXIT_STATUS: 0`, `UI_SHUTDOWN_DONE: 1`; requests to shell exit were logged. No claim about how every later exit behaves.

## Failed or unverified on real hardware

- **PKG Header direct-open diagnostic failed**: `PKG_SELF_HEADER BREW01998 observation=OPEN_FAILED location=0 header_read=0` and all six `PKG_HEADER CUSA... observation=OPEN_FAILED location=0 header_read=0`.
  - Code attempted three read-only paths per ID: `/user/app/<ID>/app.pkg`, `/mnt/ext0/user/app/<ID>/app.pkg`, `/mnt/ext1/user/app/<ID>/app.pkg`.
  - The v1 probe reports only aggregate OPEN_FAILED; it does not capture `errno` for each open. **Cannot distinguish ENOENT, EACCES, unsupported flags, or missing location yet**.
  - This does **not** disprove FPKG support and does **not** prove a title is Retail.
- `LIST /system_data/priv/appmeta/external` returned FTP 550; `external_listing_available:0`, `scan_complete:false`. This may mean no external directory/mount, unsupported path or permission; it is not proof of inaccessible external storage in general.
- `AppInstUtil` deliberately skipped pending FW14 compatibility proof. All six have `installed_confirmed:false` and `package_kind:"unknown"`.
- Six optional `icon0.png` FTP reads failed with 550; no verified local cover art in that path during this run.
- Real download/installer/TLS and FPKG Patch matching remain disabled and untested.

## Decision

**MODIFY** — do not enable FPKG classification or download/install. Before making an additional PKG, gather the minimum safe file-presence evidence through existing GoldHEN FTP from the user's PC.

1. In existing FTP client connected to port `2121`, navigate **read-only** to `/user/app/BREW01998/`. Report whether the folder exists, and whether a regular file named `app.pkg` appears. Do **not** download, rename, delete or replace that file. A screenshot of directory listing (without personal addresses) is enough.
2. If missing, test read-only directory listing of `/user/app/` (no bulk download), and report the exact FTP `550` or visible directory status. Do not recursively scan contents.
3. If present via GoldHEN FTP but direct app `open` failed, next code revision should add errno per location and determine whether app sandbox differs from FTP server process before any privileged API path. Do not switch to root or alter firmware.
4. If absent even via FTP, determine the true installed-package location via verified system metadata or a better sourced API; do not brute-force system file paths.
5. Keep any revised diagnostic independent from normal app Title ID `BREW00197`; no uncontrolled overwrite/installer tests of game content.

## Relevant primary source

- Itemzflow inspects installed `/user/app/ITEM00001/app.pkg` through privileged loader code, but **that does not prove our homebrew has the same privileges**:
  https://github.com/LightningMods/Itemzflow/blob/main/itemz-loader/source/app.cpp

No private device addresses, FTP credentials, raw game Content IDs, full uploaded logs or owned PKG binaries have been committed to this report.
