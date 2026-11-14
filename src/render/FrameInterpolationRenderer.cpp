#include "FrameInterpolationRenderer.hpp"

#include "GlStateGuard.hpp"
#include "ShaderProgram.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

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

    // Pass 1: one motion estimate per 16x16 output-pixel block. Previous
    // vectors are reused only when the current pair validates their direction
    // and appearance; otherwise a bounded five-position search is performed.
    constexpr char kMotionFragment[] = R"glsl(
precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_previous;
uniform sampler2D u_current;
uniform sampler2D u_reusedMotion;
uniform vec2 u_invResolution;
uniform vec2 u_motionGridSize;
uniform float u_hasReuse;

float matchCost(vec3 a, vec3 b) {
    vec3 d = abs(a - b);
    return max(d.r, max(d.g, d.b));
}

void main() {
    vec2 blockIndex = floor(v_uv * u_motionGridSize);
    vec2 blockUv = clamp(
        (blockIndex * 16.0 + vec2(8.0)) * u_invResolution,
        0.5 * u_invResolution, vec2(1.0) - 0.5 * u_invResolution
    );
    vec2 searchStep = 2.0 * u_invResolution;
    vec3 current = texture2D(u_current, blockUv).rgb;

    float best = 1.0;
    float second = 1.0;
    vec2 bestOffset = vec2(0.0);

    // Reuse last frame's vector only when the old confidence is high and its
    // predicted correspondence still matches this frame. A reversal or scene
    // transition naturally fails validation and triggers a fresh search.
    vec4 reused = texture2D(u_reusedMotion, blockUv);
    vec2 reusedOffset = (reused.rg * 2.0 - 1.0) * searchStep;
    float reusedCost = matchCost(
        current, texture2D(u_previous, clamp(blockUv + reusedOffset, 0.0, 1.0)).rgb
    );
    bool reuseValid = u_hasReuse > 0.5 && reused.a > 0.45 && reusedCost < 0.055;
    if (reuseValid) {
        best = reusedCost;
        bestOffset = reusedOffset;
        second = reusedCost + 0.02;
    }

    if (!reuseValid) {
        best = matchCost(current, texture2D(u_previous, blockUv).rgb);
        second = 1.0;
        bestOffset = vec2(0.0);

        float c = matchCost(current, texture2D(
            u_previous, clamp(blockUv + vec2(-searchStep.x, 0.0), 0.0, 1.0)
        ).rgb);
        if (c < best) { second = best; best = c; bestOffset = vec2(-searchStep.x, 0.0); }
        else { second = min(second, c); }

        c = matchCost(current, texture2D(
            u_previous, clamp(blockUv + vec2(searchStep.x, 0.0), 0.0, 1.0)
        ).rgb);
        if (c < best) { second = best; best = c; bestOffset = vec2(searchStep.x, 0.0); }
        else { second = min(second, c); }

        c = matchCost(current, texture2D(
            u_previous, clamp(blockUv + vec2(0.0, -searchStep.y), 0.0, 1.0)
        ).rgb);
        if (c < best) { second = best; best = c; bestOffset = vec2(0.0, -searchStep.y); }
        else { second = min(second, c); }

        c = matchCost(current, texture2D(
            u_previous, clamp(blockUv + vec2(0.0, searchStep.y), 0.0, 1.0)
        ).rgb);
        if (c < best) { second = best; best = c; bestOffset = vec2(0.0, searchStep.y); }
        else { second = min(second, c); }
    }

    // Bidirectional validation rejects occlusions and newly exposed regions.
    vec2 previousUv = clamp(blockUv + bestOffset, 0.0, 1.0);
    vec3 previous = texture2D(u_previous, previousUv).rgb;
    float reverseBest = matchCost(previous, texture2D(u_current, previousUv).rgb);
    vec2 reverseOffset = vec2(0.0);
    float c = matchCost(previous, texture2D(
        u_current, clamp(previousUv - vec2(searchStep.x, 0.0), 0.0, 1.0)
    ).rgb);
    if (c < reverseBest) { reverseBest = c; reverseOffset = vec2(-searchStep.x, 0.0); }
    c = matchCost(previous, texture2D(
        u_current, clamp(previousUv + vec2(searchStep.x, 0.0), 0.0, 1.0)
    ).rgb);
    if (c < reverseBest) { reverseBest = c; reverseOffset = vec2(searchStep.x, 0.0); }
    c = matchCost(previous, texture2D(
        u_current, clamp(previousUv - vec2(0.0, searchStep.y), 0.0, 1.0)
    ).rgb);
    if (c < reverseBest) { reverseBest = c; reverseOffset = vec2(0.0, -searchStep.y); }
    c = matchCost(previous, texture2D(
        u_current, clamp(previousUv + vec2(0.0, searchStep.y), 0.0, 1.0)
    ).rgb);
    if (c < reverseBest) { reverseBest = c; reverseOffset = vec2(0.0, searchStep.y); }

    bool inconsistent =
        abs(reverseOffset.x + bestOffset.x) > searchStep.x * 0.5 ||
        abs(reverseOffset.y + bestOffset.y) > searchStep.y * 0.5;
    float confidence = clamp((second - best) * 12.0, 0.0, 1.0) *
        clamp((0.085 - best) * 12.0, 0.0, 1.0);
    if (reuseValid) {
        confidence = min(reused.a, clamp((0.055 - best) * 18.0, 0.0, 1.0));
    }
    else if (u_hasReuse > 0.5 && reused.a < 0.65 && confidence > reused.a) {
        // Confidence recovery is deliberately gradual after a protected frame.
        confidence = mix(reused.a, confidence, 0.35);
    }
    if (best > 0.075 || reverseBest > 0.075 || inconsistent) confidence = 0.0;
    if (reuseValid && (bestOffset.x * reusedOffset.x + bestOffset.y * reusedOffset.y) < 0.0) {
        confidence = 0.0;
    }

    vec2 encodedOffset = clamp(bestOffset / max(searchStep, vec2(0.000001)), -1.0, 1.0) * 0.5 + 0.5;
    gl_FragColor = vec4(encodedOffset, clamp(best, 0.0, 1.0), confidence);
}
)glsl";

    // Pass 2: reuse the 16x16 motion field for ordinary regions. High edge
    // complexity or disagreement between neighboring blocks activates a
    // bounded 8x8 local refinement. Any low-confidence result preserves the
    // current real pixel, so no generated holes or transparent black appear.
    constexpr char kFragment[] = R"glsl(
precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_previous;
uniform sampler2D u_current;
uniform sampler2D u_motion;
uniform vec2 u_invResolution;
uniform vec2 u_motionTexel;

float matchCost(vec3 a, vec3 b) {
    vec3 d = abs(a - b);
    return max(d.r, max(d.g, d.b));
}

void main() {
    vec4 currentSample = texture2D(u_current, v_uv);
    vec4 motion = texture2D(u_motion, v_uv);
    vec2 searchStep = 2.0 * u_invResolution;
    vec2 offset = (motion.rg * 2.0 - 1.0) * searchStep;

    vec2 motionLeft = (texture2D(u_motion, v_uv - vec2(u_motionTexel.x, 0.0)).rg * 2.0 - 1.0) * searchStep;
    vec2 motionRight = (texture2D(u_motion, v_uv + vec2(u_motionTexel.x, 0.0)).rg * 2.0 - 1.0) * searchStep;
    vec2 motionUp = (texture2D(u_motion, v_uv - vec2(0.0, u_motionTexel.y)).rg * 2.0 - 1.0) * searchStep;
    vec2 motionDown = (texture2D(u_motion, v_uv + vec2(0.0, u_motionTexel.y)).rg * 2.0 - 1.0) * searchStep;
    float disagreement = max(
        max(length(motionLeft - offset), length(motionRight - offset)),
        max(length(motionUp - offset), length(motionDown - offset))
    ) / max(length(searchStep), 0.000001);

    vec3 left = texture2D(u_current, clamp(v_uv - searchStep, 0.0, 1.0)).rgb;
    vec3 right = texture2D(u_current, clamp(v_uv + searchStep, 0.0, 1.0)).rgb;
    vec3 up = texture2D(u_current, clamp(v_uv - vec2(0.0, searchStep.y), 0.0, 1.0)).rgb;
    vec3 down = texture2D(u_current, clamp(v_uv + vec2(0.0, searchStep.y), 0.0, 1.0)).rgb;
    float edgeComplexity = max(
        matchCost(left, currentSample.rgb) + matchCost(right, currentSample.rgb),
        matchCost(up, currentSample.rgb) + matchCost(down, currentSample.rgb)
    );
    // Protected Prediction is selected by low block confidence, complex
    // local edges, or neighboring-block disagreement. It uses a stricter
    // 8x8 refinement path; stable regions stay on the cheaper 16x16 vector.
    bool protectedPrediction =
        motion.a < 0.55 || edgeComplexity > 0.075 || disagreement > 0.55;
    bool refineEightPixelBlock = protectedPrediction;

    float confidence = motion.a;
    if (protectedPrediction) {
        vec2 halfStep = searchStep * 0.5;
        float best = 1.0;
        float second = 1.0;
        vec2 refinedOffset = offset;
        float c = matchCost(
            currentSample.rgb,
            texture2D(u_previous, clamp(v_uv + offset, 0.0, 1.0)).rgb
        );
        best = c;
        c = matchCost(currentSample.rgb, texture2D(
            u_previous, clamp(v_uv + offset + vec2(-halfStep.x, 0.0), 0.0, 1.0)
        ).rgb);
        if (c < best) { second = best; best = c; refinedOffset = offset + vec2(-halfStep.x, 0.0); }
        else { second = min(second, c); }
        c = matchCost(currentSample.rgb, texture2D(
            u_previous, clamp(v_uv + offset + vec2(halfStep.x, 0.0), 0.0, 1.0)
        ).rgb);
        if (c < best) { second = best; best = c; refinedOffset = offset + vec2(halfStep.x, 0.0); }
        else { second = min(second, c); }
        c = matchCost(currentSample.rgb, texture2D(
            u_previous, clamp(v_uv + offset + vec2(0.0, -halfStep.y), 0.0, 1.0)
        ).rgb);
        if (c < best) { second = best; best = c; refinedOffset = offset + vec2(0.0, -halfStep.y); }
        else { second = min(second, c); }
        c = matchCost(currentSample.rgb, texture2D(
            u_previous, clamp(v_uv + offset + vec2(0.0, halfStep.y), 0.0, 1.0)
        ).rgb);
        if (c < best) { second = best; best = c; refinedOffset = offset + vec2(0.0, halfStep.y); }
        else { second = min(second, c); }
        confidence = clamp((second - best) * 10.0, 0.0, 1.0);
        if (best > 0.075) confidence = 0.0;
        offset = refinedOffset;
    }

    if (confidence < 0.30 || (disagreement > 1.15 && !refineEightPixelBlock)) {
        gl_FragColor = currentSample;
        return;
    }

    vec2 predictedCoord = clamp(v_uv - offset, 0.0, 1.0);
    vec4 predictedSample = texture2D(u_current, predictedCoord);
    gl_FragColor = predictedSample;
}
)glsl";

    constexpr char kPresentFragment[] = R"glsl(
