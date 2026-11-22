# Unity IL2CPP backend integration

Status: **isolated integration scaffold; not yet a runtime injector or a verified game integration.**

This backend is deliberately separate from the Android Graphics Layer Loader. It targets Unity IL2CPP games that the owner controls or that explicitly permit mod/plugin integration. It does not attempt to evade anti-cheat, defeat app sandboxing, or attach to protected third-party processes.

## Selected upstream projects

| Upstream | Intended role | License / compatibility notes |
|---|---|---|
| [BepInEx-Android Launcher](https://github.com/Tb1281/BepInEx-Android) | Candidate Unity IL2CPP plugin runtime / launcher integration | Launcher GPL-3.0; BepInEx LGPL-2.1; CoreCLR MIT; Dobby Apache-2.0; LSPlant LGPL-3.0. Upstream documents Android 9+ and arm64-v8a. Android 15 + this device + each target game still require validation. |
| [NextBep/BepInEx.Android](https://github.com/NextBep/BepInEx.Android) | Alternative Android IL2CPP preloader/runtime fork | LGPL-2.1. Treat as an alternative implementation, not an additional runtime to load alongside the launcher. |
| [StArray.ModManager](https://github.com/StArraySharp/StArray.ModManager) | Candidate plugin manager, configuration UI and in-game overlay patterns | MIT. Dobby/ImGui-based project; useful UI/plugin reference, but not an upscaler and not proof of compatibility with every Unity game. |
| [Android-UnityImGui](https://github.com/xLlotus0/Android-UnityImGui) | Reference for Unity IL2CPP overlay/input handling in authorized test targets | MIT. Overlay/injection reference only; not a render-resolution/upscaling backend. |
| [Android-Virtual-Inject](https://github.com/reveny/Android-Virtual-Inject) | Research reference only | Not selected for integration: virtual-space approach and stated anti-cheat incompatibility make it unsuitable as the project's general backend. |
| [Android-GUI-Injector](https://github.com/reveny/Android-GUI-Injector) | Research reference only | Not selected: Android 15 compatibility and required privilege path are not established for this project. |

## Integration decision

1. Keep the current Shizuku + Android Graphics Debug Layer path unchanged. It remains a documented opt-in diagnostic path, not a general-purpose process injector.
2. Add Unity IL2CPP as a separate, optional backend. The launcher/runtime is a deployment mechanism for an explicitly supported Unity plugin; it is not the scaler itself.
3. Expose UltraScale's existing scaler pipeline through a plugin-facing API. The Unity plugin must obtain a valid render target from a supported Unity render callback, then pass texture, dimensions, graphics API, frame lifecycle and synchronization data to the native scaler.
4. Keep the actual scaler implementation in the existing native/render pipeline. Do not duplicate EASU, FSR Lite, MWSR, SGSR1 or CAS inside the loader.
5. Present UltraScale controls through the plugin's own configuration/overlay UI where supported. Inserting controls into the game's original settings screen requires game-side integration and is not guaranteed by a generic mod manager.
6. Keep SGSR2 disabled until the target renderer supplies depth, motion vectors, jitter/reprojection and temporal history.

## Proposed module boundary

`unity-backend/` is an isolated integration boundary. It must not be included in the app's production build until its dependencies, licenses, ABI, Android API level and runtime lifecycle have been reviewed.

Expected components:

- `UnityBackendCapability`: reports engine/runtime/API eligibility without claiming attachment.
- `UnityPluginBridge`: versioned interface between a supported Unity plugin and UltraScale's native scaler.
- `UnityRenderFrame`: render-target handle, dimensions, format, frame index, graphics API and synchronization contract.
- `UltraScaleSettingsBridge`: scaler, render-scale and CAS settings exposed to the plugin UI.
- `UnityBackendStatus`: `DETECTED`, `INTEGRATION_REQUIRED`, `UNSUPPORTED`, `FAILED`, `ACTIVE`.

Only report `ACTIVE` after a supported Unity callback supplies a real render target, the selected shader/pipeline is created, frames are processed and presented correctly, output is visually validated, and frame-time/thermal checks pass.

## Why upstream source is not vendored blindly

These projects are launchers/frameworks and complete mod managers, not drop-in Android libraries. Importing their whole source tree into the app would pull in separate runtime bootstraps, native hooks, UI stacks and licensing obligations. That would risk changing the current APK's build and startup behavior without delivering a working scaler. This scaffold records the chosen integration seam first; upstream source should be vendored only as a separately reviewed, pinned dependency with its complete license files and build recipe.

## Device and security constraints

- Shizuku shell identity is not root and does not grant arbitrary process-memory access.
- No anti-cheat bypass, stealth loading, protected-process attachment, sandbox escape or arbitrary memory patching.
- Unsupported/protected games must remain marked unavailable rather than showing a false Enable success.
- Validate first on the included sample game and an owned/debuggable Unity IL2CPP test app on arm64. Android 15 support must be tested, not inferred from Android 9+ minimum requirements.
- The Unity backend must not change or disable the existing GLES/Vulkan diagnostics or sample-game render path.

## Implementation order

1. Keep this adapter boundary isolated from the current app module.
2. Build a small owned Unity IL2CPP test target and verify the upstream launcher/runtime independently.
3. Implement the plugin-to-native frame bridge for the graphics API actually used by that test target.
4. Connect the bridge to the existing scaler pipeline and settings model.
5. Add a compatibility report and runtime validation before enabling the backend for any other package.

References:
- https://github.com/Tb1281/BepInEx-Android
- https://github.com/NextBep/BepInEx.Android
- https://github.com/StArraySharp/StArray.ModManager
- https://github.com/xLlotus0/Android-UnityImGui
- https://github.com/reveny/Android-Virtual-Inject
- https://github.com/reveny/Android-GUI-Injector


## Adapter scaffold added

The isolated `unity-backend/` folder now contains:
- `native/CMakeLists.txt` and `native/ultrascale_unity_bridge.cpp`: a standalone Android NDK shared-library target and fail-closed ABI implementation. It validates ABI, frame dimensions, texture handle and settings ranges; it returns `INTEGRATION_REQUIRED` because no GPU processor is attached.
- `unity/UltraScaleRenderBridge.cs`: a Built-in Render Pipeline `OnRenderImage` adapter and an in-game UltraScale settings panel for Bilinear, FSR Lite, MWSR, SGSR1 and FSR1 EASU, render scale and optional CAS settings. The current callback copies the source to the destination unchanged.
- `README.md`: packaging and next-step notes.

This is an owner-integrated plugin scaffold, not a BepInEx runtime import and not an external injector. The BepInEx/NextBep and StArray projects remain upstream integration candidates because importing their complete launcher/framework source into the controller APK would add unrelated runtime bootstraps and license/build obligations. The plugin adapter is designed to be packaged into an owned Unity test project after its native processor is implemented.

The Unity sample currently targets the Built-in Render Pipeline only. URP/SRP require a separate supported renderer feature / RenderGraph integration. No scaler is marked active by this scaffold.


## Managed Unity render path implemented

The adapter now contains a working managed render-path implementation for an owned Unity **Built-in Render Pipeline** project:
- `UltraScaleRenderBridge.cs` receives the camera's source/destination RenderTextures through `OnRenderImage`.
- A temporary RenderTexture is allocated at the selected 50–100% internal resolution, then the source is downsampled into it.
- Bilinear mode scales that reduced target to the destination. Experimental mode runs `UltraScaleSpatial7Tap.shader`, a project-authored seven-sample spatial reconstruction filter with optional edge sharpening.
- The in-game panel exposes only the two modes that are actually implemented in this Unity adapter. MWSR, SGSR1 and AMD FSR1 EASU are not mislabeled as active Unity modes; their Unity shader ports remain separate work.
- The seven-tap filter is not AMD FidelityFX, Qualcomm SGSR, temporal upscaling or frame generation.
- The native C ABI is still a separate scaffold and is not called to process frame textures. The managed shader path does not require the native bridge library to be packaged.

This is a real render operation only when the script and shader are added to an owned Unity Built-in RP project and attached to its camera. It does not inject into third-party games. URP/SRP support and device-side validation remain outstanding.
