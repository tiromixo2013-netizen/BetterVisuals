#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <shadowhook.h>
#include <bytehook.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

using GetNextProc = void* (*)(void*, const char*);
using EglSwapBuffers = EGLBoolean (*)(EGLDisplay, EGLSurface);
using EglQuerySurface = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLint, EGLint*);
using GlBindFramebuffer = void (*)(GLenum, GLuint);
using GlViewport = void (*)(GLint, GLint, GLsizei, GLsizei);
using GlDrawArrays = void (*)(GLenum, GLint, GLsizei);
using GlDrawElements = void (*)(GLenum, GLsizei, GLenum, const void*);
using GlFramebufferTexture2D = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);

static void* g_layerId = nullptr;
static GetNextProc g_getNext = nullptr;
static EglSwapBuffers g_nextSwapBuffers = nullptr;
static EglQuerySurface g_nextQuerySurface = nullptr;
static GlBindFramebuffer g_nextBindFramebuffer = nullptr;
static GlViewport g_nextViewport = nullptr;
static GlDrawArrays g_nextDrawArrays = nullptr;
static GlDrawElements g_nextDrawElements = nullptr;
static GlFramebufferTexture2D g_nextFramebufferTexture2D = nullptr;

static std::atomic<uint64_t> g_frames{0};
static std::atomic<uint64_t> g_drawCalls{0};
static std::atomic<uint64_t> g_attachmentUpdates{0};
static std::atomic<uint32_t> g_boundFramebuffer{0};
static std::atomic<uint32_t> g_lastAttachedTexture{0};
static std::atomic<int32_t> g_viewportX{0};
static std::atomic<int32_t> g_viewportY{0};
static std::atomic<int32_t> g_viewportWidth{0};
static std::atomic<int32_t> g_viewportHeight{0};
static std::atomic<bool> g_hookBackendReady{false};
static std::atomic<bool> g_pltBackendReady{false};

static void LayerBindFramebuffer(GLenum target, GLuint framebuffer) {
    if (g_nextBindFramebuffer) g_nextBindFramebuffer(target, framebuffer);
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
        g_boundFramebuffer.store(framebuffer, std::memory_order_relaxed);
    }
}

static void LayerViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (g_nextViewport) g_nextViewport(x, y, width, height);
    g_viewportX.store(x, std::memory_order_relaxed);
    g_viewportY.store(y, std::memory_order_relaxed);
    g_viewportWidth.store(width, std::memory_order_relaxed);
    g_viewportHeight.store(height, std::memory_order_relaxed);
}

static void LayerDrawArrays(GLenum mode, GLint first, GLsizei count) {
    g_drawCalls.fetch_add(1, std::memory_order_relaxed);
    if (g_nextDrawArrays) g_nextDrawArrays(mode, first, count);
}

static void LayerDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    g_drawCalls.fetch_add(1, std::memory_order_relaxed);
    if (g_nextDrawElements) g_nextDrawElements(mode, count, type, indices);
}

static void LayerFramebufferTexture2D(
    GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    if (g_nextFramebufferTexture2D) {
        g_nextFramebufferTexture2D(target, attachment, textarget, texture, level);
    }
    g_lastAttachedTexture.store(texture, std::memory_order_relaxed);
    g_attachmentUpdates.fetch_add(1, std::memory_order_relaxed);
}

static EGLBoolean LayerSwapBuffers(EGLDisplay display, EGLSurface surface) {
    const uint64_t frame = ++g_frames;
    if (!g_nextSwapBuffers) return EGL_FALSE;

    const bool sampleFrame = (frame % 600) == 1;
    EGLint width = 0;
    EGLint height = 0;
    if (sampleFrame && g_nextQuerySurface) {
        g_nextQuerySurface(display, surface, EGL_WIDTH, &width);
        g_nextQuerySurface(display, surface, EGL_HEIGHT, &height);
    }

    const auto start = std::chrono::steady_clock::now();
    const EGLBoolean result = g_nextSwapBuffers(display, surface);
    if (sampleFrame) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        const EGLContext context = eglGetCurrentContext();
        const uint64_t draws = g_drawCalls.exchange(0, std::memory_order_relaxed);
        __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES",
            "Layer telemetry frame=%llu surface=%dx%d context=%p fbo=%u viewport=%d,%d %dx%d draws=%llu fbo_attach_updates=%llu last_texture=%u swap_call=%lldus result=%d",
            static_cast<unsigned long long>(frame), width, height,
            reinterpret_cast<void*>(context),
            g_boundFramebuffer.load(std::memory_order_relaxed),
            g_viewportX.load(std::memory_order_relaxed),
            g_viewportY.load(std::memory_order_relaxed),
            g_viewportWidth.load(std::memory_order_relaxed),
            g_viewportHeight.load(std::memory_order_relaxed),
            static_cast<unsigned long long>(draws),
            static_cast<unsigned long long>(g_attachmentUpdates.load(std::memory_order_relaxed)),
            g_lastAttachedTexture.load(std::memory_order_relaxed),
            static_cast<long long>(elapsed), static_cast<int>(result));
    }
    return result;
}

extern "C" __attribute__((visibility("default")))
void AndroidGLESLayer_Initialize(void* layer_id, GetNextProc get_next_layer_proc_address) {
    g_layerId = layer_id;
    g_getNext = get_next_layer_proc_address;
    // Android's GLES LayerLoader has already placed this layer inside the
    // authorized target process. These libraries are optional in-process
    // hook backends; they do not attach to or inject into another process.
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
    } else if (std::strcmp(funcName, "glBindFramebuffer") == 0) {
        if (next) g_nextBindFramebuffer = reinterpret_cast<GlBindFramebuffer>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerBindFramebuffer));
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
