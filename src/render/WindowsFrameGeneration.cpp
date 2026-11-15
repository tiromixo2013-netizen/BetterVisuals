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
    using WglSetSwapInterval = BOOL (WINAPI*)(int);
    using WglGetSwapInterval = int (WINAPI*)();

    std::atomic<bool> g_windowsFrameGenerationEnabled{false};

    class WindowsFrameHistory final {
    public:
        bool beginFrame() {
            auto const context = wglGetCurrentContext();
            auto const dc = wglGetCurrentDC();
            if (!context || !dc) return false;

            // Frame generation is only useful when the active output can scan
            // at least twice the game's nominal 60 Hz source cadence.
            auto const refreshRate = GetDeviceCaps(dc, VREFRESH);
            if (refreshRate < 100) {
                if (!m_refreshWarningLogged) {
                    m_refreshWarningLogged = true;
                    log::warn("[FrameGeneration][Windows] A 100 Hz or faster display is required; generation remains inactive");
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
            if (!enableUnpacedSwap(context)) return false;

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
            restoreSwapInterval();
        }

    private:
        bool enableUnpacedSwap(HGLRC context) {
            if (m_contextWithSwapPolicy == context && m_swapPolicyReady) return true;
            auto const setInterval = reinterpret_cast<WglSetSwapInterval>(
                wglGetProcAddress("wglSwapIntervalEXT"));
            auto const getInterval = reinterpret_cast<WglGetSwapInterval>(
                wglGetProcAddress("wglGetSwapIntervalEXT"));
            if (!setInterval || !getInterval) {
                if (!m_swapExtensionWarningLogged) {
                    m_swapExtensionWarningLogged = true;
                    log::warn("[FrameGeneration][Windows] WGL swap-control extensions unavailable; generated swaps are disabled");
                }
                return false;
            }
            m_originalSwapInterval = getInterval();
            if (setInterval(0) != TRUE) return false;
            m_contextWithSwapPolicy = context;
            m_swapPolicyReady = true;
            log::info("[FrameGeneration][Windows] WGL swap interval set to 0 for best-effort 2x presentation");
            return true;
        }

        void restoreSwapInterval() {
            if (!m_swapPolicyReady) return;
            auto const setInterval = reinterpret_cast<WglSetSwapInterval>(
                wglGetProcAddress("wglSwapIntervalEXT"));
            if (setInterval) (void)setInterval(m_originalSwapInterval);
            m_swapPolicyReady = false;
            m_contextWithSwapPolicy = nullptr;
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
        HGLRC m_contextWithSwapPolicy = nullptr;
        GLsizei m_width = 0;
        GLsizei m_height = 0;
        std::size_t m_writeIndex = 0;
        std::size_t m_currentIndex = 0;
        std::size_t m_validFrames = 0;
        int m_originalSwapInterval = 1;
        bool m_swapPolicyReady = false;
        bool m_refreshWarningLogged = false;
        bool m_swapExtensionWarningLogged = false;
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
