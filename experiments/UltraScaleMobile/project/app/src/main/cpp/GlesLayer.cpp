#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <atomic>
#include <algorithm>
#include <cmath>
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
using GlBlitFramebuffer = void (*)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
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
GlBlitFramebuffer g_nextBlitFramebuffer = nullptr;
std::atomic<uint64_t> g_defaultBlitSamples{0};
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

bool TryUpscaleBlit(GLuint sourceTexture, GLsizei sourceWidth, GLsizei sourceHeight,
                    GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                    GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                    EGLContext context);

void LayerBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                          GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                          GLbitfield mask, GLenum filter) {
    if (!g_nextBlitFramebuffer) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) {
        g_nextBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
            dstX0, dstY0, dstX1, dstY1, mask, filter);
        return;
    }
    GLuint readFbo = 0, drawFbo = 0;
    TextureInfo source{};
    bool candidate = false;
    {
        std::lock_guard<std::mutex> lock(g_contextMutex);
        auto found = g_contexts.find(context);
        if (found != g_contexts.end()) {
            readFbo = found->second.boundReadFramebuffer;
            drawFbo = found->second.boundFramebuffer;
            if (readFbo != 0 && drawFbo == 0) {
                const auto framebuffer = found->second.framebufferAttachments.find(readFbo);
                if (framebuffer != found->second.framebufferAttachments.end()) {
                    const auto color = framebuffer->second.find(GL_COLOR_ATTACHMENT0);
                    if (color != framebuffer->second.end() && !color->second.isRenderbuffer) {
                        source = color->second;
                        candidate = source.texture != 0;
                    }
                }
            }
        }
    }
    if (candidate && mask == GL_COLOR_BUFFER_BIT &&
        TryUpscaleBlit(source.texture, source.width, source.height,
            srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, context)) {
        return;
    }
    g_nextBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
        dstX0, dstY0, dstX1, dstY1, mask, filter);
    if (!candidate || (mask & GL_COLOR_BUFFER_BIT) == 0) return;
    const uint64_t sample = ++g_defaultBlitSamples;
    if (sample <= 16 || (sample % 120) == 0) {
        __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES",
            "Final blit candidate #%llu read_fbo=%u draw_fbo=%u texture=%u source=%dx%d src_rect=%d,%d-%d,%d dst_rect=%d,%d-%d,%d filter=0x%x",
            static_cast<unsigned long long>(sample), readFbo, drawFbo, source.texture,
            source.width, source.height, srcX0, srcY0, srcX1, srcY1,
            dstX0, dstY0, dstX1, dstY1, filter);
    }
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


#ifndef ULTRASCALE_DEFAULT_UPSCALER
#define ULTRASCALE_DEFAULT_UPSCALER "fsr_lite"
#endif
#ifndef ULTRASCALE_DEFAULT_CAS
#define ULTRASCALE_DEFAULT_CAS 0
#endif
#ifndef ULTRASCALE_DEFAULT_CAS_SHARPNESS
#define ULTRASCALE_DEFAULT_CAS_SHARPNESS 0.25f
#endif

struct RendererConfig {
    bool casEnabled = (ULTRASCALE_DEFAULT_CAS != 0);
    float casSharpness = ULTRASCALE_DEFAULT_CAS_SHARPNESS;
    std::string upscaler = ULTRASCALE_DEFAULT_UPSCALER;
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
        else if (key == "upscaler") {
            if (value == "off" || value == "nearest" || value == "bilinear" || value == "fsr_lite" || value == "sgsr1" || value == "mwsr")
                config.upscaler = value;
        }
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

GLuint CompilePostShader(GLenum type, const char* source);

GLuint g_sgsr1Program = 0;
EGLContext g_sgsr1Context = EGL_NO_CONTEXT;
GLint g_sgsr1Sampler = -1;
GLint g_sgsr1Viewport = -1;

bool EnsureSgsr1Program(EGLContext context) {
    if (g_sgsr1Program && g_sgsr1Context == context) return true;
    static const char* vsSource = R"SGSRVS(#version 310 es
precision highp float;
out highp vec4 in_TEXCOORD0;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    in_TEXCOORD0 = vec4(p, 0.0, 1.0);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})SGSRVS";
    static const char* fsSource = R"SGSRFS(#version 310 es
