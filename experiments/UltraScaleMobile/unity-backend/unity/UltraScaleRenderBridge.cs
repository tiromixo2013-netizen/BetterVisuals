// Unity-side adapter example for an owned/test Unity project.
// Built-in Render Pipeline only: OnRenderImage supplies a real source RenderTexture.
// This sample reports capability state; it intentionally does not replace the image
// until the native GLES/Vulkan scaler backend has been implemented and validated.
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

        private void OnRenderImage(RenderTexture source, RenderTexture destination)
        {
            // Keep the game visually unchanged until a real API backend is ready.
            // This callback proves the game can provide a render target; it is not
            // itself evidence that an upscaling shader ran.
            if (source != null && destination != null)
                Graphics.Blit(source, destination);
        }
    }
}
