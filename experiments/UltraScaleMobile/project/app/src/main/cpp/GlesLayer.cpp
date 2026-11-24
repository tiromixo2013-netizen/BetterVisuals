#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <fstream>
#include <string>
#include <cstdlib>

using GetNextProc = void* (*)(void*, const char*);
using EglSwapBuffers = EGLBoolean (*)(EGLDisplay, EGLSurface);
using EglQuerySurface = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLint, EGLint*);
using EglDestroyContext = EGLBoolean (*)(EGLDisplay, EGLContext);
using GlBindFramebuffer = void (*)(GLenum, GLuint);
using GlBindTexture = void (*)(GLenum, GLuint);
using GlViewport = void (*)(GLint, GLint, GLsizei, GLsizei);
using GlDrawArrays = void (*)(GLenum, GLint, GLsizei);
using GlDrawElements = void (*)(GLenum, GLsizei, GLenum, const void*);
using GlFramebufferTexture2D = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
using GlCheckFramebufferStatus = GLenum (*)(GLenum);
using GlTexImage2D = void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
using GlTexStorage2D = void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
using GlDeleteTextures = void (*)(GLsizei, const GLuint*);
using GlDeleteFramebuffers = void (*)(GLsizei, const GLuint*);
using GlBindRenderbuffer = void (*)(GLenum, GLuint);
using GlRenderbufferStorage = void (*)(GLenum, GLenum, GLsizei, GLsizei);
using GlRenderbufferStorageMultisample = void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
using GlFramebufferRenderbuffer = void (*)(GLenum, GLenum, GLenum, GLuint);
using GlDeleteRenderbuffers = void (*)(GLsizei, const GLuint*);

