package dev.ultrascale.mobile

import android.content.Context
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.os.Build
import java.util.zip.ZipFile

enum class RenderApi { VULKAN, OPENGL_ES, UNKNOWN }
enum class GameKind { TWO_D_LIKELY, THREE_D_LIKELY, HYBRID_OR_UNKNOWN }
enum class LayerReadiness { APP_OPT_IN_REQUIRED, DEBUG_OR_PRIVILEGED_ONLY, NOT_CONFIRMED }

data class ApiReport(
    val engine: String,
    val gameKind: GameKind,
    val candidateApis: Set<RenderApi>,
    val confidence: String,
    val evidence: String,
    val vulkanLayer: LayerReadiness,
    val glesLayer: LayerReadiness
)

object GraphicsApiScanner {
    fun inspect(context: Context, app: ApplicationInfo): ApiReport {
        val libs = linkedSetOf<String>()
        val paths = listOf(app.sourceDir) + (app.splitSourceDirs?.toList() ?: emptyList())
        paths.forEach { path ->
            runCatching {
                ZipFile(path).use { zip ->
                    val entries = zip.entries()
                    var count = 0
                    while (entries.hasMoreElements() && count++ < 4000) {
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
        val apis = linkedSetOf<RenderApi>()
        if (libs.any { it.contains("vulkan") } || engine in setOf("Unity", "Unreal", "Godot")) apis += RenderApi.VULKAN
        if (libs.any { it.contains("gles") || it.contains("egl") || it.contains("opengl") } ||
            engine in setOf("Unity", "Unreal", "Godot", "Cocos", "libGDX")) apis += RenderApi.OPENGL_ES
        val gameKind = when (engine) {
            "Cocos" -> GameKind.TWO_D_LIKELY
            "Unreal" -> GameKind.THREE_D_LIKELY
            else -> GameKind.HYBRID_OR_UNKNOWN
        }
        val debuggable = app.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
        val privilegedBuild = Build.TYPE == "userdebug" || Build.TYPE == "eng"
        val layer = if (debuggable || privilegedBuild) LayerReadiness.DEBUG_OR_PRIVILEGED_ONLY else LayerReadiness.APP_OPT_IN_REQUIRED
        return ApiReport(
            engine, gameKind, apis, if (engine == "Unknown") "Low" else "Heuristic",
            "Static APK native-library signature only; not proof of active runtime API",
            layer, layer
        )
    }
}
