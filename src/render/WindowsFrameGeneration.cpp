#ifdef GEODE_IS_WINDOWS

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "FrameInterpolationRenderer.hpp"
#include "GlStateGuard.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <array>
#include <atomic>
#include <cstdint>

using namespace geode::prelude;

namespace {
    using WglGetSwapInterval = int (WINAPI*)();

    std::atomic<bool> g_windowsFrameGenerationEnabled{false};

    class WindowsFrameHistory final {
    public:
        bool beginFrame() {
            auto const context = wglGetCurrentContext();
            auto const dc = wglGetCurrentDC();
            if (!context || !dc) return false;

            // The only cadence we attempt is two VSync-paced swaps on a
            // reported 120 Hz output with the application's interval already 1:
            // real frame, then generated midpoint. DWM/driver composition can
            // still alter final display cadence, so this is not a display guarantee.
            // Do not force interval 0: that explicitly removes retrace sync.
            auto const refreshRate = GetDeviceCaps(dc, VREFRESH);
            if (refreshRate != 120) {
                if (!m_refreshWarningLogged) {
                    m_refreshWarningLogged = true;
                    log::warn("[FrameGeneration][Windows] Safe two-swap cadence currently requires a reported 120 Hz display; no swap policy will be changed");
                }
                return false;
            }

            if (m_context != context) {
                m_renderer.abandonContext();
                m_textures = {};
                m_width = 0;
                m_height = 0;
                m_writeIndex = 0;
                m_validFrames = 0;
                m_context = context;
            }

            GLint framebuffer = 0;
            GLint viewport[4] = {};
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
            glGetIntegerv(GL_VIEWPORT, viewport);
            if (framebuffer != 0 || viewport[2] <= 0 || viewport[3] <= 0) return false;
            if (!hasSafeVsyncCadence(context)) return false;

            auto const width = viewport[2];
            auto const height = viewport[3];
            if (width != m_width || height != m_height) {
                releaseSizeResources();
                m_width = width;
                m_height = height;
            }

            bv::render::GlStateGuard state;
            glActiveTexture(GL_TEXTURE0);
            if (!m_textures[0] || !m_textures[1]) {
                glGenTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
                if (!m_textures[0] || !m_textures[1]) {
                    releaseSizeResources();
                    return false;
                }
                for (auto texture : m_textures) {
                    glBindTexture(GL_TEXTURE_2D, texture);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                }
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, m_textures[m_writeIndex]);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);

            m_currentIndex = m_writeIndex;
            m_writeIndex = 1 - m_writeIndex;
            if (m_validFrames < 2) ++m_validFrames;
            return true;
        }

        bool generateAndPresent() {
            if (m_validFrames < 2 || m_width <= 0 || m_height <= 0) return false;
            auto const previousIndex = 1 - m_currentIndex;
            if (!m_renderer.prepare(m_width, m_height) ||
                !m_renderer.generate(m_textures[previousIndex], m_textures[m_currentIndex])) {
                return false;
            }
            return m_renderer.present();
        }

        void disable() {
            // Intentionally no swap-state mutation: this hook never changes
            // the game's WGL swap interval, so there is nothing to restore.
        }

    private:
        template <typename T>
        static bool isValidWglProc(T proc) {
            // wglGetProcAddress may return the legacy invalid sentinels 1, 2,
            // 3, or -1 rather than nullptr.
            return proc &&
                proc != reinterpret_cast<T>(1) &&
                proc != reinterpret_cast<T>(2) &&
                proc != reinterpret_cast<T>(3) &&
                proc != reinterpret_cast<T>(-1);
        }

        bool hasSafeVsyncCadence(HGLRC context) {
            auto const getInterval = reinterpret_cast<WglGetSwapInterval>(
                wglGetProcAddress("wglGetSwapIntervalEXT"));
            if (!isValidWglProc(getInterval)) {
                if (!m_swapExtensionWarningLogged) {
                    m_swapExtensionWarningLogged = true;
                    log::warn("[FrameGeneration][Windows] WGL swap-control query unavailable; generated swaps remain disabled");
                }
                return false;
            }

            auto const interval = getInterval();
            if (interval != 1) {
                if (!m_swapCadenceWarningLogged) {
                    m_swapCadenceWarningLogged = true;
                    log::warn("[FrameGeneration][Windows] Existing WGL swap interval is not 1; refusing unsafe generated swaps and leaving the game's setting unchanged");
                }
                return false;
            }

            if (!m_cadenceInfoLogged) {
                m_cadenceInfoLogged = true;
                log::info("[FrameGeneration][Windows] Existing VSync interval 1 confirmed on 120 Hz; WGL swap policy remains untouched");
            }
            return true;
        }

        void releaseSizeResources() {
            if (m_textures[0] || m_textures[1]) {
                glDeleteTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
            }
            m_textures = {};
            m_renderer.reset();
            m_width = 0;
            m_height = 0;
            m_validFrames = 0;
            m_writeIndex = 0;
        }

        bv::render::FrameInterpolationRenderer m_renderer;
        std::array<GLuint, 2> m_textures{};
        HGLRC m_context = nullptr;
        GLsizei m_width = 0;
        GLsizei m_height = 0;
        std::size_t m_writeIndex = 0;
        std::size_t m_currentIndex = 0;
        std::size_t m_validFrames = 0;
        bool m_refreshWarningLogged = false;
        bool m_swapExtensionWarningLogged = false;
        bool m_swapCadenceWarningLogged = false;
        bool m_cadenceInfoLogged = false;
    };

    WindowsFrameHistory g_windowsFrameHistory;
    thread_local bool g_insideWindowsFrameGenerationHook = false;
}

$on_mod(Loaded) {
    g_windowsFrameGenerationEnabled.store(
        Mod::get()->getSettingValue<bool>("windows-frame-generation"),
        std::memory_order_relaxed
    );
    listenForSettingChanges<bool>("windows-frame-generation", [](bool enabled) {
        g_windowsFrameGenerationEnabled.store(enabled, std::memory_order_relaxed);
    });
}

class $modify(BetterVisualsWindowsFrameGenerationHook, CCEGLView) {
    void swapBuffers() {
        if (g_insideWindowsFrameGenerationHook) {
            CCEGLView::swapBuffers();
            return;
        }

        if (!g_windowsFrameGenerationEnabled.load(std::memory_order_relaxed)) {
            g_windowsFrameHistory.disable();
            CCEGLView::swapBuffers();
            return;
        }

        g_insideWindowsFrameGenerationHook = true;
        bool const captured = g_windowsFrameHistory.beginFrame();
        if (!captured) g_windowsFrameHistory.disable();

        // Always present the genuine game frame first. A failed capture,
        // reconstruction, or generated swap can never replace the real frame.
        CCEGLView::swapBuffers();

        if (captured && g_windowsFrameHistory.generateAndPresent()) {
            CCEGLView::swapBuffers();
        }
        else {
            // If no midpoint was produced (including the first history frame),
            // restore the game's original swap interval instead of leaving it uncapped.
            g_windowsFrameHistory.disable();
        }
        g_insideWindowsFrameGenerationHook = false;
    }
};

#endif
