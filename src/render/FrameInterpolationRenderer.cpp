#include "FrameInterpolationRenderer.hpp"

#include "GlStateGuard.hpp"
#include "ShaderProgram.hpp"

#include <Geode/Geode.hpp>
#include <array>

using namespace geode::prelude;

namespace {

    constexpr char kVertex[] = R"glsl(
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_uv;
void main() {
    v_uv = a_texCoord;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

    // A small bounded block-matching search estimates a local displacement
    // from the current frame back into the previous frame. The midpoint is
    // reconstructed by sampling both frames halfway along that displacement.
    // This is intentionally conservative and is not a claim of production
    // quality optical flow: occlusions and large motion remain difficult.
    constexpr char kFragment[] = R"glsl(
varying vec2 v_uv;
uniform sampler2D u_previous;
uniform sampler2D u_current;
uniform vec2 u_invResolution;

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec3 current = texture2D(u_current, v_uv).rgb;
    float bestCost = 1.0;
    vec2 bestOffset = vec2(0.0);

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 offset = vec2(float(x), float(y)) * u_invResolution;
            vec3 candidate = texture2D(u_previous, clamp(v_uv + offset, 0.0, 1.0)).rgb;
            float cost = abs(luma(current) - luma(candidate));
            if (cost < bestCost) {
                bestCost = cost;
                bestOffset = offset;
            }
        }
    }

    // Reject ambiguous matches to avoid dragging unrelated pixels across cuts.
    if (bestCost > 0.16) {
        bestOffset = vec2(0.0);
    }

    vec2 halfMotion = bestOffset * 0.5;
    vec3 fromPrevious = texture2D(
        u_previous, clamp(v_uv + halfMotion, 0.0, 1.0)
    ).rgb;
    vec3 fromCurrent = texture2D(
        u_current, clamp(v_uv - halfMotion, 0.0, 1.0)
    ).rgb;
    gl_FragColor = vec4((fromPrevious + fromCurrent) * 0.5, 1.0);
}
)glsl";

} // namespace

namespace bv::render {

    bool FrameInterpolationRenderer::prepare(GLsizei width, GLsizei height) {
        if (width <= 0 || height <= 0) return false;
        GlStateGuard state;

        if (m_program == 0) {
            std::array<std::string_view, 1> vertexSources{kVertex};
            std::array<std::string_view, 1> fragmentSources{kFragment};
            m_program = compileShaderProgram(
                "Android frame interpolation", vertexSources, fragmentSources
            );
            if (m_program == 0) return false;

            m_previousUniform = glGetUniformLocation(m_program, "u_previous");
            m_currentUniform = glGetUniformLocation(m_program, "u_current");
            m_invResolutionUniform = glGetUniformLocation(m_program, "u_invResolution");
            if (m_previousUniform < 0 || m_currentUniform < 0 || m_invResolutionUniform < 0) {
                destroyResources();
                return false;
            }
            if (!m_quad.initialize("frame interpolation")) {
                destroyResources();
                return false;
            }
        }

        if (width == m_width && height == m_height && m_outputTexture && m_framebuffer) {
            return true;
        }

        if (!m_outputTexture) glGenTextures(1, &m_outputTexture);
        if (!m_framebuffer) glGenFramebuffers(1, &m_framebuffer);
        if (!m_outputTexture || !m_framebuffer) {
            destroyResources();
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, m_outputTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_outputTexture, 0
        );
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            log::error("Frame interpolation output framebuffer is incomplete");
            destroyResources();
            return false;
        }

        m_width = width;
        m_height = height;
        glUseProgram(m_program);
        glUniform1i(m_previousUniform, 0);
        glUniform1i(m_currentUniform, 1);
        glUniform2f(
            m_invResolutionUniform,
            1.f / static_cast<GLfloat>(width),
            1.f / static_cast<GLfloat>(height)
        );
        return true;
    }

    bool FrameInterpolationRenderer::generate(GLuint previousFrame, GLuint currentFrame) {
        if (!previousFrame || !currentFrame || !m_program || !m_framebuffer || !m_outputTexture) {
            return false;
        }
        GlStateGuard state;
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        glViewport(0, 0, m_width, m_height);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glUseProgram(m_program);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, previousFrame);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, currentFrame);
        m_quad.bind();
        FullscreenQuad::draw();
        return glGetError() == GL_NO_ERROR;
    }

    void FrameInterpolationRenderer::reset() {
        destroyResources();
    }

    void FrameInterpolationRenderer::abandonContext() {
        // The old GL context is gone; never delete its object names in a new context.
        m_framebuffer = 0;
        m_outputTexture = 0;
        m_program = 0;
        m_previousUniform = -1;
        m_currentUniform = -1;
        m_invResolutionUniform = -1;
        m_width = 0;
        m_height = 0;
        // FullscreenQuad::reset would issue GL calls against the replacement context.
        // Its stale VBO name is intentionally abandoned with the lost context.
    }

    void FrameInterpolationRenderer::destroyResources() {
        m_quad.reset();
        if (m_framebuffer) glDeleteFramebuffers(1, &m_framebuffer);
        if (m_outputTexture) glDeleteTextures(1, &m_outputTexture);
        if (m_program) glDeleteProgram(m_program);
        m_framebuffer = 0;
        m_outputTexture = 0;
        m_program = 0;
        m_previousUniform = -1;
        m_currentUniform = -1;
        m_invResolutionUniform = -1;
        m_width = 0;
        m_height = 0;
    }

} // namespace bv::render
