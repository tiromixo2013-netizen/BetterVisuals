package dev.ultrascale.mobile

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import org.json.JSONObject

/** Safe file adapter: validates and copies user-supplied JSON without inventing game settings. */
object Asphalt8ConfigAdapter {
    val candidateNames = listOf("gameoptions.json", "gamesettings.json")
    data class ApplyResult(val targetName: String, val backupName: String?, val bytesWritten: Long)

    fun validate(context: Context, source: Uri): Result<Int> = runCatching {
        val bytes = context.contentResolver.openInputStream(source)?.use { it.readBytes() }
            ?: error("Cannot open selected JSON file")
        require(bytes.isNotEmpty()) { "The JSON file is empty" }
        require(bytes.size <= 2 * 1024 * 1024) { "JSON file is larger than 2 MB" }
        val json = JSONObject(bytes.toString(Charsets.UTF_8))
        require(json.length() > 0) { "JSON object has no settings" }
        bytes.size
    }

    fun apply(context: Context, source: Uri, folderTree: Uri, targetName: String): Result<ApplyResult> = runCatching {
        require(targetName in candidateNames) { "Unsupported target filename" }
        val bytes = context.contentResolver.openInputStream(source)?.use { it.readBytes() }
            ?: error("Cannot read imported config")
        require(bytes.isNotEmpty() && bytes.size <= 2 * 1024 * 1024) { "Config size is invalid" }
        JSONObject(bytes.toString(Charsets.UTF_8))
        val folder = DocumentFile.fromTreeUri(context, folderTree) ?: error("Selected folder is unavailable")
        require(folder.isDirectory && folder.canWrite()) { "Selected folder is not writable" }
        val existing = folder.findFile(targetName)
        var backupName: String? = null
        if (existing != null && existing.isFile) {
            val backup = folder.createFile("application/json", "$targetName.ultrascale-backup.json")
                ?: error("Could not create backup file")
            context.contentResolver.openInputStream(existing.uri)?.use { input ->
                context.contentResolver.openOutputStream(backup.uri, "wt")?.use { output -> input.copyTo(output) }
                    ?: error("Could not write backup")
            } ?: error("Could not read existing game config")
            backupName = backup.name
            existing.delete()
        }
        val destination = folder.createFile("application/json", targetName) ?: error("Could not create destination config")
        context.contentResolver.openOutputStream(destination.uri, "wt")?.use { it.write(bytes); it.flush() }
            ?: error("Could not write destination config")
        ApplyResult(targetName, backupName, bytes.size.toLong())
    }

    fun restoreBackup(context: Context, folderTree: Uri, targetName: String): Result<String> = runCatching {
        require(targetName in candidateNames) { "Unsupported target filename" }
        val folder = DocumentFile.fromTreeUri(context, folderTree) ?: error("Selected folder is unavailable")
        val backup = folder.findFile("$targetName.ultrascale-backup.json") ?: error("No UltraScale backup found")
        val bytes = context.contentResolver.openInputStream(backup.uri)?.use { it.readBytes() }
            ?: error("Cannot read backup")
        JSONObject(bytes.toString(Charsets.UTF_8))
        folder.findFile(targetName)?.delete()
        val restored = folder.createFile("application/json", targetName) ?: error("Could not recreate original config")
        context.contentResolver.openOutputStream(restored.uri, "wt")?.use { it.write(bytes); it.flush() }
            ?: error("Could not restore config")
        "Restored $targetName from ${backup.name}"
    }
}