precision mediump float;
precision highp int;
#define OperationMode 1
#define EdgeThreshold (8.0 / 255.0)
#define EdgeSharpness 2.0
uniform highp vec4 ViewportInfo[1];
uniform mediump sampler2D ps0;
layout(location=0) in highp vec4 in_TEXCOORD0;
layout(location=0) out vec4 out_Target0;
float fastLanczos2(float x) {
    float wA = x - 4.0;
    float wB = x * wA - wA;
    wA *= wA;
    return wB * wA;
}
vec2 weightY(float dx, float dy, float c, float std) {
    float x = ((dx * dx) + (dy * dy)) * 0.55 + clamp(abs(c) * std, 0.0, 1.0);
    float w = fastLanczos2(x);
    return vec2(w, w * c);
}
void main() {
    const int mode = OperationMode;
    vec4 color = textureLod(ps0, in_TEXCOORD0.xy, 0.0);
    highp vec2 imgCoord = (in_TEXCOORD0.xy * ViewportInfo[0].zw) + vec2(-0.5, 0.5);
    highp vec2 imgCoordPixel = floor(imgCoord);
    highp vec2 coord = imgCoordPixel * ViewportInfo[0].xy;
    vec2 pl = imgCoord - imgCoordPixel;
    vec4 left = textureGather(ps0, coord, mode);
    float edgeVote = abs(left.z - left.y) + abs(color[mode] - left.y) + abs(color[mode] - left.z);
    if (edgeVote > EdgeThreshold) {
        coord.x += ViewportInfo[0].x;
        vec4 right = textureGather(ps0, coord + vec2(ViewportInfo[0].x, 0.0), mode);
        vec4 upDown;
        upDown.xy = textureGather(ps0, coord + vec2(0.0, -ViewportInfo[0].y), mode).wz;
        upDown.zw = textureGather(ps0, coord + vec2(0.0, ViewportInfo[0].y), mode).yx;
        float mean = (left.y + left.z + right.x + right.w) * 0.25;
        left -= vec4(mean);
        right -= vec4(mean);
        upDown -= vec4(mean);
        color.w = color[mode] - mean;
        float sum = abs(left.x)+abs(left.y)+abs(left.z)+abs(left.w)
                  + abs(right.x)+abs(right.y)+abs(right.z)+abs(right.w)
                  + abs(upDown.x)+abs(upDown.y)+abs(upDown.z)+abs(upDown.w);
        float std = 2.181818 / max(sum, 0.00001);
        vec2 aWY = weightY(pl.x, pl.y+1.0, upDown.x, std);
        aWY += weightY(pl.x-1.0, pl.y+1.0, upDown.y, std);
        aWY += weightY(pl.x-1.0, pl.y-2.0, upDown.z, std);
        aWY += weightY(pl.x, pl.y-2.0, upDown.w, std);
        aWY += weightY(pl.x+1.0, pl.y-1.0, left.x, std);
        aWY += weightY(pl.x, pl.y-1.0, left.y, std);
        aWY += weightY(pl.x, pl.y, left.z, std);
        aWY += weightY(pl.x+1.0, pl.y, left.w, std);
        aWY += weightY(pl.x-1.0, pl.y-1.0, right.x, std);
        aWY += weightY(pl.x-2.0, pl.y-1.0, right.y, std);
        aWY += weightY(pl.x-2.0, pl.y, right.z, std);
        aWY += weightY(pl.x-1.0, pl.y, right.w, std);
        float finalY = aWY.y / max(aWY.x, 0.00001);
        float maxY = max(max(left.y, left.z), max(right.x, right.w));
        float minY = min(min(left.y, left.z), min(right.x, right.w));
        finalY = clamp(EdgeSharpness * finalY, minY, maxY);
        float deltaY = clamp(finalY - color.w, -23.0/255.0, 23.0/255.0);
        color.rgb = clamp(color.rgb + vec3(deltaY), 0.0, 1.0);
    }
    color.a = 1.0;
    out_Target0 = color;
})SGSRFS";
    GLuint vs = CompilePostShader(GL_VERTEX_SHADER, vsSource);
    GLuint fs = CompilePostShader(GL_FRAGMENT_SHADER, fsSource);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    GLuint program = glCreateProgram();
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
        __android_log_print(ANDROID_LOG_ERROR, "UltraScaleGLES", "SGSR1 link failed: %.*s", length, log);
        glDeleteProgram(program);
        return false;
    }
    g_sgsr1Program = program;
    g_sgsr1Context = context;
    g_sgsr1Sampler = glGetUniformLocation(program, "ps0");
    g_sgsr1Viewport = glGetUniformLocation(program, "ViewportInfo[0]");
    __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES", "Qualcomm SGSR1 spatial pass compiled");
    return g_sgsr1Sampler >= 0 && g_sgsr1Viewport >= 0;
}


