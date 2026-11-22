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
- The native GLES/Vulkan C ABI remains a fail-closed scaffold and is not connected to these managed material passes.
- This code must be integrated into an owned Unity project. It does not inject into arbitrary third-party games or modify their native settings.
- No Unity build, Android runtime, shader compiler, image-quality, frame-time or thermal validation is claimed.

## Packaging

For Built-in, add `UltraScaleRenderBridge.cs` and all shaders under the Unity project's `Assets/` tree, then attach the component to a Built-in RP camera.

For URP 14, add the `URP/` folder and shaders under `Assets/`, ensure URP 14 is installed, then add **UltraScale Renderer Feature** to the active Universal Renderer Data asset. Preserve shader references or include shaders in **Always Included Shaders** to avoid build stripping.

The native library is optional for both managed paths. Do not package it as an active scaler until a real GLES/Vulkan processor is implemented and validated.

## Native bridge contract hardening

The native ABI now validates the scaler enum, CAS boolean, finite/ranged settings, frame dimension ceiling, explicitly owned render-target flag, reserved frame flags, graphics API and pixel format. It distinguishes invalid arguments, unsupported API/format and the expected `NO_SCALER_ATTACHED` state. A valid call still returns `INTEGRATION_REQUIRED` with processed frame index zero because no GLES/Vulkan processor is attached.

A CTest contract suite is included at `native/tests/ultrascale_unity_bridge_tests.cpp`. It covers the fail-closed valid-frame case and invalid mode, oversized dimensions, missing ownership declaration, unsupported format/API and invalid CAS flag. The test source was compiled with a host C++17 compiler using `-Wall -Wextra -Werror` and all assertions passed in this development session. This is a native ABI contract test only, not an Android NDK, Unity shader or device test.
