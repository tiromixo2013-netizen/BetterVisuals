# UltraScale Mobile — Android API / Injection Lab

Standalone Android app experiment in an isolated feature branch. It does not change the default branch or BetterVisuals mod source.

## Current implementation (0.6.1 GLES integration prototype)
- Kotlin / Jetpack Compose discovery of installed games using Android game-category metadata, with an all-launchable-app fallback for games that omit the category.
- APK native-library scan for engine and possible API candidates. This is heuristic, not runtime API detection.
- Native device probe for Vulkan loader and EGL vendor/version.
- Separate NDK shared libraries:
  - `libVkLayer_ultrascale_api_lab.so`: Vulkan loader layer pass-through prototype; loader-facing entry points are explicitly exported despite hidden-by-default native symbol visibility.
  - `libGLES_ultrascale.so`: GLES layer pass-through prototype intercepting `eglSwapBuffers`.
- The included sample game now has its own **Graphics Settings — Advanced** menu. Its GLES renderer renders into a lower-resolution FBO and runs a fullscreen upscale pass with Bilinear / Nearest sampling and optional lightweight CAS-style sharpening. Render scale can be changed live while the sample is running.
- Optional Shizuku UserService configures Android's documented per-app GPU debug-layer settings (using a dedicated shell-identity Binder service, not the deprecated Shizuku newProcess API). Settings changes are applied with rollback to their prior values if a write fails.
- AGP 8 BuildConfig generation is explicitly enabled for the Shizuku service component-name binding.
- Android SDK 35, JDK 17, NDK 27.2, arm64-v8a and armeabi-v7a CI build.

## Important: eligibility and limits
A third-party app cannot silently load code into arbitrary production games. For Vulkan, Android permits a target that is debuggable, has the API-30+ `com.android.graphics.injectLayers.enable=true` manifest opt-in, or is on a supported rooted userdebug environment. GLES is stricter for external local layers: the target must be debuggable or the device must provide actual root access; Vulkan's manifest opt-in alone does not authorize a production GLES target. The Shizuku control only writes documented debug-layer settings; it does not bypass these rules. ADB shell UID 2000 is not root.

The external Vulkan/GLES injection layers still provide **pass-through diagnostics only**; they do not yet post-process arbitrary third-party game frames. The sample game's in-process GLES integration is a separate working prototype demonstrating a real render path and native game settings:
- GLES layer observes `eglSwapBuffers` and forwards it.
- Vulkan layer reports itself to the loader, tracks physical devices and forwards instance/device creation, destruction and queue-present calls.
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
5. Launch the sample game. Open **Graphics Settings — Advanced** to change render scale, Bilinear / Nearest sampling, sharpening and the live upscale pass while the game is running.
6. Tap **Run Vulkan API test** to create a Vulkan instance/device.
7. Inspect logcat for `UltraScaleVK` / `UltraScaleGLES` messages.
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
