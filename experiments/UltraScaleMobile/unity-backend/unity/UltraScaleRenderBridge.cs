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
            public string scaler = "UltraScale 7-tap (experimental)";
        }

        public Settings settings = new Settings();
        public string RuntimeStatus { get; private set; } = "INITIALIZING";

        private bool panelVisible;
        private Rect panelRect = new Rect(18, 72, 320, 310);
        private readonly string[] scalerNames =
        {
            "Bilinear",
            "UltraScale 6-tap (experimental)"
        };
        private int selectedScaler = 1;
        private Material spatialMaterial;

        private void Awake()
        {
            Shader shader = Shader.Find(ShaderName);
            if (shader == null || !shader.isSupported)
            {
                RuntimeStatus = "UNSUPPORTED: spatial shader missing";
                enabled = false;
                return;
            }

            spatialMaterial = new Material(shader) { hideFlags = HideFlags.HideAndDontSave };
            RuntimeStatus = "MANAGED_RENDER_READY";

            // The native bridge is optional for this managed Unity sample path.
            // Its status is reported separately and never gates the shader pass.
            try
            {
                uint abi = ultrascale_unity_bridge_abi();
                int nativeStatus = ultrascale_unity_backend_status();
                if (abi != 1u || nativeStatus != 1)
                    RuntimeStatus += " / NATIVE_BRIDGE_NOT_CONNECTED";
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
                settings.sharpeningEnabled, "Experimental edge sharpening");
            GUI.enabled = settings.sharpeningEnabled;
            GUILayout.Label("Strength: " +
                Mathf.RoundToInt(settings.sharpness * 100f) + "%");
            settings.sharpness = GUILayout.HorizontalSlider(settings.sharpness, 0f, 1f);
            GUI.enabled = true;

            GUILayout.Space(8);
            GUILayout.Label("7-tap is a project-authored spatial filter, not AMD FSR/SGSR.");
            GUILayout.Label("MWSR, SGSR1 and FSR1 EASU need separate Unity shader ports.");
            GUI.DragWindow(new Rect(0, 0, 10000, 28));
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
            reduced.filterMode = FilterMode.Bilinear;

            try
            {
                // First pass renders the camera image into the selected internal size.
                Graphics.Blit(source, reduced);

                if (settings.scaler == "Bilinear")
                {
                    Graphics.Blit(reduced, destination);
                }
                else
                {
                    spatialMaterial.SetFloat("_Sharpness",
                        settings.sharpeningEnabled ? settings.sharpness : 0f);
                    Graphics.Blit(reduced, destination, spatialMaterial, 0);
                }
            }
            finally
            {
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
        }
    }
}
