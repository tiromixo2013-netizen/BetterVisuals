# UltraScale Mobile game adapter matrix

This document is the source of truth for game-specific adapter registration. A game name or package match is not proof that a graphics layer can be loaded.

## Supported priority targets

| Priority | Game | Package identity | Version pin | ABI | Adapter state |
|---|---|---|---|---|---|
| P0 | Asphalt 8 | `com.gameloft.android.ANMP.GloftA8HM` | `9.1.0i (91009)` observed from the user's selected APK | Not established; the scan reported ABI `none` | Identity observed; no tested adapter |
| P0 | Geometry Dash | `com.robtopx.geometryjump` (candidate; verify against the selected APK) | Android `2.2081` is the initial research target, not yet verified from a supplied APK | ARM64 is the initial target, not yet verified from a supplied APK | Research target; no tested adapter |
| P0 | Beach Buggy Racing | `com.vectorunit.purple.googleplay` (public listing; still verify selected APK) | Public listing reports `2026.11.02 (220)`; not verified from user's installed APK | Public package variants include `arm64-v8a` and `armeabi-v7a`; installed variant not established | Research target; no tested adapter |

Package IDs marked candidate must be confirmed from the APK manifest before registration. Store variants, regional variants, and sequel packages must not be silently treated as equivalent.

## Adapter contract

Each game adapter must declare:

- Exact package name and an explicit list of tested version codes.
- Supported ABIs and required native library fingerprints.
- Detected graphics API evidence, distinguishing static APK hints from runtime-confirmed API use.
- Engine-specific integration route and its validation status.
- Whether the route can only inspect, can configure a documented Android layer opt-in, or can execute a tested render integration.
- Known incompatibilities and a safe fallback.

An adapter must fail closed when package, version, ABI, library fingerprint, or required Android layer-loading eligibility does not match. Never select an adapter using only a display name or a loose package prefix.

## Registration states

- `IDENTITY_ONLY`: APK identity was read; no game integration is available.
- `RESEARCH_ONLY`: candidate package/version is known, but no exact APK and runtime evidence have been validated.
- `ELIGIBILITY_BLOCKED`: Android's documented layer-loading requirements are not met.
- `INTEGRATION_TESTED`: exact package/version/ABI and the relevant runtime route were tested.
- `RENDER_PASS_TESTED`: the game's actual render path was modified and output correctness/performance were validated.

Only `INTEGRATION_TESTED` or `RENDER_PASS_TESTED` may be described as a working adapter. A successful APK scan, successful build, or Shizuku shell permission is not an injection test.

## Android security boundary

Shizuku shell access does not grant root and does not bypass Android's graphics-layer eligibility checks. A manager app cannot grant another app the manifest opt-in by changing its own manifest. Do not patch, re-sign, or claim to inject a target APK unless that operation is actually implemented and validated. Prefer the documented opt-in/debuggable path where the target app itself supports it; otherwise report the limitation and keep the target unchanged.

## Required validation before enabling Inject

1. Parse the selected APK's manifest and record package/version.
2. Enumerate every APK split and native library; do not infer ABI from a base APK when the native libraries may be in splits or downloaded assets.
3. Fingerprint relevant engine and graphics libraries.
4. Resolve runtime API evidence separately from static heuristics.
5. Check Android layer-loading eligibility for the installed target.
6. Require an exact adapter match and a tested integration route.
7. Keep the action disabled with a specific reason when any gate fails.

## Per-game next evidence

- Asphalt 8: obtain the installed app's complete split set (or a complete APK set), then inspect native libraries and engine/API fingerprints for version code 91009.
- Geometry Dash: verify the exact package, version code, ABI, and native library set from the target APK before developing an engine-specific route.
- Beach Buggy Racing: verify the exact package and whether the APK is the original game or another Vector Unit title; then pin version and ABI before adapter registration.


## Research notes (external references; not target validation)

