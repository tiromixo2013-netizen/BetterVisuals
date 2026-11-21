#pragma once

#include "FullscreenQuad.hpp"

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
        FullscreenQuad m_quad;
    };

} // namespace bv::render
