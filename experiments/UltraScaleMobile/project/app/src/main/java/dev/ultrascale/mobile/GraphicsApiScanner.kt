package dev.ultrascale.mobile

import android.content.Context
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.os.Build
import java.util.zip.ZipFile

enum class RenderApi { VULKAN, OPENGL_ES, UNKNOWN }
enum class GameKind { TWO_D_LIKELY, THREE_D_LIKELY, HYBRID_OR_UNKNOWN }
enum class LayerReadiness { ELIGIBLE_OPT_IN, ELIGIBLE_DEBUG, APP_OPT_IN_REQUIRED }

data class ApiReport(
    val engine: String,
    val gameKind: GameKind,
    val candidateApis: Set<RenderApi>,
    val confidence: String,
    val evidence: String,
    val vulkanLayer: LayerReadiness,
    val glesLayer: LayerReadiness,
    val targetAllowsLayers: Boolean
)

object GraphicsApiScanner {
    @Suppress("DEPRECATION")
    fun inspect(context: Context, suppliedApp: ApplicationInfo): ApiReport {
        val app = runCatching {
            context.packageManager.getApplicationInfo(suppliedApp.packageName, PackageManager.GET_META_DATA)
        }.getOrDefault(suppliedApp)
        val libs = linkedSetOf<String>()
        val paths = listOf(app.sourceDir) + (app.splitSourceDirs?.toList() ?: emptyList())
        paths.forEach { path ->
            runCatching {
                ZipFile(path).use { zip ->
                    val entries = zip.entries()
                    var count = 0
                    while (entries.hasMoreElements() && count++ < 6000) {
                        val name = entries.nextElement().name.lowercase()
                        if (name.endsWith(".so")) libs += name.substringAfterLast('/')
                    }
                }
            }
        }
        val engine = when {
            libs.any { it.contains("libunity") } -> "Unity"
            libs.any { it.contains("libue4") || it.contains("unreal") } -> "Unreal"
            libs.any { it.contains("godot") } -> "Godot"
            libs.any { it.contains("cocos") } -> "Cocos"
            libs.any { it.contains("gdx") } -> "libGDX"
            else -> "Unknown"
        }
        val candidates = linkedSetOf<RenderApi>()
        if (libs.any { it.contains("vulkan") } || engine in setOf("Unity", "Unreal", "Godot")) candidates += RenderApi.VULKAN
        if (libs.any { it.contains("gles") || it.contains("egl") || it.contains("opengl") } ||
            engine in setOf("Unity", "Unreal", "Godot", "Cocos", "libGDX")) candidates += RenderApi.OPENGL_ES
        val kind = when (engine) {
            "Cocos" -> GameKind.TWO_D_LIKELY
            "Unreal" -> GameKind.THREE_D_LIKELY
            else -> GameKind.HYBRID_OR_UNKNOWN
        }
        val debuggable = app.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
        val optIn = runCatching {
            app.metaData?.getBoolean("com.android.graphics.injectLayers.enable", false) == true
        }.getOrDefault(false)
        val privilegedBuild = Build.TYPE == "userdebug" || Build.TYPE == "eng"
        val readiness = when {
            optIn -> LayerReadiness.ELIGIBLE_OPT_IN
            debuggable || privilegedBuild -> LayerReadiness.ELIGIBLE_DEBUG
            else -> LayerReadiness.APP_OPT_IN_REQUIRED
        }
        return ApiReport(
            engine, kind, candidates,
            if (engine == "Unknown") "Low" else "Heuristic",
            "APK native-library signatures only; candidate APIs, not proof of the active runtime API",
            readiness, readiness, optIn || debuggable || privilegedBuild
        )
    }
}
