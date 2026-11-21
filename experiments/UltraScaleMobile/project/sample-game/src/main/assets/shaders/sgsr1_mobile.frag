#version 310 es

// Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
precision mediump float;
precision highp int;

#define OperationMode 1
#define EdgeThreshold (8.0 / 255.0)
#define EdgeSharpness 2.0

uniform highp vec4 ViewportInfo[1];
uniform mediump sampler2D ps0;
layout(location=0) in highp vec4 in_TEXCOORD0;
layout(location=0) out vec4 out_Target0;

float fastLanczos2(float x) {
    float wA = x - 4.0;
    float wB = x * wA - wA;
    wA *= wA;
    return wB * wA;
}
vec2 weightY(float dx, float dy, float c, float std) {
    float x = ((dx * dx) + (dy * dy)) * 0.55 + clamp(abs(c) * std, 0.0, 1.0);
    float w = fastLanczos2(x);
    return vec2(w, w * c);
}
void main() {
    const int mode = OperationMode;
    vec4 color = textureLod(ps0, in_TEXCOORD0.xy, 0.0);
    highp vec2 imgCoord = (in_TEXCOORD0.xy * ViewportInfo[0].zw) + vec2(-0.5, 0.5);
    highp vec2 imgCoordPixel = floor(imgCoord);
    highp vec2 coord = imgCoordPixel * ViewportInfo[0].xy;
    vec2 pl = imgCoord - imgCoordPixel;
    vec4 left = textureGather(ps0, coord, mode);
    float edgeVote = abs(left.z - left.y) + abs(color[mode] - left.y) + abs(color[mode] - left.z);
    if (edgeVote > EdgeThreshold) {
        coord.x += ViewportInfo[0].x;
        vec4 right = textureGather(ps0, coord + vec2(ViewportInfo[0].x, 0.0), mode);
        vec4 upDown;
        upDown.xy = textureGather(ps0, coord + vec2(0.0, -ViewportInfo[0].y), mode).wz;
        upDown.zw = textureGather(ps0, coord + vec2(0.0, ViewportInfo[0].y), mode).yx;
        float mean = (left.y + left.z + right.x + right.w) * 0.25;
        left -= vec4(mean);
        right -= vec4(mean);
        upDown -= vec4(mean);
        color.w = color[mode] - mean;
        float sum = abs(left.x)+abs(left.y)+abs(left.z)+abs(left.w)
                  + abs(right.x)+abs(right.y)+abs(right.z)+abs(right.w)
                  + abs(upDown.x)+abs(upDown.y)+abs(upDown.z)+abs(upDown.w);
        float std = 2.181818 / max(sum, 0.00001);
        vec2 aWY = weightY(pl.x, pl.y+1.0, upDown.x, std);
        aWY += weightY(pl.x-1.0, pl.y+1.0, upDown.y, std);
        aWY += weightY(pl.x-1.0, pl.y-2.0, upDown.z, std);
        aWY += weightY(pl.x, pl.y-2.0, upDown.w, std);
        aWY += weightY(pl.x+1.0, pl.y-1.0, left.x, std);
        aWY += weightY(pl.x, pl.y-1.0, left.y, std);
        aWY += weightY(pl.x, pl.y, left.z, std);
        aWY += weightY(pl.x+1.0, pl.y, left.w, std);
        aWY += weightY(pl.x-1.0, pl.y-1.0, right.x, std);
        aWY += weightY(pl.x-2.0, pl.y-1.0, right.y, std);
        aWY += weightY(pl.x-2.0, pl.y, right.z, std);
        aWY += weightY(pl.x-1.0, pl.y, right.w, std);
        float finalY = aWY.y / max(aWY.x, 0.00001);
        float maxY = max(max(left.y, left.z), max(right.x, right.w));
        float minY = min(min(left.y, left.z), min(right.x, right.w));
        finalY = clamp(EdgeSharpness * finalY, minY, maxY);
        float deltaY = clamp(finalY - color.w, -23.0/255.0, 23.0/255.0);
        color.rgb = clamp(color.rgb + vec3(deltaY), 0.0, 1.0);
    }
    color.a = 1.0;
    out_Target0 = color;
}