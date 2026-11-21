#version 300 es
precision highp float;
in vec2 vUv;
uniform sampler2D uScene;
uniform vec2 uTexel;
out vec4 color;

// UltraScale FSR Lite: custom six-tap, EASU-inspired spatial reconstruction.
// This is not AMD's official FidelityFX FSR shader or a drop-in FSR implementation.
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
void main() {
    vec3 c  = texture(uScene, vUv).rgb;
    vec3 l  = texture(uScene, vUv - vec2(uTexel.x, 0.0)).rgb;
    vec3 r  = texture(uScene, vUv + vec2(uTexel.x, 0.0)).rgb;
    vec3 u  = texture(uScene, vUv + vec2(0.0, uTexel.y)).rgb;
    vec3 d  = texture(uScene, vUv - vec2(0.0, uTexel.y)).rgb;
    vec3 q  = texture(uScene, vUv + vec2(uTexel.x, uTexel.y)).rgb;

    float gx = abs(luma(r) - luma(l));
    float gy = abs(luma(u) - luma(d));
    float edge = clamp(max(gx, gy) * 3.0, 0.0, 1.0);
    vec3 alongEdge = gx > gy ? (u + d) * 0.5 : (l + r) * 0.5;
    vec3 diagonal = (c * 3.0 + l + r + u + d + q) / 8.0;
    vec3 reconstructed = mix(diagonal, alongEdge, edge * 0.35);
    float localMin = min(min(luma(l), luma(r)), min(luma(u), luma(d)));
    float localMax = max(max(luma(l), luma(r)), max(luma(u), luma(d)));
    float detail = clamp(luma(c) - luma(diagonal), -0.08, 0.08);
    reconstructed += vec3(detail * 0.35);
    float outLuma = clamp(luma(reconstructed), localMin - 0.02, localMax + 0.02);
    reconstructed *= outLuma / max(luma(reconstructed), 0.0001);
    color = vec4(clamp(reconstructed, 0.0, 1.0), 1.0);
}