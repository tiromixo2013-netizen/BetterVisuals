package dev.ultrascale.mobile

import android.content.Context
import android.net.Uri
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.security.DigestInputStream
import java.security.MessageDigest
import java.util.UUID
import java.util.zip.ZipFile

/**
 * Conservative APK injection gate.
 * It validates the archive and fingerprint, then refuses mutation unless an exact tested
 * game/version/ABI adapter exists. Adding assets alone does not integrate a game's renderer.
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
            var validZip = false
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
                validZip = entries.isNotEmpty()
            }
            val dex = entries.filter { it.matches(Regex("classes(\\d+)?\\.dex")) }.sorted()
            val abis = entries.filter { it.startsWith("lib/") && it.endsWith(".so") }
                .mapNotNull { it.split('/').getOrNull(1) }.distinct().sorted()
            val manifest = "AndroidManifest.xml" in entries
            val signatureMetadata = entries.any {
                it.startsWith("META-INF/") && (it.endsWith(".RSA", true) ||
                    it.endsWith(".DSA", true) || it.endsWith(".EC", true))
            }
            val decision = if (!validZip || !manifest || dex.isEmpty()) "REJECTED" else "ADAPTER_REQUIRED"
            val detail = if (decision == "REJECTED") {
                "APK preflight failed: ZIP, AndroidManifest.xml or DEX payload is missing."
            } else {
                "Archive verified. SHA-256: $hash. No exact tested game/version/ABI adapter is registered; patching and signing are blocked. The original APK remains unchanged."
            }
            return Preflight(validZip, manifest, dex, abis, entries.size, hash,
                signatureMetadata, decision, detail)
        } catch (error: Exception) {
            return rejected("Archive validation failed: ${error.message ?: error.javaClass.simpleName}")
        } finally {
            temp.delete()
        }
    }

    /** Exact adapter matching is mandatory; heuristics never authorize an APK patch. */
    fun adapterAvailable(packageName: String?, versionCode: Long?, abi: String?): Boolean = false

    private fun rejected(detail: String) = Preflight(
        false, false, emptyList(), emptyList(), 0, "", false, "REJECTED", detail
    )
}
