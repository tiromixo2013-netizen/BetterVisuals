#include <EGL/egl.h>
#include <android/log.h>
#include <atomic>
#include <cstdint>
#include <cstring>

using GetNextProc = void* (*)(void*, const char*);
static void* g_layerId = nullptr;
static GetNextProc g_getNext = nullptr;
static std::atomic<uint64_t> g_frames{0};
using EglSwapBuffers = EGLBoolean (*)(EGLDisplay, EGLSurface);
static EglSwapBuffers g_nextSwapBuffers = nullptr;

static EGLBoolean LayerSwapBuffers(EGLDisplay display, EGLSurface surface) {
    const uint64_t frame = ++g_frames;
    if ((frame % 600) == 1) {
        __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES",
            "Pass-through GLES layer alive; observed frame=%llu",
            static_cast<unsigned long long>(frame));
    }
    if (!g_nextSwapBuffers) return EGL_FALSE;
    return g_nextSwapBuffers(display, surface);
}

extern "C" __attribute__((visibility("default")))
void AndroidGLESLayer_Initialize(void* layer_id, GetNextProc get_next_layer_proc_address) {
    g_layerId = layer_id;
    g_getNext = get_next_layer_proc_address;
    if (g_getNext) {
        g_nextSwapBuffers = reinterpret_cast<EglSwapBuffers>(
            g_getNext(g_layerId, "eglSwapBuffers"));
    }
    __android_log_print(ANDROID_LOG_INFO, "UltraScaleGLES",
        "UltraScale GLES pass-through layer initialized");
}

extern "C" __attribute__((visibility("default")))
void* AndroidGLESLayer_GetProcAddress(const char* funcName, void* next) {
    if (funcName && std::strcmp(funcName, "eglSwapBuffers") == 0) {
        if (next) g_nextSwapBuffers = reinterpret_cast<EglSwapBuffers>(next);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&LayerSwapBuffers));
    }
    return next;
}
