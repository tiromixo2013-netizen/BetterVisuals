#pragma once

#include "FullscreenQuad.hpp"

#include <Geode/cocos/platform/CCGL.h>

namespace bv::render {

    // Lightweight dense block-match motion estimate and midpoint reconstruction.
    // Generates a GPU midpoint texture and can draw it into the active EGL back buffer.
    class FrameInterpolationRenderer final {
    public:
        FrameInterpolationRenderer() = default;
        FrameInterpolationRenderer(FrameInterpolationRenderer const&) = delete;
        FrameInterpolationRenderer& operator=(FrameInterpolationRenderer const&) = delete;

        bool prepare(GLsizei width, GLsizei height);
        bool generate(GLuint previousFrame, GLuint currentFrame);
        bool present();
        bool presentTexture(GLuint texture);
        GLuint outputTexture() const { return m_outputTexture; }
        void reset();
        void abandonContext();

    private:
        void destroyResources();

        GLuint m_program = 0;
        GLuint m_presentProgram = 0;
        GLuint m_framebuffer = 0;
        GLuint m_outputTexture = 0;
        GLint m_previousUniform = -1;
        GLint m_currentUniform = -1;
        GLint m_invResolutionUniform = -1;
        GLint m_presentTextureUniform = -1;
        GLsizei m_width = 0;
        GLsizei m_height = 0;
        FullscreenQuad m_quad;
    };

} // namespace bv::render
