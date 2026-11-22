Shader "UltraScale/Qualcomm_SGSR1"
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
            #pragma target 4.5
            #include "UnityCG.cginc"

            /*
             * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
             *
             * Redistribution and use in source and binary forms, with or without
             * modification, are permitted provided that the following conditions are met:
             * 1. Redistributions of source code must retain the above copyright notice,
             *    this list of conditions and the following disclaimer.
             * 2. Redistributions in binary form must reproduce the above copyright notice,
             *    this list of conditions and the following disclaimer in the documentation
             *    and/or other materials provided with the distribution.
             * 3. Neither the name of the copyright holder nor the names of its contributors
             *    may be used to endorse or promote products derived from this software
             *    without specific prior written permission.
             *
             * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
             * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
             * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
             * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
             * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
             * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
             * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
             * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
             * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
             * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
             * THE POSSIBILITY OF SUCH DAMAGE.
             */
            // Qualcomm SGSR1 mobile port. Texture-gather support is required.
            Texture2D<float4> _MainTex;
            SamplerState sampler_MainTex;
            float4 _ViewportInfo; // xy = inverse input size; zw = input size

            #define EdgeThreshold (8.0 / 255.0)
            #define EdgeSharpness 2.0

            float fastLanczos2(float x)
            {
                float wA = x - 4.0;
                float wB = x * wA - wA;
                wA *= wA;
                return wB * wA;
            }

            float2 weightY(float dx, float dy, float c, float std)
            {
                float x = ((dx * dx) + (dy * dy)) * 0.55 + clamp(abs(c) * std, 0.0, 1.0);
                float w = fastLanczos2(x);
                return float2(w, w * c);
            }

            fixed4 frag(v2f_img input) : SV_Target
            {
                float4 color = _MainTex.SampleLevel(sampler_MainTex, input.uv, 0.0);
                float2 imgCoord = (input.uv * _ViewportInfo.zw) + float2(-0.5, 0.5);
                float2 imgCoordPixel = floor(imgCoord);
                float2 coord = imgCoordPixel * _ViewportInfo.xy;
                float2 pl = imgCoord - imgCoordPixel;

                float4 left = _MainTex.GatherGreen(sampler_MainTex, coord);
                float edgeVote = abs(left.z - left.y) +
                    abs(color.g - left.y) + abs(color.g - left.z);
                if (edgeVote > EdgeThreshold)
                {
                    coord.x += _ViewportInfo.x;
                    float4 right = _MainTex.GatherGreen(
                        sampler_MainTex, coord + float2(_ViewportInfo.x, 0.0));
                    float4 upDown;
                    upDown.xy = _MainTex.GatherGreen(
                        sampler_MainTex, coord + float2(0.0, -_ViewportInfo.y)).wz;
                    upDown.zw = _MainTex.GatherGreen(
                        sampler_MainTex, coord + float2(0.0, _ViewportInfo.y)).yx;

                    float mean = (left.y + left.z + right.x + right.w) * 0.25;
                    left -= mean;
                    right -= mean;
                    upDown -= mean;
                    float centerY = color.g - mean;
                    float sum = abs(left.x)+abs(left.y)+abs(left.z)+abs(left.w)
                              + abs(right.x)+abs(right.y)+abs(right.z)+abs(right.w)
                              + abs(upDown.x)+abs(upDown.y)+abs(upDown.z)+abs(upDown.w);
                    float std = 2.181818 / max(sum, 0.00001);

                    float2 aWY = weightY(pl.x, pl.y+1.0, upDown.x, std);
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
                    float deltaY = clamp(finalY - centerY, -23.0/255.0, 23.0/255.0);
                    color.rgb = saturate(color.rgb + deltaY.xxx);
                }
                return fixed4(color.rgb, 1.0);
            }
            ENDCG
        }
    }
    Fallback Off
}