namespace {
struct TextureInfo {
    GLuint texture = 0;
    GLsizei width = 0;
    GLsizei height = 0;
    GLenum internalFormat = 0;
    bool isRenderbuffer = false;
    GLsizei samples = 0;
};
struct ContextState {
    // Keep draw/read bindings separate: GL_FRAMEBUFFER updates both, while
    // GL_DRAW_FRAMEBUFFER and GL_READ_FRAMEBUFFER update only their own slot.
    GLuint boundFramebuffer = 0; // draw framebuffer (legacy field name)
    GLuint boundReadFramebuffer = 0;
    GLuint boundTexture2D = 0;
    GLuint boundRenderbuffer = 0;
    GLint viewportX = 0;
    GLint viewportY = 0;
    GLsizei viewportWidth = 0;
    GLsizei viewportHeight = 0;
    uint64_t drawCalls = 0;
    uint64_t attachmentUpdates = 0;
    GLenum lastFramebufferStatus = 0;
    GLuint lastAttachedTexture = 0;
    GLuint lastDrawFramebuffer = 0;
    GLuint lastDrawTexture = 0;
    TextureInfo lastDrawTextureInfo{};
    std::unordered_map<GLuint, TextureInfo> textures;
    std::unordered_map<GLuint, TextureInfo> renderbuffers;
    std::unordered_map<GLuint, std::unordered_map<GLenum, TextureInfo>> framebufferAttachments;
};
struct FrameTelemetry {
    GLuint boundFramebuffer = 0;
    GLint viewportX = 0;
    GLint viewportY = 0;
    GLsizei viewportWidth = 0;
    GLsizei viewportHeight = 0;
    uint64_t drawCalls = 0;
    uint64_t attachmentUpdates = 0;
    GLenum lastFramebufferStatus = 0;
    GLuint lastAttachedTexture = 0;
    GLuint lastDrawFramebuffer = 0;
    GLuint lastDrawTexture = 0;
    TextureInfo targetTextureInfo{};
};

std::mutex g_contextMutex;
std::unordered_map<EGLContext, ContextState> g_contexts;
std::atomic<uint64_t> g_frames{0};
std::atomic<bool> g_glIdentityLogged{false};

void* g_layerId = nullptr;
GetNextProc g_getNext = nullptr;
EglSwapBuffers g_nextSwapBuffers = nullptr;
EglQuerySurface g_nextQuerySurface = nullptr;
EglDestroyContext g_nextDestroyContext = nullptr;
GlBindFramebuffer g_nextBindFramebuffer = nullptr;
GlBindTexture g_nextBindTexture = nullptr;
GlViewport g_nextViewport = nullptr;
GlDrawArrays g_nextDrawArrays = nullptr;
GlDrawElements g_nextDrawElements = nullptr;
GlFramebufferTexture2D g_nextFramebufferTexture2D = nullptr;
GlCheckFramebufferStatus g_nextCheckFramebufferStatus = nullptr;
GlTexImage2D g_nextTexImage2D = nullptr;
GlTexStorage2D g_nextTexStorage2D = nullptr;
GlDeleteTextures g_nextDeleteTextures = nullptr;
GlDeleteFramebuffers g_nextDeleteFramebuffers = nullptr;
GlBindRenderbuffer g_nextBindRenderbuffer = nullptr;
GlRenderbufferStorage g_nextRenderbufferStorage = nullptr;
GlRenderbufferStorageMultisample g_nextRenderbufferStorageMultisample = nullptr;
GlFramebufferRenderbuffer g_nextFramebufferRenderbuffer = nullptr;
GlDeleteRenderbuffers g_nextDeleteRenderbuffers = nullptr;

EGLContext CurrentContext() {
    return eglGetCurrentContext();
}

ContextState& StateFor(EGLContext context) {
    return g_contexts[context];
}

void RefreshFramebufferTextureInfo(ContextState& state, GLuint texture) {
    const auto found = state.textures.find(texture);
    if (found == state.textures.end()) return;
    for (auto& framebuffer : state.framebufferAttachments) {
        for (auto& attachment : framebuffer.second) {
            if (!attachment.second.isRenderbuffer && attachment.second.texture == texture)
                attachment.second = found->second;
        }
    }
    // A storage redefinition after the last draw must not leave stale dimensions
    // in the next periodic telemetry snapshot.
    if (state.lastDrawTexture == texture) state.lastDrawTextureInfo = found->second;
}


EGLBoolean LayerDestroyContext(EGLDisplay display, EGLContext context) {
    if (!g_nextDestroyContext) return EGL_FALSE;
    const EGLBoolean result = g_nextDestroyContext(display, context);
    if (result == EGL_TRUE) {
        std::lock_guard<std::mutex> lock(g_contextMutex);
        g_contexts.erase(context);
    }
    return result;
}

void LayerBindFramebuffer(GLenum target, GLuint framebuffer) {
    if (g_nextBindFramebuffer) g_nextBindFramebuffer(target, framebuffer);
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER &&
        target != GL_READ_FRAMEBUFFER) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto& state = StateFor(context);
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
        state.boundFramebuffer = framebuffer;
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)
        state.boundReadFramebuffer = framebuffer;
}

void LayerBindTexture(GLenum target, GLuint texture) {
    if (g_nextBindTexture) g_nextBindTexture(target, texture);
    if (target != GL_TEXTURE_2D) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    StateFor(context).boundTexture2D = texture;
}

void LayerTexImage2D(GLenum target, GLint level, GLint internalFormat,
                     GLsizei width, GLsizei height, GLint border,
                     GLenum format, GLenum type, const void* pixels) {
    if (g_nextTexImage2D) {
        g_nextTexImage2D(target, level, internalFormat, width, height, border, format, type, pixels);
    }
    if (target != GL_TEXTURE_2D || level != 0) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto& state = StateFor(context);
    if (state.boundTexture2D != 0) {
        state.textures[state.boundTexture2D] = {
            state.boundTexture2D, width, height, static_cast<GLenum>(internalFormat)
        };
        RefreshFramebufferTextureInfo(state, state.boundTexture2D);
    }
}

void LayerTexStorage2D(GLenum target, GLsizei levels, GLenum internalFormat,
                       GLsizei width, GLsizei height) {
    if (g_nextTexStorage2D) {
        g_nextTexStorage2D(target, levels, internalFormat, width, height);
    }
    if (target != GL_TEXTURE_2D || levels < 1) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto& state = StateFor(context);
    if (state.boundTexture2D != 0) {
        state.textures[state.boundTexture2D] = {state.boundTexture2D, width, height, internalFormat};
        RefreshFramebufferTextureInfo(state, state.boundTexture2D);
    }
}


