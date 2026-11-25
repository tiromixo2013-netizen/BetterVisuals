package dev.ultrascale.asphaltlegends

import org.json.JSONObject

/**
 * Version-gated helper for the historical Urban GT19 external-config route.
 *
 * This does not install an external-config reader, patch an APK, access private
 * app data, or inject shaders. It only updates keys already present in a
 * user-supplied JSON document, and only when the caller has independently
 * verified the exact legacy target and reader.
 */
object LegacyGameOptionsAdapter {
    enum class CarDetail(val lodValue: Int) {
        HIGH(0),
        MEDIUM(4),
        LOW(7)
    }

    enum class ShadowQuality(val value: Int) {
        OFF(0),
        LOW(1),
        MEDIUM(2),
        HIGH(3)
    }

    data class Request(
        val gameVersion: String,
        val externalConfigReaderVerified: Boolean,
        val renderScalePercent: Int,
        val carDetail: CarDetail,
        val carShadowQuality: ShadowQuality,
        val preserveUiResolution: Boolean = true
    )

    data class Result(
        val accepted: Boolean,
        val json: String?,
        val appliedKeys: List<String>,
        val skippedKeys: List<String>,
        val status: String
    )

    private const val SUPPORTED_LEGACY_VERSION = "1.0.8"

    fun applyToExistingConfig(existingJson: String, request: Request): Result {
        if (request.gameVersion != SUPPORTED_LEGACY_VERSION) {
            return blocked("This mapper is restricted to the documented legacy 1.0.8 config; detected ${request.gameVersion}.")
        }
        if (!request.externalConfigReaderVerified) {
            return blocked("The modified APK's external-config reader has not been independently verified.")
        }

        val root = try {
            JSONObject(existingJson)
        } catch (error: Exception) {
            return blocked("The supplied GameOptions JSON is invalid: ${error.message ?: "parse error"}.")
        }

        val scale = request.renderScalePercent.coerceIn(50, 100) / 100.0
        val applied = mutableListOf<String>()
        val skipped = mutableListOf<String>()

        fun putIfExisting(key: String, value: Any) {
            if (root.has(key)) {
                root.put(key, value)
                applied += key
            } else {
                skipped += key
            }
        }

        putIfExisting("render_scene3DResolutionScale", scale)
        if (!request.preserveUiResolution) {
            putIfExisting("render_scene2DResolutionScale", scale)
        } else if (!root.has("render_scene2DResolutionScale")) {
            skipped += "render_scene2DResolutionScale (preserved/default; key absent)"
        }

        putIfExisting("models_carHighestQualityLODIngame", request.carDetail.lodValue)
        putIfExisting("models_carHighestQualityLODMenu", request.carDetail.lodValue)
        putIfExisting("render_focusedVehicleShadowQuality", request.carShadowQuality.value)

        if (applied.isEmpty()) {
            return Result(
                accepted = false,
                json = null,
                appliedKeys = emptyList(),
                skippedKeys = skipped,
                status = "No allowlisted historical GameOptions keys were present; original JSON left unchanged."
            )
        }

        return Result(
            accepted = true,
            json = root.toString(2),
            appliedKeys = applied,
            skippedKeys = skipped,
            status = "Updated only existing allowlisted legacy keys. This is a config transformation, not proof the game loads the file or that any UltraScale shader is active."
        )
    }

    private fun blocked(message: String) = Result(
        accepted = false,
        json = null,
        appliedKeys = emptyList(),
        skippedKeys = emptyList(),
        status = message
    )
}
