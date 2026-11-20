#include "../../render/PostProcessRenderer.hpp"
#include "../PostProcessShaders.hpp"

namespace bv::shaders::smart_integer {
constexpr char kFragmentSource[] = R"glsl(
#ifdef GL_ES
precision mediump float;
#endif
uniform sampler2D u_texture;
uniform vec2 u_invResolution;
uniform float u_blend;
varying vec2 v_texCoord;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
    vec2 pixel = v_texCoord / u_invResolution - vec2(0.5);
    vec2 base = floor(pixel);
    vec2 f = fract(pixel);
    vec2 uv00 = (base + vec2(0.5)) * u_invResolution;
    vec2 dx = vec2(u_invResolution.x, 0.0);
    vec2 dy = vec2(0.0, u_invResolution.y);
    vec4 c00 = texture2D(u_texture, uv00);
    vec4 c10 = texture2D(u_texture, uv00 + dx);
    vec4 c01 = texture2D(u_texture, uv00 + dy);
    vec4 c11 = texture2D(u_texture, uv00 + dx + dy);

    vec4 bilinear = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
    float dMain = abs(luma(c00.rgb) - luma(c11.rgb));
    float dCross = abs(luma(c10.rgb) - luma(c01.rgb));
    float edge = abs(dMain - dCross);
    float strength = smoothstep(0.018, 0.16, max(dMain, dCross));
    float confidence = smoothstep(0.008, 0.075, edge) * strength;

    float wMain = max((1.0-f.x)*(1.0-f.y) + f.x*f.y, 0.0001);
    float wCross = max(f.x*(1.0-f.y) + (1.0-f.x)*f.y, 0.0001);
    vec4 mainDiag = (c00*((1.0-f.x)*(1.0-f.y)) + c11*(f.x*f.y)) / wMain;
    vec4 crossDiag = (c10*(f.x*(1.0-f.y)) + c01*((1.0-f.x)*f.y)) / wCross;
    bool useMain = dMain <= dCross;
    vec4 directed = useMain ? mainDiag : crossDiag;
    float support = useMain ? smoothstep(0.02, 0.25, wMain) :
                              smoothstep(0.02, 0.25, wCross);
    vec4 result = mix(bilinear, directed, confidence * support * clamp(u_blend, 0.0, 1.0));
    result.a = bilinear.a;
    gl_FragColor = result;
}
)glsl";
}
namespace bv::shaders {
render::PostProcessShader const kSmartIntegerShader{
    "Smart Integer (4-Sample Diagonal Reconstruction)",
    kFullscreenVertexSource,
    smart_integer::kFragmentSource,
    "u_blend",
};
}
