#pragma once

/*
 * UltraScale Unity bridge ABI — integration contract only.
 *
 * This header does not attach to a process or install hooks. A supported,
 * owner-controlled Unity plugin calls this API from its documented render
 * integration point after it has acquired a valid render target.
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

typedef enum USUnityGraphicsApi {
    US_UNITY_GLES = 1,
    US_UNITY_VULKAN = 2
} USUnityGraphicsApi;

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
    uint32_t format;
    uint64_t native_image_or_texture;
    uint64_t frame_index;
    uint64_t synchronization_token;
    uint32_t flags;
} USUnityRenderFrame;

typedef struct USUnityScalerSettings {
    uint32_t struct_size;
    uint32_t abi_version;
    USUpscaleMode mode;
    float render_scale;       /* Valid range: 0.50f .. 1.00f */
    float cas_sharpness;      /* Valid range: 0.00f .. 1.00f */
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
 * The plugin must pass a render target it owns and is authorized to expose.
 * Implementations must validate dimensions, format, API and synchronization.
 * No raw game-memory addresses or cross-process handles are accepted here.
 */
US_UNITY_API uint32_t ultrascale_unity_bridge_abi(void);

US_UNITY_API int32_t ultrascale_unity_process_frame(
    const USUnityRenderFrame* frame,
    const USUnityScalerSettings* settings,
    USUnityBridgeResult* result);

US_UNITY_API USUnityBackendStatus ultrascale_unity_backend_status(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
