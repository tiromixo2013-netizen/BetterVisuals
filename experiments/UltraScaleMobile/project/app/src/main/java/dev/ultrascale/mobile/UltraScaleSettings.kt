package dev.ultrascale.mobile

/**
 * Canonical UltraScale settings model.
 *
 * Values are persisted as a user profile. A game setting is not considered
 * active until its adapter proves that it owns the relevant render target,
 * frame history and presentation controls.
 */
enum class UpscaleMode(
    val title: String,
    val minGlesMajor: Int,
    val minGlesMinor: Int,
    val implementedInHarness: Boolean
) {
    OFF("Off", 2, 0, true),
    SGSR1("Qualcomm SGSR1 Mobile", 3, 1, false),
    SGSR2("Qualcomm SGSR2 · temporal", 3, 1, false),
    MWSR("MWSR", 3, 0, false),
    FSR_LITE("UltraScale FSR Lite · custom 6-tap", 3, 0, true),
    NEAREST("Nearest Neighbor", 2, 0, true),
    BILINEAR("Bilinear", 2, 0, true)
}

enum class FpsLimitOption(val value: Int) {
    FPS_60(60), FPS_90(90), FPS_120(120), FPS_144(144), FPS_165(165), FPS_240(240)
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
    ON("On"),
    ULTRA("Ultra")
}

enum class FrameGenerationMode(val title: String, val multiplier: Int) {
    OFF("Off", 1),
    X2("2×", 2),
    X3("3×", 3),
    X4("4×", 4)
}

/** Exactly one selected Frame Generation algorithm, as agreed. */
enum class FrameGenerationAlgorithm(val title: String) {
    PYRAMIDAL_LUCAS_KANADE("Pyramidal Lucas–Kanade Optical Flow")
}

data class UltraScaleSettings(
    /** Internal render resolution as a percentage of the output dimensions. */
    val renderScalePercent: Int = 70,
    /** Selectable game profile cap; 240 is the highest currently patched build target. */
    val fpsLimit: Int = 240,
    val upscaler: UpscaleMode = UpscaleMode.BILINEAR,
    /** Reconstruction-stage sharpness; separate from CAS. */
    val upscaleSharpnessPercent: Int = 20,
    val preset: UpscalePreset = UpscalePreset.CUSTOM,
    /** Independent post-upscale CAS-style sharpening strength. Zero disables it. */
    val casSharpnessPercent: Int = 0,
    val lowLatency: LowLatencyMode = LowLatencyMode.OFF,
    /** Preserve integer pixel edges when the output scale is an exact integer ratio. */
    val integerScaling: Boolean = false,

    // Section 7 — Frame Generation. These are saved profile values; they are
    // not reported as active until a game adapter provides the required pipeline.
    val frameGeneration: FrameGenerationMode = FrameGenerationMode.OFF,
    val framePacing: Boolean = true,
    val vsyncAligned: Boolean = true,
    val lowQueue: Boolean = true,
    val queueDepth: Int = 1,
    val bidirectionalFlowCheck: Boolean = true,
    val sceneCutDetection: Boolean = true,
    val disocclusionRejection: Boolean = true,
    val edgeAwareWarping: Boolean = true,
    val hudGhostingSuppression: Boolean = true
) {
    fun normalized(): UltraScaleSettings = copy(
        renderScalePercent = renderScalePercent.coerceIn(50, 100),
        fpsLimit = fpsLimit.coerceIn(60, 240),
        upscaleSharpnessPercent = upscaleSharpnessPercent.coerceIn(0, 100),
        casSharpnessPercent = casSharpnessPercent.coerceIn(0, 100),
        queueDepth = queueDepth.coerceIn(1, 3)
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
                upscaler = UpscaleMode.FSR_LITE,
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
 * Capability report supplied by a renderer/game adapter. Frame generation
 * requires an owned render target plus frame history and presentation control.
 */
data class UltraScaleCapabilities(
    val glesMajor: Int = 0,
    val glesMinor: Int = 0,
    val ownsRenderTarget: Boolean = false,
    val canControlFrameQueue: Boolean = false,
    val canGenerateFrames: Boolean = false
) {
    fun supports(mode: UpscaleMode): Boolean =
        glesMajor > mode.minGlesMajor ||
            (glesMajor == mode.minGlesMajor && glesMinor >= mode.minGlesMinor)

    fun canApply(settings: UltraScaleSettings): Boolean {
        val s = settings.normalized()
        return ownsRenderTarget &&
            supports(s.upscaler) &&
            (s.lowLatency == LowLatencyMode.OFF || canControlFrameQueue) &&
            (s.frameGeneration == FrameGenerationMode.OFF ||
                (canGenerateFrames && canControlFrameQueue))
    }
}