GLuint g_mwsrProgram = 0;
EGLContext g_mwsrContext = EGL_NO_CONTEXT;
GLint g_mwsrSampler = -1;
GLint g_mwsrTexelScale = -1;
GLint g_mwsrDimsAcross = -1;
GLint g_mwsrKernel = -1;
GLint g_mwsrZone = -1;
GLint g_mwsrZoneBonus = -1;
GLint g_mwsrBound = -1;

bool EnsureMwsrProgram(EGLContext context) {
    if (g_mwsrProgram && g_mwsrContext == context) return true;
    static const char* vsSource = R"MWV(#version 300 es
precision highp float;
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})MWV";
    static const char* fsSource = R"MWF(#version 300 es
// MWSR — MoonlightWeb Super Resolution: the upscale, GLSL ES 3.00.
 // Copyright (c) 2026 Bruno Martin
 // SPDX-License-Identifier: MIT
 // Source and parameters: https://github.com/linckosz/moonlight-web/tree/main/mwsr
 // Permission is hereby granted, free of charge, to any person obtaining a copy
 // of this software and associated documentation files (the "Software"), to deal
 // in the Software without restriction, including without limitation the rights
 // to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 // copies of the Software, and to permit persons to whom the Software is
 // furnished to do so, subject to the following conditions:
 // The above copyright notice and this permission notice shall be included in all
 // copies or substantial portions of the Software.
 // THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 // IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 // FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 // AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 // LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 // OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 // SOFTWARE.
