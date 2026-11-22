# UltraScale Unity plugin adapter

This folder contains an isolated, owner-integrated Unity plugin adapter, not an injector.

## Included
- `native/`: standalone CMake shared-library target with a versioned C ABI.
- `unity/UltraScaleRenderBridge.cs`: Built-in Render Pipeline camera callback, in-game mode panel, render-scale control and optional CAS pass.
- `unity/Shaders/UltraScaleFsrLite.shader`: project-authored six-sample FSR Lite-style filter; not AMD FSR.
- `unity/Shaders/UltraScaleSpatial7Tap.shader`: project-authored seven-sample spatial reconstruction filter.
- `unity/Shaders/UltraScaleMwsr.shader`: upstream MoonlightWeb MWSR HLSL upscale port, adapted to Unity's full-screen blit interface.
- `unity/Shaders/UltraScaleFsr1Easu.shader`: Unity HLSL adaptation of the bundled AMD FidelityFX FSR1 EASU-derived fragment shader.
- `unity/Shaders/UltraScaleSgsr1.shader`: SGSR1 HLSL port using texture-gather operations; requires a backend supporting gather (shader target 4.5).
- `unity/Shaders/UltraScaleCas.shader`: AMD FidelityFX CAS sharpen-only pass.
- `include/ultrascale_unity_bridge.h`: native ABI contract.

## Implemented managed render path
1. `OnRenderImage` receives the camera's source RenderTexture.
2. The source is downsampled into a temporary RenderTexture at the selected 50–100% internal scale.
3. The selected spatial mode renders into a full-resolution intermediate target: Bilinear, custom FSR Lite, project 7-tap, MWSR, AMD FSR1 EASU-derived, or Qualcomm SGSR1 (when its gather shader is supported).
4. Optional AMD CAS runs as a separate post-sharpen pass after the scaler.
5. Temporary render targets are released in a `finally` block. Materials are released in `OnDestroy`.

MWSR uses the upstream MIT HLSL implementation and its scale-dependent `kc/kg/kr/kl` law. FSR1 EASU and CAS retain AMD's MIT notices. SGSR1 retains Qualcomm Innovation Center's BSD-3-Clause notice. FSR Lite is a project-authored algorithm and is not official AMD FSR.

## Capability and validation limits
- Optional shaders are loaded independently. If a shader is unsupported on a graphics backend, that mode falls back to Bilinear rather than disabling all modes.
- SGSR1 requires texture-gather support; `Shader.isSupported` is checked, but actual Android driver behavior still needs device testing.
- Built-in Render Pipeline only. URP/SRP needs a separate ScriptableRendererFeature / RenderGraph implementation.
- All listed techniques are single-frame spatial passes. No temporal history, motion vectors, depth reprojection or frame generation is implemented.
- The native GLES/Vulkan C ABI remains a fail-closed scaffold and is not connected to the managed Unity material passes.
- This adapter must be integrated into an owned Unity project. It does not inject into arbitrary third-party games.
- No Unity build, Android runtime, image-quality, frame-time or thermal validation is claimed.

## Build and integration
Add `UltraScaleRenderBridge.cs` and every shader under the Unity project's `Assets/` tree. Include the shaders in **Always Included Shaders** or preserve them through serialized references so build stripping does not remove them. Attach the component to a camera using the Built-in Render Pipeline. The native library is optional for the managed shader path; build and package it only when a real native processor is implemented.
