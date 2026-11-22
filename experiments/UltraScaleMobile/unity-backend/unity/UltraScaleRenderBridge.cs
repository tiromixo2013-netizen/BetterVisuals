// Unity-side adapter for an owned/test Unity project.
// Built-in Render Pipeline only. This implementation performs a real managed
// downsample + spatial reconstruction pass; it does not inject into other apps.
using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace UltraScale.Unity
{
    [RequireComponent(typeof(Camera))]
    public sealed class UltraScaleRenderBridge : MonoBehaviour
    {
        private const string NativeLibrary = "ultrascale_unity_bridge";
        private const string ShaderName = "UltraScale/Spatial7Tap";
        private const string FsrLiteShaderName = "UltraScale/FSRLiteCustom";
        private const string MwsrShaderName = "UltraScale/MWSR";
        private const string Fsr1ShaderName = "UltraScale/AMD_FSR1_EASU";
        private const string CasShaderName = "UltraScale/AMD_CAS";

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        private static extern uint ultrascale_unity_bridge_abi();

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        private static extern int ultrascale_unity_backend_status();

        [Serializable]
        public sealed class Settings
        {
            [Range(0.5f, 1f)] public float renderScale = 0.75f;
            [Range(0f, 1f)] public float sharpness = 0.2f;
            public bool sharpeningEnabled;
            public string scaler = "UltraScale FSR Lite (custom)";
        }

        public Settings settings = new Settings();
        public string RuntimeStatus { get; private set; } = "INITIALIZING";

        private bool panelVisible;
        private Rect panelRect = new Rect(18, 72, 320, 310);
        private readonly string[] scalerNames =
        {
            "Bilinear",
            "UltraScale FSR Lite (custom)",
            "UltraScale 7-tap (experimental)",
            "MWSR (MIT)",
            "AMD FSR1 EASU"
        };
        private int selectedScaler = 1;
        private Material spatialMaterial;
        private Material fsrLiteMaterial;
        private Material mwsrMaterial;
        private Material fsr1Material;
        private Material casMaterial;

        private void Awake()
        {
            Shader shader = Shader.Find(ShaderName);
            Shader fsrLiteShader = Shader.Find(FsrLiteShaderName);
            Shader mwsrShader = Shader.Find(MwsrShaderName);
            Shader fsr1Shader = Shader.Find(Fsr1ShaderName);
            Shader casShader = Shader.Find(CasShaderName);
            if (shader == null || !shader.isSupported ||
                fsrLiteShader == null || !fsrLiteShader.isSupported ||
                mwsrShader == null || !mwsrShader.isSupported ||
                fsr1Shader == null || !fsr1Shader.isSupported ||
                casShader == null || !casShader.isSupported)
            {
                RuntimeStatus = "UNSUPPORTED: one or more Unity shaders missing";
                enabled = false;
                return;
            }

            spatialMaterial = new Material(shader) { hideFlags = HideFlags.HideAndDontSave };
            fsrLiteMaterial = new Material(fsrLiteShader) { hideFlags = HideFlags.HideAndDontSave };
            mwsrMaterial = new Material(mwsrShader) { hideFlags = HideFlags.HideAndDontSave };
            fsr1Material = new Material(fsr1Shader) { hideFlags = HideFlags.HideAndDontSave };
            casMaterial = new Material(casShader) { hideFlags = HideFlags.HideAndDontSave };
            RuntimeStatus = "MANAGED_RENDER_READY";

            // The native bridge is optional for this managed Unity sample path.
            // Its status is reported separately and never gates the shader pass.
            try
            {
                uint abi = ultrascale_unity_bridge_abi();
                int nativeStatus = ultrascale_unity_backend_status();
                if (abi != 1u)
                    RuntimeStatus += " / NATIVE_ABI_UNSUPPORTED";
                else if (nativeStatus == 1)
                    RuntimeStatus += " / NATIVE_INTEGRATION_REQUIRED";
                else
                    RuntimeStatus += " / NATIVE_BACKEND_UNAVAILABLE";
            }
            catch (DllNotFoundException)
            {
                RuntimeStatus += " / NATIVE_BRIDGE_NOT_PACKAGED";
            }
            catch (EntryPointNotFoundException)
            {
                RuntimeStatus += " / NATIVE_ABI_MISMATCH";
            }
        }

        private void OnGUI()
        {
            if (GUI.Button(new Rect(16, 16, 150, 42), "UltraScale"))
                panelVisible = !panelVisible;

            if (panelVisible)
                panelRect = GUI.Window(GetInstanceID(), panelRect, DrawPanel,
                    "UltraScale — Graphics");
        }

        private void DrawPanel(int windowId)
        {
            GUILayout.Label("Render: " + RuntimeStatus);
            GUILayout.Space(6);
            GUILayout.Label("Spatial mode");
            selectedScaler = GUILayout.SelectionGrid(selectedScaler, scalerNames, 1);
            settings.scaler = scalerNames[selectedScaler];

            GUILayout.Space(6);
            GUILayout.Label("Internal resolution: " +
                Mathf.RoundToInt(settings.renderScale * 100f) + "%");
            settings.renderScale = GUILayout.HorizontalSlider(settings.renderScale, 0.5f, 1f);

            settings.sharpeningEnabled = GUILayout.Toggle(
                settings.sharpeningEnabled, "AMD CAS post-sharpen");
            GUI.enabled = settings.sharpeningEnabled;
            GUILayout.Label("Strength: " +
                Mathf.RoundToInt(settings.sharpness * 100f) + "%");
            settings.sharpness = GUILayout.HorizontalSlider(settings.sharpness, 0f, 1f);
            GUI.enabled = true;

            GUILayout.Space(8);
            GUILayout.Label("FSR Lite is the project custom six-sample filter, not AMD FSR.");
            GUILayout.Label("7-tap is a separate experimental spatial filter.");
            GUILayout.Label("MWSR is the upstream MIT HLSL spatial upscale port.");
            GUILayout.Label("SGSR1 remains a separate port.");
            GUI.DragWindow(new Rect(0, 0, 10000, 28));
        }


        private void ConfigureMwsr(int inputWidth, int inputHeight,
            int outputWidth, int outputHeight)
        {
            float sx = (float)outputWidth / inputWidth;
            float sy = (float)outputHeight / inputHeight;
            float scale = Mathf.Min(sx, sy);
            float x = Mathf.Log(Mathf.Max(scale, 1.0001f));
            float force = 1f + 0.35f * Mathf.Clamp01(
                Mathf.Log(Mathf.Max(scale / 1.3333f, 0.0001f)) /
                Mathf.Log(2f / 1.3333f));

            float kc = 0.63536f + 0.02185f * x + 0.12781f * x * x;
            float kg = (-0.12884f + 2.58447f * x - 1.26009f * x * x) * force;
            float kr = 1.00715f - 1.02091f * x + 0.56996f * x * x;
            float kl = (0.33417f - 0.29620f * x + 0.08628f * x * x) * force;

            mwsrMaterial.SetVector("texel", new Vector4(1f / inputWidth, 1f / inputHeight, 0f, 0f));
            mwsrMaterial.SetVector("scale", new Vector4(sx, sy, 0f, 0f));
            mwsrMaterial.SetVector("dims", new Vector4(inputWidth, inputHeight, 0f, 0f));
            mwsrMaterial.SetVector("across", new Vector4(kr / inputWidth, kr / inputHeight, 0f, 0f));
            mwsrMaterial.SetFloat("kc", kc);
            mwsrMaterial.SetFloat("ft", 0.005f);
            mwsrMaterial.SetFloat("kg", kg);
            mwsrMaterial.SetFloat("kwc", 0.1f);
            mwsrMaterial.SetVector("zc", new Vector4(0.5f, 0.5f, 0f, 0f));
            mwsrMaterial.SetVector("zr", new Vector4(0.2f, 0.2f, 0f, 0f));
            mwsrMaterial.SetFloat("zring", 0.45f);
            mwsrMaterial.SetFloat("zft", 0.005f);
            mwsrMaterial.SetFloat("zkg", kg);
            mwsrMaterial.SetFloat("gaze", 0f);
            mwsrMaterial.SetFloat("kl", kl);
            mwsrMaterial.SetFloat("zref", 1f);
        }

        private void OnRenderImage(RenderTexture source, RenderTexture destination)
        {
            if (source == null || destination == null || spatialMaterial == null)
            {
                if (source != null && destination != null)
                    Graphics.Blit(source, destination);
                return;
            }

            int width = Mathf.Max(1, Mathf.RoundToInt(source.width * settings.renderScale));
            int height = Mathf.Max(1, Mathf.RoundToInt(source.height * settings.renderScale));
            RenderTexture reduced = RenderTexture.GetTemporary(
                width, height, 0, source.format, RenderTextureReadWrite.Default);
            RenderTexture upscaled = RenderTexture.GetTemporary(
                destination.width, destination.height, 0,
                destination.format, RenderTextureReadWrite.Default);
            reduced.filterMode = FilterMode.Bilinear;

            try
            {
                Graphics.Blit(source, reduced);

                if (settings.scaler == "Bilinear")
                {
                    Graphics.Blit(reduced, upscaled);
                }
                else if (settings.scaler == "UltraScale FSR Lite (custom)")
                {
                    Graphics.Blit(reduced, upscaled, fsrLiteMaterial, 0);
                }
                else if (settings.scaler == "MWSR (MIT)")
                {
                    ConfigureMwsr(reduced.width, reduced.height,
                        upscaled.width, upscaled.height);
                    Graphics.Blit(reduced, upscaled, mwsrMaterial, 0);
                }
                else if (settings.scaler == "AMD FSR1 EASU")
                {
                    fsr1Material.SetVector("_InvResolution",
                        new Vector4(1f / reduced.width, 1f / reduced.height, 0f, 0f));
                    fsr1Material.SetFloat("_Sharpness", 1f);
                    Graphics.Blit(reduced, upscaled, fsr1Material, 0);
                }
                else
                {
                    spatialMaterial.SetFloat("_Sharpness", 0f);
                    Graphics.Blit(reduced, upscaled, spatialMaterial, 0);
                }

                if (settings.sharpeningEnabled)
                {
                    casMaterial.SetFloat("_Sharpness", settings.sharpness);
                    Graphics.Blit(upscaled, destination, casMaterial, 0);
                }
                else
                {
                    Graphics.Blit(upscaled, destination);
                }
            }
            finally
            {
                RenderTexture.ReleaseTemporary(upscaled);
                RenderTexture.ReleaseTemporary(reduced);
            }
        }

        private void OnDestroy()
        {
            if (spatialMaterial != null)
            {
                if (Application.isPlaying) Destroy(spatialMaterial);
                else DestroyImmediate(spatialMaterial);
                spatialMaterial = null;
            }

            if (fsrLiteMaterial != null)
            {
                if (Application.isPlaying) Destroy(fsrLiteMaterial);
                else DestroyImmediate(fsrLiteMaterial);
                fsrLiteMaterial = null;
            }

            if (mwsrMaterial != null)
            {
                if (Application.isPlaying) Destroy(mwsrMaterial);
                else DestroyImmediate(mwsrMaterial);
                mwsrMaterial = null;
            }

            if (fsr1Material != null)
            {
                if (Application.isPlaying) Destroy(fsr1Material);
                else DestroyImmediate(fsr1Material);
                fsr1Material = null;
            }

            if (casMaterial != null)
            {
                if (Application.isPlaying) Destroy(casMaterial);
                else DestroyImmediate(casMaterial);
                casMaterial = null;
            }
        }
    }
}
