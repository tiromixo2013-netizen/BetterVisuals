#include "SgsrRenderer.hpp"

#include "GlStateGuard.hpp"
#include "ShaderProgram.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <string_view>

using namespace geode::prelude;

namespace {

    // Snapdragon Game Super Resolution 1, adapted from Qualcomm's official
    // mobile GLSL implementation (BSD-3-Clause). Requires GLES 3.1 textureGather.
    constexpr char kVertex[] = R"glsl(#version 310 es
precision highp float;
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;
out highp vec2 v_uv;
void main() {
    v_uv = a_texCoord;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

    constexpr char kFragment[] = R"glsl(#version 310 es
precision mediump float;
precision highp int;
uniform highp vec4 u_viewportInfo;
uniform mediump sampler2D u_texture;
uniform mediump sampler2D u_alphaTexture;
uniform int u_operationMode;
in highp vec2 v_uv;
layout(location = 0) out vec4 outColor;

float fastLanczos2(float x) {
    float wA = x - 4.0;
    float wB = x * wA - wA;
    wA *= wA;
    return wB * wA;
}
vec2 weightY(float dx, float dy, float c, float stdDev) {
    float x = ((dx * dx) + (dy * dy)) * 0.55 + clamp(abs(c) * stdDev, 0.0, 1.0);
    float w = fastLanczos2(x);
    return vec2(w, w * c);
}
void main() {
    vec4 sourceSample = textureLod(u_texture, v_uv, 0.0);
    float sourceAlpha = textureLod(u_alphaTexture, v_uv, 0.0).a;
    vec3 color = sourceSample.rgb;

    // LERP (mode 4) is the linear-sampling reference path.
    if (u_operationMode == 4) {
        color = min(color, vec3(sourceAlpha));
        outColor = vec4(color, sourceAlpha);
        return;
    }

    // RGBA (1) gathers green-channel luma. RGBY (3) gathers luma packed into
    // alpha by the RGBY conversion pass. The original alpha is sampled apart.
    float centerLuma = (u_operationMode == 3) ? sourceSample.a : sourceSample.g;
    highp vec2 imgCoord = (v_uv * u_viewportInfo.zw) + vec2(-0.5, 0.5);
    highp vec2 imgCoordPixel = floor(imgCoord);
    highp vec2 coord = imgCoordPixel * u_viewportInfo.xy;
    vec2 pl = imgCoord - imgCoordPixel;

    vec4 left;
    if (u_operationMode == 3) left = textureGather(u_texture, coord, 3);
    else left = textureGather(u_texture, coord, 1);
    float edgeVote = abs(left.z - left.y) + abs(centerLuma - left.y) + abs(centerLuma - left.z);
    if (edgeVote > (8.0 / 255.0)) {
        coord.x += u_viewportInfo.x;
        vec4 right;
        vec4 upDown;
        if (u_operationMode == 3) {
            right = textureGather(u_texture, coord + vec2(u_viewportInfo.x, 0.0), 3);
            upDown.xy = textureGather(u_texture, coord + vec2(0.0, -u_viewportInfo.y), 3).wz;
            upDown.zw = textureGather(u_texture, coord + vec2(0.0, u_viewportInfo.y), 3).yx;
        } else {
            right = textureGather(u_texture, coord + vec2(u_viewportInfo.x, 0.0), 1);
            upDown.xy = textureGather(u_texture, coord + vec2(0.0, -u_viewportInfo.y), 1).wz;
            upDown.zw = textureGather(u_texture, coord + vec2(0.0, u_viewportInfo.y), 1).yx;
        }

        float mean = (left.y + left.z + right.x + right.w) * 0.25;
        left -= vec4(mean);
        right -= vec4(mean);
        upDown -= vec4(mean);
        float centerY = centerLuma - mean;
        float sum = abs(left.x) + abs(left.y) + abs(left.z) + abs(left.w)
                  + abs(right.x) + abs(right.y) + abs(right.z) + abs(right.w)
                  + abs(upDown.x) + abs(upDown.y) + abs(upDown.z) + abs(upDown.w);
        float stdDev = 2.181818 / max(sum, 0.00001);
        vec2 weights = weightY(pl.x, pl.y + 1.0, upDown.x, stdDev);
        weights += weightY(pl.x - 1.0, pl.y + 1.0, upDown.y, stdDev);
        weights += weightY(pl.x - 1.0, pl.y - 2.0, upDown.z, stdDev);
        weights += weightY(pl.x, pl.y - 2.0, upDown.w, stdDev);
        weights += weightY(pl.x + 1.0, pl.y - 1.0, left.x, stdDev);
        weights += weightY(pl.x, pl.y - 1.0, left.y, stdDev);
        weights += weightY(pl.x, pl.y, left.z, stdDev);
        weights += weightY(pl.x + 1.0, pl.y, left.w, stdDev);
        weights += weightY(pl.x - 1.0, pl.y - 1.0, right.x, stdDev);
        weights += weightY(pl.x - 2.0, pl.y - 1.0, right.y, stdDev);
        weights += weightY(pl.x - 2.0, pl.y, right.z, stdDev);
        weights += weightY(pl.x - 1.0, pl.y, right.w, stdDev);
        float finalY = weights.y / max(weights.x, 0.00001);
        float maxY = max(max(left.y, left.z), max(right.x, right.w));
        float minY = min(min(left.y, left.z), min(right.x, right.w));
        finalY = clamp(2.0 * finalY, minY, maxY);
        float deltaY = clamp(finalY - centerY, -23.0 / 255.0, 23.0 / 255.0);
        color = clamp(color + vec3(deltaY), 0.0, 1.0);
    }

    // Cocos game-layer textures are premultiplied-alpha. This clamp prevents
    // edge sharpening from creating RGB fringes outside the stored alpha.
    color = min(color, vec3(sourceAlpha));
    outColor = vec4(color, sourceAlpha);
}
)glsl";

