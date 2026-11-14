#include "../../render/PostProcessRenderer.hpp"
#include "../PostProcessShaders.hpp"

namespace bv::shaders::fsr_lite {

    // Six texture reads: center, four cardinal neighbors, and one hardware-
    // filtered local fallback. This is a lightweight edge-guided scaler, not
    // AMD's EASU implementation and not a claim of FSR 1 compatibility.
    constexpr char kFragmentSource[] = R"glsl(
#ifdef GL_ES
precision mediump float;
#endif
uniform sampler2D u_texture;
uniform vec2 u_invResolution;
uniform float u_sharpness;
varying vec2 v_texCoord;

void main() {
    vec2 dx = vec2(u_invResolution.x, 0.0);
    vec2 dy = vec2(0.0, u_invResolution.y);

    vec4 center = texture2D(u_texture, v_texCoord);
    vec3 left = texture2D(u_texture, v_texCoord - dx).rgb;
    vec3 right = texture2D(u_texture, v_texCoord + dx).rgb;
    vec3 up = texture2D(u_texture, v_texCoord - dy).rgb;
    vec3 down = texture2D(u_texture, v_texCoord + dy).rgb;
    vec4 bilinearFallback = texture2D(u_texture, v_texCoord);

    float gx = abs(dot(right - left, vec3(0.299, 0.587, 0.114)));
    float gy = abs(dot(down - up, vec3(0.299, 0.587, 0.114)));
    float edgeEnergy = max(gx, gy);
    float orientationConfidence = abs(gx - gy) / max(gx + gy, 0.0001);

    // Follow the locally smoother axis instead of averaging across a strong
    // edge. Clamp the result to the observed neighborhood to limit ringing.
    vec3 directional = gx > gy ? 0.5 * (up + down) : 0.5 * (left + right);
    vec3 neighborhoodMin = min(center.rgb, min(min(left, right), min(up, down)));
    vec3 neighborhoodMax = max(center.rgb, max(max(left, right), max(up, down)));
    directional = clamp(mix(center.rgb, directional, 0.35), neighborhoodMin, neighborhoodMax);

    float edgeConfidence = smoothstep(0.012, 0.09, edgeEnergy) *
        smoothstep(0.08, 0.42, orientationConfidence);
    float reconstructionWeight = edgeConfidence * clamp(u_sharpness, 0.0, 1.0);
    vec3 result = mix(bilinearFallback.rgb, directional, reconstructionWeight);

    // Preserve premultiplied alpha exactly; only RGB is reconstructed.
    gl_FragColor = vec4(result, center.a);
}
)glsl";

} // namespace bv::shaders::fsr_lite

namespace bv::shaders {

    render::PostProcessShader const kFsrLiteShader{
        "FSR Lite (6-Tap Edge Guided)",
        kFullscreenVertexSource,
        fsr_lite::kFragmentSource,
        "u_sharpness",
    };

} // namespace bv::shaders
