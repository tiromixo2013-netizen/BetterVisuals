package dev.ultrascale.mobile

import android.content.Context
import android.net.Uri
import java.security.MessageDigest
import java.util.zip.ZipInputStream

/**
 * Conservative APK injection gate.
 *
 * This stage verifies the selected archive and computes a stable fingerprint. It deliberately
 * refuses to patch/re-sign unless an exact, tested game/version/ABI adapter is registered.
 * APK resource insertion alone does not integrate a shader into a game's render pipeline.
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
        val entries = linkedSetOf<String>()
        var validZip = false
        context.contentResolver.openInputStream(uri)?.use { raw ->
            DigestInputStreamCompat(raw, digest).use { hashed ->
                ZipInputStream(hashed).use { zip ->
                    while (true) {
                        val entry = zip.nextEntry ?: break
                        if (!entry.isDirectory) {
                            entries += entry.name
                            // Consume each entry fully so CRC and decompression errors are surfaced.
                            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                            while (zip.read(buffer) != -1) Unit
                        }
                        zip.closeEntry()
                    }
                    validZip = entries.isNotEmpty()
                }
            }
        } ?: return Preflight(false, false, emptyList(), emptyList(), 0, "", false,
            "REJECTED", "The selected document could not be opened.")

        val dex = entries.filter { it.matches(Regex("classes(\\d+)?\\.dex")) }.sorted()
        val abis = entries.filter { it.startsWith("lib/") && it.endsWith(".so") }
            .mapNotNull { it.split('/').getOrNull(1) }.distinct().sorted()
        val manifest = "AndroidManifest.xml" in entries
        val signatureMetadata = entries.any {
            it.startsWith("META-INF/") && (it.endsWith(".RSA", true) ||
                it.endsWith(".DSA", true) || it.endsWith(".EC", true))
        }
        val hash = digest.digest().joinToString("") { "%02x".format(it) }

        val decision = when {
            !validZip || !manifest || dex.isEmpty() -> "REJECTED"
            else -> "ADAPTER_REQUIRED"
        }
        val detail = when (decision) {
            "REJECTED" -> "APK preflight failed: ZIP, AndroidManifest.xml or DEX payload is missing."
            else -> "Archive verified. SHA-256: $hash. No exact, tested game/version/ABI adapter is registered yet; patching and signing are blocked. The original APK remains unchanged."
        }
        return Preflight(validZip, manifest, dex, abis, entries.size, hash,
            signatureMetadata, decision, detail)
    }

    /** Adapter registration is intentionally explicit; never infer compatibility from engine hints. */
    fun adapterAvailable(packageName: String?, versionCode: Long?, abi: String?): Boolean = false

    private class DigestInputStreamCompat(
        input: java.io.InputStream,
        digest: MessageDigest
    ) : java.security.DigestInputStream(input, digest)
}
