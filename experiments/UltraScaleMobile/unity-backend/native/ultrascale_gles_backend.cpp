#include "ultrascale_gles_backend.h"
#include <cmath>
#include <cstdint>
#include <atomic>
#include <deque>
#include <mutex>

#if defined(__ANDROID__)
namespace {
struct GlesJob { uint32_t src, dst, in_w, in_h, out_w, out_h; USGlesMode mode; float sharpness; };
std::mutex g_job_mutex;
std::deque<GlesJob> g_jobs;
std::atomic<int32_t> g_last_result{US_GLES_UNSUPPORTED_PLATFORM};
constexpr size_t kMaxQueuedJobs = 4;
}
#endif

#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES3/gl3.h>

namespace {
struct GlesResources {
    EGLContext context = EGL_NO_CONTEXT;
    GLuint program = 0;
    GLuint framebuffer = 0;
    GLuint vertex_array = 0;
};
GlesResources g_resources;

const char* kVertexShader = R"(#version 300 es
precision highp float;
out vec2 v_uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_uv = p * 0.5;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

const char* kFragmentShader = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_source;
uniform vec2 u_texel;
uniform int u_mode;
uniform float u_sharpness;
out vec4 out_color;
void main() {
    vec4 c = texture(u_source, v_uv);
    if (u_mode == 1) {
        vec4 n = texture(u_source, v_uv + vec2(0.0, -u_texel.y));
        vec4 s = texture(u_source, v_uv + vec2(0.0,  u_texel.y));
        vec4 e = texture(u_source, v_uv + vec2( u_texel.x, 0.0));
        vec4 w = texture(u_source, v_uv + vec2(-u_texel.x, 0.0));
        vec3 cross_avg = (n.rgb + s.rgb + e.rgb + w.rgb) * 0.25;
        vec3 detail = c.rgb - cross_avg;
        float edge = clamp(length(detail) * 2.0, 0.0, 1.0);
        c.rgb += detail * clamp(u_sharpness, 0.0, 1.0) * edge;
    }
    out_color = vec4(c.rgb, c.a);
})";

GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint create_program() {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertexShader);
    if (!vs) return 0;
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

bool has_current_context() {
    return eglGetCurrentContext() != EGL_NO_CONTEXT &&
           glGetString(GL_VERSION) != nullptr;
}

bool ensure_resources() {
    EGLContext current = eglGetCurrentContext();
    if (current == EGL_NO_CONTEXT) return false;
    if (g_resources.context != current) {
        // Names from the previous context are no longer valid in this one.
        g_resources = {};
        g_resources.context = current;
    }
    if (!g_resources.program || !glIsProgram(g_resources.program))
        g_resources.program = create_program();
    if (!g_resources.framebuffer || !glIsFramebuffer(g_resources.framebuffer))
        glGenFramebuffers(1, &g_resources.framebuffer);
    if (!g_resources.vertex_array || !glIsVertexArray(g_resources.vertex_array))
        glGenVertexArrays(1, &g_resources.vertex_array);
    return g_resources.program != 0 && g_resources.framebuffer != 0 &&
           g_resources.vertex_array != 0;
}

struct GlState {
    GLint draw_framebuffer = 0;
    GLint read_framebuffer = 0;
    GLint viewport[4] = {};
    GLint color_mask[4] = {1, 1, 1, 1};
    GLint depth_mask = 1;
    GLint program = 0;
    GLint active_texture = 0;
    GLint texture0 = 0;
    GLint vao = 0;
    GLint array_buffer = 0;
    GLint blend = 0, depth = 0, scissor = 0, cull = 0, stencil = 0;
};

