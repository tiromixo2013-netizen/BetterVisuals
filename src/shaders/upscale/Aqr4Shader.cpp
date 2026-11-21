#include "../../render/PostProcessRenderer.hpp"
#include "../PostProcessShaders.hpp"

namespace bv::shaders::aqr4 {
constexpr char kFragmentSource[] = R"glsl(
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif
uniform sampler2D u_texture;
uniform vec2 u_invResolution;
uniform float u_edgeStrength;
varying vec2 v_texCoord;

float edgeDistance(vec4 a, vec4 b) {
    vec3 d = abs(a.rgb - b.rgb);
    float lumaDistance = dot(d, vec3(0.299, 0.587, 0.114));
    float chromaDistance = max(d.r, max(d.g, d.b));
    return max(lumaDistance, 0.5 * chromaDistance) + 0.25 * abs(a.a - b.a);
}

void main() {
    vec2 sourcePosition = v_texCoord / u_invResolution - vec2(0.5);
    vec2 basePixel = floor(sourcePosition);
    vec2 phase = fract(sourcePosition);
    vec2 halfTexel = 0.5 * u_invResolution;
    vec2 minUv = halfTexel;
    vec2 maxUv = vec2(1.0) - halfTexel;
    vec2 baseUv = (basePixel + vec2(0.5)) * u_invResolution;
    vec2 stepX = vec2(u_invResolution.x, 0.0);
    vec2 stepY = vec2(0.0, u_invResolution.y);
    vec4 c00 = texture2D(u_texture, clamp(baseUv, minUv, maxUv));
    vec4 c10 = texture2D(u_texture, clamp(baseUv + stepX, minUv, maxUv));
    vec4 c01 = texture2D(u_texture, clamp(baseUv + stepY, minUv, maxUv));
    vec4 c11 = texture2D(u_texture, clamp(baseUv + stepX + stepY, minUv, maxUv));

    float gx = 0.5 * (edgeDistance(c00, c10) + edgeDistance(c01, c11));
    float gy = 0.5 * (edgeDistance(c00, c01) + edgeDistance(c10, c11));
    float contrast = smoothstep(0.04, 0.14, max(gx, gy));
    float totalGradient = gx + gy + 0.0001;
    float strength = clamp(u_edgeStrength, 0.0, 1.0);
    float xConfidence = contrast * max(gx - gy, 0.0) / totalGradient;
    float yConfidence = contrast * max(gy - gx, 0.0) / totalGradient;
    float phaseX = mix(phase.x, smoothstep(0.0, 1.0, phase.x),
                       clamp(0.85 * strength * xConfidence, 0.0, 0.85));
    float phaseY = mix(phase.y, smoothstep(0.0, 1.0, phase.y),
                       clamp(0.85 * strength * yConfidence, 0.0, 0.85));

    vec4 topRow = mix(c00, c10, phaseX);
    vec4 bottomRow = mix(c01, c11, phaseX);
    vec4 axisReconstruction = mix(topRow, bottomRow, phaseY);
    float mainDiagonal = edgeDistance(c00, c11);
    float antiDiagonal = edgeDistance(c10, c01);
    float diagonalDominance = abs(mainDiagonal - antiDiagonal) /
                              (mainDiagonal + antiDiagonal + 0.0001);
    float saddleResidual = edgeDistance(c00 + c11, c10 + c01);
    float residualConfidence = smoothstep(0.05, 0.22, saddleResidual);
    float dominanceConfidence = smoothstep(0.20, 0.70, diagonalDominance);
    float diagonalConfidence = clamp(strength * contrast * residualConfidence *
                                     dominanceConfidence, 0.0, 0.85);

    vec4 mainUpper = (1.0 - phase.x) * c00 +
                     (phase.x - phase.y) * c10 + phase.y * c11;
    vec4 mainLower = (1.0 - phase.y) * c00 +
                     (phase.y - phase.x) * c01 + phase.x * c11;
    vec4 mainTri = mix(mainLower, mainUpper, step(phase.y, phase.x));
    float phaseSum = phase.x + phase.y;
    vec4 antiUpper = (1.0 - phaseSum) * c00 + phase.x * c10 + phase.y * c01;
    vec4 antiLower = (1.0 - phase.y) * c10 +
                     (1.0 - phase.x) * c01 + (phaseSum - 1.0) * c11;
    vec4 antiTri = mix(antiLower, antiUpper, step(phaseSum, 1.0));
    vec4 diagonalReconstruction = mix(antiTri, mainTri,
                                      step(mainDiagonal, antiDiagonal));
    gl_FragColor = mix(axisReconstruction, diagonalReconstruction,
                       diagonalConfidence);
}
)glsl";
}
namespace bv::shaders {
render::PostProcessShader const kAqr4Shader{
    "AQR-4 (4-Tap Adaptive Quad Reconstruction)",
    kFullscreenVertexSource,
    aqr4::kFragmentSource,
    "u_edgeStrength",
};
}