varying vec2 v_uv;
uniform sampler2D u_texture;
void main() { gl_FragColor = texture2D(u_texture, v_uv); }
)glsl";

    bv::render::VertexAttributeState captureFramePassAttribute(GLuint index) {
        bv::render::VertexAttributeState state;
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &state.enabled);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &state.buffer);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &state.size);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &state.stride);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &state.type);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &state.normalized);
        glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, &state.pointer);
        return state;
    }

    void restoreFramePassAttribute(GLuint index, bv::render::VertexAttributeState const& state) {
        glBindBuffer(GL_ARRAY_BUFFER, state.buffer);
        glVertexAttribPointer(
            index, state.size, state.type, static_cast<GLboolean>(state.normalized),
            state.stride, state.pointer
        );
        if (state.enabled == GL_TRUE) glEnableVertexAttribArray(index);
        else glDisableVertexAttribArray(index);
    }

    void restoreCapability(GLenum capability, GLboolean enabled) {
        if (enabled == GL_TRUE) glEnable(capability);
        else glDisable(capability);
    }

    class FramePassStateGuard final {
    public:
        FramePassStateGuard() {
            glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &m_activeTexture);
            for (GLint unit = 0; unit < 3; ++unit) {
                glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_textures[unit]);
            }
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
            m_attributes[0] = captureFramePassAttribute(bv::render::FullscreenQuad::kPositionAttribute);
            m_attributes[1] = captureFramePassAttribute(bv::render::FullscreenQuad::kTexCoordAttribute);
        }

        ~FramePassStateGuard() {
            restoreFramePassAttribute(bv::render::FullscreenQuad::kPositionAttribute, m_attributes[0]);
            restoreFramePassAttribute(bv::render::FullscreenQuad::kTexCoordAttribute, m_attributes[1]);
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(m_arrayBuffer));
            for (GLint unit = 0; unit < 3; ++unit) {
                glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_textures[unit]));
            }
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
        GLint m_textures[3] = {};
        GLint m_arrayBuffer = 0;
        GLint m_framebuffer = 0;
        GLint m_viewport[4] = {};
        GLboolean m_colorMask[4] = {};
        GLboolean m_blend = GL_FALSE;
        GLboolean m_depthTest = GL_FALSE;
        GLboolean m_stencilTest = GL_FALSE;
        GLboolean m_scissorTest = GL_FALSE;
        GLboolean m_cullFace = GL_FALSE;
        std::array<bv::render::VertexAttributeState, 2> m_attributes = {};
    };
} // namespace