void LayerBindRenderbuffer(GLenum target, GLuint renderbuffer) {
    if (g_nextBindRenderbuffer) g_nextBindRenderbuffer(target, renderbuffer);
    if (target != GL_RENDERBUFFER) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    StateFor(context).boundRenderbuffer = renderbuffer;
}

void LayerRenderbufferStorage(GLenum target, GLenum internalFormat, GLsizei width, GLsizei height) {
    if (g_nextRenderbufferStorage) g_nextRenderbufferStorage(target, internalFormat, width, height);
    if (target != GL_RENDERBUFFER) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto& state = StateFor(context);
    if (state.boundRenderbuffer) {
        state.renderbuffers[state.boundRenderbuffer] = {
            state.boundRenderbuffer, width, height, internalFormat, true
        };
        for (auto& framebuffer : state.framebufferAttachments) {
            for (auto& attachment : framebuffer.second) {
                if (attachment.second.isRenderbuffer && attachment.second.texture == state.boundRenderbuffer)
                    attachment.second = state.renderbuffers[state.boundRenderbuffer];
            }
        }
        if (state.lastDrawTextureInfo.isRenderbuffer &&
            state.lastDrawTextureInfo.texture == state.boundRenderbuffer)
            state.lastDrawTextureInfo = state.renderbuffers[state.boundRenderbuffer];
    }
}

void LayerRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalFormat,
                                         GLsizei width, GLsizei height) {
    if (g_nextRenderbufferStorageMultisample)
        g_nextRenderbufferStorageMultisample(target, samples, internalFormat, width, height);
    if (target != GL_RENDERBUFFER) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto& state = StateFor(context);
    if (!state.boundRenderbuffer) return;
    state.renderbuffers[state.boundRenderbuffer] = {
        state.boundRenderbuffer, width, height, internalFormat, true, samples
    };
    // The sample count is diagnostic metadata; this probe still does not resolve
    // or read a multisampled renderbuffer.
    for (auto& framebuffer : state.framebufferAttachments) {
        for (auto& attachment : framebuffer.second) {
            if (attachment.second.isRenderbuffer && attachment.second.texture == state.boundRenderbuffer)
                attachment.second = state.renderbuffers[state.boundRenderbuffer];
        }
    }
    if (state.lastDrawTextureInfo.isRenderbuffer &&
        state.lastDrawTextureInfo.texture == state.boundRenderbuffer)
        state.lastDrawTextureInfo = state.renderbuffers[state.boundRenderbuffer];
}

void LayerFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbufferTarget, GLuint renderbuffer) {
    if (g_nextFramebufferRenderbuffer)
        g_nextFramebufferRenderbuffer(target, attachment, renderbufferTarget, renderbuffer);
    if ((target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER) || renderbufferTarget != GL_RENDERBUFFER) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto& state = StateFor(context);
    ++state.attachmentUpdates;
    state.lastAttachedTexture = 0;
    if (renderbuffer == 0) {
        auto fb = state.framebufferAttachments.find(state.boundFramebuffer);
        if (fb != state.framebufferAttachments.end()) {
            fb->second.erase(attachment);
            if (fb->second.empty()) state.framebufferAttachments.erase(fb);
        }
        return;
    }
    const auto found = state.renderbuffers.find(renderbuffer);
    if (found != state.renderbuffers.end())
        state.framebufferAttachments[state.boundFramebuffer][attachment] = found->second;
}

void LayerDeleteRenderbuffers(GLsizei count, const GLuint* renderbuffers) {
    if (g_nextDeleteRenderbuffers) g_nextDeleteRenderbuffers(count, renderbuffers);
    if (!renderbuffers || count <= 0) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto found = g_contexts.find(context);
    if (found == g_contexts.end()) return;
    auto& state = found->second;
    for (GLsizei i = 0; i < count; ++i) {
        const GLuint id = renderbuffers[i];
        state.renderbuffers.erase(id);
        if (state.boundRenderbuffer == id) state.boundRenderbuffer = 0;
        for (auto fb = state.framebufferAttachments.begin(); fb != state.framebufferAttachments.end();) {
            for (auto attachment = fb->second.begin(); attachment != fb->second.end();) {
                if (attachment->second.isRenderbuffer && attachment->second.texture == id)
                    attachment = fb->second.erase(attachment);
                else ++attachment;
            }
            if (fb->second.empty()) fb = state.framebufferAttachments.erase(fb);
            else ++fb;
        }
    }
}

