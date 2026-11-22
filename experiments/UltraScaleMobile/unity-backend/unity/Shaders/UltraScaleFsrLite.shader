Shader "UltraScale/FSRLiteCustom"
{
    Properties { _MainTex ("Source", 2D) = "white" {} }

    SubShader
    {
        Cull Off
        ZWrite Off
        ZTest Always

        Pass
        {
            CGPROGRAM
            #pragma vertex vert_img
            #pragma fragment frag
            #pragma target 3.0
            #include "UnityCG.cginc"

            sampler2D _MainTex;
            float4 _MainTex_TexelSize;

            float Luma(float3 c)
            {
                return dot(c, float3(0.2126, 0.7152, 0.0722));
            }

            float4 frag(v2f_img input) : SV_Target
            {
                float2 uv = input.uv;
                float2 t = _MainTex_TexelSize.xy;

                float3 c = tex2D(_MainTex, uv).rgb;
                float3 l = tex2D(_MainTex, uv - float2(t.x, 0.0)).rgb;
                float3 r = tex2D(_MainTex, uv + float2(t.x, 0.0)).rgb;
                float3 u = tex2D(_MainTex, uv + float2(0.0, t.y)).rgb;
                float3 d = tex2D(_MainTex, uv - float2(0.0, t.y)).rgb;
                float3 q = tex2D(_MainTex, uv + t).rgb;

                float gx = abs(Luma(r) - Luma(l));
                float gy = abs(Luma(u) - Luma(d));
                float edge = saturate(max(gx, gy) * 3.0);
                float3 alongEdge = gx > gy ? (u + d) * 0.5 : (l + r) * 0.5;
                float3 diagonal = (c * 3.0 + l + r + u + d + q) * 0.125;
                float3 reconstructed = lerp(diagonal, alongEdge, edge * 0.35);

                float localMin = min(min(Luma(l), Luma(r)), min(Luma(u), Luma(d)));
                float localMax = max(max(Luma(l), Luma(r)), max(Luma(u), Luma(d)));
                float detail = clamp(Luma(c) - Luma(diagonal), -0.08, 0.08);
                reconstructed += detail * 0.35;
                float outLuma = clamp(Luma(reconstructed), localMin - 0.02, localMax + 0.02);
                reconstructed *= outLuma / max(Luma(reconstructed), 0.0001);

                return float4(saturate(reconstructed), 1.0);
            }
            ENDCG
        }
    }
    Fallback Off
}