    constexpr char kRgbYFragment[] = R"glsl(#version 310 es
precision mediump float;
uniform sampler2D u_texture;
in highp vec2 v_uv;
layout(location = 0) out vec4 outColor;
void main() {
    vec4 source = textureLod(u_texture, v_uv, 0.0);
    float luma = dot(source.rgb, vec3(0.299, 0.587, 0.114));
    outColor = vec4(source.rgb, luma);
}
)glsl";

    bool supportsGles31() {
        auto const raw = reinterpret_cast<char const*>(glGetString(GL_VERSION));
        if (!raw) return false;
        std::string_view version(raw);
        auto const marker = version.find("OpenGL ES ");
        if (marker == std::string_view::npos) return false;
        version.remove_prefix(marker + 10);
        if (version.size() < 3 || version[0] < '0' || version[0] > '9' ||
            version[1] != '.' || version[2] < '0' || version[2] > '9') {
            return false;
        }
        int const major = version[0] - '0';
        int const minor = version[2] - '0';
        return major > 3 || (major == 3 && minor >= 1);
    }

} // namespace

namespace bv::render {

    bool SgsrRenderer::prepare() {
        if (m_attempted) return m_program != 0;
        m_attempted = true;
        if (!supportsGles31()) {
            log::warn("[SGSR1] OpenGL ES 3.1 is unavailable in the active game context; using bilinear fallback");
            return false;
        }

        GlStateGuard state;
        std::array<std::string_view, 1> vertexSources{kVertex};
        std::array<std::string_view, 1> fragmentSources{kFragment};
        m_program = compileShaderProgram("Snapdragon Game Super Resolution 1", vertexSources, fragmentSources);
        std::array<std::string_view, 1> rgbYSources{kRgbYFragment};
        m_rgbYProgram = compileShaderProgram("SGSR1 RGBY input conversion", vertexSources, rgbYSources);
        if (!m_program || !m_rgbYProgram) {
            reset();
            return false;
        }

        m_textureUniform = glGetUniformLocation(m_program, "u_texture");
        m_alphaTextureUniform = glGetUniformLocation(m_program, "u_alphaTexture");
        m_viewportUniform = glGetUniformLocation(m_program, "u_viewportInfo");
        m_operationModeUniform = glGetUniformLocation(m_program, "u_operationMode");
        m_rgbYTextureUniform = glGetUniformLocation(m_rgbYProgram, "u_texture");
        if (m_textureUniform < 0 || m_alphaTextureUniform < 0 ||
            m_viewportUniform < 0 || m_operationModeUniform < 0 ||
            m_rgbYTextureUniform < 0 || !m_quad.initialize("SGSR1")) {
            reset();
            return false;
        }
        glUseProgram(m_program);
        glUniform1i(m_textureUniform, 0);
        glUniform1i(m_alphaTextureUniform, 1);
        glUseProgram(m_rgbYProgram);
        glUniform1i(m_rgbYTextureUniform, 0);
        return true;
    }

