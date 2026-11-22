// URP 14 compatibility-mode renderer feature for an owned Unity project.
// Install in a URP Renderer Data asset. This is not a third-party game injector.
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

namespace UltraScale.Unity.URP
{
    public sealed class UltraScaleRendererFeature : ScriptableRendererFeature
    {
        public enum Upscaler
        {
            Bilinear,
            FsrLiteCustom,
            Spatial7Tap,
            Mwsr,
            Fsr1Easu,
            Sgsr1
        }

        [System.Serializable]
        public sealed class FeatureSettings
        {
            [Range(0.5f, 1f)] public float renderScale = 0.75f;
            public Upscaler upscaler = Upscaler.FsrLiteCustom;
            public bool casEnabled;
            [Range(0f, 1f)] public float casSharpness = 0.2f;
            public RenderPassEvent injectionPoint = RenderPassEvent.BeforeRenderingPostProcessing;
        }

        public FeatureSettings settings = new FeatureSettings();
        private UltraScalePass pass;

        public override void Create()
        {
            pass?.Dispose();
            pass = new UltraScalePass(settings);
            pass.renderPassEvent = settings.injectionPoint;
        }

        public override void SetupRenderPasses(ScriptableRenderer renderer, in RenderingData renderingData)
        {
            if (pass != null)
                pass.SetTarget(renderer.cameraColorTargetHandle);
        }

        public override void AddRenderPasses(ScriptableRenderer renderer, ref RenderingData renderingData)
        {
            if (pass == null || renderingData.cameraData.isPreviewCamera ||
                renderingData.cameraData.cameraType == CameraType.Reflection)
                return;

            pass.renderPassEvent = settings.injectionPoint;
            renderer.EnqueuePass(pass);
        }

        protected override void Dispose(bool disposing)
        {
            pass?.Dispose();
            pass = null;
        }

        private sealed class UltraScalePass : ScriptableRenderPass
        {
            private readonly FeatureSettings settings;
            private RTHandle cameraColor;
            private RTHandle reduced;
            private RTHandle reconstructed;
            private Material fsrLite;
            private Material spatial7;
            private Material mwsr;
            private Material easu;
            private Material sgsr1;
            private Material cas;
            private static readonly int SharpnessId = Shader.PropertyToID("_Sharpness");
            private static readonly int InvResolutionId = Shader.PropertyToID("_InvResolution");
            private static readonly int ViewportInfoId = Shader.PropertyToID("_ViewportInfo");

            public UltraScalePass(FeatureSettings settings)
            {
                this.settings = settings;
                fsrLite = MakeMaterial("UltraScale/FSRLiteCustom");
                spatial7 = MakeMaterial("UltraScale/Spatial7Tap");
                mwsr = MakeMaterial("UltraScale/MWSR");
                easu = MakeMaterial("UltraScale/AMD_FSR1_EASU");
                sgsr1 = MakeMaterial("UltraScale/Qualcomm_SGSR1");
                cas = MakeMaterial("UltraScale/AMD_CAS");
            }

            private static Material MakeMaterial(string shaderName)
            {
                Shader shader = Shader.Find(shaderName);
                return shader != null && shader.isSupported
                    ? CoreUtils.CreateEngineMaterial(shader) : null;
            }

            public void SetTarget(RTHandle target) => cameraColor = target;

            public override void OnCameraSetup(CommandBuffer cmd, ref RenderingData renderingData)
            {
                RenderTextureDescriptor descriptor = renderingData.cameraData.cameraTargetDescriptor;
                descriptor.depthBufferBits = 0;
                descriptor.msaaSamples = 1;
                descriptor.bindMS = false;

                float scale = Mathf.Clamp(settings.renderScale, 0.5f, 1f);
                RenderTextureDescriptor reducedDescriptor = descriptor;
                reducedDescriptor.width = Mathf.Max(1, Mathf.RoundToInt(descriptor.width * scale));
                reducedDescriptor.height = Mathf.Max(1, Mathf.RoundToInt(descriptor.height * scale));

                RenderingUtils.ReAllocateIfNeeded(ref reduced, reducedDescriptor,
                    FilterMode.Bilinear, TextureWrapMode.Clamp, name: "_UltraScaleReduced");
                RenderingUtils.ReAllocateIfNeeded(ref reconstructed, descriptor,
                    FilterMode.Bilinear, TextureWrapMode.Clamp, name: "_UltraScaleReconstructed");
            }