precision highp float;
precision highp int;
uniform sampler2D uTex;
uniform vec4 uTexelScale;
uniform vec4 uDimsAcross;
uniform vec4 uKernel;
uniform vec4 uZone;
uniform vec4 uZoneBonus;
uniform vec4 uBound;
in vec2 vUv;
out vec4 oColor;
vec3 S(vec2 uv) { return textureLod(uTex, uv, 0.0).rgb; }
const uint SHOW = 0u;
const uint CHROMA444 = 0u;
const uint GAZE = 0u;
const vec3 LUMA = vec3(0.25, 0.625, 0.125);
float gazeWeight(vec2 uv) {
    if (GAZE != 1u) return 0.0;
    float d = length((uv - uZone.xy) / uZone.zw);
    return (1.0 - smoothstep(1.0, 1.0 + uZoneBonus.x, d)) * uZoneBonus.w;
}
void main() {
    vec2 uv = vUv;
    vec2 texel = uTexelScale.xy;
    vec2 sp = uv * uDimsAcross.xy;
    vec2 f0 = floor(sp - 0.5);
    vec2 t = sp - 0.5 - f0;
    vec2 ss = 1.0 - t;
    vec2 ku = uKernel.x * t * ss;
    vec2 w12 = 1.0 + ku;
    vec2 p12 = (f0 + 0.5 + (t * t * (3.0 - 2.0 * t) + ku * ss) / w12) * texel;
    vec3 rM = S(p12);
    float zg = gazeWeight(uv);
    float ft = uKernel.y;
    if (GAZE == 1u) ft = mix(uKernel.y, uZoneBonus.y, zg);
    vec2 gM;
    bool isFlat;
    if (CHROMA444 == 1u) {
        vec3 dx = dFdx(rM) * uTexelScale.z;
        vec3 dy = -dFdy(rM) * uTexelScale.w;
        vec3 m = abs(dx) + abs(dy);
        bool rg = m.g > m.r;
        bool b = m.b > max(m.r, m.g);
        gM = vec2(b ? dx.b : (rg ? dx.g : dx.r), b ? dy.b : (rg ? dy.g : dy.r));
        isFlat = max(m.r, max(m.g, m.b)) < ft;
    } else {
        float lM = dot(rM, LUMA);
        gM = vec2(dFdx(lM) * uTexelScale.z, -dFdy(lM) * uTexelScale.w);
        isFlat = abs(gM.x) + abs(gM.y) < ft;
    }
    if (SHOW == 1u) { oColor = vec4(vec3(isFlat ? 0.0 : 1.0), 1.0); return; }
    if (SHOW == 2u) { oColor = vec4(vec3(zg), 1.0); return; }
    if (isFlat) { oColor = vec4(rM, 1.0); return; }
    vec2 p0 = (f0 - 0.5) * texel;
    vec2 p3 = (f0 + 2.5) * texel;
    vec3 rT = S(vec2(p12.x, p0.y));
    vec3 rL = S(vec2(p0.x, p12.y));
    vec3 rR = S(vec2(p3.x, p12.y));
    vec3 rB = S(vec2(p12.x, p3.y));
    vec2 dn = gM * inversesqrt(max(dot(gM, gM), 1e-12)) * uDimsAcross.zw;
    vec3 ra = S(uv - dn);
    vec3 rb = S(uv + dn);
    float kg = uKernel.z;
    if (GAZE == 1u) kg = mix(uKernel.z, uZoneBonus.z, zg);
    mediump vec3 hM = rM;
    mediump vec3 hA = ra;
    mediump vec3 hB = rb;
    mediump vec3 hTop = rT;
    mediump vec3 hLeft = rL;
    mediump vec3 hRight = rR;
    mediump vec3 hBottom = rB;
    mediump vec2 hu = ku;
    mediump vec2 hs = ss;
    mediump vec2 ht = t;
    mediump vec2 h12 = w12;
    mediump vec3 K = (h12.x * (h12.y * hM - hu.y * (hs.y * hTop + ht.y * hBottom)) - h12.y * hu.x * (hs.x * hLeft + ht.x * hRight)) / (1.0 - hu.x * hu.y);
    mediump vec3 D = K - 0.5 * (hA + hB);
    mediump float hg = kg;
    mediump float hw = uKernel.w;
    mediump vec3 R = K + hg * D + hw * (D - vec3(dot(D, LUMA)));
    mediump vec3 mn = min(hM, min(hA, hB));
    mediump vec3 mx = max(hM, max(hA, hB));
    mediump float hl = uBound.x;
    mediump vec3 mg = (mx - mn) * hl;
    oColor = vec4(clamp(R, max(mn - mg, vec3(0.0)), mx + mg), 1.0);
}
)MWF";
    GLuint vs = CompilePostShader(GL_VERTEX_SHADER, vsSource);
    GLuint fs = CompilePostShader(GL_FRAGMENT_SHADER, fsSource);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    GLuint program = glCreateProgram();
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
        __android_log_print(ANDROID_LOG_ERROR, "UltraScaleGLES", "MWSR link failed: %.*s", length, log);
        glDeleteProgram(program);
        return false;
    }
    g_mwsrProgram = program;
    g_mwsrContext = context;
    g_mwsrSampler = glGetUniformLocation(program, "uTex");
    g_mwsrTexelScale = glGetUniformLocation(program, "uTexelScale");
    g_mwsrDimsAcross = glGetUniformLocation(program, "uDimsAcross");
    g_mwsrKernel = glGetUniformLocation(program, "uKernel");
    g_mwsrZone = glGetUniformLocation(program, "uZone");
    g_mwsrZoneBonus = glGetUniformLocation(program, "uZoneBonus");
    g_mwsrBound = glGetUniformLocation(program, "uBound");
    // GAZE is compiled as zero in the bundled Performance shader, so the
    // zone uniforms may be optimized out by the GLES compiler.
    const bool valid = g_mwsrSampler >= 0 && g_mwsrTexelScale >= 0 &&
        g_mwsrDimsAcross >= 0 && g_mwsrKernel >= 0 && g_mwsrBound >= 0;
    if (valid) __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES", "MWSR Performance spatial shader compiled");
    return valid;
}