    bool SgsrRenderer::apply(
        GLuint inputTexture, GLsizei inputWidth, GLsizei inputHeight,
        GLsizei outputWidth, GLsizei outputHeight, int operationMode
    ) {
        if (!inputTexture || inputWidth <= 0 || inputHeight <= 0 ||
            outputWidth <= 0 || outputHeight <= 0 || !prepare()) {
            return false;
        }
        if (operationMode != 1 && operationMode != 3 && operationMode != 4) {
            operationMode = 1;
        }

        GlStateGuard state;
        GLint callerFramebuffer = 0;
        GLint callerViewport[4] = {};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &callerFramebuffer);
        glGetIntegerv(GL_VIEWPORT, callerViewport);
        GLuint upscaleTexture = inputTexture;

        if (operationMode == 3) {
            if (!m_rgbYTexture) glGenTextures(1, &m_rgbYTexture);
            if (!m_rgbYFramebuffer) glGenFramebuffers(1, &m_rgbYFramebuffer);
            if (!m_rgbYTexture || !m_rgbYFramebuffer) return false;

            if (m_rgbYWidth != inputWidth || m_rgbYHeight != inputHeight) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m_rgbYTexture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(
                    GL_TEXTURE_2D, 0, GL_RGBA, inputWidth, inputHeight, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, nullptr
                );
                glBindFramebuffer(GL_FRAMEBUFFER, m_rgbYFramebuffer);
                glFramebufferTexture2D(
                    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_rgbYTexture, 0
                );
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    log::error("[SGSR1] RGBY conversion framebuffer is incomplete");
                    return false;
                }
                m_rgbYWidth = inputWidth;
                m_rgbYHeight = inputHeight;
            }

            // RGBY uses alpha as luminance. Convert into a temporary texture,
            // while the original input remains bound separately for true alpha.
            glBindFramebuffer(GL_FRAMEBUFFER, m_rgbYFramebuffer);
            glViewport(0, 0, inputWidth, inputHeight);
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_CULL_FACE);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glUseProgram(m_rgbYProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, inputTexture);
            m_quad.bind();
            FullscreenQuad::draw();
            upscaleTexture = m_rgbYTexture;
        }

        // SGSR operates on the game-layer capture and composites with
        // premultiplied alpha over the parent scene/background.
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(callerFramebuffer));
        glViewport(callerViewport[0], callerViewport[1], callerViewport[2], callerViewport[3]);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(m_program);
        glUniform4f(
            m_viewportUniform,
            1.f / static_cast<GLfloat>(inputWidth),
            1.f / static_cast<GLfloat>(inputHeight),
            static_cast<GLfloat>(outputWidth),
            static_cast<GLfloat>(outputHeight)
        );
        glUniform1i(m_operationModeUniform, operationMode);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, upscaleTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        m_quad.bind();
        FullscreenQuad::draw();
        return true;
    }

    void SgsrRenderer::reset() {
        m_quad.reset();
        if (m_rgbYFramebuffer) glDeleteFramebuffers(1, &m_rgbYFramebuffer);
        if (m_rgbYTexture) glDeleteTextures(1, &m_rgbYTexture);
        if (m_program) glDeleteProgram(m_program);
        if (m_rgbYProgram) glDeleteProgram(m_rgbYProgram);
        m_rgbYFramebuffer = 0;
        m_rgbYTexture = 0;
        m_program = 0;
        m_rgbYProgram = 0;
        m_textureUniform = -1;
        m_alphaTextureUniform = -1;
        m_viewportUniform = -1;
        m_operationModeUniform = -1;
        m_rgbYTextureUniform = -1;
        m_rgbYWidth = 0;
        m_rgbYHeight = 0;
        m_attempted = false;
    }
} // namespace bv::render
