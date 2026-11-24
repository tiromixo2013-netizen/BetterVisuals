package dev.ultrascale.mobile

import android.content.Context
import org.json.JSONObject

/**
 * Reads the bundled per-game UltraScale sidecar profile.
 *
 * This deliberately does NOT write unknown keys into Asphalt 8's native
 * gameoptions.json. It reads only our namespaced profile asset; the game
 * engine will not consume these settings unless a supported game adapter
 * explicitly connects them to its render pipeline.
 */
object Asphalt8GraphicsProfileReader {
    private const val PROFILE_ASSET = "profiles/asphalt8_m14_ultrascale.json"
    private const val EXPECTED_PACKAGE = "com.gameloft.android.ANMP.GloftA8HM"

    data class LoadedProfile(
        val settings: UltraScaleSettings,
        val isForAsphalt8: Boolean,
        val runtimeInjectionReady: Boolean,
        val status: String
    )

    fun read(context: Context): LoadedProfile {
        val jsonText = context.assets.open(PROFILE_ASSET)
            .bufferedReader()
            .use { it.readText() }
        return parse(jsonText)
    }

    /** Public for validation/tests and future SAF-selected profile files. */
    fun parse(jsonText: String): LoadedProfile {
        val root = JSONObject(jsonText)
        val target = root.optJSONObject("target") ?: JSONObject()
        val gates = root.optJSONObject("capabilityGates") ?: JSONObject()

        fun enumName(key: String, fallback: String): String =
            root.optString(key, fallback).ifBlank { fallback }

        fun <T : Enum<T>> enumOrDefault(values: Array<T>, raw: String, fallback: T): T =
            values.firstOrNull { it.name.equals(raw, ignoreCase = true) } ?: fallback

        val mode = enumOrDefault(
            UpscaleMode.entries.toTypedArray(),
            enumName("upscaler", UpscaleMode.BILINEAR.name),
            UpscaleMode.BILINEAR
        )
        val lowLatency = enumOrDefault(
            LowLatencyMode.entries.toTypedArray(),
            enumName("lowLatency", LowLatencyMode.OFF.name),
            LowLatencyMode.OFF
        )
        val preset = enumOrDefault(
            UpscalePreset.entries.toTypedArray(),
            enumName("preset", UpscalePreset.CUSTOM.name),
            UpscalePreset.CUSTOM
        )
        val frameGeneration = enumOrDefault(
            FrameGenerationMode.entries.toTypedArray(),
            enumName("frameGeneration", FrameGenerationMode.OFF.name),
            FrameGenerationMode.OFF
        )

        // CAS is explicitly independent from the selected upscaler.
        val casEnabled = root.optBoolean("casEnabled", false)
        val casSharpness = if (casEnabled) root.optInt("casSharpnessPercent", 0) else 0

        val settings = UltraScaleSettings(
            renderScalePercent = root.optInt("renderScalePercent", 70),
            fpsLimit = root.optInt("fpsLimit", 60),
            upscaler = mode,
            upscaleSharpnessPercent = root.optInt("upscaleSharpnessPercent", 20),
            preset = preset,
            casSharpnessPercent = casSharpness,
            lowLatency = lowLatency,
            integerScaling = root.optBoolean("integerScaling", false),
            frameGeneration = frameGeneration,
            framePacing = root.optBoolean("framePacing", true),
            vsyncAligned = true,
            lowQueue = root.optInt("queueDepth", 1) <= 1,
            queueDepth = root.optInt("queueDepth", 1),
            bidirectionalFlowCheck = true,
            sceneCutDetection = true,
            disocclusionRejection = true,
            edgeAwareWarping = true,
            hudGhostingSuppression = true
        ).normalized()

        val isForAsphalt8 = target.optString("packageName") == EXPECTED_PACKAGE
        // A JSON profile is configuration only; it cannot grant permission to
        // attach a graphics layer to a signed retail game.
        val runtimeReady = root.optBoolean("runtimeAdapterVerified", false) &&
            !gates.optBoolean("doNotClaimActiveWithoutGameAdapter", true)

        return LoadedProfile(
            settings = settings,
            isForAsphalt8 = isForAsphalt8,
            runtimeInjectionReady = runtimeReady,
            status = root.optString("status", "profile_only_not_runtime_injection")
        )
    }
}
