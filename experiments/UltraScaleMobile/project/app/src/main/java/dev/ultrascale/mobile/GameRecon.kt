package dev.ultrascale.mobile

import android.content.Context
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import java.io.File
import java.util.zip.ZipInputStream

/**
 * Read-only game reconnaissance. It inspects installed APK metadata and APK entries only.
 * It never opens /data/user/0/<package>, app databases, process memory, or protected OBB data.
 */
object GameRecon {
    data class Report(
        val label: String,
        val packageName: String?,
        val versionName: String?,
        val versionCode: Long?,
        val installer: String?,
        val installKind: String,
        val apkFiles: List<String>,
        val nativeAbis: List<String>,
        val graphicsLibraries: List<String>,
        val engine: String,
        val assetHints: List<String>,
        val confidenceNotes: List<String>,
        val entryCount: Int = 0,
        val hasManifest: Boolean = false,
        val dexFiles: List<String> = emptyList(),
        val topLevelFolders: List<String> = emptyList(),
        val apkReadable: Boolean = true
    )

    private val targetHints = listOf(
        "com.robtopx.geometryjump" to "Geometry Dash",
        "com.vectorunit.purple.googleplay" to "Beach Buggy Racing",
        "com.gameloft.android.ANMP.GloftA8HM" to "Asphalt 8"
    )

    /** Enumerates the three requested titles when installed; also returns other game-like packages. */
    fun scanInstalled(context: Context): List<Report> {
        val pm = context.packageManager
        val installed = if (Build.VERSION.SDK_INT >= 33) {
            pm.getInstalledApplications(PackageManager.ApplicationInfoFlags.of(0L))
        } else {
            @Suppress("DEPRECATION")
            pm.getInstalledApplications(0)
        }
        return installed.mapNotNull { app ->
            val label = runCatching { pm.getApplicationLabel(app).toString() }
                .getOrDefault(app.packageName)
            val known = targetHints.any { (pkg, title) ->
                app.packageName.equals(pkg, true) || label.contains(title, true)
            }
            val looksLikeGame = (app.flags and ApplicationInfo.FLAG_IS_GAME) != 0
            if (!known && !looksLikeGame) return@mapNotNull null
            inspectInstalled(context, app, label)
        }.sortedBy { report ->
            targetHints.indexOfFirst { (pkg, title) ->
                report.packageName == pkg || report.label.contains(title, true)
            }.let { if (it < 0) Int.MAX_VALUE else it }
        }
    }

    fun inspectInstalled(context: Context, app: ApplicationInfo, label: String? = null): Report {
        val pm = context.packageManager
        val pkg = app.packageName
        val info = runCatching {
            if (Build.VERSION.SDK_INT >= 33) pm.getPackageInfo(
                pkg, PackageManager.PackageInfoFlags.of(0L)
            ) else {
                @Suppress("DEPRECATION")
                pm.getPackageInfo(pkg, 0)
            }
        }.getOrNull()
        val apkPaths = buildList {
            app.sourceDir?.let(::add)
            app.splitSourceDirs?.forEach(::add)
        }.distinct()
        val entries = apkPaths.flatMap { path -> readApkEntries(File(path)) }.toSet()
        val nativeEntries = entries.filter { it.startsWith("lib/") && it.endsWith(".so") }.sorted()
        val abis = nativeEntries.mapNotNull { it.split('/').getOrNull(1) }.distinct().sorted()
        val graphics = nativeEntries.filter {
            val n = it.substringAfterLast('/').lowercase()
            listOf("libunity", "unreal", "vulkan", "gles", "cocos", "godot", "il2cpp").any(n::contains)
        }
        val engine = detectEngine(entries, nativeEntries)
        val installer = runCatching {
            if (Build.VERSION.SDK_INT >= 30) pm.getInstallSourceInfo(pkg).installingPackageName
            else {
                @Suppress("DEPRECATION")
                pm.getInstallerPackageName(pkg)
            }
        }.getOrNull()
        val installKind = when {
            installer == "com.android.vending" -> "Google Play"
            installer.isNullOrBlank() -> "Sideloaded / installer unknown"
            else -> "Other installer: $installer"
        }
        val assetHints = entries.filter { path ->
            val p = path.lowercase()
            p.startsWith("assets/") && (
                p.contains("config") || p.contains("quality") || p.contains("graphics") ||
                    p.endsWith(".ini") || p.endsWith(".json") || p.endsWith(".xml")
                )
        }.take(80).sorted()
        return Report(
            label = label ?: pkg,
            packageName = pkg,
            versionName = info?.versionName,
            versionCode = info?.let { if (Build.VERSION.SDK_INT >= 28) it.longVersionCode else @Suppress("DEPRECATION") it.versionCode.toLong() },
            installer = installer,
            installKind = installKind,
            apkFiles = apkPaths,
            nativeAbis = abis,
            graphicsLibraries = graphics,
            engine = engine,
            assetHints = assetHints,
            confidenceNotes = listOf(
                "APK metadata and public APK entries only.",
                "No private app data, runtime memory, anti-cheat, or DRM access was attempted."
            )
        )
    }

