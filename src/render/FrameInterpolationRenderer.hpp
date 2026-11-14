#pragma once

#include "FullscreenQuad.hpp"

#include <Geode/cocos/platform/CCGL.h>
#include <array>

namespace bv::render {

    // Two-pass forward prediction: a 16x16 coarse motion field is generated
    // first, then high-complexity 8x8 regions are locally refined.
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

        GLuint m_motionProgram = 0;
        GLuint m_program = 0;
        GLuint m_presentProgram = 0;
        std::array<GLuint, 2> m_motionFramebuffers = {};
        std::array<GLuint, 2> m_motionTextures = {};
        GLuint m_framebuffer = 0;
        GLuint m_outputTexture = 0;

        GLint m_motionPreviousUniform = -1;
        GLint m_motionCurrentUniform = -1;
        GLint m_motionReuseUniform = -1;
        GLint m_motionInvResolutionUniform = -1;
        GLint m_motionGridSizeUniform = -1;
        GLint m_motionHasReuseUniform = -1;
        GLint m_previousUniform = -1;
        GLint m_currentUniform = -1;
        GLint m_motionUniform = -1;
        GLint m_invResolutionUniform = -1;
        GLint m_motionTexelUniform = -1;
        GLint m_presentTextureUniform = -1;

        GLsizei m_width = 0;
        GLsizei m_height = 0;
        GLsizei m_surfaceWidth = 0;
        GLsizei m_surfaceHeight = 0;
        GLsizei m_motionWidth = 0;
        GLsizei m_motionHeight = 0;
        std::size_t m_motionReadIndex = 0;
        bool m_hasMotionHistory = false;
        FullscreenQuad m_quad;
    };

} // namespace bv::render