void LayerDeleteTextures(GLsizei count, const GLuint* textures) {
    if (g_nextDeleteTextures) g_nextDeleteTextures(count, textures);
    if (!textures || count <= 0) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto found = g_contexts.find(context);
    if (found == g_contexts.end()) return;
    auto& state = found->second;
    for (GLsizei i = 0; i < count; ++i) {
        const GLuint texture = textures[i];
        state.textures.erase(texture);
        if (state.boundTexture2D == texture) state.boundTexture2D = 0;
        for (auto framebuffer = state.framebufferAttachments.begin();
             framebuffer != state.framebufferAttachments.end();) {
            for (auto attachment = framebuffer->second.begin();
                 attachment != framebuffer->second.end();) {
                if (!attachment->second.isRenderbuffer && attachment->second.texture == texture) {
                    attachment = framebuffer->second.erase(attachment);
                } else {
                    ++attachment;
                }
            }
            if (framebuffer->second.empty()) framebuffer = state.framebufferAttachments.erase(framebuffer);
            else ++framebuffer;
        }
    }
}

void LayerDeleteFramebuffers(GLsizei count, const GLuint* framebuffers) {
    if (g_nextDeleteFramebuffers) g_nextDeleteFramebuffers(count, framebuffers);
    if (!framebuffers || count <= 0) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto& state = StateFor(context);
    for (GLsizei i = 0; i < count; ++i) {
        state.framebufferAttachments.erase(framebuffers[i]);
        if (state.boundFramebuffer == framebuffers[i]) state.boundFramebuffer = 0;
        if (state.boundReadFramebuffer == framebuffers[i]) state.boundReadFramebuffer = 0;
    }
}

void LayerViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (g_nextViewport) g_nextViewport(x, y, width, height);
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto& state = StateFor(context);
    state.viewportX = x;
    state.viewportY = y;
    state.viewportWidth = width;
    state.viewportHeight = height;
}

void RecordDrawForCurrentContext() {
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto& state = StateFor(context);
    ++state.drawCalls;
    state.lastDrawFramebuffer = state.boundFramebuffer;
    state.lastDrawTexture = 0;
    state.lastDrawTextureInfo = {};
    const auto framebuffer = state.framebufferAttachments.find(state.boundFramebuffer);
    if (framebuffer != state.framebufferAttachments.end()) {
        const auto color = framebuffer->second.find(GL_COLOR_ATTACHMENT0);
        if (color != framebuffer->second.end()) {
            state.lastDrawTexture = color->second.texture;
            state.lastDrawTextureInfo = color->second;
        }
    }
}

void LayerDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (g_nextDrawArrays) g_nextDrawArrays(mode, first, count);
    RecordDrawForCurrentContext();
}

void LayerDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    if (g_nextDrawElements) g_nextDrawElements(mode, count, type, indices);
    RecordDrawForCurrentContext();
}

void LayerFramebufferTexture2D(
    GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    if (g_nextFramebufferTexture2D) {
        g_nextFramebufferTexture2D(target, attachment, textarget, texture, level);
    }
    if ((target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER) ||
        textarget != GL_TEXTURE_2D || level != 0) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto& state = StateFor(context);
    ++state.attachmentUpdates;
    state.lastAttachedTexture = texture;
    if (texture == 0) {
        auto framebuffer = state.framebufferAttachments.find(state.boundFramebuffer);
        if (framebuffer != state.framebufferAttachments.end()) {
            framebuffer->second.erase(attachment);
            if (framebuffer->second.empty()) state.framebufferAttachments.erase(framebuffer);
        }
        return;
    }
    auto found = state.textures.find(texture);
    if (found != state.textures.end()) {
        state.framebufferAttachments[state.boundFramebuffer][attachment] = found->second;
    }
}

