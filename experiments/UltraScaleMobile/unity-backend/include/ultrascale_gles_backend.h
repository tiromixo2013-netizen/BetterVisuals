#pragma once
#include <stdint.h>
#if defined(_WIN32)
# define US_GLES_API __declspec(dllexport)
#else
# define US_GLES_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef enum USGlesMode {
    US_GLES_MODE_BILINEAR = 0,
    US_GLES_MODE_EDGE_AWARE = 1
} USGlesMode;

typedef enum USGlesResult {
    US_GLES_OK = 0,
    US_GLES_INVALID_ARGUMENT = -1,
    US_GLES_NO_CONTEXT = -2,
    US_GLES_SHADER_ERROR = -3,
    US_GLES_FRAMEBUFFER_ERROR = -4,
    US_GLES_GL_ERROR = -5,
    US_GLES_UNSUPPORTED_PLATFORM = -6
} USGlesResult;

/*
 * Execute on the owning Unity OpenGL ES render thread with its context current.
 * src_texture and dst_texture are GL_TEXTURE_2D names owned by the caller.
 * They must be distinct, initialized, non-multisampled RGBA textures.
 * This function preserves the GL state it changes. It does not create a
 * context, cross process boundaries, or inject into another application.
 */
US_GLES_API int32_t ultrascale_gles_process_texture(
    uint32_t src_texture, uint32_t dst_texture,
    uint32_t input_width, uint32_t input_height,
    uint32_t output_width, uint32_t output_height,
    USGlesMode mode, float sharpness);

US_GLES_API const char* ultrascale_gles_backend_name(void);

#ifdef __cplusplus
}
#endif
