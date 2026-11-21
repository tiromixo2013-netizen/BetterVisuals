# UltraScale Mobile — Android API / Injection Lab

Standalone Android companion-app experiment, isolated from the BetterVisuals mod and default branch.

## Focus
- Android 15+ target (compile SDK 35), JDK 17 and NDK C++17.
- Independent Vulkan and OpenGL ES/EGL native capability probes; ANGLE is not a dependency.
- Optional Shizuku bridge; ADB shell UID 2000 is not root.
- Installed launchable app catalog, likely engine and conservative 2D/3D/Hybrid classification.
- Static APK native-library scanning for candidate APIs; never presented as proof of the active runtime API.
- Separate Vulkan and GLES readiness states.
- NDK build for arm64-v8a and armeabi-v7a.

## Build locally
Open `project/` in Android Studio (JDK 17, Android SDK 35, NDK and CMake 3.22.1), or run:
`gradle --no-daemon lintDebug assembleDebug`

GitHub Actions builds a debug APK and publishes it as the `UltraScaleMobile-api-lab-debug` artifact.

## Injection model
1. **Developer-integrated mode (preferred):** the game bundles the SDK/backend and explicitly supplies render targets and synchronization.
2. **Opt-in graphics layer mode:** only where the target app and device graphics loader explicitly support it.
3. **Debug / privileged lab mode:** debuggable target or authorized userdebug/root test device, subject to vendor and OS restrictions.
4. **Ordinary production game without opt-in:** report integration required; do not claim injection.

Shizuku is an optional control plane for permitted ADB/Root operations. It does not bypass Android's app sandbox or grant arbitrary access to another game's memory or graphics loader. No exploit, process patching or security bypass is included.

## Detection limitations
APK scanning and engine signatures identify likely support only. Unity, Unreal and Godot can bundle several backends and select one at runtime. The native Vulkan/EGL probe reports the current device's loader capabilities from UltraScale's own process; it does **not** prove which API another game is currently using.

## Backend design
Vulkan and GLES have separate adapter contracts. Technique modules must declare texture formats, target lifecycle, synchronization, scale, temporal inputs and device requirements. SGSR2 is unavailable until the game integration supplies frame history and motion data. AMD CAS is treated as a sharpening pass.

A technique may be marked ACTIVE only after it receives a valid render target from an integrated game, creates its pipeline, processes and presents frames, and passes performance checks.
