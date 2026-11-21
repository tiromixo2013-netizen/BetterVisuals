#include "MwsrRenderer.hpp"

#include "GlStateGuard.hpp"
#include "ShaderProgram.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

using namespace geode::prelude;

namespace {

// MWSR — MoonlightWeb Super Resolution.
// Copyright (c) 2026 Bruno Martin <brunoocto@gmail.com>
// SPDX-License-Identifier: MIT
// Upstream algorithm and parameter law:
// https://github.com/linckosz/moonlight-web/tree/main/mwsr
//
// This GLES 3.00 port keeps the upstream single-frame, edge-adaptive cubic
// reconstruction. The upstream default (luma edge voting) is preserved; the
// optional CHROMA444 path can be enabled for full-RGB game content. Alpha is
// adapted for the game's premultiplied render target.

    constexpr char kVertex[] = R"glsl(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;
out highp vec2 vUv;
void main() {
    vUv = a_texCoord;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

    constexpr char kFragment[] = R"glsl(#version 300 es
precision highp float;
precision highp int;
uniform sampler2D uTex;
uniform vec4 uTexelScale; // texel.xy, scale.xy
uniform vec4 uDimsAcross; // input size, across-tap distance
uniform vec4 uKernel;     // kc, ft, kg, kwc
uniform vec4 uBound;      // kl, zref, padding
in highp vec2 vUv;
layout(location = 0) out vec4 oColor;

const vec3 LUMA = vec3(0.25, 0.625, 0.125);
uniform bool uChroma444;


vec3 S(vec2 uv) {
    return textureLod(uTex, uv, 0.0).rgb;
}

void main() {
    vec2 uv = vUv;
    vec2 texel = uTexelScale.xy;
    vec2 sp = uv * uDimsAcross.xy;
    vec2 f0 = floor(sp - 0.5);
    vec2 t = sp - 0.5 - f0;
    vec2 s = 1.0 - t;
    vec2 ku = uKernel.x * t * s;
    vec2 w12 = 1.0 + ku;
    vec2 p12 = (f0 + 0.5 + (t * t * (3.0 - 2.0 * t) + ku * s) / w12) * texel;
    vec3 rM = S(p12);
    float outAlpha = textureLod(uTex, uv, 0.0).a;

    vec2 gM;
    bool isFlat;
    if (uChroma444) {
        vec3 dx = dFdx(rM) * uTexelScale.z;
        vec3 dy = -dFdy(rM) * uTexelScale.w;
        vec3 m = abs(dx) + abs(dy);
        bool rg = m.g > m.r;
        bool b = m.b > max(m.r, m.g);
        gM = vec2(b ? dx.b : (rg ? dx.g : dx.r), b ? dy.b : (rg ? dy.g : dy.r));
        isFlat = max(m.r, max(m.g, m.b)) < uKernel.y;
    }
    else {
        float lM = dot(rM, LUMA);
        gM = vec2(dFdx(lM) * uTexelScale.z, -dFdy(lM) * uTexelScale.w);
        isFlat = abs(gM.x) + abs(gM.y) < uKernel.y;
    }

    if (isFlat) {
        oColor = vec4(min(max(rM, vec3(0.0)), vec3(outAlpha)), outAlpha);
        return;
    }

    vec2 p0 = (f0 - 0.5) * texel;
    vec2 p3 = (f0 + 2.5) * texel;
    vec3 rT = S(vec2(p12.x, p0.y));
    vec3 rL = S(vec2(p0.x, p12.y));
    vec3 rR = S(vec2(p3.x, p12.y));
    vec3 rB = S(vec2(p12.x, p3.y));
    vec2 dn = gM * inversesqrt(max(dot(gM, gM), 1e-12)) * uDimsAcross.zw;
    vec3 ra = S(uv - dn);
    vec3 rb = S(uv + dn);

    mediump vec3 hM = rM;
    mediump vec3 hA = ra;
    mediump vec3 hB = rb;
    mediump vec3 hTop = rT;
    mediump vec3 hLeft = rL;
    mediump vec3 hRight = rR;
    mediump vec3 hBottom = rB;
    mediump vec2 hu = ku;
    mediump vec2 hs = s;
    mediump vec2 ht = t;
    mediump vec2 h12 = w12;
    mediump vec3 K = (h12.x * (h12.y * hM - hu.y * (hs.y * hTop + ht.y * hBottom))
        - h12.y * hu.x * (hs.x * hLeft + ht.x * hRight)) / (1.0 - hu.x * hu.y);
    mediump vec3 D = K - 0.5 * (hA + hB);
    mediump vec3 R = K + uKernel.z * D + uKernel.w * (D - vec3(dot(D, LUMA)));
    mediump vec3 mn = min(hM, min(hA, hB));
    mediump vec3 mx = max(hM, max(hA, hB));
    mediump vec3 mg = (mx - mn) * uBound.x;
    vec3 result = clamp(R, max(mn - mg, vec3(0.0)), mx + mg);
    result = min(result, vec3(outAlpha));
    oColor = vec4(result, outAlpha);
}
)glsl";

    bool supportsGles30() {
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
        return major > 3 || (major == 3 && minor >= 0);
    }

    struct KernelParameters {
        float kc;
        float ft;
        float kg;
        float kr;
        float kwc;
        float kl;
    };

