#include "../../include/ultrascale_unity_bridge.h"
#include "../../include/ultrascale_gles_backend.h"
#include <cassert>
#include <cstdint>

namespace {
USUnityRenderFrame frame() {
    return {sizeof(USUnityRenderFrame), US_UNITY_BRIDGE_ABI_VERSION,
            US_UNITY_GLES, 1280, 720, US_UNITY_FORMAT_RGBA8,
            1, 42, 0, US_UNITY_FRAME_TARGET_OWNED |
                       US_UNITY_FRAME_SAME_QUEUE_ORDERED};
}
USUnityScalerSettings settings() {
    return {sizeof(USUnityScalerSettings), US_UNITY_BRIDGE_ABI_VERSION,
            US_UPSCALE_FSR1_EASU, 0.75f, 0.25f, 1};
}
USUnityBridgeResult result() {
    return {sizeof(USUnityBridgeResult), US_UNITY_BRIDGE_ABI_VERSION,
            US_UNITY_FAILED, 99, 999};
}
}

int main() {
    assert(ultrascale_unity_bridge_abi() == US_UNITY_BRIDGE_ABI_VERSION);
#if !defined(__ANDROID__)
    assert(ultrascale_gles_backend_name() != nullptr);
    assert(ultrascale_gles_process_texture(1, 2, 16, 16, 32, 32,
        US_GLES_MODE_EDGE_AWARE, 0.25f) == US_GLES_UNSUPPORTED_PLATFORM);
#endif
    assert(ultrascale_unity_backend_status() == US_UNITY_INTEGRATION_REQUIRED);

    auto f = frame();
    auto s = settings();
    auto r = result();

    // Valid frame is rejected as not-yet-integrated, never marked ACTIVE.
    assert(ultrascale_unity_process_frame(&f, &s, &r) ==
           US_UNITY_ERROR_NO_SCALER_ATTACHED);
    assert(r.status == US_UNITY_INTEGRATION_REQUIRED);
    assert(r.processed_frame_index == 0);

    // Unknown scaler values are rejected.
    s.mode = static_cast<USUpscaleMode>(99);
    r = result();
    assert(ultrascale_unity_process_frame(&f, &s, &r) ==
           US_UNITY_ERROR_INVALID_ARGUMENT);

    // Invalid dimensions are rejected.
    s = settings();
    f.width = US_UNITY_MAX_FRAME_DIMENSION + 1;
    r = result();
    assert(ultrascale_unity_process_frame(&f, &s, &r) ==
           US_UNITY_ERROR_INVALID_ARGUMENT);

    // The caller must explicitly declare ownership of the render target.
    f = frame();
    f.flags = US_UNITY_FRAME_SAME_QUEUE_ORDERED;
    r = result();
    assert(ultrascale_unity_process_frame(&f, &s, &r) ==
           US_UNITY_ERROR_INVALID_ARGUMENT);

    // Unsupported pixel formats are reported distinctly.
    f = frame();
    f.format = static_cast<USUnityPixelFormat>(77);
    r = result();
    assert(ultrascale_unity_process_frame(&f, &s, &r) ==
           US_UNITY_ERROR_UNSUPPORTED_FORMAT);
    assert(r.status == US_UNITY_UNSUPPORTED);

    // Unsupported graphics APIs are reported distinctly.
    f = frame();
    f.graphics_api = static_cast<USUnityGraphicsApi>(77);
    r = result();
    assert(ultrascale_unity_process_frame(&f, &s, &r) ==
           US_UNITY_ERROR_UNSUPPORTED_API);
    assert(r.status == US_UNITY_UNSUPPORTED);

    // Non-boolean CAS flags are invalid.
    f = frame();
    s.cas_enabled = 2;
    r = result();
    assert(ultrascale_unity_process_frame(&f, &s, &r) ==
           US_UNITY_ERROR_INVALID_ARGUMENT);

    // ABI mismatches and non-finite floats must not cross the boundary.
    f = frame();
    s = settings();
    s.abi_version = US_UNITY_BRIDGE_ABI_VERSION + 1;
    r = result();
    assert(ultrascale_unity_process_frame(&f, &s, &r) ==
           US_UNITY_ERROR_INVALID_ARGUMENT);

    s = settings();
    s.render_scale = 0.0f / 0.0f;
    r = result();
    assert(ultrascale_unity_process_frame(&f, &s, &r) ==
           US_UNITY_ERROR_INVALID_ARGUMENT);

    // Reserved flag bits are rejected rather than silently ignored.
    s = settings();
    f.flags |= 1u << 9;
    r = result();
    assert(ultrascale_unity_process_frame(&f, &s, &r) ==
           US_UNITY_ERROR_INVALID_ARGUMENT);

    return 0;
}
