# UltraScale Mobile — Multi-Game Integration Plan

Status: reconnaissance baseline; no universal injection or protected-file bypass is implemented.

## Target games and integration routes

| Priority | Game | First supported route | Current confidence / blocker |
|---|---|---|---|
| P0 | Geometry Dash Android 2.2081 ARM64 | Official Geode Android launcher + Geode SDK mod; add UltraScale controls to the game's OptionsLayer where supported, otherwise a native-styled submenu opened from OptionsLayer | Best-supported first vertical slice. Must build and test against the exact Android game/Geode versions. |
| P1 | Asphalt 8 (exact package, store variant and version TBD) | First inspect engine, graphics API, package metadata and official/community mod policy. Prefer documented game plugin/config route; otherwise only app-owner/debuggable or vendor-authorized graphics-layer integration | Unknown until exact build fingerprint and API are measured. No promise of production-app injection. |
| P2 | Beach Buggy Racing (exact package, version and edition TBD) | Same read-only reconnaissance; prefer documented extension/config route or a developer-controlled build | Unknown until exact build fingerprint and API are measured. |

"Geometry Dash types" means separately fingerprinting each installed Geometry Dash edition/version; a mod built for 2.2081 is not assumed compatible with other versions.

## Shared settings contract

The in-game settings order is fixed:

1. Render Scale
2. Upscaler
3. Upscale Sharpness
4. Upscale Preset
5. CAS enabled / CAS strength
6. Low Latency

The shared config schema should use versioned, validated values:
- renderScale: 0.50–1.00 initially; >1.00 only where the host renderer and memory budget explicitly support supersampling.
- upscaler: bilinear, nearest, fsr1_easu, fsr_lite, mwsr, sgsr1_mobile.
- upscaleSharpness: 0.0–1.0; only applied by algorithms that expose a valid sharpness control.
- preset: ultra_performance, performance, balanced, quality, ultra_quality, custom. Presets map to per-game tested render-scale defaults, not universal assumptions.
- casEnabled: boolean; casSharpness: 0.0–1.0.
- lowLatency: off, balanced, aggressive. Do not claim reduced input latency until frame pacing and input-to-present measurements confirm it.

Unsupported algorithms must be disabled or visibly marked unavailable for that game/API. Never show a setting as active if only its UI/config value changed.

## Toolchain inventory and appropriate boundaries

| Tool / library | Intended use | Not a solution for |
|---|---|---|
| Android NDK + CMake | Build ARM64 native GLES/Vulkan components and sample renderer | Crossing Android app sandbox boundaries |
| Android Graphics API Layers (GLES/Vulkan) | Documented debug/profile layer loading for eligible apps; inspect API lifecycle and resources | Forcing arbitrary non-debuggable production games to load foreign code |
| Shizuku UserService (shell UID 2000) | Apply documented per-app GPU debug-layer settings, query package/system metadata and launch activities | Root, arbitrary process memory access, private app-data access or universal injection |
| Geode SDK + Android launcher | Supported Geometry Dash mod loader, hooks, menus and game bindings | Modding Asphalt 8 or Beach Buggy Racing |
| TulipHook / Geode hooks | In-process hooks inside a Geode-loaded Geometry Dash mod | Standalone cross-process injector |
| ShadowHook / ByteHook | In-process PLT/inline hook backends for code already loaded in an authorized process | Bypassing target-app eligibility or sandbox policy |
| APK parser / manifest & DEX signature scan | Bounded read-only engine/version/API hints from accessible APK metadata | Extracting protected runtime data or proving the live renderer |
| RenderDoc / AGI / Perfetto (where supported) | Capture/profile owned, debuggable or authorized builds; verify GPU timings and render passes | Capturing protected production processes outside supported access |

## Android security boundary

- Android GLES external layers require the documented eligibility conditions (for example debuggable targets; check the current platform/API requirements).
- Vulkan external layers on production non-rooted devices are restricted to eligible targets, including apps that explicitly opt in using the documented manifest metadata. A Shizuku setting write does not grant that opt-in.
- Shell UID 2000 is not root and cannot read another app's private `/data/user/0/<package>` files.
- No exploit chains, privilege escalation, anti-cheat evasion, signature bypass, DRM bypass, or private-data extraction will be implemented.
- For player-accessible game files, use only files exposed through Android's Storage Access Framework, app-specific external storage, official mod/config folders, or files the game owner explicitly exports.

## Reconnaissance checklist for each exact game build

1. Record package name, versionName/versionCode, installer/source, target SDK, ABI, and APK split layout using package-manager metadata.
2. Inspect only accessible APK manifest/DEX metadata for engine signatures; classify findings as heuristic.
3. Determine actual GLES/Vulkan use from an authorized runtime layer or developer-provided diagnostics; APK library presence alone is not proof.
4. Record GPU/driver, Android build, resolution, refresh rate, frame pacing, thermal state and baseline frame-time percentiles.
5. Search official SDKs, mod loaders, developer statements and reputable community modding documentation for that exact game version.
6. Choose the least invasive supported route: official mod API/config > official loader > owner-authorized graphics layer > standalone external companion.
7. Run a vertical slice: load/attach proof, render-target ownership proof, actual scaled output, visual comparison, GPU time, stability and rollback.
8. Only then mark a capability supported.

## Acceptance gates

- **Discovery:** exact package/build identified; engine/API confidence documented.
- **Attachment:** runtime log proves the authorized loader/layer attached to the intended process.
- **Rendering:** a controlled test proves the selected scaler processed a game-owned render target; telemetry-only attachment does not pass.
- **Quality:** image comparison and text/UI integrity review at each scale/preset.
- **Performance:** median and p95/p99 frame time, dropped frames, thermals and memory measured against baseline.
- **Latency:** input-to-present/frame-queue evidence required before claiming Low Latency benefits.
- **Safety:** settings rollback, app launch fallback and no access to protected private data.

## Current repository baseline

The existing Android app already has:
- a sample GLES renderer with selectable spatial shader paths and independent CAS;
- pass-through GLES/Vulkan diagnostic layers;
- Shizuku-based configuration of Android's documented debug-layer settings;
- APK metadata inspection and a shader source catalog.

The external layers are diagnostic/pass-through only. The sample renderer's upscaling is not proof that any third-party game is being upscaled. The next implementation milestone is the Geometry Dash 2.2081 Geode vertical slice, followed by read-only fingerprints for the exact Asphalt 8 and Beach Buggy Racing installs.


## GameRecon implementation status (2026-11-22)

A first read-only Kotlin scanner has been added at
`project/app/src/main/java/dev/ultrascale/mobile/GameRecon.kt`.

It supports two discovery paths:

- **Installed APKs:** enumerates installed game-flagged apps and the known target titles, reads package/version/installer metadata, enumerates base and split APK paths, and fingerprints native libraries and public APK entries.
- **User-selected APK:** inspects an APK selected through the Storage Access Framework, without requiring broad filesystem access. Split APKs must be selected/analysed as a set for complete coverage.

The report distinguishes Google Play installer metadata (`com.android.vending`) from sideloaded/unknown installers. This is evidence about the installer recorded by Android, not proof of the APK's provenance or integrity.

Engine and asset findings are heuristic fingerprints, not definitive engine/API detection. Presence of a Vulkan/GLES-related library does not prove that the running game uses that API. The scanner does not inspect private app data, OBB contents, process memory, or protected runtime state.

**Not yet complete:** this scanner is a source-level module and is not yet wired into the Compose screen; no APK build or on-device test is claimed. Next step is to connect its report model to the installed-games UI, add an APK picker, then build and validate on the target Android device.
