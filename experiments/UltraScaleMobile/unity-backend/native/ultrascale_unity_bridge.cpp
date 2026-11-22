#include "../include/ultrascale_unity_bridge.h"
#include <cmath>
#include <cstdint>

namespace {
bool supported_api(USUnityGraphicsApi api) {
    return api == US_UNITY_GLES || api == US_UNITY_VULKAN;
}
bool supported_format(USUnityPixelFormat format) {
    return format == US_UNITY_FORMAT_RGBA8 ||
           format == US_UNITY_FORMAT_BGRA8 ||
           format == US_UNITY_FORMAT_RGBA16F;
}
bool valid_mode(USUpscaleMode mode) {
    return mode >= US_UPSCALE_BILINEAR && mode <= US_UPSCALE_FSR1_EASU;
}
bool valid(const USUnityRenderFrame* f, const USUnityScalerSettings* s,
           const USUnityBridgeResult* r) {
    constexpr uint32_t allowed_flags =
        US_UNITY_FRAME_TARGET_OWNED | US_UNITY_FRAME_SAME_QUEUE_ORDERED;
    return f && s && r &&
        f->struct_size >= sizeof(USUnityRenderFrame) &&
        s->struct_size >= sizeof(USUnityScalerSettings) &&
        r->struct_size >= sizeof(USUnityBridgeResult) &&
        f->abi_version == US_UNITY_BRIDGE_ABI_VERSION &&
        s->abi_version == US_UNITY_BRIDGE_ABI_VERSION &&
        r->abi_version == US_UNITY_BRIDGE_ABI_VERSION &&
        f->width > 0 && f->height > 0 &&
        f->width <= US_UNITY_MAX_FRAME_DIMENSION &&
        f->height <= US_UNITY_MAX_FRAME_DIMENSION &&
        f->native_image_or_texture != 0 &&
        (f->flags & US_UNITY_FRAME_TARGET_OWNED) != 0 &&
        (f->flags & ~allowed_flags) == 0 &&
        valid_mode(s->mode) &&
        (s->cas_enabled == 0 || s->cas_enabled == 1) &&
        std::isfinite(s->render_scale) &&
        s->render_scale >= 0.5f && s->render_scale <= 1.0f &&
        std::isfinite(s->cas_sharpness) &&
        s->cas_sharpness >= 0.0f && s->cas_sharpness <= 1.0f;
}
void set_result(USUnityBridgeResult* r, USUnityBackendStatus status,
                int32_t error, uint64_t processed_index) {
    r->status = status;
    r->error_code = error;
    r->processed_frame_index = processed_index;
}
}

extern "C" {
uint32_t ultrascale_unity_bridge_abi(void) {
    return US_UNITY_BRIDGE_ABI_VERSION;
}

int32_t ultrascale_unity_process_frame(const USUnityRenderFrame* frame,
                                      const USUnityScalerSettings* settings,
                                      USUnityBridgeResult* result) {
    if (!valid(frame, settings, result))
        return US_UNITY_ERROR_INVALID_ARGUMENT;

    if (!supported_api(frame->graphics_api)) {
        set_result(result, US_UNITY_UNSUPPORTED,
                   US_UNITY_ERROR_UNSUPPORTED_API, 0);
        return US_UNITY_ERROR_UNSUPPORTED_API;
    }
    if (!supported_format(frame->format)) {
        set_result(result, US_UNITY_UNSUPPORTED,
                   US_UNITY_ERROR_UNSUPPORTED_FORMAT, 0);
        return US_UNITY_ERROR_UNSUPPORTED_FORMAT;
    }

    // No GLES/Vulkan processor is attached yet. Never claim a frame was
    // processed; keep the status explicit until a real GPU backend is wired.
    set_result(result, US_UNITY_INTEGRATION_REQUIRED,
               US_UNITY_ERROR_NO_SCALER_ATTACHED, 0);
    return US_UNITY_ERROR_NO_SCALER_ATTACHED;
}

USUnityBackendStatus ultrascale_unity_backend_status(void) {
    return US_UNITY_INTEGRATION_REQUIRED;
}
}
