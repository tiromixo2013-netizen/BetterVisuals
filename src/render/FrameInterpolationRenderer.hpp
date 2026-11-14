#pragma once

#include "FullscreenQuad.hpp"

#include <Geode/cocos/platform/CCGL.h>

namespace bv::render {

    // Legacy class name retained for source compatibility. Estimates forward motion
    // from two real frames and generates a one-frame-ahead prediction texture.
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
        // Prediction is rendered at half the surface dimensions to bound the
        // per-pixel motion-search workload on mobile GPUs.
        GLsizei m_width = 0;
        GLsizei m_height = 0;
        GLsizei m_surfaceWidth = 0;
        GLsizei m_surfaceHeight = 0;
        FullscreenQuad m_quad;
    };

} // namespace bv::render
