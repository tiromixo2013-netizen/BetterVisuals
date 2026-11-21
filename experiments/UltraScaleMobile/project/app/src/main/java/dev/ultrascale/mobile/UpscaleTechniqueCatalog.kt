package dev.ultrascale.mobile

import android.content.Context

enum class TechniqueRole { UPSCALE, RECONSTRUCTION, TEMPORAL_REFERENCE, SHARPEN }

data class UpscaleTechnique(
    val id: String,
    val title: String,
    val role: TechniqueRole,
    val assetName: String,
    val graphicsProfile: String,
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
            id = "sgsr2_2pass_fs",
            title = "Qualcomm SGSR2 · 2-pass fragment",
            role = TechniqueRole.TEMPORAL_REFERENCE,
            assetName = "sgsr2_2pass_upscale.frag",
            graphicsProfile = "Vulkan GLSL ES 3.20",
            license = "BSD-3-Clause",
            note = "Official temporal shader set optimized for Adreno. Convert/upscale/vertex sources are bundled; requires depth, motion vectors, jitter, camera transforms and ping-pong history. Reference only, not enabled in the sample renderer."
        ),
        UpscaleTechnique(
            id = "amd_fsr1_easu",
            title = "AMD FidelityFX FSR1 · EASU",
            role = TechniqueRole.UPSCALE,
            assetName = "amd_fsr1_easu.frag",
            graphicsProfile = "OpenGL ES 2.0+",
            license = "MIT",
            note = "Full EASU spatial reconstruction source; higher sampling cost than the lightweight 6-tap fallback."
        ),
        UpscaleTechnique(
            id = "fsr_lite_6tap",
            title = "FSR Lite · 6-tap",
            role = TechniqueRole.UPSCALE,
            assetName = "fsr_lite_6tap.frag",
            graphicsProfile = "OpenGL ES 3.0",
            license = "Project custom shader",
            note = "Lightweight EASU-inspired reconstruction; not official AMD FSR."
        ),
        UpscaleTechnique(
            id = "mwsr",
            title = "MWSR",
            role = TechniqueRole.RECONSTRUCTION,
            assetName = "mwsr_up.frag",
            graphicsProfile = "OpenGL ES 3.0",
            license = "MIT",
            note = "MoonlightWeb Super Resolution shader; parameter mapping and integration pass required."
        ),
        UpscaleTechnique(
            id = "sgsr1_mobile",
            title = "SGSR1 Mobile",
            role = TechniqueRole.RECONSTRUCTION,
            assetName = "sgsr1_mobile.frag",
            graphicsProfile = "OpenGL ES 3.1",
            license = "BSD-3-Clause",
            note = "Qualcomm-derived mobile shader; GLES 3.1 shader compilation and viewport mapping required."
        ),
        UpscaleTechnique(
            id = "amd_cas",
            title = "AMD FidelityFX CAS",
            role = TechniqueRole.SHARPEN,
            assetName = "amd_cas_sharpen.frag",
            graphicsProfile = "OpenGL ES 3.0",
            license = "MIT",
            note = "Sharpen-only pass; does not increase render resolution."
        )
    )

    fun source(context: Context, technique: UpscaleTechnique): String =
        context.assets.open("upscalers/${technique.assetName}")
            .bufferedReader()
            .use { it.readText() }
}
