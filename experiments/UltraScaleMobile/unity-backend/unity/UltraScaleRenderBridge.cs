// Unity-side adapter example for an owned/test Unity project.
// Built-in Render Pipeline only: OnRenderImage receives a real source RenderTexture.
// The in-game panel exposes UltraScale settings, but this scaffold does not apply
// a scaler yet; the render callback preserves the original image until a processor
// is attached and validated.
using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace UltraScale.Unity
{
    public sealed class UltraScaleRenderBridge : MonoBehaviour
    {
        private const string NativeLibrary = "ultrascale_unity_bridge";

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        private static extern uint ultrascale_unity_bridge_abi();

        [DllImport(NativeLibrary, CallingConvention = CallingConvention.Cdecl)]
        private static extern int ultrascale_unity_backend_status();

        [Serializable]
        public sealed class Settings
        {
            [Range(0.5f, 1f)] public float renderScale = 0.75f;
            [Range(0f, 1f)] public float casSharpness = 0.35f;
            public bool casEnabled;
            public string scaler = "FSR Lite";
        }

        public Settings settings = new Settings();
        public string RuntimeStatus { get; private set; } = "INTEGRATION_REQUIRED";
        private bool panelVisible;
        private Rect panelRect = new Rect(18, 72, 300, 300);
        private string[] scalerNames = { "Bilinear", "FSR Lite", "MWSR", "SGSR1", "FSR1 EASU" };
        private int selectedScaler = 1;

        private void Awake()
        {
            try
            {
                RuntimeStatus = ultrascale_unity_bridge_abi() == 1u &&
                    ultrascale_unity_backend_status() == 1
                    ? "INTEGRATION_REQUIRED"
                    : "UNSUPPORTED";
            }
            catch (DllNotFoundException)
            {
                RuntimeStatus = "UNSUPPORTED: native bridge not packaged";
            }
            catch (EntryPointNotFoundException)
            {
                RuntimeStatus = "UNSUPPORTED: bridge ABI mismatch";
            }
        }

        private void OnGUI()
        {
            if (GUI.Button(new Rect(16, 16, 150, 42), "UltraScale"))
                panelVisible = !panelVisible;

            if (!panelVisible) return;

            panelRect = GUI.Window(GetInstanceID(), panelRect, DrawPanel, "UltraScale — Graphics");
        }

        private void DrawPanel(int windowId)
        {
            GUILayout.Label("Backend: " + RuntimeStatus);
            GUILayout.Space(6);
            GUILayout.Label("Upscaler");
            selectedScaler = GUILayout.SelectionGrid(selectedScaler, scalerNames, 2);
            settings.scaler = scalerNames[selectedScaler];

            GUILayout.Space(6);
            GUILayout.Label("Render scale: " + Mathf.RoundToInt(settings.renderScale * 100f) + "%");
            settings.renderScale = GUILayout.HorizontalSlider(settings.renderScale, 0.5f, 1f);

            settings.casEnabled = GUILayout.Toggle(settings.casEnabled, "AMD CAS sharpening");
            GUI.enabled = settings.casEnabled;
            GUILayout.Label("CAS sharpness: " + Mathf.RoundToInt(settings.casSharpness * 100f) + "%");
            settings.casSharpness = GUILayout.HorizontalSlider(settings.casSharpness, 0f, 1f);
            GUI.enabled = true;

            GUILayout.Space(8);
            GUILayout.Label("Settings are staged only. Render processing is not active yet.");
            GUI.DragWindow(new Rect(0, 0, 10000, 28));
        }

        private void OnRenderImage(RenderTexture source, RenderTexture destination)
        {
            // This is a real Built-in RP image callback. Keep output unchanged until
            // a graphics-API-specific processor is attached and runtime-validated.
            if (source != null && destination != null)
                Graphics.Blit(source, destination);
        }
    }
}