    /** Inspects a user-selected APK via Android's Storage Access Framework. */
    fun inspectSelectedApk(context: Context, uri: Uri, displayName: String): Report {
        val entries = context.contentResolver.openInputStream(uri)?.use { input ->
            ZipInputStream(input).use { zip ->
                buildSet {
                    while (true) {
                        val entry = zip.nextEntry ?: break
                        if (!entry.isDirectory) add(entry.name)
                        zip.closeEntry()
                    }
                }
            }
        }.orEmpty()
        val nativeEntries = entries.filter { it.startsWith("lib/") && it.endsWith(".so") }.sorted()
        val abis = nativeEntries.mapNotNull { it.split('/').getOrNull(1) }.distinct().sorted()
        val graphics = nativeEntries.filter {
            val n = it.substringAfterLast('/').lowercase()
            listOf("libunity", "unreal", "vulkan", "gles", "cocos", "godot", "il2cpp").any(n::contains)
        }
        return Report(
            label = displayName,
            packageName = null,
            versionName = null,
            versionCode = null,
            installer = null,
            installKind = "User-selected APK file",
            apkFiles = listOf(uri.toString()),
            nativeAbis = abis,
            graphicsLibraries = graphics,
            engine = detectEngine(entries, nativeEntries),
            assetHints = entries.filter {
                val p = it.lowercase()
                p.startsWith("assets/") && (p.contains("config") || p.contains("quality") ||
                    p.contains("graphics") || p.endsWith(".ini") || p.endsWith(".json"))
            }.take(80).sorted(),
            confidenceNotes = listOf(
                "Read-only scan of the APK selected by the user.",
                "A base APK may not contain all resources or native libraries; inspect matching split APK files too."
            ),
            entryCount = entries.size,
            hasManifest = "AndroidManifest.xml" in entries,
            dexFiles = entries.filter { it.matches(Regex("classes(\\d+)?\\.dex")) }.sorted(),
            topLevelFolders = entries.map { it.substringBefore('/') }.distinct().sorted().take(30),
            apkReadable = entries.isNotEmpty() && "AndroidManifest.xml" in entries
        )
    }

    private fun readApkEntries(file: File): List<String> {
        if (!file.isFile || !file.canRead()) return emptyList()
        return runCatching {
            java.util.zip.ZipFile(file).use { zip ->
                val names = zip.entries()
                buildList { while (names.hasMoreElements()) add(names.nextElement().name) }
            }
        }.getOrDefault(emptyList())
    }

    private fun detectEngine(entries: Set<String>, native: List<String>): String {
        val all = (entries + native).joinToString("\n").lowercase()
        return when {
            "libil2cpp.so" in all || "globalgamemanagers" in all || "unityplayer.dll" in all -> "Unity (high-confidence signature)"
            "libue4.so" in all || "libunreal.so" in all || "ue4commandline.txt" in all -> "Unreal Engine (signature)"
            "libcocos2d" in all || "cocos2d" in all -> "Cocos2d family (signature)"
            "libgodot" in all || "godot" in all -> "Godot (signature)"
            "librobtop" in all -> "Custom/native engine; Geometry Dash-style signature"
            else -> "Unknown / needs deeper permitted analysis"
        }
    }
}
