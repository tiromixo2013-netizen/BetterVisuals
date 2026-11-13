#include "FrameInterpolationRenderer.hpp"

#include "GlStateGuard.hpp"
#include "ShaderProgram.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
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

    // Bidirectional 3x3 block matching estimates local motion and checks that
    // the reverse correspondence closes the motion cycle before reconstruction.
    // The output runs at half surface resolution to bound fragment work on
    // mobile GPUs; the native frame remains full resolution.
    // This is conservative local flow, not production optical flow: large or
    // ambiguous motion falls back to the native current-frame pixel.
    constexpr char kPresentFragment[] = R"glsl(
varying vec2 v_uv;
uniform sampler2D u_texture;
void main() { gl_FragColor = texture2D(u_texture, v_uv); }
)glsl";

    // Low-cost bidirectional cross search. Five candidates in each direction
    // replace the previous 3x3 + 3x3 search (18 match samples). Output remains
    // half resolution; uncertain motion falls back to the native current frame.
    constexpr char kFragment[] = R"glsl(
precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_previous;
uniform sampler2D u_current;
uniform vec2 u_invResolution;

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

float matchCost(vec3 a, vec3 b) {
    return abs(luma(a) - luma(b));
}

void considerCandidate(
    vec3 referenceColor, vec3 candidateColor, vec2 offset,
    inout float bestCost, inout float secondBestCost, inout vec2 bestOffset
) {
    float cost = matchCost(referenceColor, candidateColor);
    if (cost < bestCost) {
        secondBestCost = bestCost;
        bestCost = cost;
        bestOffset = offset;
    }
    else if (cost < secondBestCost) {
        secondBestCost = cost;
    }
}

