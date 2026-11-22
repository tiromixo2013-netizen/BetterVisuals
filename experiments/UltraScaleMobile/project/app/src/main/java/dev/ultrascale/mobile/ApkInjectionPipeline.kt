package dev.ultrascale.mobile

import android.content.Context
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import java.io.File
import java.io.FileOutputStream
import java.security.DigestInputStream
import java.security.MessageDigest
import java.util.UUID
import java.util.zip.ZipFile

/**
 * Conservative APK injection gate.
 *
 * This stage validates the selected archive and extracts its real package identity. It only
 * authorizes mutation when an exact, tested game/version/ABI adapter is registered. Merely
 * inserting shader assets does not connect them to a game's rendering pipeline.
 */
object ApkInjectionPipeline {
    data class Preflight(
        val validZip: Boolean,
        val hasManifest: Boolean,
        val dexFiles: List<String>,
        val nativeAbis: List<String>,
        val entryCount: Int,
        val sha256: String,
        val hasV1SignatureMetadata: Boolean,
        val packageName: String?,
        val versionName: String?,
        val versionCode: Long?,
        val decision: String,
        val detail: String
    )

    suspend fun inspect(context: Context, uri: Uri): Preflight {
        val digest = MessageDigest.getInstance("SHA-256")
        val temp = File(context.cacheDir, "ultrascale-preflight-${UUID.randomUUID()}.apk")
        try {
            val input = context.contentResolver.openInputStream(uri)
                ?: return rejected("The selected document could not be opened.")
            input.use { source ->
                DigestInputStream(source, digest).use { hashed ->
                    FileOutputStream(temp).use { output -> hashed.copyTo(output) }
                }
            }
            val hash = digest.digest().joinToString("") { "%02x".format(it) }
            val entries = linkedSetOf<String>()
            ZipFile(temp).use { zip ->
                val enumeration = zip.entries()
                while (enumeration.hasMoreElements()) {
                    val entry = enumeration.nextElement()
                    if (!entry.isDirectory) {
                        entries += entry.name
                        zip.getInputStream(entry).use { stream ->
                            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                            while (stream.read(buffer) != -1) Unit
                        }
                    }
                }
            }
            val dex = entries.filter { it.matches(Regex("classes(\\d+)?\\.dex")) }.sorted()
            val abis = entries.filter { it.startsWith("lib/") && it.endsWith(".so") }
                .mapNotNull { it.split('/').getOrNull(1) }.distinct().sorted()
            val manifest = "AndroidManifest.xml" in entries
            val signatureMetadata = entries.any {
                it.startsWith("META-INF/") && (it.endsWith(".RSA", true) ||
                    it.endsWith(".DSA", true) || it.endsWith(".EC", true))
            }
            val packageInfo = if (manifest) runCatching {
                if (Build.VERSION.SDK_INT >= 33) {
                    context.packageManager.getPackageArchiveInfo(
                        temp.absolutePath, PackageManager.PackageInfoFlags.of(0L)
                    )
                } else {
                    @Suppress("DEPRECATION")
                    context.packageManager.getPackageArchiveInfo(temp.absolutePath, 0)
                }
            }.getOrNull() else null
            val packageName = packageInfo?.packageName
            val versionName = packageInfo?.versionName
            val versionCode = packageInfo?.let {
                if (Build.VERSION.SDK_INT >= 28) it.longVersionCode else {
                    @Suppress("DEPRECATION")
                    it.versionCode.toLong()
                }
            }
            val valid = entries.isNotEmpty() && manifest && dex.isNotEmpty() && packageName != null
            val decision = if (valid) "ADAPTER_REQUIRED" else "REJECTED"
            val detail = if (!valid) {
                "APK preflight failed: ZIP, binary manifest, DEX or package identity is missing/unreadable."
            } else {
                "APK identity verified: $packageName v${versionName ?: "?"} ($versionCode). SHA-256: $hash. No exact tested adapter is registered for this package/version/ABI; patching and signing remain blocked."
            }
            return Preflight(entries.isNotEmpty(), manifest, dex, abis, entries.size, hash,
                signatureMetadata, packageName, versionName, versionCode, decision, detail)
        } catch (error: Exception) {
            return rejected("Archive validation failed: ${error.message ?: error.javaClass.simpleName}")
        } finally {
            temp.delete()
        }
    }

    private fun rejected(detail: String) = Preflight(
        false, false, emptyList(), emptyList(), 0, "", false,
        null, null, null, "REJECTED", detail
    )
}
