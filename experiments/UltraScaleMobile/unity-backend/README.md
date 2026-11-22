# UltraScale Unity plugin adapter

This folder contains an isolated, owner-integrated Unity plugin adapter, not an injector.

## Included
- `native/`: standalone CMake shared-library target with a versioned C ABI.
- `unity/UltraScaleRenderBridge.cs`: Built-in Render Pipeline callback example. It receives Unity's source/destination RenderTextures and preserves the original image with Graphics.Blit.
- `include/ultrascale_unity_bridge.h`: shared ABI contract.

## Current truthful behavior
The native bridge validates ABI version, frame dimensions, texture handle and settings ranges. It then returns `INTEGRATION_REQUIRED` because no GLES/Vulkan frame processor is attached yet. The Unity sample intentionally copies the source image unchanged. It must never display ACTIVE or claim FSR/MWSR/CAS is running.

## Next engineering work
1. Add a render-pipeline-specific processor: GLES FBO/shader path for GLES targets and a Vulkan render pass for Vulkan targets.
2. Define ownership and synchronization for Unity's native texture handle; do not retain transient handles across frames.
3. Connect the existing EASU, FSR Lite, MWSR, SGSR1 and CAS shaders through the processor.
4. Add URP/SRP integration using supported ScriptableRendererFeature / RenderGraph APIs; `OnRenderImage` is Built-in RP only.
5. Validate output, frame pacing, GPU time, thermal behavior and context/device-loss recovery on an owned Unity IL2CPP sample app.

## Build
Build this folder independently with Android NDK/CMake for the target ABI, then package the resulting `libultrascale_unity_bridge.so` into the owned Unity test project's `Assets/Plugins/Android/arm64-v8a/` directory. This module is intentionally not wired into the controller APK's Gradle build.