    KernelParameters mwsrLaw(GLsizei inputWidth, GLsizei inputHeight,
                             GLsizei outputWidth, GLsizei outputHeight) {
        float const scale = std::min(
            static_cast<float>(outputWidth) / static_cast<float>(inputWidth),
            static_cast<float>(outputHeight) / static_cast<float>(inputHeight)
        );
        float const s = std::clamp(scale, 1.07f, 4.0f);
        float const x = std::log(s);
        auto value = [x](std::array<float, 3> const& c) {
            return c[0] + x * (c[1] + x * c[2]);
        };
        float const force = 1.0f + 0.35f * std::clamp(
            std::log(s / (4.0f / 3.0f)) / std::log(1.5f), 0.0f, 1.0f
        );
        std::array<float, 3> const kcCurve{
            0.6353583202388096f, 0.021845681633595574f, 0.12781268100437676f
        };
        std::array<float, 3> const kgCurve{
            -0.1288413863403059f, 2.5844683855070816f, -1.260092021482419f
        };
        std::array<float, 3> const krCurve{
            1.0071546009267338f, -1.0209066704914191f, 0.5699605831985769f
        };
        std::array<float, 3> const klCurve{
            0.33416994543071277f, -0.2962022126856069f, 0.08628272130287401f
        };
        float const strength = std::clamp((scale - 1.0f) / 0.07f, 0.0f, 1.0f);
        return {
            value(kcCurve),
            0.005f,
            value(kgCurve) * force * strength,
            value(krCurve),
            0.1f * strength,
            value(klCurve) * force
        };
    }

} // namespace

namespace bv::render {

    bool MwsrRenderer::prepare() {
        if (m_attempted) return m_program != 0;
        m_attempted = true;
        if (!supportsGles30()) {
            log::warn("[MWSR] OpenGL ES 3.0 is unavailable in the active game context; using linear fallback");
            return false;
        }

        GlStateGuard state;
        std::array<std::string_view, 1> vertexSources{kVertex};
        std::array<std::string_view, 1> fragmentSources{kFragment};
        m_program = compileShaderProgram("MWSR (MoonlightWeb Super Resolution)", vertexSources, fragmentSources);
        if (m_program == 0 || !m_quad.initialize("MWSR")) {
            if (m_program) glDeleteProgram(m_program);
            m_program = 0;
            m_quad.reset();
            return false;
        }

        m_textureUniform = glGetUniformLocation(m_program, "uTex");
        m_texelScaleUniform = glGetUniformLocation(m_program, "uTexelScale");
        m_dimsAcrossUniform = glGetUniformLocation(m_program, "uDimsAcross");
        m_kernelUniform = glGetUniformLocation(m_program, "uKernel");
        m_boundUniform = glGetUniformLocation(m_program, "uBound");
        m_chroma444Uniform = glGetUniformLocation(m_program, "uChroma444");
        if (m_textureUniform < 0 || m_texelScaleUniform < 0 || m_dimsAcrossUniform < 0 ||
            m_kernelUniform < 0 || m_boundUniform < 0 || m_chroma444Uniform < 0) {
            log::error("[MWSR] Shader is missing required uniforms");
            glDeleteProgram(m_program);
            m_program = 0;
            m_quad.reset();
            return false;
        }
        glUseProgram(m_program);
        glUniform1i(m_textureUniform, 0);
        return true;
    }

    bool MwsrRenderer::apply(
        GLuint inputTexture, GLsizei inputWidth, GLsizei inputHeight,
        GLsizei outputWidth, GLsizei outputHeight, bool chroma444
    ) {
        if (!inputTexture || inputWidth <= 0 || inputHeight <= 0 ||
            outputWidth <= 0 || outputHeight <= 0 ||
            outputWidth < inputWidth || outputHeight < inputHeight || !prepare()) {
            return false;
        }

        GlStateGuard state;
        // The parameter law uses logarithms and depends only on the four
        // dimensions. Recompute only after a resize or render-scale change.
        if (m_cachedInputWidth != inputWidth || m_cachedInputHeight != inputHeight ||
            m_cachedOutputWidth != outputWidth || m_cachedOutputHeight != outputHeight) {
            auto const p = mwsrLaw(inputWidth, inputHeight, outputWidth, outputHeight);
            m_cachedKernel = {p.kc, p.ft, p.kg, p.kr, p.kwc, p.kl};
            m_cachedInputWidth = inputWidth;
            m_cachedInputHeight = inputHeight;
            m_cachedOutputWidth = outputWidth;
            m_cachedOutputHeight = outputHeight;
        }
        auto const& p = m_cachedKernel;
        float const inputW = static_cast<float>(inputWidth);
        float const inputH = static_cast<float>(inputHeight);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(m_program);
        glUniform4f(
            m_texelScaleUniform,
            1.0f / inputW, 1.0f / inputH,
            static_cast<float>(outputWidth) / inputW,
            static_cast<float>(outputHeight) / inputH
        );
        glUniform4f(
            m_dimsAcrossUniform,
            inputW, inputH, p[3] / inputW, p[3] / inputH
        );
        glUniform4f(m_kernelUniform, p[0], p[1], p[2], p[4]);
        glUniform4f(m_boundUniform, p[5], 1.0f, 0.0f, 0.0f);
        glUniform1i(m_chroma444Uniform, chroma444 ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        m_quad.bind();
        FullscreenQuad::draw();
        return true;
    }

    void MwsrRenderer::reset() {
        m_quad.reset();
        if (m_program) glDeleteProgram(m_program);
        m_program = 0;
        m_textureUniform = -1;
        m_texelScaleUniform = -1;
        m_dimsAcrossUniform = -1;
        m_kernelUniform = -1;
        m_boundUniform = -1;
        m_chroma444Uniform = -1;
        m_attempted = false;
        m_cachedKernel = {};
        m_cachedInputWidth = 0;
        m_cachedInputHeight = 0;
        m_cachedOutputWidth = 0;
        m_cachedOutputHeight = 0;
    }

} // namespace bv::render