void SetMwsrUniforms(GLsizei inW, GLsizei inH, GLsizei outW, GLsizei outH) {
    const float sx = static_cast<float>(outW) / static_cast<float>(inW);
    const float sy = static_cast<float>(outH) / static_cast<float>(inH);
    const float scale = std::min(sx, sy);
    const float clampedScale = std::max(1.07f, std::min(4.0f, scale));
    const float x = std::log(clampedScale);
    const auto curve = [x](float c0, float c1, float c2) {
        return c0 + x * (c1 + x * c2);
    };
    const float baseForce = 1.15f;
    const float forceT = std::max(0.0f, std::min(1.0f,
        std::log(std::max(scale, 1e-4f) / (4.0f / 3.0f)) / std::log(2.0f / (4.0f / 3.0f))));
    const float force = baseForce + (1.35f - baseForce) * forceT;
    const float strength = std::max(0.0f, std::min(1.0f, (scale - 1.0f) / (1.07f - 1.0f)));
    const float kc = curve(0.6353583202388096f, 0.021845681633595574f, 0.12781268100437676f);
    const float kg = curve(-0.1288413863403059f, 2.5844683855070816f, -1.260092021482419f) * force * strength;
    const float kr = curve(1.0071546009267338f, -1.0209066704914191f, 0.5699605831985769f);
    const float kl = curve(0.33416994543071277f, -0.2962022126856069f, 0.08628272130287401f) * force;
    const float kwc = 0.1f * strength;
    glUniform1i(g_mwsrSampler, 0);
    glUniform4f(g_mwsrTexelScale, 1.0f/inW, 1.0f/inH, sx, sy);
    glUniform4f(g_mwsrDimsAcross, static_cast<float>(inW), static_cast<float>(inH), kr/inW, kr/inH);
    glUniform4f(g_mwsrKernel, kc, 0.005f, kg, kwc);
    glUniform4f(g_mwsrZone, 0.5f, 0.5f, 0.2f, 0.45f);
    glUniform4f(g_mwsrZoneBonus, 0.75f, 0.005f, kg*1.5f, 0.0f);
    glUniform4f(g_mwsrBound, kl, 1.0f, 0.0f, 0.0f);
}

GLuint g_blitUpscaleProgram = 0;
EGLContext g_blitUpscaleContext = EGL_NO_CONTEXT;
GLint g_blitUpscaleSampler = -1;
GLint g_blitUpscaleSize = -1;
GLint g_blitUpscaleMode = -1;
std::atomic<uint64_t> g_blitUpscaleFrames{0};

bool EnsureBlitUpscaleProgram(EGLContext context) {
    if (g_blitUpscaleProgram && g_blitUpscaleContext == context) return true;
    static const char* vsSource = R"GLSL(#version 300 es
precision highp float;
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})GLSL";
    static const char* fsSource = R"GLSL(#version 300 es
