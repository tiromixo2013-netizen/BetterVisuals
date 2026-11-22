package dev.ultrascale.mobile

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import org.json.JSONObject
import java.util.zip.ZipInputStream

/**
 * Imports a user-selected, already-built Geode module into a user-selected Geode mods folder.
 * The user chooses the actual folder through SAF; no private app directory is accessed.
 */
object GeodeModInstaller {
    data class Validation(val valid: Boolean, val modId: String?, val detail: String)
    data class InstallResult(val success: Boolean, val fileName: String, val detail: String)

    fun validate(context: Context, uri: Uri, requiredAbi: String = "arm64-v8a"): Validation {
        return runCatching {
            var manifestText: String? = null
            var hasRequiredAndroidBinary = false
            context.contentResolver.openInputStream(uri)?.use { input ->
                ZipInputStream(input).use { zip ->
                    while (true) {
                        val entry = zip.nextEntry ?: break
                        if (!entry.isDirectory) {
                            if (entry.name == "mod.json") {
                                manifestText = zip.readBytes().toString(Charsets.UTF_8)
                            }
                            val lower = entry.name.lowercase()
                            val expectedSuffix = if (requiredAbi == "arm64-v8a") ".android64.so" else ".android32.so"
                            if (lower.endsWith(expectedSuffix)) {
                                hasRequiredAndroidBinary = true
                            }
                        }
                        zip.closeEntry()
                    }
                }
            } ?: return Validation(false, null, "Could not open the selected .geode file.")
            val manifest = manifestText ?: return Validation(false, null, "This archive does not contain mod.json.")
            val json = JSONObject(manifest)
            val modId = json.optString("id").takeIf { it.isNotBlank() }
                ?: return Validation(false, null, "The Geode manifest has no mod ID.")
            if (modId != "imes.bettervisuals") {
                return Validation(false, modId, "Expected imes.bettervisuals, found $modId.")
            }
            if (!hasRequiredAndroidBinary) {
                val expectedSuffix = if (requiredAbi == "arm64-v8a") ".android64.so" else ".android32.so"
                return Validation(false, modId, "No $expectedSuffix module was found. Choose the Geode artifact built for $requiredAbi.")
            }
            Validation(true, modId, "Verified Geode mod $modId with an Android native module.")
        }.getOrElse {
            Validation(false, null, "Geode archive validation failed: ${it.message ?: it.javaClass.simpleName}")
        }
    }

    fun install(context: Context, moduleUri: Uri, modsTreeUri: Uri, requiredAbi: String = "arm64-v8a"): InstallResult {
        val validation = validate(context, moduleUri, requiredAbi)
        if (!validation.valid) return InstallResult(false, "", validation.detail)
        return runCatching {
            val resolver = context.contentResolver
            val folderId = DocumentsContract.getTreeDocumentId(modsTreeUri)
            val folderUri = DocumentsContract.buildDocumentUriUsingTree(modsTreeUri, folderId)
            val displayName = resolver.query(moduleUri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                ?.use { cursor ->
                    if (cursor.moveToFirst()) cursor.getString(0) else null
                }
            val targetName = (displayName ?: "imes.bettervisuals.geode")
                .substringAfterLast('/').takeIf { it.endsWith(".geode", true) }
                ?: "imes.bettervisuals.geode"
            val targetUri = DocumentsContract.createDocument(
                resolver, folderUri, "application/octet-stream", targetName
            ) ?: return InstallResult(false, targetName, "Could not create the module in the selected folder. Check that this is the Geode mods directory and that it is writable.")
            resolver.openInputStream(moduleUri)?.use { input ->
                resolver.openOutputStream(targetUri, "w")?.use { output ->
                    input.copyTo(output)
                } ?: error("Could not open the destination module file.")
            } ?: error("Could not reopen the selected module.")
            InstallResult(true, targetName, "Installed $targetName. Restart Geometry Dash through Geode to load the mod. This operation copied a Geode module; it did not patch or re-sign the game APK.")
        }.getOrElse {
            InstallResult(false, "", "Install failed: ${it.message ?: it.javaClass.simpleName}")
        }
    }
}
