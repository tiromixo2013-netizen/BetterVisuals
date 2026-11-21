package dev.ultrascale.mobile

/**
 * Adapter boundary for game-integrated rendering only.
 * The API intentionally accepts render targets supplied by the cooperating game;
 * it does not attempt to open or modify another application's process.
 */
interface RenderBackend {
    val apiName: String
    fun initialize(context: BackendContext): BackendResult
    fun processFrame(frame: RenderFrame): BackendResult
    fun shutdown()
}

data class BackendContext(
    val width: Int,
    val height: Int,
    val colorFormat: String,
    val gameProvidesSynchronization: Boolean
)

data class RenderFrame(
    val inputHandle: Long,
    val outputHandle: Long,
    val frameIndex: Long,
    val hasTemporalHistory: Boolean = false,
    val hasMotionVectors: Boolean = false
)

enum class BackendStatus { READY, INTEGRATION_REQUIRED, UNSUPPORTED, FAILED }

data class BackendResult(
    val status: BackendStatus,
    val detail: String
)

object TechniqueRequirements {
    fun check(method: String, frame: RenderFrame): BackendResult = when (method) {
        "SGSR2" -> if (frame.hasTemporalHistory && frame.hasMotionVectors)
            BackendResult(BackendStatus.READY, "Temporal inputs supplied")
        else BackendResult(BackendStatus.INTEGRATION_REQUIRED, "SGSR2 requires frame history and motion data")
        "AMD CAS" -> BackendResult(BackendStatus.INTEGRATION_REQUIRED, "CAS is a sharpening pass; requires an integrated render target")
        "SGSR1", "MWSR", "FSR Lite", "Nearest", "Bilinear" ->
            BackendResult(BackendStatus.INTEGRATION_REQUIRED, "Requires a render target supplied by the game integration")
        else -> BackendResult(BackendStatus.UNSUPPORTED, "Unknown technique")
    }
}