namespace bv::render {

    bool FrameInterpolationRenderer::prepare(GLsizei width, GLsizei height) {
        if (width <= 0 || height <= 0) return false;
        if (width == m_surfaceWidth && height == m_surfaceHeight &&
            m_motionProgram && m_program && m_presentProgram && m_outputTexture &&
            m_framebuffer && m_motionTextures[0] && m_motionTextures[1] &&
            m_motionFramebuffers[0] && m_motionFramebuffers[1]) {
            return true;
        }
        GlStateGuard state;

        if (!m_motionProgram) {
            std::array<std::string_view, 1> vertexSources{kVertex};
            std::array<std::string_view, 1> motionSources{kMotionFragment};
            m_motionProgram = compileShaderProgram(
                "Android adaptive block motion estimation", vertexSources, motionSources
            );
            std::array<std::string_view, 1> fragmentSources{kFragment};
            m_program = compileShaderProgram(
                "Android predictive frame reconstruction", vertexSources, fragmentSources
            );
            std::array<std::string_view, 1> presentSources{kPresentFragment};
            m_presentProgram = compileShaderProgram(
                "Android generated-frame presentation", vertexSources, presentSources
            );
            if (!m_motionProgram || !m_program || !m_presentProgram) {
                destroyResources();
                return false;
            }

            m_motionPreviousUniform = glGetUniformLocation(m_motionProgram, "u_previous");
            m_motionCurrentUniform = glGetUniformLocation(m_motionProgram, "u_current");
            m_motionReuseUniform = glGetUniformLocation(m_motionProgram, "u_reusedMotion");
            m_motionInvResolutionUniform = glGetUniformLocation(m_motionProgram, "u_invResolution");
            m_motionGridSizeUniform = glGetUniformLocation(m_motionProgram, "u_motionGridSize");
            m_motionHasReuseUniform = glGetUniformLocation(m_motionProgram, "u_hasReuse");
            m_previousUniform = glGetUniformLocation(m_program, "u_previous");
            m_currentUniform = glGetUniformLocation(m_program, "u_current");
            m_motionUniform = glGetUniformLocation(m_program, "u_motion");
            m_invResolutionUniform = glGetUniformLocation(m_program, "u_invResolution");
            m_motionTexelUniform = glGetUniformLocation(m_program, "u_motionTexel");
            m_presentTextureUniform = glGetUniformLocation(m_presentProgram, "u_texture");

            if (m_motionPreviousUniform < 0 || m_motionCurrentUniform < 0 ||
                m_motionReuseUniform < 0 || m_motionInvResolutionUniform < 0 ||
                m_motionGridSizeUniform < 0 || m_motionHasReuseUniform < 0 ||
                m_previousUniform < 0 || m_currentUniform < 0 || m_motionUniform < 0 ||
                m_invResolutionUniform < 0 || m_motionTexelUniform < 0 ||
                m_presentTextureUniform < 0 || !m_quad.initialize("predictive frame generation")) {
                destroyResources();
                return false;
            }
        }

        if (!m_outputTexture) glGenTextures(1, &m_outputTexture);
        if (!m_framebuffer) glGenFramebuffers(1, &m_framebuffer);
        if (!m_motionTextures[0] || !m_motionTextures[1]) {
            glGenTextures(2, m_motionTextures.data());
        }
        if (!m_motionFramebuffers[0] || !m_motionFramebuffers[1]) {
            glGenFramebuffers(2, m_motionFramebuffers.data());
        }
        if (!m_outputTexture || !m_framebuffer || !m_motionTextures[0] ||
            !m_motionTextures[1] || !m_motionFramebuffers[0] || !m_motionFramebuffers[1]) {
            destroyResources();
            return false;
        }

        auto const outputWidth = std::max<GLsizei>(1, (width + 1) / 2);
        auto const outputHeight = std::max<GLsizei>(1, (height + 1) / 2);
        auto const motionWidth = std::max<GLsizei>(1, (outputWidth + 15) / 16);
        auto const motionHeight = std::max<GLsizei>(1, (outputHeight + 15) / 16);

        glBindTexture(GL_TEXTURE_2D, m_outputTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, outputWidth, outputHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_outputTexture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            log::error("Predictive frame output framebuffer is incomplete");
            destroyResources();
            return false;
        }

        for (std::size_t i = 0; i < m_motionTextures.size(); ++i) {
            glBindTexture(GL_TEXTURE_2D, m_motionTextures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, motionWidth, motionHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, m_motionFramebuffers[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_motionTextures[i], 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                log::error("Predictive motion-field framebuffer {} is incomplete", i);
                destroyResources();
                return false;
            }
        }

        m_width = outputWidth;
        m_height = outputHeight;
        m_motionWidth = motionWidth;
        m_motionHeight = motionHeight;
        m_surfaceWidth = width;
        m_surfaceHeight = height;
        m_motionReadIndex = 0;
        m_hasMotionHistory = false;

        glUseProgram(m_motionProgram);
        glUniform1i(m_motionPreviousUniform, 0);
        glUniform1i(m_motionCurrentUniform, 1);
        glUniform1i(m_motionReuseUniform, 2);
        glUseProgram(m_program);
        glUniform1i(m_previousUniform, 0);
        glUniform1i(m_currentUniform, 1);
        glUniform1i(m_motionUniform, 2);
        glUseProgram(m_presentProgram);
        glUniform1i(m_presentTextureUniform, 0);
        return true;
    }

    bool FrameInterpolationRenderer::generate(GLuint previousFrame, GLuint currentFrame) {
        if (!previousFrame || !currentFrame || previousFrame == currentFrame ||
            !m_motionProgram || !m_program || !m_framebuffer || !m_outputTexture ||
            !m_motionTextures[0] || !m_motionTextures[1] ||
            m_width <= 0 || m_height <= 0 || m_motionWidth <= 0 || m_motionHeight <= 0) {
            return false;
        }

        FramePassStateGuard state;
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        auto const writeIndex = m_hasMotionHistory ? 1 - m_motionReadIndex : 0;

        // Coarse 16x16 motion field with validated temporal vector reuse.
        glBindFramebuffer(GL_FRAMEBUFFER, m_motionFramebuffers[writeIndex]);
        glViewport(0, 0, m_motionWidth, m_motionHeight);
        glUseProgram(m_motionProgram);
        glUniform2f(m_motionInvResolutionUniform, 1.f / m_width, 1.f / m_height);
        glUniform2f(m_motionGridSizeUniform, static_cast<GLfloat>(m_motionWidth), static_cast<GLfloat>(m_motionHeight));
        glUniform1f(m_motionHasReuseUniform, m_hasMotionHistory ? 1.f : 0.f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, previousFrame);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, currentFrame);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_motionTextures[m_motionReadIndex]);
        m_quad.bind();
        FullscreenQuad::draw();

