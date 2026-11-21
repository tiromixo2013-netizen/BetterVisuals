# Research notes — Android game graphics layers

Research sources reviewed before designing this branch:

1. [Android NDK: GLES layers](https://developer.android.com/ndk/guides/rootless-debug-gles)
   - Defines the required `AndroidGLESLayer_Initialize` and `AndroidGLESLayer_GetProcAddress` entry points.
   - Documents per-app settings, layer filenames, and the opt-in/debuggable/userdebug eligibility rules.
2. [Android NDK: Vulkan validation layers](https://developer.android.com/ndk/guides/graphics/validation-layer)
   - Documents packaging `libVkLayer*.so` in APK native library directories and the per-app `gpu_debug_*` settings.
   - Android's loader enumerates Android layers through introspection entry points; desktop JSON manifest workflows should not be blindly copied.
3. [AOSP: Vulkan implementation and security model](https://source.android.com/docs/core/graphics/implement-vulkan)
   - Confirms that external code injection into arbitrary non-debuggable production processes is not a supported Android use case.
4. [ARM-software/patrace Android GLES layer notes](https://github.com/ARM-software/patrace/blob/master/patrace/doc/manual.md)
   - A practical community implementation showing the GLES layer chain and per-app debug settings. Its older rooted/system-directory steps are not copied into this no-root path.
5. [android-gpu-profiler community project](https://github.com/hongsulovey/android-gpu-profiler)
   - Demonstrates a separate layer APK, target package selection, Vulkan/GLES layer names, and runtime attach diagnostics.
6. [RikkaApps Shizuku-API UserService guide](https://github.com/RikkaApps/Shizuku-API)
   - Recommends UserService for longer-lived privileged operations and marks `newProcess` as deprecated/planned for removal in API 14. This branch uses UserService IPC.
7. [Vulkan loader layer guide](https://docs.vulkan.org/guide/latest/layers.html)
   - Explains layer chaining and the loader/layer interface.
8. [Reddit community discussion: switching Android HWUI to Vulkan](https://www.reddit.com/r/GalaxyS23Ultra/comments/1k8trp4/switch_to_vulkan_easily/)
   - Community members share Shizuku/ADB commands that change the Android UI renderer. This changes HWUI, not a game's Vulkan/GLES process, so it is not treated as an injection solution.
9. [Android NDK sample: LearnGLES](https://github.com/zoozooll/LearnGLES)
   - Useful reference for GLES shaders, native CMake, and Android rendering tests.

## Community findings translated into our design
- The useful repeatable pattern is a loader-supported layer, not a screenshot overlay or a generic process-memory patch.
- Both APIs need independent adapters and entry points; an EGL hook is not a Vulkan hook.
- Keep the first layer pass-through and observable. Prove it loads and forwards calls before inserting render passes.
- Layer discovery and API detection are separate: APK signatures are only hints; runtime loader logs are the confirmation.
- The Reddit posts about changing `debug.hwui.renderer` to `skiavk` alter Android UI rendering and do not inject into a game's renderer. That approach is deliberately excluded.
- Shizuku can set supported debug-layer settings when started with ADB, but shell UID 2000 is not root and cannot remove target-app opt-in requirements.

## Current experiment status
- GLES: pass-through layer intercepts `eglSwapBuffers` and logs periodic frame counts.
- Vulkan: pass-through layer exposes Android loader introspection, tracks instance/physical-device/device dispatch, and observes queue present calls.
- The owned sample-game module opts in, renders a synthetic checker/grid/diagonal test scene through an off-screen GLES framebuffer, and creates a Vulkan instance/device for a separate API smoke test. Its in-process spatial shader passes are distinct from the external pass-through layers.
- Android CI has compiled and packaged the sample APK and shader assets. Actual on-device shader compilation, rendered-image inspection, GPU timing and thermal validation remain unverified; a successful CI build is not a device-runtime pass.