precision highp float;
in vec2 vUv;
uniform sampler2D uScene;
uniform vec2 uInputSize;
uniform int uMode;
out vec4 oColor;
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec3 fsrLite(vec2 uv) {
    vec2 t = 1.0 / uInputSize;
    vec2 hi = vec2(1.0) - t * 0.5;
    vec3 c = texture(uScene, clamp(uv, t * 0.5, hi)).rgb;
    vec3 l = texture(uScene, clamp(uv-vec2(t.x,0.0),t*0.5,hi)).rgb;
    vec3 r = texture(uScene, clamp(uv+vec2(t.x,0.0),t*0.5,hi)).rgb;
    vec3 u = texture(uScene, clamp(uv+vec2(0.0,t.y),t*0.5,hi)).rgb;
    vec3 d = texture(uScene, clamp(uv-vec2(0.0,t.y),t*0.5,hi)).rgb;
    vec3 q = texture(uScene, clamp(uv+t,t*0.5,hi)).rgb;
    float gx = abs(luma(r)-luma(l));
    float gy = abs(luma(u)-luma(d));
    float edge = clamp(max(gx,gy)*3.0,0.0,1.0);
    vec3 along = gx > gy ? (u+d)*0.5 : (l+r)*0.5;
    vec3 diagonal = (c*3.0+l+r+u+d+q)*0.125;
    vec3 outColor = mix(diagonal,along,edge*0.35);
    float mn = min(min(luma(l),luma(r)),min(luma(u),luma(d)));
    float mx = max(max(luma(l),luma(r)),max(luma(u),luma(d)));
    float detail = clamp(luma(c)-luma(diagonal),-0.08,0.08);
    outColor += vec3(detail*0.35);
    float y = clamp(luma(outColor),mn-0.02,mx+0.02);
    return clamp(outColor*(y/max(luma(outColor),0.0001)),0.0,1.0);
}
void main() {
    vec2 uv = clamp(vUv, vec2(0.5)/uInputSize, vec2(1.0)-vec2(0.5)/uInputSize);
    vec3 c;
    if (uMode == 1) {
        ivec2 p = clamp(ivec2(uv*uInputSize), ivec2(0), ivec2(uInputSize)-ivec2(1));
        c = texelFetch(uScene,p,0).rgb;
    } else if (uMode == 2) {
        c = fsrLite(uv);
    } else {
        c = texture(uScene,uv).rgb;
    }
    oColor = vec4(c,1.0);
})GLSL";
    GLuint vs = CompilePostShader(GL_VERTEX_SHADER, vsSource);
    GLuint fs = CompilePostShader(GL_FRAGMENT_SHADER, fsSource);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    GLuint program = glCreateProgram();
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
        __android_log_print(ANDROID_LOG_ERROR, "UltraScaleGLES", "Upscale shader link failed: %.*s", length, log);
        glDeleteProgram(program);
        return false;
    }
    g_blitUpscaleProgram = program;
    g_blitUpscaleContext = context;
    g_blitUpscaleSampler = glGetUniformLocation(program, "uScene");
    g_blitUpscaleSize = glGetUniformLocation(program, "uInputSize");
    g_blitUpscaleMode = glGetUniformLocation(program, "uMode");
    __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES", "Spatial blit upscaler compiled");
    return true;
}

