Shader "UltraScale/AMD_CAS"
{
    Properties
    {
        _MainTex ("Source", 2D) = "white" {}
        _Sharpness ("CAS sharpness", Range(0, 1)) = 0.35
    }
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

            // AMD FidelityFX Contrast Adaptive Sharpening (CAS), sharpen-only path.
            // Fragment adaptation of the official CasFilter(..., noScaling=true) math.
            // Copyright (c) 2017-2020 Advanced Micro Devices, Inc.
            // SPDX-License-Identifier: MIT
            sampler2D _MainTex;
            float4 _MainTex_TexelSize;
            float _Sharpness;

            float3 loadCAS(float2 uv, float2 offset)
            {
                return tex2D(_MainTex, uv + offset * _MainTex_TexelSize.xy).rgb;
            }

            fixed4 frag(v2f_img input) : SV_Target
            {
                float2 uv = input.uv;
                float3 a = loadCAS(uv, float2(-1.0, -1.0));
                float3 b = loadCAS(uv, float2( 0.0, -1.0));
                float3 c = loadCAS(uv, float2( 1.0, -1.0));
                float3 d = loadCAS(uv, float2(-1.0,  0.0));
                float3 e = loadCAS(uv, float2( 0.0,  0.0));
                float3 f = loadCAS(uv, float2( 1.0,  0.0));
                float3 g = loadCAS(uv, float2(-1.0,  1.0));
                float3 h = loadCAS(uv, float2( 0.0,  1.0));
                float3 i = loadCAS(uv, float2( 1.0,  1.0));

                float3 mn = min(min(d, e), min(f, min(b, h)));
                float3 mx = max(max(d, e), max(f, max(b, h)));
                float ampG = clamp(min(mn.g, 1.0 - mx.g) / max(mx.g, 1e-4), 0.0, 1.0);
                ampG = sqrt(ampG);
                float peak = -1.0 / lerp(8.0, 5.0, saturate(_Sharpness));
                float w = ampG * peak;
                float rcpWeight = 1.0 / (1.0 + 4.0 * w);
                float3 result = saturate((b * w + d * w + f * w + h * w + e) * rcpWeight);
                return fixed4(result, tex2D(_MainTex, uv).a);
            }
            ENDCG
        }
    }
    Fallback Off
}
