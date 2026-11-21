#pragma once

#include "FullscreenQuad.hpp"

#include <array>

namespace bv::render {

    class MwsrRenderer final {
    public:
        bool apply(
            GLuint inputTexture, GLsizei inputWidth, GLsizei inputHeight,
            GLsizei outputWidth, GLsizei outputHeight
        );
        void reset();

    private:
        bool prepare();
        GLuint m_program = 0;
        GLint m_textureUniform = -1;
        GLint m_texelScaleUniform = -1;
        GLint m_dimsAcrossUniform = -1;
        GLint m_kernelUniform = -1;
        GLint m_boundUniform = -1;
        bool m_attempted = false;
        // MWSR coefficients depend only on source/destination dimensions.
        // Cache them so the render loop does not execute logarithms per frame.
        std::array<float, 6> m_cachedKernel{};
        GLsizei m_cachedInputWidth = 0;
        GLsizei m_cachedInputHeight = 0;
        GLsizei m_cachedOutputWidth = 0;
        GLsizei m_cachedOutputHeight = 0;
        FullscreenQuad m_quad;
    };

} // namespace bv::render
