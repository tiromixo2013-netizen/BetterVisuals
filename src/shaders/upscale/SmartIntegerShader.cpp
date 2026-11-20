#include "../../render/PostProcessRenderer.hpp"
#include "../PostProcessShaders.hpp"

namespace bv::shaders::smart_integer {
constexpr char kFragmentSource[] = R"glsl(
#ifdef GL_ES
precision mediump float;
#endif
uniform sampler2D u_texture;
uniform vec2 u_invResolution;
varying vec2 v_texCoord;

void main() {
    // One hardware-filtered texture lookup. Clamp to texel centers so edge
    // pixels never depend on the texture object's wrap mode.
    vec2 halfTexel = 0.5 * u_invResolution;
    vec2 uv = clamp(v_texCoord, halfTexel, vec2(1.0) - halfTexel);
    gl_FragColor = texture2D(u_texture, uv);
}
)glsl";
}
namespace bv::shaders {
render::PostProcessShader const kSmartIntegerShader{
    "UltraScale2D (1-Sample Hardware Linear)",
    kFullscreenVertexSource,
    smart_integer::kFragmentSource,
    nullptr,
};
}
