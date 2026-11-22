package dev.ultrascale.mobile

/**
 * Canonical UltraScale settings model.
 *
 * This is the shared settings contract for the app-owned renderer and future
 * game adapters. A setting is not considered active in a game unless that
 * adapter reports the required render-pipeline capability.
 */
enum class UpscaleMode(
    val title: String,
    val minGlesMajor: Int,
    val minGlesMinor: Int,
    val implementedInHarness: Boolean
) {
    OFF("Off", 2, 0, true),
    NEAREST("Nearest Neighbor", 2, 0, true),
    BILINEAR("Bilinear", 2, 0, true),
    CATMULL_ROM("Catmull-Rom · experimental", 2, 0, true),
    FSR_LITE("UltraScale FSR Lite · custom 6-tap", 2, 0, true),
    AMD_FSR1_EASU("AMD FSR1 · EASU", 2, 0, false),
    MWSR("MWSR", 3, 0, false),
    SGSR1("Qualcomm SGSR1 Mobile", 3, 1, false),
    SGSR2("Qualcomm SGSR2 · temporal", 3, 1, false)
}

enum class UpscalePreset(val title: String) {
    ULTRA_PERFORMANCE("Ultra Performance"),
    PERFORMANCE("Performance"),
    BALANCED("Balanced"),
    QUALITY("Quality"),
    ULTRA_QUALITY("Ultra Quality"),
    CUSTOM("Custom")
}

enum class LowLatencyMode(val title: String) {
    OFF("Off"),
    REDUCE_QUEUE("Reduce Queue"),
    AGGRESSIVE("Aggressive · adapter dependent")
}

data class UltraScaleSettings(
    /** Internal render resolution as a percentage of the output dimensions. */
    val renderScalePercent: Int = 70,
    val upscaler: UpscaleMode = UpscaleMode.BILINEAR,
    /** Reconstruction-stage sharpness; separate from CAS. */
    val upscaleSharpnessPercent: Int = 20,
    val preset: UpscalePreset = UpscalePreset.CUSTOM,
    /** Independent post-upscale CAS-style sharpening strength. Zero disables it; official AMD CAS requires a separately validated backend. */
    val casSharpnessPercent: Int = 0,
    val lowLatency: LowLatencyMode = LowLatencyMode.OFF,
    /** Preserve integer pixel edges when the output scale is an exact integer ratio. */
    val integerScaling: Boolean = false
) {
    fun normalized(): UltraScaleSettings = copy(
        renderScalePercent = renderScalePercent.coerceIn(50, 100),
        upscaleSharpnessPercent = upscaleSharpnessPercent.coerceIn(0, 100),
        casSharpnessPercent = casSharpnessPercent.coerceIn(0, 100)
    )

    companion object {
        fun forPreset(preset: UpscalePreset): UltraScaleSettings = when (preset) {
            UpscalePreset.CUSTOM -> UltraScaleSettings()
            UpscalePreset.ULTRA_PERFORMANCE -> UltraScaleSettings(
                renderScalePercent = 50,
                upscaler = UpscaleMode.BILINEAR,
                upscaleSharpnessPercent = 5,
                preset = preset,
                casSharpnessPercent = 5
            )
            UpscalePreset.PERFORMANCE -> UltraScaleSettings(
                renderScalePercent = 55,
                upscaler = UpscaleMode.BILINEAR,
                upscaleSharpnessPercent = 10,
                preset = preset,
                casSharpnessPercent = 10
            )
            UpscalePreset.BALANCED -> UltraScaleSettings(
                renderScalePercent = 70,
                upscaler = UpscaleMode.BILINEAR,
                upscaleSharpnessPercent = 25,
                preset = preset,
                casSharpnessPercent = 20
            )
            UpscalePreset.QUALITY -> UltraScaleSettings(
                renderScalePercent = 85,
                upscaler = UpscaleMode.CATMULL_ROM,
                upscaleSharpnessPercent = 35,
                preset = preset,
                casSharpnessPercent = 25
            )
            UpscalePreset.ULTRA_QUALITY -> UltraScaleSettings(
                renderScalePercent = 90,
                upscaler = UpscaleMode.FSR_LITE,
                upscaleSharpnessPercent = 25,
                preset = preset,
                casSharpnessPercent = 15
            )
        }
    }
}

/**
 * Capability report supplied by a renderer/game adapter. Low-latency options
 * remain unavailable unless the adapter can actually control frame submission.
 */
data class UltraScaleCapabilities(
    val glesMajor: Int = 0,
    val glesMinor: Int = 0,
    val ownsRenderTarget: Boolean = false,
    val canControlFrameQueue: Boolean = false
) {
    fun supports(mode: UpscaleMode): Boolean =
        glesMajor > mode.minGlesMajor ||
            (glesMajor == mode.minGlesMajor && glesMinor >= mode.minGlesMinor)

    fun canApply(settings: UltraScaleSettings): Boolean {
        val s = settings.normalized()
        return ownsRenderTarget &&
            supports(s.upscaler) &&
            (s.lowLatency == LowLatencyMode.OFF || canControlFrameQueue)
    }
}
