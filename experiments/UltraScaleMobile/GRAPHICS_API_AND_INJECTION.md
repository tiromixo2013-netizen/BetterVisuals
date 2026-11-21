# Graphics API and injection design (Android 15+)

## 1. Keep APIs independent
- Vulkan adapter: Vulkan loader / instance / device / queue lifecycle, synchronization, swapchain image ownership, format and present-mode negotiation.
- OpenGL ES adapter: EGL display/context/surface lifecycle, GLES version and extensions, framebuffer/texture state preservation.
- Hybrid games: classify as multi-backend-capable, then determine the active API only from trustworthy runtime telemetry. APK library names are hints, not proof.
- ANGLE is not a dependency and must never be silently selected as a fallback.

## 2. Injection modes
A. **Developer-integrated mode (preferred):** game bundles or explicitly loads UltraScale's SDK/backend. The game hands the render target, dimensions, format, frame lifecycle and synchronization to the adapter. This is the reliable mode for SGSR1, MWSR, FSR Lite, CAS and spatial filters.

B. **Opt-in graphics layer mode:** the **target game itself** must opt into supported layer loading. For the Android Vulkan opt-in path, the relevant metadata must be present in the target game's own manifest (and the device loader must support the path). Putting `com.android.graphics.injectLayers.enable` in UltraScale's controller-app manifest only affects UltraScale itself; it does not grant opt-in to another installed game. GLES has separate eligibility rules; Vulkan opt-in must not be treated as GLES opt-in. Package-level opt-in is not a universal Android 15+ guarantee.

C. **Debug / privileged lab mode:** debuggable target, userdebug device, or root-enabled test device, subject to Android version, vendor graphics loader and ABI. This is not the default consumer mode.

D. **No-root external game mode:** if the production game does not opt in, report injection unavailable. Do not claim a shader is active. MediaProjection is a separate capture/composition fallback and is not in-game injection.

## 3. Shizuku
Shizuku is an optional control plane for authorized ADB/Root operations. Shell UID 2000 is not root. Use it for package inspection and supported system operations only. It does not grant arbitrary ptrace, process-memory writes, Vulkan layer registration or injection into protected production apps.

## 4. API detection confidence
- Static APK scan: detects packaged engine/runtime libraries and possible API support.
- Runtime telemetry: confirms active API only when target integration or supported instrumentation exposes it.
- Unknown: required when evidence is insufficient.
Never infer active Vulkan solely because libvulkan is packaged.

## 5. Per-technique integration contract
Each technique declares:
- accepted API(s) and input texture formats;
- required render scale / intermediate target;
- spatial or temporal requirements;
- synchronization and lifetime requirements;
- GPU feature requirements;
- memory bandwidth / frame-time budget;
- output format and color-space behavior.

SGSR2 remains unavailable until temporal history and motion information are supplied. AMD CAS is a sharpening stage, not a substitute for an upscaler.

## 6. Compatibility gate
A technique can be marked ACTIVE only after:
1. adapter successfully obtains a valid render target;
2. shader/pipeline creation succeeds;
3. frame is processed and presented;
4. output dimensions/format are validated;
5. GPU time, dropped frames and thermal behavior pass thresholds.

Otherwise show DETECTED, INTEGRATION_REQUIRED, UNSUPPORTED or FAILED — never ACTIVE.

## 7. Security
No exploit chains, hidden permission bypasses, process-memory patching, or bypass of app sandboxing. Research must use documented opt-in interfaces, owned test apps, debuggable builds and authorized lab devices.


- APK inspection now also uses `net.dongliu:apk-parser` 2.6.10 (BSD-2-Clause) to recognize engine signatures in DEX files when native library names are inconclusive. Parsing is bounded to APK/split files up to 128 MiB each and runs on an IO dispatcher; failures fall back to existing native-library heuristics. DEX results remain static evidence, not proof of the live render API.

## 8. Reusable in-process hook backends
- The Android app packages ByteDance ShadowHook 2.0.1 (MIT) for inline hooks and ByteHook 1.1.2 (MIT) for PLT hooks. Both are initialized from the GLES layer after Android has loaded that layer into the authorized target process; ByteHook reuses ShadowHook as a dependency.
- This removes the need to implement ARM64/ARM32 inline-hook trampolines, PLT relocation patching, hook chaining, and recursion management from scratch for an owned or explicitly authorized test target.
- ShadowHook is **not** a process injector and does not grant access to arbitrary production games. Android must first load the layer through its documented opt-in/debuggable/userdebug path. The backend is not yet attached to GLES draw calls; render-target acquisition and a safe frame integration contract remain separate work.
- The sample-game is the authorized opt-in validation target. Do not report third-party game injection as supported unless the target has opted in and the full frame path is proven.

## 9. Engine-side render integration references
For a target game that can include an SDK/plugin, prefer the engine's supported render callback over guessing internal function addresses:
- Unity: native rendering plug-in interface and render-thread events. The callback receives the engine-controlled render-thread opportunity; texture/resource ownership still follows Unity's documented graphics interfaces.
- Godot: Android plugin / GDExtension integration, with the engine owning the RenderingDevice and render lifecycle.
- Unreal: engine plugin / renderer extension using the engine's RHI and render graph; this is a source/plugin integration, not a generic external Android hook.
- Cocos: use the matching Cocos native renderer/plugin API for the specific Creator/native version; do not assume one ABI across all game versions.

These are integration routes for owned games or developers who explicitly opt in. They do not create a universal injection route for unrelated production APKs.

## 10. Validation tools
- Android GPU Inspector (AGI): system profiling and supported frame profiling for GPU/CPU/memory behavior.
- RenderDoc: frame capture and inspection of Vulkan and OpenGL ES for supported debuggable/authorized targets.
- Khronos Vulkan validation layers: validate Vulkan API usage and layer-chain correctness before measuring performance.

A hook backend reporting "ready" is only an initialization result. It is not evidence that a game's draw calls were intercepted, a render target was obtained, or an upscaler was presented.
