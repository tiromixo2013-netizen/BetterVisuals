package dev.ultrascale.mobile

import android.content.Context

enum class TechniqueRole { UPSCALE, RECONSTRUCTION, SHARPEN }

data class UpscaleTechnique(
    val id: String,
    val title: String,
    val role: TechniqueRole,
    val assetName: String,
    val minimumGles: String,
    val license: String,
    val note: String
)

/**
 * Bundled shader source catalog.
 *
 * These are source assets only. They are not compiled or applied to third-party
 * games by this catalog. A compatible render pass, source image, output target,
 * synchronization and per-technique parameter mapping are still required.
 */
object UpscaleTechniqueCatalog {
    val all = listOf(
        UpscaleTechnique(
            id = "fsr_lite_6tap",
            title = "FSR Lite · 6-tap",
            role = TechniqueRole.UPSCALE,
            assetName = "fsr_lite_6tap.frag",
            minimumGles = "OpenGL ES 3.0",
            license = "Project custom shader",
            note = "Lightweight EASU-inspired reconstruction; not official AMD FSR."
        ),
        UpscaleTechnique(
            id = "mwsr",
            title = "MWSR",
            role = TechniqueRole.RECONSTRUCTION,
            assetName = "mwsr_up.frag",
            minimumGles = "OpenGL ES 3.0",
            license = "MIT",
            note = "MoonlightWeb Super Resolution shader; parameter mapping and integration pass required."
        ),
        UpscaleTechnique(
            id = "sgsr1_mobile",
            title = "SGSR1 Mobile",
            role = TechniqueRole.RECONSTRUCTION,
            assetName = "sgsr1_mobile.frag",
            minimumGles = "OpenGL ES 3.1",
            license = "BSD-3-Clause",
            note = "Qualcomm-derived mobile shader; GLES 3.1 shader compilation and viewport mapping required."
        ),
        UpscaleTechnique(
            id = "amd_cas",
            title = "AMD FidelityFX CAS",
            role = TechniqueRole.SHARPEN,
            assetName = "amd_cas_sharpen.frag",
            minimumGles = "OpenGL ES 3.0",
            license = "MIT",
            note = "Sharpen-only pass; does not increase render resolution."
        )
    )

    fun source(context: Context, technique: UpscaleTechnique): String =
        context.assets.open("upscalers/${technique.assetName}")
            .bufferedReader()
            .use { it.readText() }
}