GLenum LayerCheckFramebufferStatus(GLenum target) {
    if (!g_nextCheckFramebufferStatus) return 0;
    const GLenum status = g_nextCheckFramebufferStatus(target);
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
        const EGLContext context = CurrentContext();
        if (context != EGL_NO_CONTEXT) {
            std::lock_guard<std::mutex> lock(g_contextMutex);
            StateFor(context).lastFramebufferStatus = status;
        }
    }
    return status;
}


struct RendererConfig {
    bool casEnabled = false;
    float casSharpness = 0.5f;
};
GLuint g_postProgram = 0;
GLuint g_postTexture = 0;
EGLContext g_postContext = EGL_NO_CONTEXT;
GLint g_postSharpnessLocation = -1;
GLint g_postSizeLocation = -1;
GLint g_postSamplerLocation = -1;
GLsizei g_postWidth = 0;
GLsizei g_postHeight = 0;
std::atomic<bool> g_postInitLogged{false};

RendererConfig ReadRendererConfig() {
    static RendererConfig cached{};
    static uint64_t reads = 0;
    if ((++reads % 120) != 1) return cached;
    RendererConfig config{};
    // The game process owns this Android/data directory, so the layer reads a
    // small sidecar deployed by the UltraScale controller via Shizuku.
    std::ifstream input("/storage/emulated/0/Android/data/com.gameloft.android.ANMP.GloftA8HM/files/ultrascale-renderer.ini");
    std::string line;
    while (std::getline(input, line)) {
        const auto split = line.find('=');
        if (split == std::string::npos) continue;
        const std::string key = line.substr(0, split);
        const std::string value = line.substr(split + 1);
        if (key == "cas") config.casEnabled = (value == "1" || value == "true");
        else if (key == "sharpness") {
            char* end = nullptr;
            const float parsed = std::strtof(value.c_str(), &end);
            if (end && *end == '\0' && parsed >= 0.0f && parsed <= 1.0f)
                config.casSharpness = parsed;
        }
    }
    cached = config;
    return cached;
}

GLuint CompilePostShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        char log[2048]{};
        GLsizei length = 0;
        glGetShaderInfoLog(shader, sizeof(log), &length, log);
        __android_log_print(ANDROID_LOG_ERROR, "UltraScaleGLES", "Post shader compile failed: %.*s", length, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool EnsurePostProcessor(EGLContext context) {
    if (context == EGL_NO_CONTEXT) return false;
    if (g_postProgram && g_postTexture && g_postContext == context) return true;
    static const char* vertexSource = R"GLSL(#version 300 es
precision highp float;
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})GLSL";
    static const char* fragmentSource = R"GLSL(#version 300 es
precision highp float;
precision highp int;
in vec2 vUv;
uniform sampler2D uScene;
uniform vec2 uInputSize;
uniform float uSharpness;
out vec4 oColor;
vec3 loadCAS(ivec2 p) {
    ivec2 size = ivec2(uInputSize);
    return texelFetch(uScene, clamp(p, ivec2(0), size - ivec2(1)), 0).rgb;
}
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 a = loadCAS(p + ivec2(-1,-1));
    vec3 b = loadCAS(p + ivec2( 0,-1));
    vec3 c = loadCAS(p + ivec2( 1,-1));
    vec3 d = loadCAS(p + ivec2(-1, 0));
    vec3 e = loadCAS(p);
    vec3 f = loadCAS(p + ivec2( 1, 0));
    vec3 g = loadCAS(p + ivec2(-1, 1));
    vec3 h = loadCAS(p + ivec2( 0, 1));
    vec3 i = loadCAS(p + ivec2( 1, 1));
    vec3 mn = min(min(d,e),min(f,min(b,h)));
    vec3 mx = max(max(d,e),max(f,max(b,h)));
    float ampG = clamp(min(mn.g,1.0-mx.g)/max(mx.g,1e-4),0.0,1.0);
    ampG = sqrt(ampG);
    float peak = -1.0 / mix(8.0,5.0,clamp(uSharpness,0.0,1.0));
    float w = ampG * peak;
    float rcpWeight = 1.0 / (1.0 + 4.0*w);
    vec3 result = clamp((b*w+d*w+f*w+h*w+e)*rcpWeight,0.0,1.0);
    oColor = vec4(result,1.0);
})GLSL";
    const GLuint vs = CompilePostShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fs = CompilePostShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char log[2048]{};
        GLsizei length = 0;
        glGetProgramInfoLog(program, sizeof(log), &length, log);
        __android_log_print(ANDROID_LOG_ERROR, "UltraScaleGLES", "Post program link failed: %.*s", length, log);
        glDeleteProgram(program);
        return false;
    }
    GLint previousActiveTexture = 0, previousTextureBinding = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture) {
        glDeleteProgram(program);
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBinding));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));

    g_postProgram = program;
    g_postTexture = texture;
    g_postContext = context;
    g_postSamplerLocation = glGetUniformLocation(program, "uScene");
    g_postSizeLocation = glGetUniformLocation(program, "uInputSize");
    g_postSharpnessLocation = glGetUniformLocation(program, "uSharpness");
    __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES", "Final-frame CAS postprocessor compiled and initialized");
    return true;
}