- Geometry Dash: a public Android native-mod example documents the package class path `com/robtopx/geometryjump/GeometryJump`, a `cocos2dcpp` library load, and explicitly limits its procedure to Android v2.2081 / arm64-v8a. This is a useful investigation lead for identifying the game's native engine boundary, not proof that UltraScale's graphics layer can be loaded or that the same APK variant is installed. Reference: https://github.com/ItzZyann/gd-native-mod-android
- Beach Buggy Racing: public listings identify package `com.vectorunit.purple.googleplay`; recent published builds include version `2026.11.02 (220)`, with architecture-specific split variants including ARM64 and ARM32. A base APK scan alone may therefore miss the ABI-bearing split. Reference: https://www.apkmirror.com/apk/vector-unit/beach-buggy-racing/
- Beach Buggy Racing is reported as using Vector Unit's custom C++ engine in a third-party compatibility inventory. Treat this only as a hypothesis until the selected APK's libraries and runtime are inspected. Reference: https://digitalisx64.github.io/apps/android17/

These public references guide investigation only. They do not replace inspection of the exact APK installed on the device and do not upgrade any adapter's registration state.


## Public APK acquisition leads (checked 2026-11-22)

These are leads for obtaining test inputs, not files downloaded or verified by this repository.

| Game | Public source lead | Reported package/version/ABI | Integrity note |
|---|---|---|---|
| Asphalt 8 | APKFab 9.1.0i XAPK listing: https://apkfab.com/asphalt-8-airborne-fun-real-car-racing-game/com.gameloft.android.ANMP.GloftA8HM | Package matches; version 9.1.0i (91009); ARM64 variant listed with split_config.arm64_v8a and config.hdpi | Listing reports SHA-1 8ddd8006c7487040163a6a0edfe3004d85d2f2ee. The user's scan screenshot showed a different SHA-256, so this is not established as the same binary. |
| Geometry Dash | Official Google Play listing: https://play.google.com/store/apps/details?id=com.robtopx.geometryjump ; third-party 2.2081 listing: https://batur-does-gaming.itch.io/gd-22081 | Official package com.robtopx.geometryjump; Google Play page reports a newer 2.209 line. The 2.2081 itch.io file is reported as 171 MB but is user-uploaded. | Do not use the user-uploaded 2.2081 binary as a trusted test fixture without signature and hash verification. Prefer a user-provided copy obtained from their legitimate purchase or a trusted archival source. |
| Beach Buggy Racing | APKMirror 2026.11.02 bundle: https://www.apkmirror.com/apk/vector-unit/beach-buggy-racing/beach-buggy-racing-2026-11-02-release/beach-buggy-racing-2026-11-02-android-apk-download/ | Package com.vectorunit.purple.googleplay; version 2026.11.02 (220); bundle reports Base APK + 37 splits and arm64-v8a among supported ABIs | Bundle metadata is a useful ARM64 investigation lead; actual downloaded files and signatures still need local verification. |

No APK/XAPK/APKS binary was downloaded or inspected in this update. Do not register any game adapter based solely on these listing pages.


## Fresh source check (2026-11-22)

- Asphalt 8 APKMirror upload index currently lists both 9.2.0i (15 Oct 2026, 141.32 MB) and the requested 9.1.0i (11 Aug 2026, 138.90 MB). The index is a listing, not the binary itself: https://www.apkmirror.com/uploads/?appcategory=asphalt-8-airborne . Do not assume its 9.1.0i package hash matches the user's selected APK.
- Beach Buggy Racing APKMirror release page gives a bundle-level SHA-256 of `b02331be24bb5297b0c2f198428788c740662601162eb4823f385de8d93d9480`, signing certificate SHA-256 `66b46a40a5647e9e514daaad03eb9467601c7329414f6a146db485a86ea4d124`, and an arm64-v8a split of 17.63 MB. This gives concrete expected values for later local verification: https://www.apkmirror.com/apk/vector-unit/beach-buggy-racing/beach-buggy-racing-2026-11-02-release/beach-buggy-racing-2026-11-02-android-apk-download/
- Geometry Dash's official Android version has advanced to 2.209 / 2.2091 by October 2026; 2.2081 is now an older target. The 2.2081 itch.io upload is user-posted and its own comments raise malware concerns; do not treat it as a trusted binary. The official Geode project states compatibility with 2.2081, and the native-mod template documents the arm64-only `cocos2dcpp`/Java load boundary, but neither is an official 2.2081 APK mirror. Use a legitimately acquired APK and verify signing certificate/hash before analysis.
- This update records source metadata only. No third-party game APK binary has been downloaded, extracted, or cryptographically verified in the UltraScale build environment.
