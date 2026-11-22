Shader "UltraScale/Spatial7Tap"
{
    Properties
    {
        _MainTex ("Source", 2D) = "white" {}
        _Sharpness ("Experimental edge sharpness", Range(0, 1)) = 0.2
    }

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
            float _Sharpness;

            fixed4 frag(v2f_img input) : SV_Target
            {
                float2 uv = input.uv;
                float2 t = _MainTex_TexelSize.xy;
                float4 c = tex2D(_MainTex, uv);
                float4 a = tex2D(_MainTex, uv + float2(-t.x, 0.0));
                float4 b = tex2D(_MainTex, uv + float2( t.x, 0.0));
                float4 d = tex2D(_MainTex, uv + float2(0.0, -t.y));
                float4 e = tex2D(_MainTex, uv + float2(0.0,  t.y));
                float4 f = tex2D(_MainTex, uv + float2(-0.5 * t.x, -0.5 * t.y));
                float4 g = tex2D(_MainTex, uv + float2( 0.5 * t.x,  0.5 * t.y));

                // Seven taps: centre plus three opposing pairs.
                // Original spatial reconstruction; not AMD FidelityFX or Qualcomm SGSR.
                float4 neighbours = (a + b + d + e + f + g) * (1.0 / 6.0);
                float4 result = c * 0.55 + neighbours * 0.45;
                result += (c - neighbours) * saturate(_Sharpness) * 0.65;
                return fixed4(saturate(result.rgb), c.a);
            }
            ENDCG
        }
    }
    Fallback Off
}