void ApplyFinalFramePostProcess(EGLDisplay display, EGLSurface surface, EGLContext context) {
    const RendererConfig config = ReadRendererConfig();
    if (!config.casEnabled) return;
    if (!EnsurePostProcessor(context)) return;

    EGLint width = 0, height = 0;
    if (!g_nextQuerySurface ||
        g_nextQuerySurface(display, surface, EGL_WIDTH, &width) != EGL_TRUE ||
        g_nextQuerySurface(display, surface, EGL_HEIGHT, &height) != EGL_TRUE ||
        width <= 2 || height <= 2) return;

    GLint drawFbo = 0, readFbo = 0, sampleBuffers = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    glGetIntegerv(GL_SAMPLE_BUFFERS, &sampleBuffers);
    // Only touch the EGL window's completed default framebuffer. If the game
    // leaves an off-screen FBO bound, or the surface is multisampled, skip it.
    if (drawFbo != 0 || readFbo != 0 || sampleBuffers != 0) return;

    GLint oldProgram = 0, oldActiveTexture = 0, oldTexture0 = 0;
    GLint oldViewport[4]{}, oldVao = 0, oldArrayBuffer = 0, oldPixelUnpackBuffer = 0;
    GLboolean oldColorMask[4]{};
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActiveTexture);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldArrayBuffer);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &oldPixelUnpackBuffer);
    glGetBooleanv(GL_COLOR_WRITEMASK, oldColorMask);

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture0);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean stencil = glIsEnabled(GL_STENCIL_TEST);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);

    glBindTexture(GL_TEXTURE_2D, g_postTexture);
    if (g_postWidth != width || g_postHeight != height) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(oldPixelUnpackBuffer));
        g_postWidth = width;
        g_postHeight = height;
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, width, height);
    glUseProgram(g_postProgram);
    glUniform1i(g_postSamplerLocation, 0);
    glUniform2f(g_postSizeLocation, static_cast<float>(width), static_cast<float>(height));
    glUniform1f(g_postSharpnessLocation, config.casSharpness);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glUseProgram(static_cast<GLuint>(oldProgram));
    glBindVertexArray(static_cast<GLuint>(oldVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(oldArrayBuffer));
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    glColorMask(oldColorMask[0], oldColorMask[1], oldColorMask[2], oldColorMask[3]);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(oldTexture0));
    glActiveTexture(static_cast<GLenum>(oldActiveTexture));
    if (!g_postInitLogged.exchange(true, std::memory_order_acq_rel))
        __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES", "Final-frame CAS pass executed at %dx%d", width, height);
}