void main() {
    vec3 current = texture2D(u_current, v_uv).rgb;
    float bestCost = 1.0;
    float secondBestCost = 1.0;
    vec2 bestOffset = vec2(0.0);
    vec2 searchStep = 2.0 * u_invResolution;

    // Center + four cardinal neighbours: substantially fewer texture reads
    // and ALU operations than the former 3x3 search.
    considerCandidate(current, texture2D(u_previous, v_uv).rgb,
        vec2(0.0), bestCost, secondBestCost, bestOffset);
    considerCandidate(current, texture2D(u_previous,
        clamp(v_uv + vec2(-searchStep.x, 0.0), 0.0, 1.0)).rgb,
        vec2(-searchStep.x, 0.0), bestCost, secondBestCost, bestOffset);
    considerCandidate(current, texture2D(u_previous,
        clamp(v_uv + vec2(searchStep.x, 0.0), 0.0, 1.0)).rgb,
        vec2(searchStep.x, 0.0), bestCost, secondBestCost, bestOffset);
    considerCandidate(current, texture2D(u_previous,
        clamp(v_uv + vec2(0.0, -searchStep.y), 0.0, 1.0)).rgb,
        vec2(0.0, -searchStep.y), bestCost, secondBestCost, bestOffset);
    considerCandidate(current, texture2D(u_previous,
        clamp(v_uv + vec2(0.0, searchStep.y), 0.0, 1.0)).rgb,
        vec2(0.0, searchStep.y), bestCost, secondBestCost, bestOffset);

    bool hitSearchBoundary =
        abs(bestOffset.x) >= searchStep.x * 0.99 ||
        abs(bestOffset.y) >= searchStep.y * 0.99;

    // Reject flat/ambiguous regions, large motion and newly exposed pixels.
    if (hitSearchBoundary || bestCost > 0.08 ||
        (secondBestCost - bestCost) < 0.0025) {
        gl_FragColor = vec4(current, 1.0);
        return;
    }

    vec3 previousMatch = texture2D(
        u_previous, clamp(v_uv + bestOffset, 0.0, 1.0)
    ).rgb;

    // Five-tap reverse check rejects occlusions and false matches. This keeps
    // the no-ghost fallback while using 10 match samples instead of 18.
    float reverseBestCost = 1.0;
    float reverseSecondBestCost = 1.0;
    vec2 reverseBestOffset = vec2(0.0);

    considerCandidate(previousMatch, texture2D(u_current,
        clamp(v_uv + bestOffset, 0.0, 1.0)).rgb,
        vec2(0.0), reverseBestCost, reverseSecondBestCost, reverseBestOffset);
    considerCandidate(previousMatch, texture2D(u_current,
        clamp(v_uv + bestOffset + vec2(-searchStep.x, 0.0), 0.0, 1.0)).rgb,
        vec2(-searchStep.x, 0.0), reverseBestCost, reverseSecondBestCost, reverseBestOffset);
    considerCandidate(previousMatch, texture2D(u_current,
        clamp(v_uv + bestOffset + vec2(searchStep.x, 0.0), 0.0, 1.0)).rgb,
        vec2(searchStep.x, 0.0), reverseBestCost, reverseSecondBestCost, reverseBestOffset);
    considerCandidate(previousMatch, texture2D(u_current,
        clamp(v_uv + bestOffset + vec2(0.0, -searchStep.y), 0.0, 1.0)).rgb,
        vec2(0.0, -searchStep.y), reverseBestCost, reverseSecondBestCost, reverseBestOffset);
    considerCandidate(previousMatch, texture2D(u_current,
        clamp(v_uv + bestOffset + vec2(0.0, searchStep.y), 0.0, 1.0)).rgb,
        vec2(0.0, searchStep.y), reverseBestCost, reverseSecondBestCost, reverseBestOffset);

    vec2 motionCycleError = bestOffset + reverseBestOffset;
    vec2 consistencyLimit = 0.75 * u_invResolution;
    bool inconsistentMotion =
        abs(motionCycleError.x) > consistencyLimit.x ||
        abs(motionCycleError.y) > consistencyLimit.y;

    if (inconsistentMotion || reverseBestCost > 0.08 ||
        (reverseSecondBestCost - reverseBestCost) < 0.0015) {
        gl_FragColor = vec4(current, 1.0);
        return;
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

    VertexAttributeState captureFramePassAttribute(GLuint index) {
        VertexAttributeState state;
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &state.enabled);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &state.buffer);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &state.size);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &state.stride);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &state.type);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &state.normalized);
        glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, &state.pointer);
        return state;
    }

    void restoreFramePassAttribute(GLuint index, VertexAttributeState const& state) {
        glBindBuffer(GL_ARRAY_BUFFER, state.buffer);
        glVertexAttribPointer(
            index, state.size, state.type, static_cast<GLboolean>(state.normalized),
            state.stride, state.pointer
        );
        if (state.enabled == GL_TRUE) glEnableVertexAttribArray(index);
        else glDisableVertexAttribArray(index);
    }

    // The full GlStateGuard also queries blend equations, stencil functions,
    // clear values and renderbuffer state. Frame interpolation changes none of
    // those, so this smaller guard avoids dozens of driver queries per frame.
    class FramePassStateGuard final {
    public:
        FramePassStateGuard() {
            glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &m_activeTexture);
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_textures[0]);
            glActiveTexture(GL_TEXTURE1);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_textures[1]);
            glActiveTexture(static_cast<GLenum>(m_activeTexture));
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &m_arrayBuffer);
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_framebuffer);
            glGetIntegerv(GL_VIEWPORT, m_viewport);
            glGetBooleanv(GL_COLOR_WRITEMASK, m_colorMask);
            m_blend = glIsEnabled(GL_BLEND);
            m_depthTest = glIsEnabled(GL_DEPTH_TEST);
            m_stencilTest = glIsEnabled(GL_STENCIL_TEST);
            m_scissorTest = glIsEnabled(GL_SCISSOR_TEST);
            m_cullFace = glIsEnabled(GL_CULL_FACE);
            m_attributes[0] = captureFramePassAttribute(FullscreenQuad::kPositionAttribute);
            m_attributes[1] = captureFramePassAttribute(FullscreenQuad::kTexCoordAttribute);
        }

        ~FramePassStateGuard() {
            restoreFramePassAttribute(FullscreenQuad::kPositionAttribute, m_attributes[0]);
            restoreFramePassAttribute(FullscreenQuad::kTexCoordAttribute, m_attributes[1]);
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(m_arrayBuffer));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_textures[0]));
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_textures[1]));
            glActiveTexture(static_cast<GLenum>(m_activeTexture));
            glUseProgram(static_cast<GLuint>(m_program));
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_framebuffer));
            glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
            glColorMask(m_colorMask[0], m_colorMask[1], m_colorMask[2], m_colorMask[3]);
            restoreCapability(GL_BLEND, m_blend);
            restoreCapability(GL_DEPTH_TEST, m_depthTest);
            restoreCapability(GL_STENCIL_TEST, m_stencilTest);
            restoreCapability(GL_SCISSOR_TEST, m_scissorTest);
            restoreCapability(GL_CULL_FACE, m_cullFace);
        }

        FramePassStateGuard(FramePassStateGuard const&) = delete;
        FramePassStateGuard& operator=(FramePassStateGuard const&) = delete;

    private:
        GLint m_program = 0;
        GLint m_activeTexture = GL_TEXTURE0;
        GLint m_textures[2] = {};
        GLint m_arrayBuffer = 0;
        GLint m_framebuffer = 0;
        GLint m_viewport[4] = {};
        GLboolean m_colorMask[4] = {};
        GLboolean m_blend = GL_FALSE;
        GLboolean m_depthTest = GL_FALSE;
        GLboolean m_stencilTest = GL_FALSE;
        GLboolean m_scissorTest = GL_FALSE;
        GLboolean m_cullFace = GL_FALSE;
        std::array<VertexAttributeState, 2> m_attributes = {};
    };

    void restoreCapability(GLenum capability, GLboolean enabled) {
        if (enabled == GL_TRUE) glEnable(capability);
        else glDisable(capability);
    }

} // namespace

namespace bv::render {