bool TryUpscaleBlit(GLuint sourceTexture, GLsizei sourceWidth, GLsizei sourceHeight,
                    GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                    GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                    EGLContext context) {
    const RendererConfig config = ReadRendererConfig();
    if (config.upscaler == "off" || sourceWidth <= 0 || sourceHeight <= 0) return false;
    const GLsizei srcWidth = srcX1 - srcX0;
    const GLsizei srcHeight = srcY1 - srcY0;
    const GLsizei dstWidth = dstX1 - dstX0;
    const GLsizei dstHeight = dstY1 - dstY0;
    // Only replace a full-image, low-resolution scene-to-window color blit.
    // Unknown rectangles, same-size copies and downscales stay on the game's path.
    if (srcX0 != 0 || srcY0 != 0 || srcWidth != sourceWidth || srcHeight != sourceHeight ||
        dstWidth <= sourceWidth || dstHeight <= sourceHeight || dstWidth <= 0 || dstHeight <= 0)
        return false;
    const bool useSgsr1 = config.upscaler == "sgsr1";
    const bool useMwsr = config.upscaler == "mwsr";
    if (useMwsr ? !EnsureMwsrProgram(context) :
        useSgsr1 ? !EnsureSgsr1Program(context) : !EnsureBlitUpscaleProgram(context)) return false;
    const GLuint selectedProgram = useMwsr ? g_mwsrProgram :
        useSgsr1 ? g_sgsr1Program : g_blitUpscaleProgram;

    GLint oldDrawFbo = 0, oldReadFbo = 0, oldProgram = 0, oldActiveTexture = 0;
    GLint oldTexture0 = 0, oldViewport[4]{}, oldVao = 0, oldArrayBuffer = 0;
    GLint oldMinFilter = GL_LINEAR, oldMagFilter = GL_LINEAR;
    GLboolean oldColorMask[4]{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldReadFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActiveTexture);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldArrayBuffer);
    glGetBooleanv(GL_COLOR_WRITEMASK, oldColorMask);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean stencil = glIsEnabled(GL_STENCIL_TEST);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &oldMinFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &oldMagFilter);
    const GLint sampleFilter = config.upscaler == "nearest" ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sampleFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sampleFilter);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(dstX0, dstY0, dstWidth, dstHeight);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(selectedProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    if (useMwsr) {
        SetMwsrUniforms(sourceWidth, sourceHeight, dstWidth, dstHeight);
    } else if (useSgsr1) {
        glUniform1i(g_sgsr1Sampler, 0);
        glUniform4f(g_sgsr1Viewport, 1.0f / sourceWidth, 1.0f / sourceHeight,
                    static_cast<float>(sourceWidth), static_cast<float>(sourceHeight));
    } else {
        glUniform1i(g_blitUpscaleSampler, 0);
        glUniform2f(g_blitUpscaleSize, static_cast<float>(sourceWidth), static_cast<float>(sourceHeight));
        const GLint mode = config.upscaler == "nearest" ? 1 : (config.upscaler == "fsr_lite" ? 2 : 0);
        glUniform1i(g_blitUpscaleMode, mode);
    }
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
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, oldMinFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, oldMagFilter);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(oldTexture0));
    glActiveTexture(static_cast<GLenum>(oldActiveTexture));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(oldDrawFbo));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(oldReadFbo));

    const uint64_t count = ++g_blitUpscaleFrames;
    if (count <= 8 || count % 300 == 0) {
        __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES",
            "Spatial upscale applied mode=%s source=%dx%d output=%dx%d frame=%llu",
            config.upscaler.c_str(), sourceWidth, sourceHeight, dstWidth, dstHeight,
            static_cast<unsigned long long>(count));
    }
    return true;
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
    g_postWidth = 0;
    g_postHeight = 0;
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
    } else if (std::strcmp(funcName, "glBlitFramebuffer") == 0) {
        if (next) g_nextBlitFramebuffer = reinterpret_cast<GlBlitFramebuffer>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerBlitFramebuffer));
    } else if (std::strcmp(funcName, "glFramebufferTexture2D") == 0) {
        if (next) g_nextFramebufferTexture2D = reinterpret_cast<GlFramebufferTexture2D>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerFramebufferTexture2D));
    } else if (std::strcmp(funcName, "glCheckFramebufferStatus") == 0) {
        if (next) g_nextCheckFramebufferStatus = reinterpret_cast<GlCheckFramebufferStatus>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerCheckFramebufferStatus));
    }
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(next));
}
