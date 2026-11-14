#pragma once

#include "FullscreenQuad.hpp"

namespace bv::render {

    class SgsrRenderer final {
    public:
        bool prepare();
        bool apply(
            GLuint inputTexture, GLsizei inputWidth, GLsizei inputHeight,
            GLsizei outputWidth, GLsizei outputHeight
        );
        void reset();

    private:
        GLuint m_program = 0;
        GLint m_textureUniform = -1;
        GLint m_viewportUniform = -1;
        bool m_attempted = false;
        FullscreenQuad m_quad;
    };

} // namespace bv::render
