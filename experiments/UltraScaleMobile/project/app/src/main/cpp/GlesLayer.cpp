#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <shadowhook.h>
#include <bytehook.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

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
using GlTexImage2D = void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
using GlTexStorage2D = void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
using GlDeleteTextures = void (*)(GLsizei, const GLuint*);
using GlDeleteFramebuffers = void (*)(GLsizei, const GLuint*);
using GlBindRenderbuffer = void (*)(GLenum, GLuint);
using GlRenderbufferStorage = void (*)(GLenum, GLenum, GLsizei, GLsizei);
using GlFramebufferRenderbuffer = void (*)(GLenum, GLenum, GLenum, GLuint);
using GlDeleteRenderbuffers = void (*)(GLsizei, const GLuint*);

namespace {
struct TextureInfo {
    GLuint texture = 0;
    GLsizei width = 0;
    GLsizei height = 0;
    GLenum internalFormat = 0;
    bool isRenderbuffer = false;
};
struct ContextState {
    GLuint boundFramebuffer = 0;
    GLuint boundTexture2D = 0;
    GLuint boundRenderbuffer = 0;
    GLint viewportX = 0;
    GLint viewportY = 0;
    GLsizei viewportWidth = 0;
    GLsizei viewportHeight = 0;
    uint64_t drawCalls = 0;
    uint64_t attachmentUpdates = 0;
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
    GLuint lastAttachedTexture = 0;
    GLuint lastDrawFramebuffer = 0;
    GLuint lastDrawTexture = 0;
    TextureInfo targetTextureInfo{};
};

std::mutex g_contextMutex;
std::unordered_map<EGLContext, ContextState> g_contexts;
std::atomic<uint64_t> g_frames{0};
std::atomic<bool> g_hookBackendReady{false};
std::atomic<bool> g_pltBackendReady{false};

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
GlTexImage2D g_nextTexImage2D = nullptr;
GlTexStorage2D g_nextTexStorage2D = nullptr;
GlDeleteTextures g_nextDeleteTextures = nullptr;
GlDeleteFramebuffers g_nextDeleteFramebuffers = nullptr;
GlBindRenderbuffer g_nextBindRenderbuffer = nullptr;
GlRenderbufferStorage g_nextRenderbufferStorage = nullptr;
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
            if (attachment.second.texture == texture) attachment.second = found->second;
        }
    }
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
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER) return;
    const EGLContext context = CurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    std::lock_guard<std::mutex> lock(g_contextMutex);
    StateFor(context).boundFramebuffer = framebuffer;
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
    }
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
        for (auto framebuffer = state.framebufferAttachments.begin();
             framebuffer != state.framebufferAttachments.end();) {
            for (auto attachment = framebuffer->second.begin();
                 attachment != framebuffer->second.end();) {
                if (attachment->second.texture == texture) {
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
    FrameTelemetry snapshot{};
    if (sampleFrame && context != EGL_NO_CONTEXT) {
        std::lock_guard<std::mutex> lock(g_contextMutex);
        const auto found = g_contexts.find(context);
        if (found != g_contexts.end()) {
            const auto& state = found->second;
            snapshot.boundFramebuffer = state.boundFramebuffer;
            snapshot.viewportX = state.viewportX;
            snapshot.viewportY = state.viewportY;
            snapshot.viewportWidth = state.viewportWidth;
            snapshot.viewportHeight = state.viewportHeight;
            snapshot.drawCalls = state.drawCalls;
            state.drawCalls = 0;
            snapshot.attachmentUpdates = state.attachmentUpdates;
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
            "Layer telemetry frame=%llu surface=%dx%d context=%p bound_fbo=%u last_draw_fbo=%u last_draw_texture=%u viewport=%d,%d %dx%d draws=%llu attachments=%llu last_attached=%u texture_size=%dx%d internal_format=0x%x swap_call=%lldus result=%d",
            static_cast<unsigned long long>(frame), width, height,
            reinterpret_cast<void*>(context), snapshot.boundFramebuffer,
            snapshot.lastDrawFramebuffer, snapshot.lastDrawTexture,
            snapshot.viewportX, snapshot.viewportY,
            snapshot.viewportWidth, snapshot.viewportHeight,
            static_cast<unsigned long long>(snapshot.drawCalls),
            static_cast<unsigned long long>(snapshot.attachmentUpdates),
            snapshot.lastAttachedTexture,
            snapshot.targetTextureInfo.width,
            snapshot.targetTextureInfo.height,
            snapshot.targetTextureInfo.internalFormat,
            static_cast<long long>(elapsed), static_cast<int>(result));
    }
    return result;
}
} // namespace

extern "C" __attribute__((visibility("default")))
void AndroidGLESLayer_Initialize(void* layer_id, GetNextProc get_next_layer_proc_address) {
    g_layerId = layer_id;
    g_getNext = get_next_layer_proc_address;
    // Android's documented GLES LayerLoader is the process attachment mechanism.
    // ShadowHook and ByteHook are optional in-process backends; they do not inject
    // into arbitrary processes and are not required for the wrappers below.
    const int hookInit = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
    g_hookBackendReady.store(hookInit == 0, std::memory_order_release);
    const int pltInit = hookInit == 0 ? bytehook_init(BYTEHOOK_MODE_AUTOMATIC, false) : -1;
    g_pltBackendReady.store(pltInit == BYTEHOOK_STATUS_CODE_OK, std::memory_order_release);

    if (g_getNext) {
        g_nextSwapBuffers = reinterpret_cast<EglSwapBuffers>(
            g_getNext(g_layerId, "eglSwapBuffers"));
        g_nextQuerySurface = reinterpret_cast<EglQuerySurface>(
            g_getNext(g_layerId, "eglQuerySurface"));
    }
    __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES",
        "Android GLES LayerLoader attached; inline=%s (%d), PLT=%s (%d)",
        g_hookBackendReady.load(std::memory_order_acquire) ? "ready" : "unavailable", hookInit,
        g_pltBackendReady.load(std::memory_order_acquire) ? "ready" : "unavailable", pltInit);
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
    }
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(next));
}
