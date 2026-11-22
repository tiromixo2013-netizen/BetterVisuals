Shader "UltraScale/Spatial6Tap"
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
                float2 texel = _MainTex_TexelSize.xy;

                // Six symmetric-direction taps: centre, horizontal pair,
                // and a vertically offset pair plus the opposite diagonal.
                // This is an original lightweight spatial filter, not FSR/EASU.
                float4 centre = tex2D(_MainTex, uv);
                float4 left = tex2D(_MainTex, uv + float2(-texel.x, 0.0));
                float4 right = tex2D(_MainTex, uv + float2(texel.x, 0.0));
                float4 upLeft = tex2D(_MainTex, uv + float2(-0.5 * texel.x, texel.y));
                float4 upRight = tex2D(_MainTex, uv + float2(0.5 * texel.x, texel.y));
                float4 down = tex2D(_MainTex, uv + float2(0.0, -texel.y));

                float4 reconstruction =
                    centre * 0.40 +
                    (left + right) * 0.16 +
                    (upLeft + upRight) * 0.10 +
                    down * 0.08;

                float4 localAverage =
                    (left + right + upLeft + upRight + down) * 0.2;
                float4 sharpened = reconstruction +
                    (centre - localAverage) * saturate(_Sharpness) * 0.65;

                return fixed4(saturate(sharpened.rgb), centre.a);
            }
            ENDCG
        }
    }
    Fallback Off
}
