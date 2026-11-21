# UltraScale Mobile — Android API / Injection Lab

This is an isolated Android companion-app experiment. It does not change the BetterVisuals mod or default branch.

## Focus
- Android 15+ compatibility (API 35 baseline; CI compiles against SDK 35).
- Independent Vulkan and OpenGL ES paths; ANGLE is deliberately not required.
- Optional Shizuku bridge. ADB UID 2000 is not root.
- Game catalog and conservative 2D / 3D / hybrid classification.
- APK backend candidate scanning, clearly distinguished from confirmed runtime API use.
- Build artifact from GitHub Actions.

## Injection truth table
| Target app situation | External graphics layer |
|---|---|
| Production game, not opted in, non-root device | Not generally loadable |
| Game integrates UltraScale SDK / bundles a layer | Supported integration target |
| Debuggable game / authorized debug device | Development/testing path |
| Root or userdebug system | Device-specific privileged path |

Shizuku does not independently bypass Android's graphics loader policy or grant arbitrary access to another app's memory. No exploit, process patching, or security bypass is part of this project.

## Build locally
1. Decode `source-v0.3.zip.b64` into a ZIP file.
2. Extract it.
3. Open `UltraScaleMobile-v03` in Android Studio (JDK 17, Android SDK 35).
4. Run `gradle assembleDebug lintDebug testDebugUnitTest`.

## Next implementation milestones
1. Compile this baseline in CI and fix build findings.
2. Add native NDK adapters for Vulkan and GLES separately.
3. Build an opt-in integration SDK/sample game that bundles the relevant backend.
4. Add real runtime API probes only in processes that cooperate or are legitimately instrumentable.
5. Add Android 15/16 device tests and performance/thermal regression gates.

Loaded Edge skills: android-3d-development and android-ci-cd-release-playstore.
