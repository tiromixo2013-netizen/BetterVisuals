# UltraScale Unity plugin adapter

This folder contains an isolated, owner-integrated Unity plugin adapter, not an injector.

## Included
- `native/`: standalone CMake shared-library target with a versioned C ABI.
- `unity/UltraScaleRenderBridge.cs`: Built-in Render Pipeline camera callback with a live in-game settings panel.
- `unity/Shaders/UltraScaleFsrLite.shader`: Unity HLSL port of the project's custom six-sample FSR Lite shader.
- `unity/Shaders/UltraScaleSpatial7Tap.shader`: separate project-authored seven-sample spatial reconstruction and optional edge-sharpen pass.
- `include/ultrascale_unity_bridge.h`: shared native ABI contract.

## Current implemented behavior
The managed Unity path is now connected:
1. `OnRenderImage` receives the camera's real source RenderTexture.
2. It creates a temporary RenderTexture at the selected 50–100% internal scale.
3. It downsamples the camera image into that target.
4. Bilinear mode scales the reduced target to the destination with Unity's bilinear blit.
5. UltraScale FSR Lite mode runs the Unity port of the project's custom six-sample GLSL algorithm; the seven-tap experimental mode runs its separate spatial shader with optional edge sharpening.
6. The temporary target is released after the pass.

This is an actual managed render operation in an owned Unity Built-in Render Pipeline project. The custom FSR Lite port is not AMD FidelityFX FSR1/EASU, Qualcomm SGSR1/SGSR2, or upstream MWSR. MWSR, SGSR1 and official FSR1/EASU still need separate Unity shader ports and validation. The native C ABI remains a scaffold and does not yet process Unity texture handles.

## Current limitations
- Built-in Render Pipeline only. URP/SRP requires a separate ScriptableRendererFeature / RenderGraph implementation.
- The Unity shader is project-authored spatial filtering; no temporal history, motion vectors, depth reprojection or frame generation.
- Native GLES/Vulkan bridge is not connected to the managed shader pass.
- This adapter must be integrated into an owned/debug Unity project; it does not silently load into arbitrary third-party games.
- No Android device, image-quality, frame-time or thermal validation has been claimed.

## Build and integration
Build the native folder independently with Android NDK/CMake for the target ABI, then package the resulting `libultrascale_unity_bridge.so` into the owned Unity test project's `Assets/Plugins/Android/arm64-v8a/` directory. Add `UltraScaleRenderBridge.cs` and the shader under the Unity project's `Assets/` tree, then attach the component to the camera. The managed shader path can be tested independently of the optional native library.