    bool FrameInterpolationRenderer::prepare(GLsizei width, GLsizei height) {
        if (width <= 0 || height <= 0) return false;
        // Avoid the expensive full GL-state snapshot on every frame when the
        // renderer is already prepared for the current surface dimensions.
        if (width == m_surfaceWidth && height == m_surfaceHeight &&
            m_program && m_presentProgram && m_outputTexture && m_framebuffer) {
            return true;
        }
        GlStateGuard state;

        if (m_program == 0) {
            std::array<std::string_view, 1> vertexSources{kVertex};
            std::array<std::string_view, 1> fragmentSources{kFragment};
            m_program = compileShaderProgram(
                "Android frame interpolation", vertexSources, fragmentSources
            );
            if (m_program == 0) return false;

            std::array<std::string_view, 1> presentSources{kPresentFragment};
            m_presentProgram = compileShaderProgram(
                "Android midpoint presentation", vertexSources, presentSources
            );
            if (m_presentProgram == 0) {
                destroyResources();
                return false;
            }
            m_presentTextureUniform = glGetUniformLocation(m_presentProgram, "u_texture");
            if (m_presentTextureUniform < 0) {
                destroyResources();
                return false;
            }

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

        if (!m_outputTexture) glGenTextures(1, &m_outputTexture);
        if (!m_framebuffer) glGenFramebuffers(1, &m_framebuffer);
        if (!m_outputTexture || !m_framebuffer) {
            destroyResources();
            return false;
        }

        auto const outputWidth = std::max<GLsizei>(1, (width + 1) / 2);
        auto const outputHeight = std::max<GLsizei>(1, (height + 1) / 2);
        glBindTexture(GL_TEXTURE_2D, m_outputTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, outputWidth, outputHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_outputTexture, 0
        );
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            log::error("Frame interpolation output framebuffer is incomplete");
            destroyResources();
            return false;
        }

        m_width = outputWidth;
        m_height = outputHeight;
        m_surfaceWidth = width;
        m_surfaceHeight = height;
        glUseProgram(m_program);
        glUniform1i(m_previousUniform, 0);
        glUniform1i(m_currentUniform, 1);
        glUniform2f(
            m_invResolutionUniform,
            1.f / static_cast<GLfloat>(outputWidth),
            1.f / static_cast<GLfloat>(outputHeight)
        );
        return true;
    }

    bool FrameInterpolationRenderer::generate(GLuint previousFrame, GLuint currentFrame) {
        if (!previousFrame || !currentFrame || previousFrame == currentFrame ||
            !m_program || !m_framebuffer || !m_outputTexture ||
            m_width <= 0 || m_height <= 0) {
            return false;
        }
        FramePassStateGuard state;
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        glViewport(0, 0, m_width, m_height);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(m_program);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, previousFrame);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, currentFrame);
        m_quad.bind();
        FullscreenQuad::draw();

        // Do not consume the caller's pre-existing GL error state here:
        // glGetError() would steal errors produced by the game before this pass.
        // FBO completeness is validated in prepare(); once inputs/resources are
        // valid, the draw has been issued and state is restored by the guard.
        return true;
    }

    bool FrameInterpolationRenderer::present() {
        return presentTexture(m_outputTexture);
    }

    bool FrameInterpolationRenderer::presentTexture(GLuint texture) {
        if (!texture || !m_presentProgram || m_surfaceWidth <= 0 || m_surfaceHeight <= 0) return false;
        FramePassStateGuard state;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_surfaceWidth, m_surfaceHeight);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(m_presentProgram);
        glUniform1i(m_presentTextureUniform, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        m_quad.bind();
        FullscreenQuad::draw();
        return true;
    }

    void FrameInterpolationRenderer::reset() {
        destroyResources();
    }

    void FrameInterpolationRenderer::abandonContext() {
        // The old GL context is gone; never delete its object names in a new context.
        m_framebuffer = 0;
        m_outputTexture = 0;
        m_program = 0;
        m_presentProgram = 0;
        m_previousUniform = -1;
        m_currentUniform = -1;
        m_invResolutionUniform = -1;
        m_presentTextureUniform = -1;
        m_width = 0;
        m_height = 0;
        m_surfaceWidth = 0;
        m_surfaceHeight = 0;
        m_quad.abandonContext();
    }

    void FrameInterpolationRenderer::destroyResources() {
        m_quad.reset();
        if (m_framebuffer) glDeleteFramebuffers(1, &m_framebuffer);
        if (m_outputTexture) glDeleteTextures(1, &m_outputTexture);
        if (m_program) glDeleteProgram(m_program);
        if (m_presentProgram) glDeleteProgram(m_presentProgram);
        m_framebuffer = 0;
        m_outputTexture = 0;
        m_program = 0;
        m_presentProgram = 0;
        m_previousUniform = -1;
        m_currentUniform = -1;
        m_invResolutionUniform = -1;
        m_width = 0;
        m_height = 0;
        m_surfaceWidth = 0;
        m_surfaceHeight = 0;
    }

} // namespace bv::render
