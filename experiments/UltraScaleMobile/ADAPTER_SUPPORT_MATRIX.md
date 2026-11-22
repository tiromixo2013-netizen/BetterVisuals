# UltraScale Mobile adapter support matrix

This file is the compatibility contract for the APK inspector and Geode import route.
A package name, engine fingerprint, graphics library, or ABI match alone never authorizes
rewriting an APK or modifying another process.

## Supported route

| Game | Exact target | Integration mechanism | Status |
|---|---|---|---|
| Geometry Dash | Android 2.2081, package `com.robtopx.geometryjump`, `arm64-v8a` | Install this repository's `imes.bettervisuals` Geode module into a user-selected Geode mods folder | Adapter route implemented; Geode module must be built by the Geode workflow and imported by the user |

The Geometry Dash Geode mod already owns its rendering integration. Its current real effects
include Render Scale, Nearest/Bilinear presentation, FSR 1 upscaling, FSR sharpness, CAS and
other post-processing. The new Upscale Preset setting maps to render-scale ratios:
Ultra Performance 0.33, Performance 0.50, Balanced 0.67, Quality 0.77, Ultra Quality 0.88.
Custom leaves the manual Render Scale value unchanged.

The Android app validates the selected APK identity and the selected `.geode` archive
(`mod.json` ID plus Android native module), then copies the module into a directory selected
through Android's Storage Access Framework. It does not patch or re-sign Geometry Dash's APK.
The user must select the actual Geode mods folder and restart the game through Geode.

## Not supported yet

- Asphalt 8: no verified renderer adapter for a specific Android build.
- Beach Buggy Racing: no verified renderer adapter for a specific Android build.
- Arbitrary Unity, Unreal, Cocos or Godot APKs: engine detection is reconnaissance only.
- Direct APK code injection, arbitrary process injection, private app-data access, anti-cheat
  bypass, DRM bypass and privilege escalation are not implemented.

## Runtime claims

The app must not display an adapter as active merely because an APK was scanned or a module
was copied. A successful file import means only that the Geode module was copied to the selected
folder. Runtime activation still depends on a compatible Geode loader, the correct game build,
successful module loading and the mod's own runtime initialization.

## Settings truthfulness

- Render Scale is implemented in the Geode renderer.
- Upscaler choices currently implemented in the Geode renderer: Nearest, Bilinear, FSR 1.
- Upscale Sharpness currently affects FSR 1 only.
- Upscale Presets update Render Scale; Custom preserves the manual value.
- CAS and CAS Strength are implemented as existing post-process controls.
- A Low Latency toggle is intentionally not advertised until a real, measurable frame-queue
  or presentation implementation exists. EGL swap-interval changes alone are not proof of
  lower input-to-photon latency.
