#include "../../include/ultrascale_unity_bridge.h"
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

    return 0;
}
