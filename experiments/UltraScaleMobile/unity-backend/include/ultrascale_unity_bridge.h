#pragma once

/*
 * UltraScale Unity bridge ABI — integration contract only.
 * This API accepts only render targets explicitly supplied by an owner-controlled
 * Unity integration. It does not attach to other processes or install hooks.
 */
#include <stdint.h>

#if defined(_WIN32)
#  define US_UNITY_API __declspec(dllexport)
#else
#  define US_UNITY_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define US_UNITY_BRIDGE_ABI_VERSION 1u
#define US_UNITY_MAX_FRAME_DIMENSION 16384u

typedef enum USUnityGraphicsApi {
    US_UNITY_GLES = 1,
    US_UNITY_VULKAN = 2
} USUnityGraphicsApi;

typedef enum USUnityPixelFormat {
    US_UNITY_FORMAT_RGBA8 = 1,
    US_UNITY_FORMAT_BGRA8 = 2,
    US_UNITY_FORMAT_RGBA16F = 3
} USUnityPixelFormat;

typedef enum USUnityFrameFlags {
    US_UNITY_FRAME_TARGET_OWNED = 1u << 0,
    US_UNITY_FRAME_SAME_QUEUE_ORDERED = 1u << 1
} USUnityFrameFlags;

typedef enum USUnityBridgeError {
    US_UNITY_OK = 0,
    US_UNITY_ERROR_INVALID_ARGUMENT = -1,
    US_UNITY_ERROR_UNSUPPORTED_API = -2,
    US_UNITY_ERROR_NO_SCALER_ATTACHED = -3,
    US_UNITY_ERROR_UNSUPPORTED_FORMAT = -4,
    US_UNITY_ERROR_INVALID_FRAME = -5
} USUnityBridgeError;

typedef enum USUnityBackendStatus {
    US_UNITY_DETECTED = 0,
    US_UNITY_INTEGRATION_REQUIRED = 1,
    US_UNITY_UNSUPPORTED = 2,
    US_UNITY_FAILED = 3,
    US_UNITY_ACTIVE = 4
} USUnityBackendStatus;

typedef enum USUpscaleMode {
    US_UPSCALE_BILINEAR = 0,
    US_UPSCALE_FSR_LITE = 1,
    US_UPSCALE_MWSR = 2,
    US_UPSCALE_SGSR1 = 3,
    US_UPSCALE_FSR1_EASU = 4
} USUpscaleMode;

typedef struct USUnityRenderFrame {
    uint32_t struct_size;
    uint32_t abi_version;
    USUnityGraphicsApi graphics_api;
    uint32_t width;
    uint32_t height;
    USUnityPixelFormat format;
    uint64_t native_image_or_texture;
    uint64_t frame_index;
    uint64_t synchronization_token;
    uint32_t flags;
} USUnityRenderFrame;

typedef struct USUnityScalerSettings {
    uint32_t struct_size;
    uint32_t abi_version;
    USUpscaleMode mode;
    float render_scale;
    float cas_sharpness;
    uint32_t cas_enabled;
} USUnityScalerSettings;

typedef struct USUnityBridgeResult {
    uint32_t struct_size;
    uint32_t abi_version;
    USUnityBackendStatus status;
    int32_t error_code;
    uint64_t processed_frame_index;
} USUnityBridgeResult;

/*
 * native_image_or_texture is an opaque API-owned render-target handle, never
 * a CPU pointer or cross-process address. TARGET_OWNED is mandatory.
 * SAME_QUEUE_ORDERED declares that the caller guarantees ordering on the
 * graphics queue; otherwise a future native backend must consume a real
 * synchronization primitive represented by synchronization_token.
 */
US_UNITY_API uint32_t ultrascale_unity_bridge_abi(void);
US_UNITY_API int32_t ultrascale_unity_process_frame(
    const USUnityRenderFrame* frame,
    const USUnityScalerSettings* settings,
    USUnityBridgeResult* result);
US_UNITY_API USUnityBackendStatus ultrascale_unity_backend_status(void);

#ifdef __cplusplus
}
#endif
