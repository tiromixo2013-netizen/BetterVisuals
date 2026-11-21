# UltraScale Mobile — Android API / Injection Lab

Standalone Android app experiment in an isolated feature branch. It does not change the default branch or BetterVisuals mod source.

## Current implementation (0.6.0 layer lab)
- Kotlin / Jetpack Compose game discovery, filtered using Android's game category metadata.
- APK native-library scan for engine and possible API candidates. This is heuristic, not runtime API detection.
- Native device probe for Vulkan loader and EGL vendor/version.
- Separate NDK shared libraries:
  - `libVkLayer_ultrascale_api_lab.so`: Vulkan loader layer pass-through prototype.
  - `libGLES_ultrascale.so`: GLES layer pass-through prototype intercepting `eglSwapBuffers`.
- Optional Shizuku UserService configures Android's documented per-app GPU debug-layer settings (using a dedicated shell-identity Binder service, not the deprecated Shizuku newProcess API).
- Android SDK 35, JDK 17, NDK 27.2, arm64-v8a and armeabi-v7a CI build.

## Important: eligibility and limits
A third-party app cannot silently load code into arbitrary production games. Android requires the target app to opt in using `com.android.graphics.injectLayers.enable=true`, be debuggable, or run on a supported userdebug/rooted environment. The Shizuku control only writes the documented debug-layer settings; it does not bypass these rules. ADB shell UID 2000 is not root.

The app currently provides **pass-through diagnostics**, not upscaling yet:
- GLES layer observes `eglSwapBuffers` and forwards it.
- Vulkan layer reports itself to the loader, tracks physical devices and forwards instance/device creation, destruction and queue-present calls.
- Vulkan and GLES layer settings can be enabled together for games that switch APIs.
- Neither layer modifies textures, render targets, shaders, resolution or frame timing. Do not label an upscaler active until an actual render pass is integrated and measured.

## Test with the included sample game
1. Build and install both APKs from the GitHub Actions artifact.
2. Start Shizuku using Wireless Debugging and grant UltraScale permission.
3. In UltraScale, connect the Shizuku shell service.
4. Select **UltraScale Layer Test Game** and enable Vulkan or GLES.
5. Launch the sample game. Its GLES triangle continuously renders; tap **Run Vulkan API test** to create a Vulkan instance/device.
6. Inspect logcat for `UltraScaleVK` / `UltraScaleGLES` messages.
7. Clear debug-layer settings after testing.

## Test with another owned game
1. Build and install the UltraScale Mobile app.
2. Create an owned/debuggable Android game or test harness that uses Vulkan or GLES.
3. For API 30+, add the documented manifest opt-in metadata to that test app.
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
4. Add actual spatial upscaling and CAS to the integrated sample; benchmark GPU time, latency, and thermal behavior.
5. Only then port the proven path into supported game integrations.

No exploit chains, hidden permission bypasses, arbitrary process-memory modification or sandbox escapes are part of this project.