void restore_state(const GlState& s) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(s.draw_framebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(s.read_framebuffer));
    glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
    glUseProgram(static_cast<GLuint>(s.program));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(s.texture0));
    glBindVertexArray(static_cast<GLuint>(s.vao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(s.array_buffer));
    if (s.blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (s.depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (s.scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (s.cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (s.stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    glColorMask(s.color_mask[0], s.color_mask[1], s.color_mask[2], s.color_mask[3]);
    glDepthMask(s.depth_mask);
    glActiveTexture(static_cast<GLenum>(s.active_texture));
}
}

extern "C" int32_t ultrascale_gles_process_texture(
    uint32_t src_texture, uint32_t dst_texture,
    uint32_t input_width, uint32_t input_height,
    uint32_t output_width, uint32_t output_height,
    USGlesMode mode, float sharpness) {
    if (!src_texture || !dst_texture || src_texture == dst_texture ||
        !input_width || !input_height || !output_width || !output_height ||
        input_width > 16384 || input_height > 16384 ||
        output_width > 16384 || output_height > 16384 ||
        (mode != US_GLES_MODE_BILINEAR && mode != US_GLES_MODE_EDGE_AWARE) ||
        !std::isfinite(sharpness) || sharpness < 0.0f || sharpness > 1.0f)
        return US_GLES_INVALID_ARGUMENT;
    if (!has_current_context()) return US_GLES_NO_CONTEXT;

    while (glGetError() != GL_NO_ERROR) {}
    GlState old;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old.draw_framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old.read_framebuffer);
    glGetIntegerv(GL_VIEWPORT, old.viewport);
    glGetIntegerv(GL_COLOR_WRITEMASK, old.color_mask);
    glGetIntegerv(GL_DEPTH_WRITEMASK, &old.depth_mask);
    glGetIntegerv(GL_CURRENT_PROGRAM, &old.program);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old.active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old.texture0);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old.vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old.array_buffer);
    old.blend = glIsEnabled(GL_BLEND);
    old.depth = glIsEnabled(GL_DEPTH_TEST);
    old.scissor = glIsEnabled(GL_SCISSOR_TEST);
    old.cull = glIsEnabled(GL_CULL_FACE);
    old.stencil = glIsEnabled(GL_STENCIL_TEST);

    if (!glIsTexture(src_texture) || !glIsTexture(dst_texture)) {
        restore_state(old);
        return US_GLES_INVALID_ARGUMENT;
    }
    // GLES 3.0 has no core texture-level dimension query. The owner supplies
    // dimensions from the Unity RenderTexture descriptors; FBO completeness
    // below validates that the destination can be rendered to.
    if (!ensure_resources()) {
        restore_state(old);
        return US_GLES_GL_ERROR;
    }
    const GLuint program = g_resources.program;
    const GLuint fbo = g_resources.framebuffer;
    const GLuint vao = g_resources.vertex_array;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, dst_texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        restore_state(old);
        return US_GLES_FRAMEBUFFER_ERROR;
    }

    glViewport(0, 0, static_cast<GLsizei>(output_width),
               static_cast<GLsizei>(output_height));
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vao);
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_texture);
    glUniform1i(glGetUniformLocation(program, "u_source"), 0);
    glUniform2f(glGetUniformLocation(program, "u_texel"),
                1.0f / static_cast<float>(input_width),
                1.0f / static_cast<float>(input_height));
    glUniform1i(glGetUniformLocation(program, "u_mode"), static_cast<GLint>(mode));
    glUniform1f(glGetUniformLocation(program, "u_sharpness"), sharpness);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    GLenum error = glGetError();

    restore_state(old);
    return error == GL_NO_ERROR ? US_GLES_OK : US_GLES_GL_ERROR;
}

extern "C" const char* ultrascale_gles_backend_name(void) {
    return "UltraScale GLES3 spatial backend (bilinear + edge-aware sharpen)";
}

extern "C" void ultrascale_gles_render_event(int event_id) {
    if (event_id != 1) return;
    GlesJob job{};
    {
        std::lock_guard<std::mutex> lock(g_job_mutex);
        if (g_jobs.empty()) return;
        job = g_jobs.front();
        g_jobs.pop_front();
    }
    g_last_result.store(ultrascale_gles_process_texture(
        job.src, job.dst, job.in_w, job.in_h, job.out_w, job.out_h,
        job.mode, job.sharpness));
}

extern "C" int32_t ultrascale_gles_queue_texture_job(
    uint32_t src, uint32_t dst, uint32_t in_w, uint32_t in_h,
    uint32_t out_w, uint32_t out_h, USGlesMode mode, float sharpness) {
    if (!src || !dst || src == dst || !in_w || !in_h || !out_w || !out_h ||
        in_w > 16384 || in_h > 16384 || out_w > 16384 || out_h > 16384 ||
        (mode != US_GLES_MODE_BILINEAR && mode != US_GLES_MODE_EDGE_AWARE) ||
        !std::isfinite(sharpness) || sharpness < 0.0f || sharpness > 1.0f)
        return US_GLES_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_job_mutex);
    if (g_jobs.size() >= kMaxQueuedJobs) return US_GLES_INVALID_ARGUMENT;
    g_jobs.push_back({src, dst, in_w, in_h, out_w, out_h, mode, sharpness});
    return US_GLES_OK;
}

extern "C" USGlesUnityRenderEvent ultrascale_gles_get_render_event_func(void) {
    return &ultrascale_gles_render_event;
}

extern "C" int32_t ultrascale_gles_get_last_result(void) {
    return g_last_result.load();
}

#else
extern "C" int32_t ultrascale_gles_process_texture(
    uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
    USGlesMode, float) {
    return US_GLES_UNSUPPORTED_PLATFORM;
}
extern "C" const char* ultrascale_gles_backend_name(void) {
    return "UltraScale GLES backend unavailable on this platform";
}
extern "C" int32_t ultrascale_gles_queue_texture_job(
    uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
    USGlesMode, float) { return US_GLES_UNSUPPORTED_PLATFORM; }
extern "C" USGlesUnityRenderEvent ultrascale_gles_get_render_event_func(void) { return nullptr; }
extern "C" int32_t ultrascale_gles_get_last_result(void) {
    return US_GLES_UNSUPPORTED_PLATFORM;
}
#endif
