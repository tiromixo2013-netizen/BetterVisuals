# UltraScale Mobile — Android API / Injection Lab

Standalone Android app experiment in an isolated feature branch. It does not change the default branch or BetterVisuals mod source.

## Current implementation (0.6.1 GLES integration prototype)
- Kotlin / Jetpack Compose discovery of installed games using Android game-category metadata, with an all-launchable-app fallback for games that omit the category.
- APK native-library scan for engine and possible API candidates. This is heuristic, not runtime API detection.
- Native device probe for Vulkan loader and EGL vendor/version.
- Separate NDK shared libraries:
  - `libVkLayer_ultrascale_api_lab.so`: Vulkan loader layer pass-through prototype; tracks instance/device/queue dispatch, swapchain creation/destruction, image enumeration/acquisition, format, extent and sampled present indices.
  - `libGLES_ultrascale.so`: GLES layer pass-through prototype intercepting framebuffer/texture binding, texture allocation, viewport and draw calls; sampled frame logs include per-context texture dimensions, attachment metadata, surface size and swap-call duration.
- The included sample game now has its own **Graphics Settings — Advanced** menu. Its GLES renderer renders into a lower-resolution FBO and offers Bilinear, Nearest, Qualcomm SGSR1 Mobile, UltraScale FSR Lite, upstream MWSR, and AMD FidelityFX FSR1 EASU. An optional AMD CAS post-sharpen pass can be enabled independently. Render scale can be changed live while the sample is running. The quality presets are Ultra Quality (90%), Quality (82%), Balanced (75%), Performance (67%) and Ultra Performance (59%), with a 50–100% Custom slider. The optional AMD FidelityFX CAS switch is a separate post-process with adjustable sharpness; legacy extra-sharpening remains limited to Bilinear / Nearest.
- The standalone Android app now packages six technique entries across eight shader source files in `app/src/main/assets/upscalers/` and exposes them in a Built-in Shader Library card: Qualcomm SGSR2 2-pass fragment reference, AMD FidelityFX FSR1 EASU, FSR Lite 6-tap, MWSR, SGSR1 Mobile and AMD CAS. `UpscaleTechniqueCatalog.source(...)` loads the source assets. This is a source bundle/catalog, not a claim that the external layer applies these shaders to a third-party game. See [SHADER_BUNDLE.md](SHADER_BUNDLE.md) for provenance, licenses and integration boundaries.
- ShadowHook 1.1.1 and ByteHook 1.1.1 are bundled as optional in-process hook backends and pinned to Android SDK 35-compatible releases. The documented Android Graphics Layer Loader remains the actual external layer attachment route; neither hook library bypasses target-app eligibility. APK Parser 2.6.10 supplies bounded manifest/DEX engine-signature inspection on an IO dispatcher.\n- Optional Shizuku UserService configures Android's documented per-app GPU debug-layer settings (using a dedicated shell-identity Binder service, not the deprecated Shizuku newProcess API). Settings changes are applied with rollback to their prior values if a write fails.
- AGP 8 BuildConfig generation is explicitly enabled for the Shizuku service component-name binding.
- Android SDK 35, JDK 17, NDK 27.2, arm64-v8a and armeabi-v7a CI build.

## Sample renderer techniques and current limits
- **SGSR1 Mobile:** Qualcomm's upstream single-pass spatial shader is bundled with its BSD-3-Clause notice in `THIRD_PARTY_NOTICES.md`. It uses its built-in adaptive sharpening; it is not a temporal upscaler.
- **Qualcomm SGSR2 (2-pass fragment):** the official Vulkan GLSL ES 3.20 convert/upscale/vertex shader set is bundled in the app under the BSD-3-Clause license. It is a temporal algorithm, not a drop-in spatial filter: it needs the game's depth and motion-vector buffers, camera reprojection/jitter data, per-frame history ping-pong and Vulkan descriptor/pipeline integration. It is listed as reference-only and is not enabled in the sample renderer.
- **AMD FidelityFX FSR1 EASU:** the official-derived EASU spatial reconstruction shader is packaged in the Android app and adapted to GLSL ES 3.00 for the sample game's live renderer. It uses a wider reconstruction neighbourhood than FSR Lite and may cost more GPU time; sharpness is set to full EASU reconstruction in the sample. The complete AMD MIT notice is retained in the shader asset.
- **FSR Lite (6-tap):** a project-authored, lightweight EASU-inspired spatial shader using six texture taps. It is a custom fallback, not AMD's official FidelityFX FSR source or a claim of bit-for-bit FSR compatibility.
- **MWSR (MoonlightWeb Super Resolution):** the upstream GLSL ES 3.00 upscale shader from Bruno Martin's MIT-licensed mwsr/ folder is integrated directly. Its edge vote keeps flat 2×2 quads to one bilinear read and reconstructs edge quads with the upstream cubic/detail path (up to seven bilinear reads). CHROMA444 and gaze mode remain disabled, matching upstream defaults. Scale-law constants are calculated only when input/output dimensions change, not every frame. This avoids the previous incorrect port's extra chroma/alpha work and per-frame parameter calculations.
- **AMD FidelityFX CAS:** a separate optional post-sharpen pass based on the official AMD CAS sharpen-only (noScaling) filter math. It is not an upscaler; it runs after the selected scaler and exposes sharpness from 0–100%. This GLES fragment adaptation uses a 3×3 texel neighbourhood and the default shared-green-coefficient path. CAS expects linear 0–1 input and is best after tone mapping; the sample's generated test scene is in that range.
- Shader programs are compiled at runtime. If SGSR1, FSR Lite, MWSR or CAS fails to compile on a device, the requested upscaler falls back to Bilinear or the CAS pass is disabled. The renderer also invalidates and rebuilds its GL programs, textures and FBOs after EGL context recreation, then rechecks the selected method. MWSR's scale-law is clamped to a true 1:1 minimum rather than forcing an artificial 1.07x ratio. Device-side image-quality and frame-time validation is still required. MWSR's upstream phone measurements show 4–5 ms on an entry-level Mali-G57 when upscaling from half resolution and 12–13 ms from 720p to 1440p, so its single-pass design is not a guarantee of low cost on every mobile GPU.