            public override void Execute(ScriptableRenderContext context, ref RenderingData renderingData)
            {
                if (cameraColor == null || reduced == null || reconstructed == null)
                    return;

                Material scaler = SelectMaterial();
                CommandBuffer cmd = CommandBufferPool.Get("UltraScale URP Spatial Upscale");
                try
                {
                    Blit(cmd, cameraColor, reduced);
                    if (scaler == null || settings.upscaler == Upscaler.Bilinear)
                    {
                        Blit(cmd, reduced, reconstructed);
                    }
                    else
                    {
                        ConfigureMaterial(scaler, reduced.rt.width, reduced.rt.height,
                            reconstructed.rt.width, reconstructed.rt.height);
                        Blit(cmd, reduced, reconstructed, scaler, 0);
                    }

                    if (settings.casEnabled && cas != null)
                    {
                        cas.SetFloat(SharpnessId, Mathf.Clamp01(settings.casSharpness));
                        Blit(cmd, reconstructed, cameraColor, cas, 0);
                    }
                    else
                    {
                        Blit(cmd, reconstructed, cameraColor);
                    }
                    context.ExecuteCommandBuffer(cmd);
                }
                finally
                {
                    CommandBufferPool.Release(cmd);
                }
            }

            private Material SelectMaterial()
            {
                switch (settings.upscaler)
                {
                    case Upscaler.FsrLiteCustom: return fsrLite;
                    case Upscaler.Spatial7Tap: return spatial7;
                    case Upscaler.Mwsr: return mwsr;
                    case Upscaler.Fsr1Easu: return easu;
                    case Upscaler.Sgsr1: return sgsr1;
                    default: return null;
                }
            }

            private void ConfigureMaterial(Material material, int inW, int inH, int outW, int outH)
            {
                if (material == easu)
                {
                    material.SetVector(InvResolutionId, new Vector4(1f / inW, 1f / inH, 0f, 0f));
                    material.SetFloat(SharpnessId, 1f);
                }
                else if (material == sgsr1)
                {
                    material.SetVector(ViewportInfoId,
                        new Vector4(1f / inW, 1f / inH, inW, inH));
                }
                else if (material == spatial7)
                {
                    material.SetFloat(SharpnessId, 0f);
                }
                else if (material == mwsr)
                {
                    float sx = (float)outW / inW;
                    float sy = (float)outH / inH;
                    float scale = Mathf.Min(sx, sy);
                    float x = Mathf.Log(Mathf.Max(scale, 1.0001f));
                    float force = 1f + 0.35f * Mathf.Clamp01(
                        Mathf.Log(Mathf.Max(scale / 1.3333f, 0.0001f)) /
                        Mathf.Log(2f / 1.3333f));
                    float kc = 0.63536f + 0.02185f * x + 0.12781f * x * x;
                    float kg = (-0.12884f + 2.58447f * x - 1.26009f * x * x) * force;
                    float kr = 1.00715f - 1.02091f * x + 0.56996f * x * x;
                    float kl = (0.33417f - 0.29620f * x + 0.08628f * x * x) * force;
                    material.SetVector("texel", new Vector4(1f / inW, 1f / inH, 0f, 0f));
                    material.SetVector("scale", new Vector4(sx, sy, 0f, 0f));
                    material.SetVector("dims", new Vector4(inW, inH, 0f, 0f));
                    material.SetVector("across", new Vector4(kr / inW, kr / inH, 0f, 0f));
                    material.SetFloat("kc", kc);
                    material.SetFloat("ft", 0.005f);
                    material.SetFloat("kg", kg);
                    material.SetFloat("kwc", 0.1f);
                    material.SetVector("zc", new Vector4(0.5f, 0.5f, 0f, 0f));
                    material.SetVector("zr", new Vector4(0.2f, 0.2f, 0f, 0f));
                    material.SetFloat("zring", 0.45f);
                    material.SetFloat("zft", 0.005f);
                    material.SetFloat("zkg", kg);
                    material.SetFloat("gaze", 0f);
                    material.SetFloat("kl", kl);
                    material.SetFloat("zref", 1f);
                }
            }

            public void Dispose()
            {
                reduced?.Release();
                reconstructed?.Release();
                reduced = null;
                reconstructed = null;
                CoreUtils.Destroy(fsrLite);
                CoreUtils.Destroy(spatial7);
                CoreUtils.Destroy(mwsr);
                CoreUtils.Destroy(easu);
                CoreUtils.Destroy(sgsr1);
                CoreUtils.Destroy(cas);
                fsrLite = spatial7 = mwsr = easu = sgsr1 = cas = null;
            }
        }
    }
}