EGLBoolean LayerSwapBuffers(EGLDisplay display, EGLSurface surface) {
    const uint64_t frame = ++g_frames;
    if (!g_nextSwapBuffers) return EGL_FALSE;

    const bool sampleFrame = (frame % 600) == 1;
    EGLint width = 0;
    EGLint height = 0;
    if (sampleFrame && g_nextQuerySurface) {
        g_nextQuerySurface(display, surface, EGL_WIDTH, &width);
        g_nextQuerySurface(display, surface, EGL_HEIGHT, &height);
    }

    const EGLContext context = CurrentContext();
    if (context != EGL_NO_CONTEXT) ApplyFinalFramePostProcess(display, surface, context);
    if (context != EGL_NO_CONTEXT && !g_glIdentityLogged.exchange(true, std::memory_order_acq_rel)) {
        const auto* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const auto* shading = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
        GLint maxTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
        __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES",
            "GL identity vendor=%s renderer=%s version=%s shading=%s max_texture=%d",
            vendor ? vendor : "unknown", renderer ? renderer : "unknown",
            version ? version : "unknown", shading ? shading : "unknown", maxTextureSize);
    }
    FrameTelemetry snapshot{};
    if (sampleFrame && context != EGL_NO_CONTEXT) {
        std::lock_guard<std::mutex> lock(g_contextMutex);
        const auto found = g_contexts.find(context);
        if (found != g_contexts.end()) {
            auto& state = found->second;
            snapshot.boundFramebuffer = state.boundFramebuffer;
            snapshot.viewportX = state.viewportX;
            snapshot.viewportY = state.viewportY;
            snapshot.viewportWidth = state.viewportWidth;
            snapshot.viewportHeight = state.viewportHeight;
            snapshot.drawCalls = state.drawCalls;
            state.drawCalls = 0;
            snapshot.attachmentUpdates = state.attachmentUpdates;
            snapshot.lastFramebufferStatus = state.lastFramebufferStatus;
            snapshot.lastAttachedTexture = state.lastAttachedTexture;
            snapshot.lastDrawFramebuffer = state.lastDrawFramebuffer;
            snapshot.lastDrawTexture = state.lastDrawTexture;
            snapshot.targetTextureInfo = state.lastDrawTextureInfo;
        }
    }

    const auto start = std::chrono::steady_clock::now();
    const EGLBoolean result = g_nextSwapBuffers(display, surface);
    if (sampleFrame) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES",
            "Layer telemetry frame=%llu surface=%dx%d context=%p bound_fbo=%u last_draw_fbo=%u last_draw_texture=%u viewport=%d,%d %dx%d draws=%llu attachments=%llu fbo_status=0x%x last_attached=%u texture_size=%dx%d internal_format=0x%x samples=%d swap_call=%lldus result=%d",
            static_cast<unsigned long long>(frame), width, height,
            reinterpret_cast<void*>(context), snapshot.boundFramebuffer,
            snapshot.lastDrawFramebuffer, snapshot.lastDrawTexture,
            snapshot.viewportX, snapshot.viewportY,
            snapshot.viewportWidth, snapshot.viewportHeight,
            static_cast<unsigned long long>(snapshot.drawCalls),
            static_cast<unsigned long long>(snapshot.attachmentUpdates),
            snapshot.lastFramebufferStatus,
            snapshot.lastAttachedTexture,
            snapshot.targetTextureInfo.width,
            snapshot.targetTextureInfo.height,
            snapshot.targetTextureInfo.internalFormat,
            snapshot.targetTextureInfo.samples,
            static_cast<long long>(elapsed), static_cast<int>(result));
    }
    return result;
}
} // namespace

extern "C" __attribute__((visibility("default")))
void AndroidGLESLayer_Initialize(void* layer_id, GetNextProc get_next_layer_proc_address) {
    g_layerId = layer_id;
    g_getNext = get_next_layer_proc_address;
    // Android's documented GLES LayerLoader is the sole attachment mechanism.
    // This pass-through telemetry layer has no third-party hook-engine dependency.
    if (g_getNext) {
        g_nextSwapBuffers = reinterpret_cast<EglSwapBuffers>(
            g_getNext(g_layerId, "eglSwapBuffers"));
        g_nextQuerySurface = reinterpret_cast<EglQuerySurface>(
            g_getNext(g_layerId, "eglQuerySurface"));
    }
    __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES",
        "Android GLES LayerLoader initialized; pass-through telemetry layer ready");
}

