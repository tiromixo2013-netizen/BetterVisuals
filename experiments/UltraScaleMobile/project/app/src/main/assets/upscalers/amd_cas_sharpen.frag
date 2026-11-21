#version 300 es
precision highp float;
precision highp int;

in vec2 vUv;
uniform sampler2D uScene;
uniform vec2 uInputSize;
uniform float uSharpness;
out vec4 oColor;

// AMD FidelityFX Contrast Adaptive Sharpening (CAS), sharpen-only path.
// Fragment adaptation of the official CasFilter(..., noScaling=true) math.
// Copyright (c) 2017-2020 Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT
//
// The official CAS path uses a minimal 3x3 neighbourhood. This GLES fragment
// port uses texelFetch for deterministic integer taps and keeps the default
// shared-green-coefficient filter (CAS_SLOW is intentionally not enabled).
vec3 loadCAS(ivec2 p) {
    ivec2 size = ivec2(uInputSize);
    return texelFetch(uScene, clamp(p, ivec2(0), size - ivec2(1)), 0).rgb;
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 a = loadCAS(p + ivec2(-1, -1));
    vec3 b = loadCAS(p + ivec2( 0, -1));
    vec3 c = loadCAS(p + ivec2( 1, -1));
    vec3 d = loadCAS(p + ivec2(-1,  0));
    vec3 e = loadCAS(p);
    vec3 f = loadCAS(p + ivec2( 1,  0));
    vec3 g = loadCAS(p + ivec2(-1,  1));
    vec3 h = loadCAS(p + ivec2( 0,  1));
    vec3 i = loadCAS(p + ivec2( 1,  1));

    // CAS's default soft range uses the cross (four neighbours + centre).
    vec3 mn = min(min(d, e), min(f, min(b, h)));
    vec3 mx = max(max(d, e), max(f, max(b, h)));

    // The default CAS path shares the green channel's adaptive coefficient
    // across RGB, matching the official non-CAS_SLOW branch.
    float ampG = clamp(min(mn.g, 1.0 - mx.g) / max(mx.g, 1e-4), 0.0, 1.0);
    ampG = sqrt(ampG);
    float peak = -1.0 / mix(8.0, 5.0, clamp(uSharpness, 0.0, 1.0));
    float w = ampG * peak;
    float rcpWeight = 1.0 / (1.0 + 4.0 * w);
    vec3 result = clamp((b * w + d * w + f * w + h * w + e) * rcpWeight, 0.0, 1.0);
    oColor = vec4(result, 1.0);
}
