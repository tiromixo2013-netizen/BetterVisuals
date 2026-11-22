Shader "UltraScale/AMD_FSR1_EASU"
{
    Properties { _MainTex ("Source", 2D) = "white" {} }
    SubShader
    {
        Cull Off ZWrite Off ZTest Always
        Pass
        {
            CGPROGRAM
            #pragma vertex vert_img
            #pragma fragment frag
            #pragma target 3.0
            #include "UnityCG.cginc"
            /*
 * This shader is derived from AMD FidelityFX Super Resolution 1.0 (EASU).
 * https://github.com/GPUOpen-Effects/FidelityFX-FSR
 *
 * Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
sampler2D _MainTex;
float2 _InvResolution;
float _Sharpness;

#define FSR_EPSILON (1e-7)

float tapLuma(float3 color) {
    return color.b * 0.5 + (color.r * 0.5 + color.g);
}

void accumulateDirection(
    inout float2 dir,
    inout float len,
    float weight,
    float lA,
    float lB,
    float lC,
    float lD,
    float lE
) {
    float dc = lD - lC;
    float cb = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    lenX = 1.0 / max(lenX, FSR_EPSILON);
    float dirX = lD - lB;
    dir.x += dirX * weight;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    lenX *= lenX;
    len += lenX * weight;

    float ec = lE - lC;
    float ca = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY = 1.0 / max(lenY, FSR_EPSILON);
    float dirY = lE - lA;
    dir.y += dirY * weight;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    lenY *= lenY;
    len += lenY * weight;
}

void accumulateTap(
    inout float3 color,
    inout float weight,
    float2 offset,
    float2 dir,
    float2 len2,
    float lob,
    float clip,
    float3 tapColor
) {
    float2 rotated;
    rotated.x = offset.x * dir.x + offset.y * dir.y;
    rotated.y = offset.x * (-dir.y) + offset.y * dir.x;
    rotated *= len2;
    float d2 = rotated.x * rotated.x + rotated.y * rotated.y;
    d2 = min(d2, clip);
    float wB = 0.4 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 1.5625 * wB - 0.5625;
    float w = wB * wA;
    color += tapColor * w;
    weight += w;
}

fixed4 frag(v2f_img input) {
    float2 inputSize = 1.0 / _InvResolution;

    // Output pixel centers {0 to output-1} map to input texel space with a
    // half-texel shift, matching FsrEasuCon() constants.
    float2 pp = input.uv * inputSize - 0.5;
    float2 fp = floor(pp);
    pp -= fp;

    // Tap coordinates are texel-space offsets from the center, built with
    // per-axis steps to avoid repeated multiplies.
    float2 base = (fp + float2(0.5)) * _InvResolution;
    float2 stepX = float2(_InvResolution.x, 0.0);
    float2 stepY = float2(0.0, _InvResolution.y);

    float3 b = tex2D(_MainTex, base - stepY).rgb;
    float3 c = tex2D(_MainTex, base + stepX - stepY).rgb;
    float3 i = tex2D(_MainTex, base - stepX + stepY).rgb;
    float3 j = tex2D(_MainTex, base + stepY).rgb;
    float4 f = tex2D(_MainTex, base);
    float3 e = tex2D(_MainTex, base - stepX).rgb;
    float3 k = tex2D(_MainTex, base + stepX + stepY).rgb;
    float3 l = tex2D(_MainTex, base + 2.0 * stepX + stepY).rgb;
    float3 h = tex2D(_MainTex, base + 2.0 * stepX).rgb;
    float3 g = tex2D(_MainTex, base + stepX).rgb;
    float3 o = tex2D(_MainTex, base + stepX + 2.0 * stepY).rgb;
    float3 n = tex2D(_MainTex, base + 2.0 * stepY).rgb;

    float bL = tapLuma(b);
    float cL = tapLuma(c);
    float iL = tapLuma(i);
    float jL = tapLuma(j);
    float fL = tapLuma(f.rgb);
    float eL = tapLuma(e);
    float kL = tapLuma(k);
    float lL = tapLuma(l);
    float hL = tapLuma(h);
    float gL = tapLuma(g);
    float oL = tapLuma(o);
    float nL = tapLuma(n);

    float2 dir = float2(0.0);
    float len = 0.0;
    accumulateDirection(
        dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL
    );
    accumulateDirection(
        dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL
    );
    accumulateDirection(
        dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL
    );
    accumulateDirection(
        dir, len, pp.x * pp.y, gL, jL, kL, lL, oL
    );

    float2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zero = dirR < (1.0 / 32768.0);
    dirR = zero ? 1.0 : inversesqrt(max(dirR, 1e-10));
    dir.x = zero ? 1.0 : dir.x;
    dir *= float2(dirR);

    len = len * 0.5;
    len *= len;
    float stretch =
        (dir.x * dir.x + dir.y * dir.y) /
        max(max(abs(dir.x), abs(dir.y)), FSR_EPSILON);
    float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 - 0.29 * len;
    float clip = 1.0 / max(lob, FSR_EPSILON);

    float3 min4 = min(
        f.rgb,
        min(b, min(c, min(i, min(j, min(e, min(g, min(h, min(k, min(l, min(n, o))))))))))
    );
    float3 max4 = max(
        f.rgb,
        max(b, max(c, max(i, max(j, max(e, max(g, max(h, max(k, max(l, max(n, o))))))))))
    );

    float3 accumulation = float3(0.0);
    float weight = 0.0;
    accumulateTap(accumulation, weight, float2(0.0, -1.0), dir, len2, lob, clip, b);
    accumulateTap(accumulation, weight, float2(1.0, -1.0), dir, len2, lob, clip, c);
    accumulateTap(accumulation, weight, float2(-1.0, 1.0), dir, len2, lob, clip, i);
    accumulateTap(accumulation, weight, float2(0.0, 1.0), dir, len2, lob, clip, j);
    accumulateTap(accumulation, weight, float2(0.0, 0.0), dir, len2, lob, clip, f.rgb);
    accumulateTap(accumulation, weight, float2(-1.0, 0.0), dir, len2, lob, clip, e);
    accumulateTap(accumulation, weight, float2(1.0, 1.0), dir, len2, lob, clip, k);
    accumulateTap(accumulation, weight, float2(2.0, 1.0), dir, len2, lob, clip, l);
    accumulateTap(accumulation, weight, float2(2.0, 0.0), dir, len2, lob, clip, h);
    accumulateTap(accumulation, weight, float2(1.0, 0.0), dir, len2, lob, clip, g);
    accumulateTap(accumulation, weight, float2(1.0, 2.0), dir, len2, lob, clip, o);
    accumulateTap(accumulation, weight, float2(0.0, 2.0), dir, len2, lob, clip, n);

    float3 bilinear = lerp(lerp(f.rgb, g, pp.x), lerp(j, k, pp.x), pp.y);

    float3 pixel = lerp(
        bilinear,
        min(max4, max(min4, accumulation * (1.0 / weight))),
        clamp(_Sharpness, 0.0, 1.0)
    );
    return fixed4(pixel, f.a);
}

            ENDCG
        }
    }
    Fallback Off
}
