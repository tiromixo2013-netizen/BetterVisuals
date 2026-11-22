#include "../include/ultrascale_unity_bridge.h"
#include <cmath>
#include <cstddef>

namespace {
constexpr int32_t kInvalidArgument = -1;
constexpr int32_t kUnsupportedApi = -2;
constexpr int32_t kNoScalerAttached = -3;

bool valid(const USUnityRenderFrame* f, const USUnityScalerSettings* s,
           const USUnityBridgeResult* r) {
    return f && s && r &&
        f->struct_size >= sizeof(USUnityRenderFrame) &&
        s->struct_size >= sizeof(USUnityScalerSettings) &&
        r->struct_size >= sizeof(USUnityBridgeResult) &&
        f->abi_version == US_UNITY_BRIDGE_ABI_VERSION &&
        s->abi_version == US_UNITY_BRIDGE_ABI_VERSION &&
        r->abi_version == US_UNITY_BRIDGE_ABI_VERSION &&
        f->width > 0 && f->height > 0 &&
        f->native_image_or_texture != 0 &&
        std::isfinite(s->render_scale) && s->render_scale >= 0.5f && s->render_scale <= 1.0f &&
        std::isfinite(s->cas_sharpness) && s->cas_sharpness >= 0.0f && s->cas_sharpness <= 1.0f;
}
}

extern "C" {

uint32_t ultrascale_unity_bridge_abi(void) {
    return US_UNITY_BRIDGE_ABI_VERSION;
}

int32_t ultrascale_unity_process_frame(const USUnityRenderFrame* frame,
                                      const USUnityScalerSettings* settings,
                                      USUnityBridgeResult* result) {
    if (!valid(frame, settings, result)) return kInvalidArgument;

    result->status = US_UNITY_INTEGRATION_REQUIRED;
    result->error_code = (frame->graphics_api == US_UNITY_GLES ||
                          frame->graphics_api == US_UNITY_VULKAN)
                             ? kNoScalerAttached : kUnsupportedApi;
    result->processed_frame_index = 0;

    // Deliberately fail closed: this SDK boundary does not pretend a frame was
    // upscaled. A graphics-API-specific renderer must be attached and validated
    // before this function can return US_UNITY_ACTIVE.
    return result->error_code;
}

USUnityBackendStatus ultrascale_unity_backend_status(void) {
    return US_UNITY_INTEGRATION_REQUIRED;
}

}
