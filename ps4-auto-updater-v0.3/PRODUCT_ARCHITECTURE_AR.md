# PS4 Game Update Manager — Product Architecture and Release Gates

**Decision: MODIFY. This draft is not ready for distribution.** Target is PS4 Firmware 14.00 + GoldHEN; firmware-specific APIs must be confirmed individually on real hardware. Never promise zero bugs or general FPKG compatibility.

## Hardware evidence, 8 Nov 2026
- v0.6.5 / PKG 01.14: 9 SFOs recovered and validated by CUSA/Content ID; 4 base-game records; 5 additional category records. Native UI opens, Circle cleanly exits via SystemService (user-tested).
- Old AppInstUtil direct init likely crashed firmware 14.00; keep disabled. Any installed state remains UNKNOWN.
- External LIST returned FTP 550: incomplete scan, not empty or missing library. The VideoOut buffer unregister returned 0x80290009 (RESOURCE_BUSY), so direct memory was left for the OS to reclaim upon process exit.
- CI host simulation and OpenOrbis PKG builds do not prove Download/Install functionality.

## Proposed modules
1. **Platform:** OpenOrbis VideoOut/Pad/SystemService, error messages, module lifecycle, timeouts.
2. **GameScanner:** read-only appmeta and validated app.db / safe installation lookup, internal and external files; deduplicate identities and distinguish installation from metadata.
3. **Assets:** load local icon0.png, bounded PNG decoder, texture cache, fallback images, licensed UTF-8 font with Arabic RTL support.
4. **Sources:** two independent providers: official authorized Retail patch metadata and separately authorized FPKG metadata. Label untrusted/unauthenticated entries clearly.
5. **Compatibility:** Title ID, complete Content ID, base-game kind, patch kind, region when demonstrable, installed version, target version, minimum firmware, base-version constraints, verified rights and source integrity; DEFAULT DENY.
6. **Downloader:** explicit user action, TLS verification, permitted host policy, streaming to temporary file, size/quota limits, HTTP Range resume only after ETag validation, cancellation and integrity verification. No guessed progress.
7. **Installer:** verified and safe FW14 API only. Real status from system, one job at a time, free-space preflight, error recovery, no writing app.db or saved data directly.
8. **Native UI:** clear cards for games (icon/title/version/status) and patches (availability/size/fw/source), blockers with explanations, real progress and cancellation.
9. **Diagnostics:** never upload user inventory automatically, keep crash stage checkpoints, log rotation and error categories.

## Existing tools and scope
- ItemzFlow already supports RETAIL updates for original purchased discs/digital titles and has icons, UI and installation work. Build a replacement only if independent hardware evidence proves better compatibility screening and reliable errors.
- OrbisPatches Patch Installer handles older RETAIL versions; neither it nor Sony XML proves arbitrary FPKG compatibility.
- There is no verified comprehensive, licensed and reliably compatible FPKG catalog. Do not invent availability, links or package metadata. Distinguish Retail PKG and FPKG.
- Third-party code/art/fonts need license checks. OpenOrbis and ItemzFlow use GPLv3; direct reuse requires complying with applicable licenses.
- Do not download system firmware, or undermine system-update blocking.

## Release gates
1. **UI/scanner:** every displayed CUSA and local icon checked on PS4, proper Arabic/UTF-8, unknown and external failures visibly distinguished.
2. **Installed games:** independent read-only proof; SFO directories alone are not confirmation.
3. **Update discovery:** authorized source and list of versions, real size/firmware if available; offline/network failure handled.
4. **Package compatibility:** verified source/type/content ID/base/firmware, negative tests for wrong region, FPKG/retail mismatch and missing metadata.
5. **Download:** real PS4 network test with authorized small fixtures first, retry/resume/cancel, disk full, checksum and cleanup.
6. **Install:** isolated authorized test package installed safely using proved FW14 mechanism; genuine status and failure handling.
7. **Stability:** 0/1/50/256 titles, corrupted SFO, external disk, offline, low storage, unexpected shutdown/recovery, 100 repeated open/close cycles.
8. **Release:** sign-off evidence for each gate, fresh CI build and SHA256, no copyrighted game content bundled. Artifacts cannot be published before the gates pass.

## Decisions
- ACCEPT: stable read-only scanner, strict identity checks, separate metadata categories, native UI, diagnostics, robust explicit PS4 shell exit.
- MODIFY: VideoOut resource lifetime, app.db confirmation, catalog trust policy and icon/text rendering.
- DEFER: download/install until authorized source, FW14 installer interface and compatibility proven.
- REJECT: universal automatic FPKG updater with unverified sources; guarantee of zero bugs; release of an untested PKG.

GitHub draft: https://github.com/obsp081-coder/ps4-updates/pull/1
Primary sources: https://github.com/LightningMods/Itemzflow ; https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain ; https://github.com/GoldHEN/GoldHEN ; https://orbispatches.com/en/homebrew
