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
    vec3 color = sourceSample.rgb;
    highp vec2 imgCoord = (v_uv * u_viewportInfo.zw) + vec2(-0.5, 0.5);
    highp vec2 imgCoordPixel = floor(imgCoord);
    highp vec2 coord = imgCoordPixel * u_viewportInfo.xy;
    vec2 pl = imgCoord - imgCoordPixel;
    vec4 left = textureGather(u_texture, coord, 1);
    float edgeVote = abs(left.z - left.y) + abs(color.g - left.y) + abs(color.g - left.z);
    if (edgeVote > (8.0 / 255.0)) {
        coord.x += u_viewportInfo.x;
        vec4 right = textureGather(u_texture, coord + vec2(u_viewportInfo.x, 0.0), 1);
        vec4 upDown;
        upDown.xy = textureGather(u_texture, coord + vec2(0.0, -u_viewportInfo.y), 1).wz;
        upDown.zw = textureGather(u_texture, coord + vec2(0.0, u_viewportInfo.y), 1).yx;
        float mean = (left.y + left.z + right.x + right.w) * 0.25;
        left -= vec4(mean);
        right -= vec4(mean);
        upDown -= vec4(mean);
        float centerY = color.g - mean;
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
    // Preserve the scene alpha channel; forcing alpha to 1 made transparent
    // sprites and edges opaque after SGSR reconstruction.
    outColor = vec4(color, sourceSample.a);
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
        if (!m_program) return false;

        m_textureUniform = glGetUniformLocation(m_program, "u_texture");
        m_viewportUniform = glGetUniformLocation(m_program, "u_viewportInfo");
        if (m_textureUniform < 0 || m_viewportUniform < 0 || !m_quad.initialize("SGSR1")) {
            reset();
            return false;
        }
        return true;
    }

    bool SgsrRenderer::apply(
        GLuint inputTexture, GLsizei inputWidth, GLsizei inputHeight,
        GLsizei outputWidth, GLsizei outputHeight
    ) {
        if (!inputTexture || inputWidth <= 0 || inputHeight <= 0 ||
            outputWidth <= 0 || outputHeight <= 0 || !prepare()) {
            return false;
        }

        GlStateGuard state;
        glUseProgram(m_program);
        glUniform1i(m_textureUniform, 0);
        glUniform4f(
            m_viewportUniform,
            1.f / static_cast<GLfloat>(inputWidth),
            1.f / static_cast<GLfloat>(inputHeight),
            // SGSR maps output pixel coordinates back into source texels:
            // zw is the output viewport size, xy is the inverse source size.
            static_cast<GLfloat>(outputWidth),
            static_cast<GLfloat>(outputHeight)
        );
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        m_quad.bind();
        FullscreenQuad::draw();
        return true;
    }

    void SgsrRenderer::reset() {
        m_quad.reset();
        if (m_program) glDeleteProgram(m_program);
        m_program = 0;
        m_textureUniform = -1;
        m_viewportUniform = -1;
        m_attempted = false;
    }

} // namespace bv::render