        // Reconstruct at half surface resolution; uncertain regions use the
        // current real frame as a safe fallback instead of inventing pixels.
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        glViewport(0, 0, m_width, m_height);
        glUseProgram(m_program);
        glUniform2f(m_invResolutionUniform, 1.f / m_width, 1.f / m_height);
        glUniform2f(m_motionTexelUniform, 1.f / m_motionWidth, 1.f / m_motionHeight);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, previousFrame);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, currentFrame);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_motionTextures[writeIndex]);
        m_quad.bind();
        FullscreenQuad::draw();

        m_motionReadIndex = writeIndex;
        m_hasMotionHistory = true;
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
        m_motionFramebuffers = {};
        m_motionTextures = {};
        m_framebuffer = 0;
        m_outputTexture = 0;
        m_motionProgram = 0;
        m_program = 0;
        m_presentProgram = 0;
        m_motionPreviousUniform = -1;
        m_motionCurrentUniform = -1;
        m_motionReuseUniform = -1;
        m_motionInvResolutionUniform = -1;
        m_motionGridSizeUniform = -1;
        m_motionHasReuseUniform = -1;
        m_previousUniform = -1;
        m_currentUniform = -1;
        m_motionUniform = -1;
        m_invResolutionUniform = -1;
        m_motionTexelUniform = -1;
        m_presentTextureUniform = -1;
        m_width = 0;
        m_height = 0;
        m_surfaceWidth = 0;
        m_surfaceHeight = 0;
        m_motionWidth = 0;
        m_motionHeight = 0;
        m_motionReadIndex = 0;
        m_hasMotionHistory = false;
        m_quad.abandonContext();
    }

    void FrameInterpolationRenderer::destroyResources() {
        m_quad.reset();
        if (m_motionFramebuffers[0] || m_motionFramebuffers[1]) {
            glDeleteFramebuffers(static_cast<GLsizei>(m_motionFramebuffers.size()), m_motionFramebuffers.data());
        }
        if (m_motionTextures[0] || m_motionTextures[1]) {
            glDeleteTextures(static_cast<GLsizei>(m_motionTextures.size()), m_motionTextures.data());
        }
        if (m_framebuffer) glDeleteFramebuffers(1, &m_framebuffer);
        if (m_outputTexture) glDeleteTextures(1, &m_outputTexture);
        if (m_motionProgram) glDeleteProgram(m_motionProgram);
        if (m_program) glDeleteProgram(m_program);
        if (m_presentProgram) glDeleteProgram(m_presentProgram);
        m_motionFramebuffers = {};
        m_motionTextures = {};
        m_framebuffer = 0;
        m_outputTexture = 0;
        m_motionProgram = 0;
        m_program = 0;
        m_presentProgram = 0;
        m_motionPreviousUniform = -1;
        m_motionCurrentUniform = -1;
        m_motionReuseUniform = -1;
        m_motionInvResolutionUniform = -1;
        m_motionGridSizeUniform = -1;
        m_motionHasReuseUniform = -1;
        m_previousUniform = -1;
        m_currentUniform = -1;
        m_motionUniform = -1;
        m_invResolutionUniform = -1;
        m_motionTexelUniform = -1;
        m_presentTextureUniform = -1;
        m_width = 0;
        m_height = 0;
        m_surfaceWidth = 0;
        m_surfaceHeight = 0;
        m_motionWidth = 0;
        m_motionHeight = 0;
        m_motionReadIndex = 0;
        m_hasMotionHistory = false;
    }

} // namespace bv::render
