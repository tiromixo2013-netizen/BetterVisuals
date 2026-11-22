# UltraScale Unity plugin adapter

This folder contains isolated, owner-integrated Unity render adapters, not an injector.

## Included

- `native/`: standalone CMake shared-library target with a versioned, fail-closed C ABI.
- `unity/UltraScaleRenderBridge.cs`: Built-in Render Pipeline camera callback, in-game settings panel and managed render chain.
- `unity/URP/UltraScaleRendererFeature.cs`: separate URP 14 compatibility-mode Renderer Feature for URP Renderer Data assets.
- `unity/URP/UltraScale.Unity.URP.asmdef`: isolated assembly referencing URP Runtime and Core Runtime.
- `unity/Shaders/UltraScaleFsrLite.shader`: project-authored six-sample FSR Lite-style filter; not AMD FSR.
- `unity/Shaders/UltraScaleSpatial7Tap.shader`: project-authored seven-sample spatial reconstruction filter.
- `unity/Shaders/UltraScaleMwsr.shader`: upstream MoonlightWeb MWSR HLSL spatial-upscale port.
- `unity/Shaders/UltraScaleFsr1Easu.shader`: Unity HLSL adaptation of the bundled AMD FidelityFX FSR1 EASU-derived fragment shader.
- `unity/Shaders/UltraScaleSgsr1.shader`: SGSR1 HLSL port using texture-gather operations; requires a supported shader backend.
- `unity/Shaders/UltraScaleCas.shader`: AMD FidelityFX CAS sharpen-only pass.
- `include/ultrascale_unity_bridge.h`: native ABI contract.

## Managed render paths

### Built-in Render Pipeline

1. `OnRenderImage` receives the camera source RenderTexture.
2. The source is downsampled to a temporary target at the selected 50–100% internal scale.
3. The selected spatial mode reconstructs into a full-resolution intermediate: Bilinear, custom FSR Lite, project 7-tap, MWSR, AMD FSR1 EASU-derived, or Qualcomm SGSR1 (if supported).
4. Optional AMD CAS runs as a separate post-sharpen pass.
5. Temporary render targets are released in `finally`; materials are released in `OnDestroy`.

### Universal Render Pipeline

The separate `URP/UltraScaleRendererFeature.cs` targets **URP 14 / Unity 2022.3 compatibility-mode ScriptableRenderPass** APIs. Add the `URP` folder and shader files under `Assets/`, add UltraScale Renderer Feature to the active URP Renderer Data asset, then tune the feature in its inspector. It exposes render scale, scaler choice, CAS enable/strength and injection point.

The URP adapter uses command-buffer blits and temporary RTHandles; it does not use Built-in's `OnRenderImage`. This initial adapter is for the URP compatibility path, not Unity 6 RenderGraph-native execution. Do not add the URP asmdef to a Built-in-only project unless URP packages are installed.

## Algorithm and licensing notes

MWSR uses the upstream MIT HLSL implementation and scale-dependent parameter law. FSR1 EASU and CAS retain AMD MIT notices. SGSR1 retains Qualcomm Innovation Center's BSD-3-Clause notice. FSR Lite and 7-tap are project-authored filters, not official AMD/Qualcomm algorithms.

All modes here are single-frame spatial processing. No temporal history, motion vectors, depth reprojection or frame generation is implemented. SGSR1 uses texture gather and needs actual target-driver validation. EASU shader math and all ports need Unity shader compilation and image-quality comparison before release.

## Limits and validation status

- Shader support is checked independently; an unavailable selected shader falls back to bilinear.
- Built-in and URP adapters are separate. HDRP is not implemented.
- The legacy generic GLES/Vulkan frame ABI remains fail-closed. A separate GLES3 texture backend is now wired through a Unity render event for the Built-in adapter; it provides only bilinear and project-authored edge/detail sharpening. Vulkan still has no processor.
- This code must be integrated into an owned Unity project. It does not inject into arbitrary third-party games or modify their native settings.
- A GitHub Actions host contract workflow was added for CMake/CTest. Its host branch tests ABI validation and the non-Android GLES fallback; it does not compile the Android GLES implementation. No Unity build, Android NDK build, shader compiler, device image-quality, frame-time or thermal validation is claimed.

## Packaging

For Built-in, add `UltraScaleRenderBridge.cs` and all shaders under the Unity project's `Assets/` tree, then attach the component to a Built-in RP camera.

For URP 14, add the `URP/` folder and shaders under `Assets/`, ensure URP 14 is installed, then add **UltraScale Renderer Feature** to the active Universal Renderer Data asset. Preserve shader references or include shaders in **Always Included Shaders** to avoid build stripping.

The native library is optional for both managed paths. Do not package it as an active scaler until a real GLES/Vulkan processor is implemented and validated.

## Native bridge contract hardening

The native ABI now validates the scaler enum, CAS boolean, finite/ranged settings, frame dimension ceiling, explicitly owned render-target flag, reserved frame flags, graphics API and pixel format. It distinguishes invalid arguments, unsupported API/format and the expected `NO_SCALER_ATTACHED` state. A valid call still returns `INTEGRATION_REQUIRED` with processed frame index zero because no GLES/Vulkan processor is attached.

A CTest contract suite is included at `native/tests/ultrascale_unity_bridge_tests.cpp`. It covers the fail-closed valid-frame case and invalid mode, oversized dimensions, missing ownership declaration, unsupported format/API and invalid CAS flag. The test source was compiled with a host C++17 compiler using `-Wall -Wextra -Werror` and all assertions passed in this development session. This is a native ABI contract test only, not an Android NDK, Unity shader or device test.

## Native OpenGL ES 3 backend (new, experimental)

The native library now includes `include/ultrascale_gles_backend.h` and `native/ultrascale_gles_backend.cpp`. On Android builds it links `GLESv3` and provides an actual texture-to-texture GLES 3 fragment pass. The pass:
- accepts caller-owned source and destination `GL_TEXTURE_2D` names;
- renders a fullscreen triangle into the destination texture through an FBO;
- offers bilinear sampling and a project-authored cross-neighbour edge/detail sharpening mode;
- caches its program/FBO/VAO per EGL context and saves/restores the GL state it changes;
- is scheduled through a bounded queue and Unity `GL.IssuePluginEvent`, so GLES calls run on the Unity render thread with its context current.

The Built-in adapter exposes **Native GLES Bilinear + Detail Sharpen** as a selectable mode on Android devices using OpenGL ES 3.0. It first writes a bilinear fallback, then queues the native pass to overwrite the intermediate if the render event succeeds. If the library, callback or queue is unavailable, the bilinear result remains. This native mode is **not** AMD FSR1, MWSR or Qualcomm SGSR1; it is a small native spatial/sharpen pass. It does not implement temporal upscaling or frame generation.

Important constraints:
- Use an Android Unity build configured for **OpenGLES3**, not Vulkan, for this path.
- Integrate the native shared library into an owned Unity app and preserve the plugin event ordering.
- The native texture path assumes 2D, non-multisampled RGBA render textures with distinct source and destination texture names; the caller must pass dimensions matching the actual textures.
- Driver orientation, texture format, FBO completeness, event ordering and actual image output still require a Unity Android build and device validation.
- The native GLES API is not currently wired into the URP adapter.
- Vulkan still has no native processing implementation; its ABI remains fail-closed.

## Automated native contract check

`.github/workflows/ultrascale-native-contract.yml` configures and builds the native CMake target on Ubuntu, then runs CTest for the ABI contract and host-platform GLES fallback. The Android GLES source is conditionally compiled only on Android, so this CI job is not an Android build or GPU validation.
