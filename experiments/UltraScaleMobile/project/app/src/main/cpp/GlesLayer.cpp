#include <EGL/egl.h>
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

static void* g_layerId = nullptr;
static GetNextProc g_getNext = nullptr;
static EglSwapBuffers g_nextSwapBuffers = nullptr;
static EglQuerySurface g_nextQuerySurface = nullptr;
static std::atomic<uint64_t> g_frames{0};
static std::atomic<bool> g_hookBackendReady{false};
static std::atomic<bool> g_pltBackendReady{false};

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
        __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES",
            "Pass-through sample frame=%llu surface=%dx%d context=%p swap_call=%lldus result=%d",
            static_cast<unsigned long long>(frame), width, height,
            reinterpret_cast<void*>(context), static_cast<long long>(elapsed),
            static_cast<int>(result));
    }
    return result;
}

extern "C" __attribute__((visibility("default")))
void AndroidGLESLayer_Initialize(void* layer_id, GetNextProc get_next_layer_proc_address) {
    g_layerId = layer_id;
    g_getNext = get_next_layer_proc_address;
    // Initializes the reusable in-process hook engine. No cross-process attach,
    // ptrace, permission bypass, or automatic game-memory modification occurs.
    const int hookInit = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
    g_hookBackendReady.store(hookInit == 0, std::memory_order_release);
    // ByteHook supplies PLT hooks; initialize only after its ShadowHook dependency.
    const int pltInit = hookInit == 0 ? bytehook_init(BYTEHOOK_MODE_AUTOMATIC, false) : -1;
    g_pltBackendReady.store(pltInit == BYTEHOOK_STATUS_CODE_OK, std::memory_order_release);
    if (g_getNext) {
        g_nextSwapBuffers = reinterpret_cast<EglSwapBuffers>(
            g_getNext(g_layerId, "eglSwapBuffers"));
        g_nextQuerySurface = reinterpret_cast<EglQuerySurface>(
            g_getNext(g_layerId, "eglQuerySurface"));
    }
    __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES",
        "UltraScale GLES diagnostic layer initialized; inline=%s (%d), PLT=%s (%d)",
        g_hookBackendReady.load(std::memory_order_acquire) ? "ready" : "unavailable", hookInit,
        g_pltBackendReady.load(std::memory_order_acquire) ? "ready" : "unavailable", pltInit);
}

extern "C" __attribute__((visibility("default")))
void* AndroidGLESLayer_GetProcAddress(const char* funcName, __eglMustCastToProperFunctionPointerType next) {
    if (funcName && std::strcmp(funcName, "eglSwapBuffers") == 0) {
        if (next) g_nextSwapBuffers = reinterpret_cast<EglSwapBuffers>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerSwapBuffers));
    }
    if (funcName && std::strcmp(funcName, "eglQuerySurface") == 0 && next) {
        g_nextQuerySurface = reinterpret_cast<EglQuerySurface>(next);
    }
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(next));
}
