package dev.ultrascale.mobile

import android.content.Context
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.os.Build
import java.io.File
import java.util.zip.ZipFile
import net.dongliu.apk.parser.ApkFile

enum class RenderApi { VULKAN, OPENGL_ES, UNKNOWN }
enum class GameKind { TWO_D_LIKELY, THREE_D_LIKELY, HYBRID_OR_UNKNOWN }
enum class LayerReadiness { ELIGIBLE_OPT_IN, ELIGIBLE_DEBUG, ELIGIBLE_ROOT, NOT_ELIGIBLE }

data class ApiReport(
    val engine: String,
    val gameKind: GameKind,
    val candidateApis: Set<RenderApi>,
    val confidence: String,
    val evidence: String,
    val vulkanLayer: LayerReadiness,
    val glesLayer: LayerReadiness,
    val targetAllowsVulkan: Boolean,
    val targetAllowsGles: Boolean,
    val versionName: String,
    val versionCode: Long,
    val targetSdk: Int,
    val nativeAbis: Set<String>,
    val nativeLibraryCount: Int,
    val manifestLayerOptIn: Boolean
)

object GraphicsApiScanner {
    @Suppress("DEPRECATION")
    fun inspect(context: Context, suppliedApp: ApplicationInfo): ApiReport {
        val pm = context.packageManager
        val app = runCatching {
            pm.getApplicationInfo(suppliedApp.packageName, PackageManager.GET_META_DATA)
        }.getOrDefault(suppliedApp)
        val packageInfo = runCatching {
            if (Build.VERSION.SDK_INT >= 33) pm.getPackageInfo(
                app.packageName, PackageManager.PackageInfoFlags.of(PackageManager.GET_META_DATA.toLong())
            ) else pm.getPackageInfo(app.packageName, PackageManager.GET_META_DATA)
        }.getOrNull()

        val libs = linkedSetOf<String>()
        var nativeLibraryEntries = 0
        val abis = linkedSetOf<String>()
        val paths = listOfNotNull(app.sourceDir) + (app.splitSourceDirs?.toList() ?: emptyList())
        paths.forEach { path ->
            runCatching {
                ZipFile(path).use { zip ->
                    val entries = zip.entries()
                    var count = 0
                    while (entries.hasMoreElements() && count++ < 12000) {
                        val name = entries.nextElement().name
                        if (name.startsWith("lib/") && name.endsWith(".so")) {
                            nativeLibraryEntries++
                            val parts = name.split('/')
                            if (parts.size >= 3) {
                                abis += parts[1]
                                libs += parts.last().lowercase()
                            }
                        }
                    }
                }
            }
        }

        // DEX signatures complement native-library names. Keep parsing bounded and
        // best-effort: oversized APKs or unusual DEX layouts fall back to native evidence.
        val dexEngine = paths.asSequence()
            .filter { runCatching { File(it).length() in 1..(128L * 1024L * 1024L) }.getOrDefault(false) }
            .take(4)
            .mapNotNull { path ->
                runCatching {
                    ApkFile(File(path)).use { apk ->
                        apk.dexClasses.asSequence().map { it.classType.lowercase() }.firstNotNullOfOrNull { type ->
                            when {
                                type.contains("com/unity3d/player/unityplayer") -> "Unity"
                                type.contains("com/epicgames/unreal/gameactivity") ||
                                    type.contains("com/epicgames/ue4/gameactivity") -> "Unreal"
                                type.contains("org/godotengine/godot/godot") -> "Godot"
                                type.contains("org/cocos2dx/lib/cocos2dxactivity") ||
                                    type.contains("org/cocos2dx/lib/cocos2dxhelper") -> "Cocos"
                                type.contains("com/badlogic/gdx/backends/android/androidapplication") -> "libGDX"
                                else -> null
                            }
                        }
                    }
                }.getOrNull()
            }
            .firstOrNull()

        val engine = when {
            libs.any { it.contains("sample_vulkan_probe") } -> "UltraScale Test Harness"
            libs.any { it.contains("libunity") } -> "Unity"
            libs.any { it.contains("libue4") || it.contains("unreal") } -> "Unreal"
            libs.any { it.contains("godot") } -> "Godot"
            libs.any { it.contains("cocos") } -> "Cocos"
            libs.any { it.contains("gdx") } -> "libGDX"
            dexEngine != null -> dexEngine
            else -> "Unknown"
        }
        val candidates = linkedSetOf<RenderApi>()
        if (libs.any { it.contains("vulkan") } || engine in setOf("Unity", "Unreal", "Godot")) candidates += RenderApi.VULKAN
        if (libs.any { it.contains("gles") || it.contains("egl") || it.contains("opengl") } ||
            engine in setOf("Unity", "Unreal", "Godot", "Cocos", "libGDX", "UltraScale Test Harness")) candidates += RenderApi.OPENGL_ES

        val kind = when (engine) {
            "Cocos" -> GameKind.TWO_D_LIKELY
            "Unreal" -> GameKind.THREE_D_LIKELY
            else -> GameKind.HYBRID_OR_UNKNOWN
        }
        val debuggable = app.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
        val optIn = app.targetSdkVersion >= Build.VERSION_CODES.R && runCatching {
            app.metaData?.getBoolean("com.android.graphics.injectLayers.enable", false) == true
        }.getOrDefault(false)
        // Android documents root-based external layers for userdebug/eng builds, not merely UID 0 on a production build.
        val rootAvailable = ShizukuBridge.uid() == 0 && Build.TYPE in setOf("userdebug", "eng")

        val vulkanReadiness = when {
            optIn -> LayerReadiness.ELIGIBLE_OPT_IN
            debuggable -> LayerReadiness.ELIGIBLE_DEBUG
            rootAvailable -> LayerReadiness.ELIGIBLE_ROOT
            else -> LayerReadiness.NOT_ELIGIBLE
        }
        // The manifest opt-in is the documented Vulkan path. It does not
        // grant a production app permission to load a local GLES layer.
        val glesReadiness = when {
            // Android's documented manifest opt-in applies to GLES layers as well as Vulkan.
            optIn -> LayerReadiness.ELIGIBLE_OPT_IN
            debuggable -> LayerReadiness.ELIGIBLE_DEBUG
            rootAvailable -> LayerReadiness.ELIGIBLE_ROOT
            else -> LayerReadiness.NOT_ELIGIBLE
        }
        val versionCode = if (Build.VERSION.SDK_INT >= 28) packageInfo?.longVersionCode ?: 0L
            else packageInfo?.versionCode?.toLong() ?: 0L

        return ApiReport(
            engine = engine,
            gameKind = kind,
            candidateApis = candidates,
            confidence = if (engine == "Unknown") "Low" else "Heuristic",
            evidence = "Static APK scan: $nativeLibraryEntries native-library entries across ${abis.size} ABI(s)." +
                (dexEngine?.let { " DEX class signature: $it." } ?: " No recognized DEX engine signature.") +
                " Candidate APIs are not proof of the active runtime API.",
            vulkanLayer = vulkanReadiness,
            glesLayer = glesReadiness,
            targetAllowsVulkan = optIn || debuggable || rootAvailable,
            targetAllowsGles = optIn || debuggable || rootAvailable,
            versionName = packageInfo?.versionName ?: "Unknown",
            versionCode = versionCode,
            targetSdk = app.targetSdkVersion,
            nativeAbis = abis,
            nativeLibraryCount = nativeLibraryEntries,
            manifestLayerOptIn = optIn
        )
    }
}