## Important: eligibility and limits
A third-party app cannot silently load code into arbitrary production games. For Vulkan, Android permits a target that is debuggable, has the API-30+ `com.android.graphics.injectLayers.enable=true` manifest opt-in, or is on a supported rooted userdebug environment. GLES is stricter for external local layers: the target must be debuggable or the device must provide actual root access; Vulkan's manifest opt-in alone does not authorize a production GLES target. The Shizuku control only writes documented debug-layer settings; it does not bypass these rules. ADB shell UID 2000 is not root.

The external Vulkan/GLES injection layers still provide **pass-through diagnostics only**; they do not yet post-process arbitrary third-party game frames. The sample game's in-process GLES integration is a separate working prototype demonstrating a real render path and native game settings:
- GLES layer observes render-target texture allocation/attachment and draw/viewport state per EGL context, then forwards every call unchanged.
- Vulkan layer reports itself to the loader, tracks physical devices and forwards instance/device creation, destruction, swapchain lifecycle, image acquisition and queue-present calls.
- Vulkan and GLES layer settings can be enabled together for games that switch APIs; the UI has separate disable controls for each API.
- Existing colon-separated layer names and layer-package entries are preserved when UltraScale is enabled or disabled. The app refuses to overwrite another app's active GPU debug target.
- Android's documented global settings select one target package at a time, so this prototype cannot configure multiple games simultaneously.
- The sample game's integrated GLES path does render at a reduced internal resolution and upscale into the display surface. This proves the sample integration only; it is not yet connected to the external GLES layer or arbitrary games.
- The external layers do not yet modify textures, render targets, shaders, resolution or frame timing. Do not label upscaling active for a selected third-party game until its actual render path is integrated and measured.

## Test with the included sample game
1. Build and install both APKs from the GitHub Actions artifact.
2. Start Shizuku using Wireless Debugging and grant UltraScale permission.
3. In UltraScale, connect the Shizuku shell service.
4. Select **UltraScale Layer Test Game** and enable Vulkan or GLES.
5. Launch the sample game. Open **Graphics Settings — Advanced** to change render scale, Bilinear / Nearest / SGSR1 / FSR Lite / MWSR, legacy sharpening, and optional AMD CAS post-sharpen with its strength while the game is running.
6. Tap **Run Vulkan API test** to create a Vulkan instance/device.
7. Inspect logcat for `UltraScaleVK` / `UltraScaleGLES` messages. The GLES sample line includes surface size, context handle, swap-call time and EGL result; this is instrumentation only, not GPU execution time.
8. Use the matching **Disable Vulkan** / **Disable GLES** control after testing.

## Test with another owned game
1. Build and install the UltraScale Mobile app.
2. Create an owned/debuggable Android game or test harness that uses Vulkan or GLES.
3. For Vulkan on API 30+, add the documented manifest opt-in metadata; for GLES, make the test target debuggable (or use a rooted test device).
4. Start Shizuku through Wireless Debugging and grant UltraScale permission.
5. In UltraScale, select the test app and enable the matching layer.
6. Fully stop and relaunch the test app.
7. Check logcat for `UltraScaleVK` or `UltraScaleGLES` messages. Confirm the app still renders correctly.
8. Disable the layer when finished.

## Build
Open `project/` in Android Studio (JDK 17, SDK 35, NDK 27.2.12479018, CMake 3.22.1), or let GitHub Actions run:
`gradle lintDebug testDebugUnitTest assembleDebug`.

Research references and community implementation notes are in [RESEARCH_NOTES.md](RESEARCH_NOTES.md).

## Next milestones
1. Compile and validate the loader prototypes on a real Android 15 device.
2. Validate both layer paths against the included opt-in sample on Android 15/16 devices.
3. Implement safe frame-boundary render-target acquisition and API-specific state/synchronization handling.
4. Benchmark the sample game's reduced-resolution FBO + upscale path for GPU time, latency, image quality and thermal behavior.
5. Design a safe, API-specific way to connect supported game render paths to the proven pipeline; generic layer interception alone does not add settings to a game's native menus.
6. Add per-engine native settings integration for games that can be supported through an explicit in-process integration.

No exploit chains, hidden permission bypasses, arbitrary process-memory modification or sandbox escapes are part of this project.
