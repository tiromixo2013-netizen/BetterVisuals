# Asphalt 8 UltraScale integration status

## What the inspected build establishes

The acquired Asphalt 8 Android build is version 9.2.0i (versionCode 92009), ARM64, and its inspected native game library links against EGL/OpenGL ES. The inspection does not establish that Android will load a third-party graphics layer into this production package, nor does it expose the game's internal render targets or frame submission queue.

## Settings model added to the UltraScale manager

The manager now stores a local profile with:
- Render scale (50–100%)
- Upscaler selection (Off, nearest, bilinear, Catmull-Rom, FSR Lite, FSR1 EASU, MWSR, SGSR1, SGSR2)
- Reconstruction sharpness
- CAS-style post-upscale sharpening
- Integer scaling toggle
- Low-latency mode
- Presets: Ultra Performance, Performance, Balanced, Quality, Ultra Quality, Custom

These are profile controls and not proof that Asphalt 8 consumes the settings. The current Asphalt 8 adapter remains config-only for existing keys such as scaleDisplay; no undocumented graphics keys are written.

## Frame Generation decision

Frame Generation is deliberately not exposed as an active switch for Asphalt 8. The inspected build and current adapter provide no supported motion vectors, depth, per-frame history, jitter data, or a controllable present queue. A generic post-process frame interpolator without those inputs would be an unvalidated optical-flow approximation, not a reliable game Frame Generation implementation, and could add latency or visual artifacts. Revisit only after a game-owned render hook can provide the required inputs and the result is validated on-device.

## Activation boundary

The new settings card persists values inside the UltraScale manager. It does not inject code into Asphalt 8, alter the official APK signature, or modify the game's own settings screen. Applying these options to the game requires a permitted, version-tested render-pass adapter and a successful device test.


## 2026-11-23 acquisition result

The APK acquisition and read-only inspection workflow completed successfully. It produced XAPK package `com.gameloft.android.ANMP.GloftA8HM`, version `9.2.0i` (92009), plus base/ARM64/HDPI split APKs and inspection reports. The embedded base APK contains `assets/releaseminigame/gameoptions.json` with ColorCorrection, CPU, GPU, MEM and Specific profiles. Existing controls include `GFXOption`, `FPSLimit`, shadow/post-FX toggles, texture filtering/anisotropy, memory budgets, and `Specific.scaleDisplay` / `scaleAABuffer` overrides. Strings in the ARM64 `libAsphalt8.so` corroborate a native `GameOptionsManager` and recognized keys such as `scaleDisplay`, `GFXOption`, `useShadows` and `GameOptions_saved.json`.

This is a useful version-matched config discovery, not yet a proven live-game patch: the embedded file is under `assets/releaseminigame/`, effective external-file precedence is not verified on-device, and the exact effect of `scaleDisplay` is not yet measured. No UltraScale shader keys were added. The custom in-game menu is not yet modified; the inventory found custom `.bgin` menu assets but no verified registration point for adding new controls. No APK was modified or re-signed, and no UltraScale-injected APK was built.