extern "C" __attribute__((visibility("default")))
void* AndroidGLESLayer_GetProcAddress(
    const char* funcName, __eglMustCastToProperFunctionPointerType next) {
    if (!funcName) return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(next));

    if (std::strcmp(funcName, "eglSwapBuffers") == 0) {
        if (next) g_nextSwapBuffers = reinterpret_cast<EglSwapBuffers>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerSwapBuffers));
    }
    if (std::strcmp(funcName, "eglQuerySurface") == 0) {
        if (next) g_nextQuerySurface = reinterpret_cast<EglQuerySurface>(next);
    } else if (std::strcmp(funcName, "eglDestroyContext") == 0) {
        if (next) g_nextDestroyContext = reinterpret_cast<EglDestroyContext>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerDestroyContext));
    } else if (std::strcmp(funcName, "glBindFramebuffer") == 0) {
        if (next) g_nextBindFramebuffer = reinterpret_cast<GlBindFramebuffer>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerBindFramebuffer));
    } else if (std::strcmp(funcName, "glBindTexture") == 0) {
        if (next) g_nextBindTexture = reinterpret_cast<GlBindTexture>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerBindTexture));
    } else if (std::strcmp(funcName, "glTexImage2D") == 0) {
        if (next) g_nextTexImage2D = reinterpret_cast<GlTexImage2D>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerTexImage2D));
    } else if (std::strcmp(funcName, "glTexStorage2D") == 0) {
        if (next) g_nextTexStorage2D = reinterpret_cast<GlTexStorage2D>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerTexStorage2D));
    } else if (std::strcmp(funcName, "glDeleteTextures") == 0) {
        if (next) g_nextDeleteTextures = reinterpret_cast<GlDeleteTextures>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerDeleteTextures));
    } else if (std::strcmp(funcName, "glDeleteFramebuffers") == 0) {
        if (next) g_nextDeleteFramebuffers = reinterpret_cast<GlDeleteFramebuffers>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerDeleteFramebuffers));
    } else if (std::strcmp(funcName, "glBindRenderbuffer") == 0) {
        if (next) g_nextBindRenderbuffer = reinterpret_cast<GlBindRenderbuffer>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerBindRenderbuffer));
    } else if (std::strcmp(funcName, "glRenderbufferStorage") == 0) {
        if (next) g_nextRenderbufferStorage = reinterpret_cast<GlRenderbufferStorage>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerRenderbufferStorage));
    } else if (std::strcmp(funcName, "glRenderbufferStorageMultisample") == 0) {
        if (next) g_nextRenderbufferStorageMultisample = reinterpret_cast<GlRenderbufferStorageMultisample>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerRenderbufferStorageMultisample));
    } else if (std::strcmp(funcName, "glFramebufferRenderbuffer") == 0) {
        if (next) g_nextFramebufferRenderbuffer = reinterpret_cast<GlFramebufferRenderbuffer>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerFramebufferRenderbuffer));
    } else if (std::strcmp(funcName, "glDeleteRenderbuffers") == 0) {
        if (next) g_nextDeleteRenderbuffers = reinterpret_cast<GlDeleteRenderbuffers>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerDeleteRenderbuffers));
    } else if (std::strcmp(funcName, "glViewport") == 0) {
        if (next) g_nextViewport = reinterpret_cast<GlViewport>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerViewport));
    } else if (std::strcmp(funcName, "glDrawArrays") == 0) {
        if (next) g_nextDrawArrays = reinterpret_cast<GlDrawArrays>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerDrawArrays));
    } else if (std::strcmp(funcName, "glDrawElements") == 0) {
        if (next) g_nextDrawElements = reinterpret_cast<GlDrawElements>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerDrawElements));
    } else if (std::strcmp(funcName, "glFramebufferTexture2D") == 0) {
        if (next) g_nextFramebufferTexture2D = reinterpret_cast<GlFramebufferTexture2D>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerFramebufferTexture2D));
    } else if (std::strcmp(funcName, "glCheckFramebufferStatus") == 0) {
        if (next) g_nextCheckFramebufferStatus = reinterpret_cast<GlCheckFramebufferStatus>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerCheckFramebufferStatus));
    }
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(next));
}
