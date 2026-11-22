# Priority-game graphics integration plan

This plan separates a source-level render integration from Android's external graphics-layer mechanism. A successful manager build or a static APK fingerprint is not proof that a game's frames passed through UltraScale.

## Current evidence and routes

| Game / build | Known render path | Current integration | What is not yet proven |
|---|---|---|---|
| Geometry Dash Android 2.2081 ARM64 | Cocos2d-style OpenGL ES, based on the repository's Geode hooks and render code | Existing BetterVisuals Geode module has an in-process framebuffer pipeline and Render Scale / FSR1 / CAS stages. The Geode import route is version-pinned to 2.2081. | Runtime on the user's exact phone/game build, frame timing, stability, and visual/performance results. |
| Geometry Dash Android 2.2091 | Expected to need the same conceptual settings, but binary compatibility is a separate question | Shared settings design only; no 2.2091 route is enabled by the current Geode manifest. | A 2.2091-specific module build and compatibility/runtime validation. |
| Asphalt 8 Android 9.1.0i (91009) | Not confirmed from the base APK scan | No game render adapter. External layer route is blocked when the installed production app has no opt-in/debug eligibility. | Full installed split set, native ABI/library inventory, active GLES/Vulkan renderer, and a supported in-process integration point. |
| Beach Buggy Racing (original package only) | Not confirmed from the package profile | No game render adapter. Do not confuse this package with Beach Buggy Racing 2. | Exact installed version, complete split/native library inventory, active renderer, and an integration point. |

## Why Shizuku settings alone cannot inject a render pass

Android's documented GLES and Vulkan debug-layer loaders require the target to opt in through manifest metadata, be debuggable, or run on a suitable userdebug/rooted environment. Writing the global GPU debug settings is only configuration; it does not grant a production target permission to load the layer. Shizuku shell access does not change that target-app eligibility.

Official platform references:
- https://developer.android.com/ndk/guides/rootless-debug-gles
- https://developer.android.com/ndk/guides/graphics/validation-layer

## Integration choices

1. **Game-owned plugin/mod route (preferred):** the code runs inside the game and captures the actual scene/render target at a safe point. This is already the Geometry Dash / Geode approach for 2.2081.
2. **External Android graphics layer:** viable only when the target satisfies Android's layer-loading eligibility requirements. It still needs correct GLES/Vulkan interception, synchronization, and a real post-process implementation.
3. **Repackaged APK with target opt-in:** technically distinct from Shizuku injection and entails modifying the manifest, rebuilding/re-signing the APK, signature/update/data consequences, and compatibility/security risks. It is not implemented and must not be presented as a ready or safe one-click solution.

## Required implementation gates

- Fingerprint exact package, version, signing identity, and every installed split's ABI/native libraries.
- Establish the active renderer from runtime evidence, not library-name guesses.
- Identify a supported hook point and the game's actual color/depth targets, viewport, orientation, and synchronization.
- Implement render-scale capture, reconstruction/upscale, then sharpening (CAS) in that order, with a safe bypass path.
- Test device loss/context recreation, pause/resume, resolution changes, UI composition, frame pacing, and fallback behavior.
- Mark a route as runtime-tested only after logs/captures from the exact target build show UltraScale's render pass processing game frames.

## Immediate next evidence

Use the app's installed-game scan (it reads the installed base APK and split APK paths) for Asphalt 8 and Beach Buggy Racing. Save/export the scan report or share screenshots of the engine, native ABI, graphics-library and version fields. Do not scan only a downloaded base.apk if the installed game is split. The reports will narrow the engine adapter, but a frame capture/runtime trace is still needed before claiming the render pass works.
