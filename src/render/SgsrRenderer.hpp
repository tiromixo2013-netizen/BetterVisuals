#pragma once

#include "FullscreenQuad.hpp"

namespace bv::render {

    class SgsrRenderer final {
    public:
        bool prepare();
        bool apply(
            GLuint inputTexture, GLsizei inputWidth, GLsizei inputHeight,
            GLsizei outputWidth, GLsizei outputHeight, int operationMode
        );
        void reset();

    private:
        GLuint m_program = 0;
        GLuint m_rgbYProgram = 0;
        GLuint m_rgbYFramebuffer = 0;
        GLuint m_rgbYTexture = 0;
        GLint m_textureUniform = -1;
        GLint m_alphaTextureUniform = -1;
        GLint m_viewportUniform = -1;
        GLint m_operationModeUniform = -1;
        GLint m_rgbYTextureUniform = -1;
        GLsizei m_rgbYWidth = 0;
        GLsizei m_rgbYHeight = 0;
        bool m_attempted = false;
        FullscreenQuad m_quad;
    };

} // namespace bv::render