Detailed findings: [docs/asphalt8-config-adapter-research.md](../../docs/asphalt8-config-adapter-research.md#verified-apk-acquisition-asphalt-8-android-920i-2026-11-23). Acquisition artifact is attached to GitHub Actions run [45087768296](https://github.com/tiromixo2013-netizen/BetterVisuals/actions/runs/45087768296).


### Native menu path hints (read-only)

The ARM64 native library also contains strings for `options_menu/screen_options_ads`, `options_menu/template_control`, `ingame_menu/template_ingame_options_controls`, `visual_quality_container_android`, `$STR_MENU_SOUND_AND_DISPLAY_OPTIONS`, `OPTIONS_CONTROLS_CUSTOM`, and `GameOptionsOverrideSlider`. These are promising names for the game's existing menu/quality UI, but the matching resource pack and registration code are not in the acquired XAPK. The next acquisition target is the installed game's downloaded resource data; the current APK artifact alone is insufficient to safely add an UltraScale menu entry.


## CI packaging retry

The packaging workflow now removes stale `META-INF` BNDLTOOL/JAR signature metadata from original split APKs before signing all splits with the same temporary test key. This is a packaging integrity fix only; runtime GLES layer activation and image-processing features remain unverified.


The split-signature cleanup predicate was corrected to match `.RSA`, `.DSA`, `.EC`, `.SF`, and `MANIFEST.MF` entries using case-insensitive filename suffix checks; a fresh CI run is triggered for this revision.


## Native engine identification — Asphalt 8 9.2.0i (ARM64)

The pinned split APK was inspected directly. `config.arm64_v8a.apk` contains
`libAsphalt8.so`, whose ELF dependencies include `libEGL.so` and
`libGLESv2.so`; its exported JNI entry points include
`GL2JNILib_initGL`, `resize`, and `step`. This is a native Gameloft
C/C++ renderer using EGL/OpenGL ES, not a Unity or Unreal title. The
`libAsphalt8.so` binary is stripped, so the engine's internal render-pass
callbacks and render-target ownership are not exposed by symbols.

The game's own `gameoptions.json` contains a recognized `Specific` profile
with `scaleDisplay` and `scaleAABuffer` values, including multiple
`RES_*` overrides. These are the correct engine-owned resolution controls
to investigate before attempting to override GLES viewports. Their live
effect and active resolution-profile selection still require on-device
measurement; do not treat the values as a proven UltraScale adapter.

The safe external integration boundary remains the Android GLES layer's
`eglSwapBuffers` chain. A final-frame post-process can be attached there
only after confirming the layer is loaded and the default framebuffer is
the game's final scene. It cannot by itself recover a lower-resolution
internal scene or provide motion vectors/depth for SGSR2. The current layer
is still telemetry/pass-through; no upscaler or frame-generation mode is
reported active.

Android's documented GLES layer eligibility includes target-app manifest
opt-in metadata for apps targeting API 30+, debuggable apps, or supported
userdebug/root lab devices. The metadata must be placed in Asphalt 8's own
manifest, not the UltraScale controller app. The user's re-signed APK path
permits editing that manifest, but the target SDK and runtime loader attach
must be verified on the phone.


## Experimental GLES spatial pass integration — 2026-11-24

The ARM64 GLES layer now has an experimental scene-blit dispatch in
`glBlitFramebuffer`. It only substitutes a full-image color blit from a
tracked texture-backed off-screen framebuffer to the default window framebuffer
when the destination is larger than the source. Other blits continue through
the game's original function.

Implemented spatial modes in this dispatch:

- Bilinear and nearest sampling.
- UltraScale FSR Lite (custom six-tap, not official AMD FSR).
- Qualcomm-derived SGSR1 fragment pass with GLES 3.1 shader compilation.
- MWSR Performance fragment pass, with scale-derived uniforms from the
  upstream MIT implementation; the copyright and MIT notice are retained.

The CAS final-frame pass remains independently selectable at build time and
runs only when enabled. The build workflow also accepts a render-scale value
and patches the game's recognized `Specific.scaleDisplay` overrides while
preserving `GFXOption` and other graphics effects.

These are code-path integrations, not yet proof of successful interception in
the retail Asphalt 8 process. Android GLES layer attachment, the game's actual
render-target pattern, shader compilation on Adreno 610, output correctness
and frame-time cost still require device logs and visual testing.

SGSR2 temporal reconstruction is not implemented: the current generic layer
does not receive game depth, motion vectors, jitter or temporal history.
Pyramidal Lucas–Kanade frame generation, x2/x3/x4 presentation, in-game menu
injection, and queue/frame-pacing controls are also not implemented. Do not
present their saved settings as active renderer features.

## Play Asset Delivery investigation — 2026-11-24

A version-matched APKPure XAPK was downloaded from the separate reference branch and inspected read-only. This source is **not verified as a Google Play download**.

The XAPK contains the base APK, ARM64 configuration split, HDPI configuration split and manifest only; it contains no separately delivered asset-pack archives. The base APK includes `asset-delivery.properties` (client 2.1.0). DEX string tables include Play Core Asset Delivery classes and API references such as `AssetPackManager`, `getPackStates`, `getPackLocation`, `AssetPackState` and `AssetPackLocation`. This shows that the package includes PAD client code; it does not establish the actual request call graph or the runtime pack IDs.

The modified package is signed with a temporary test key. A Play-delivered PAD request may fail if Play does not recognize the installed package/signature/delivery provenance. This is a leading hypothesis for the 0% screen, but is not confirmed without a device log showing the Asset Delivery status/error code. Do not add a second PAD library: the client API surface is already present.

The exact cause remains open between Play Asset Delivery ownership/recognition and a separate Gameloft download path. No direct CDN fallback, PAD downloader patch, or asset-pack acquisition has been implemented. The evidence and safe logcat checklist are recorded in [the isolated original-package triage branch](../../tree/reference/asphalt8-9.2.0i-original-analysis/reports/asphalt8-9.2.0i-pad-triage-2026-11-24.md).

## Remaining temporal features — explicit implementation boundary

SGSR2 source files are already bundled in the standalone app, but the Asphalt 8 external GLES layer does not receive the game depth, motion vectors, camera jitter or temporal history required by the algorithm. It therefore cannot honestly expose SGSR2 as an active Asphalt 8 upscaler yet. Real x2/x3/x4 frame generation likewise needs a motion source and a controlled presentation cadence/queue; adding a shader toggle or repeating frames would be a fake implementation. Low-latency queue and frame-pacing controls are not active in the current target-game adapter. These features remain gated until the relevant game-owned inputs and presentation path can be demonstrated.

## Direct original-vs-modified APK comparison — 2026-11-24

The original APKPure XAPK and the UltraScale test XAPK were downloaded from their archived workflow artifacts and compared directly.

- All 11 `classes*.dex` files in the base APK are byte-identical between the two builds.
- `asset-delivery.properties` is byte-identical (PAD client 2.1.0).
- PAD API symbols and error strings are identical in both DEX sets. The game's Java/Kotlin PAD downloader was not removed or modified by the UltraScale patch.
- ABI and density split APK payloads are identical apart from signing metadata.
- The modified base APK adds `libGLES_ultrascale.so` and `com.android.graphics.injectLayers.enable=true`.
- Manifest differences also include `android:isSplitRequired=true` and one deep-link scheme serialized as its resolved literal.
- The only semantic gameoptions JSON changes are GPU_5 FPSLimit 60 -> 240 and existing Specific profile scaleDisplay values -> 70. No PAD-related gameoptions key changed.

The comparison source called "original" is APKPure, not an authenticated Google Play delivery. Both available artifacts are sideloaded; neither proves Play ownership/recognition. Google PAD error codes `APP_NOT_OWNED (-13)` and `UNRECOGNIZED_INSTALLATION (-15)` are the leading explanation to verify on-device. The graphics-layer manifest flag is an unrelated isolation candidate, not a confirmed PAD cause.

Detailed comparison: [reports/asphalt8-9.2.0i-pad-comparison-2026-11-24.md](../../../../reports/asphalt8-9.2.0i-pad-comparison-2026-11-24.md)
