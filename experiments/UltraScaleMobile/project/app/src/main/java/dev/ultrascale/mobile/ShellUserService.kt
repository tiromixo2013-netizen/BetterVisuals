package dev.ultrascale.mobile

import android.os.Parcel
import android.os.RemoteException
import java.io.File
import java.io.FileOutputStream
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.util.regex.Pattern
import org.json.JSONObject

class ShellUserService : ICommandService.Stub() {
    override fun readLayerTelemetry(targetPackage: String): String {
        val packagePattern = Pattern.compile("[A-Za-z0-9_.]+")
        require(packagePattern.matcher(targetPackage).matches()) { "Invalid target package" }

        val pidProcess = ProcessBuilder("pidof", targetPackage).redirectErrorStream(true).start()
        val pidOutput = pidProcess.inputStream.bufferedReader().readText().trim()
        val pidExit = pidProcess.waitFor()
        if (pidExit != 0 || pidOutput.isBlank()) {
            return "Target process is not running. Launch the game, enter gameplay, then check again."
        }
        val pid = pidOutput.split(Regex("\\s+")).firstOrNull { it.matches(Regex("\\d+")) }
            ?: return "Could not resolve the target process ID."

        val logProcess = ProcessBuilder(
            "logcat", "-d", "--pid", pid, "-s",
            "UltraScaleGLES:I", "UltraScaleVK:I"
        ).redirectErrorStream(true).start()
        val output = logProcess.inputStream.bufferedReader().use { it.readText() }
        val exit = logProcess.waitFor()
        check(exit == 0) { output.takeLast(3000).ifBlank { "Unable to read target process logcat." } }
        val relevant = output.lineSequence()
            .filter { it.contains("UltraScaleGLES") || it.contains("UltraScaleVK") }
            .toList()
        if (relevant.isEmpty()) {
            return "No UltraScale layer telemetry found for PID $pid. This does not prove failure: the target may not be layer-eligible, may use another graphics API, or may not have reached a logged frame yet."
        }
        val loaded = relevant.any {
            it.contains("LayerLoader attached", ignoreCase = true) ||
                it.contains("Vulkan layer attached", ignoreCase = true)
        }
        return (if (loaded) "LAYER LOAD CONFIRMED for PID $pid. " else "Layer log activity found for PID $pid, but loader attachment is not confirmed. ") +
            relevant.takeLast(20).joinToString("\n").takeLast(10000)
    }

    override fun configureLayer(targetPackage: String, layerPackage: String, api: String, enable: Boolean): String {
        val packagePattern = Pattern.compile("[A-Za-z0-9_.]+")
        require(packagePattern.matcher(targetPackage).matches()) { "Invalid target package" }
        require(packagePattern.matcher(layerPackage).matches()) { "Invalid layer package" }
        require(api == "vulkan" || api == "gles") { "Unsupported graphics API" }

        val targetNow = readSetting("gpu_debug_app")
        val layerPackagesNow = readSetting("gpu_debug_layer_app").split(':').filter { it.isNotBlank() }.toMutableSet()
        val layerKey = if (api == "vulkan") "gpu_debug_layers" else "gpu_debug_layers_gles"
        val layerName = if (api == "vulkan") "VK_LAYER_ULTRASCALE_api_lab" else "libGLES_ultrascale.so"

        if (enable) {
            require(targetNow.isBlank() || targetNow == targetPackage) {
                "Android supports one per-app GPU debug target at a time ($targetNow). Disable that target first."
            }
            val existingLayers = readSetting(layerKey).split(':').filter { it.isNotBlank() }.toMutableSet()
            existingLayers += layerName
            layerPackagesNow += layerPackage
            // Stage the complete layer configuration before switching it on. This avoids
            // exposing a partially-written setup if the target process starts concurrently.
            writeSettingsTransaction(
                mapOf(
                    "gpu_debug_layer_app" to layerPackagesNow.joinToString(":"),
                    layerKey to existingLayers.joinToString(":"),
                    "gpu_debug_app" to targetPackage,
                    "enable_gpu_debug_layers" to "1"
                )
            )
            check(readSetting("gpu_debug_app") == targetPackage) { "Android did not retain the selected debug target." }
            check(readSetting("enable_gpu_debug_layers") == "1") { "Android did not enable GPU debug layers." }
            check(readSetting("gpu_debug_layer_app").split(':').contains(layerPackage)) { "Android did not retain the UltraScale layer package." }
            check(readSetting(layerKey).split(':').contains(layerName)) { "Android did not retain the requested $api layer name." }
            return "Verified Android $api debug-layer settings for $targetPackage. Fully stop and relaunch the game; actual loading still depends on Android and target eligibility."
        }

        require(targetNow == targetPackage) {
            "The active GPU debug target is '$targetNow', not '$targetPackage'; refusing to change another app's settings."
        }
        val existingLayers = readSetting(layerKey).split(':').filter { it.isNotBlank() }.toMutableSet()
        existingLayers.remove(layerName)
        val changes = linkedMapOf<String, String?>()
        changes[layerKey] = existingLayers.takeIf { it.isNotEmpty() }?.joinToString(":")
        // Keep the layer-package search list intact: it may also host unrelated layers.
        // Disable the global switch only when both Vulkan and GLES layer lists are empty.
        val anyVulkanLayers = if (api == "vulkan") existingLayers.isNotEmpty()
            else readSetting("gpu_debug_layers").isNotBlank()
        val anyGlesLayers = if (api == "gles") existingLayers.isNotEmpty()
            else readSetting("gpu_debug_layers_gles").isNotBlank()
        if (!anyVulkanLayers && !anyGlesLayers) {
            changes["enable_gpu_debug_layers"] = null
            changes["gpu_debug_app"] = null
        }
        writeSettingsTransaction(changes)
        check(!readSetting(layerKey).split(':').contains(layerName)) { "Android still reports the UltraScale $api layer as enabled." }
        if (!anyVulkanLayers && !anyGlesLayers) {
            check(readSetting("gpu_debug_app").isBlank()) { "Android still has a GPU debug target configured." }
            check(readSetting("enable_gpu_debug_layers").isBlank()) { "Android GPU debug layers are still enabled." }
        }
        return "Verified UltraScale $api layer disabled for $targetPackage; unrelated layer names and package search entries were preserved. Restart the game."

    }

    override fun readAsphalt8Config(): String {
        requireAsphalt8Stopped()
        val directory = File(
            "/storage/emulated/0/Android/data/com.gameloft.android.ANMP.GloftA8HM/files"
        ).canonicalFile
        require(directory.isDirectory && directory.canRead()) {
            "Asphalt 8 files folder is unavailable or cannot be read through Shizuku."
        }
        val destination = File(directory, "gameoptions.json").canonicalFile
        require(destination.parentFile == directory && destination.isFile) {
            "No installed gameoptions.json was found in Asphalt 8's files folder."
        }
        require(destination.length() in 1..(512L * 1024L)) {
            "Installed gameoptions.json is empty or exceeds the 512 KiB safety limit."
        }
        val json = destination.readText(Charsets.UTF_8)
        val root = JSONObject(json)
        val profiles = root.optJSONObject("profiles")
        require(root.optJSONObject("header") != null &&
            profiles?.optJSONObject("GPU") != null &&
            profiles.optJSONObject("Specific") != null) {
            "The installed file does not match the supported Asphalt 8 gameoptions schema."
        }
        return json
    }

    override fun deployAsphalt8Config(json: String): String {
        val bytes = json.toByteArray(Charsets.UTF_8)
        require(bytes.isNotEmpty() && bytes.size <= 512 * 1024) {
            "The config must be between 1 byte and 512 KiB."
        }
        val root = JSONObject(json)
        require(!root.has("ultrascale")) {
            "UltraScale renderer settings are not part of Asphalt 8's native gameoptions schema. Keep them in a separate UltraScale profile; no game file was changed."
        }
        require(root.optJSONObject("header") != null &&
            root.optJSONObject("profiles")?.has("GPU") == true &&
            root.optJSONObject("profiles")?.has("Specific") == true) {
            "The selected JSON is not a supported Asphalt 8 gameoptions.json file."
        }
        requireAsphalt8Stopped()

        val directory = File(
            "/storage/emulated/0/Android/data/com.gameloft.android.ANMP.GloftA8HM/files"
        ).canonicalFile
        require(directory.isDirectory) {
            "Asphalt 8 files folder was not found. Install the game and open it once before deploying."
        }
        require(directory.canWrite()) {
            "Shizuku shell cannot write to Android/data on this device. No file was changed."
        }

        val destination = File(directory, "gameoptions.json").canonicalFile
        require(destination.parentFile == directory) { "Invalid Asphalt 8 config destination." }
        val temporary = File(directory, ".gameoptions.json.ultrascale.tmp")
        val originMarker = File(directory, ".ultrascale-asphalt8-config-origin")
        val previousOrigin = if (originMarker.isFile) originMarker.readText().trim() else null
        require(
            previousOrigin == null || previousOrigin == "CREATED" ||
                Regex("""^gameoptions\.json\.ultrascale-\d+\.bak$""").matches(previousOrigin)
        ) { "The existing UltraScale backup marker is invalid; refusing to overwrite the game config." }
        if (previousOrigin != null && previousOrigin != "CREATED") {
            val recordedBackup = File(directory, previousOrigin).canonicalFile
            require(recordedBackup.parentFile == directory && recordedBackup.isFile) {
                "The original backup recorded by UltraScale is missing; refusing to replace the current game config."
            }
        }
        val backup = if (previousOrigin == null && destination.isFile) {
            File(directory, "gameoptions.json.ultrascale-${System.currentTimeMillis()}.bak").also {
                Files.copy(destination.toPath(), it.toPath(), StandardCopyOption.COPY_ATTRIBUTES)
            }
        } else null
        val originValue = previousOrigin ?: backup?.name ?: "CREATED"
        FileOutputStream(originMarker, false).use { marker ->
            marker.write(originValue.toByteArray(Charsets.UTF_8))
            marker.flush()
            marker.fd.sync()
        }

        try {
            FileOutputStream(temporary, false).use { output ->
                output.write(bytes)
                output.flush()
                output.fd.sync()
            }
            try {
                Files.move(
                    temporary.toPath(),
                    destination.toPath(),
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING
                )
            } catch (_: AtomicMoveNotSupportedException) {
                Files.move(
                    temporary.toPath(),
                    destination.toPath(),
                    StandardCopyOption.REPLACE_EXISTING
                )
            }
            check(destination.isFile && destination.length() == bytes.size.toLong()) {
                "The written config failed its size verification."
            }
        } finally {
            temporary.delete()
        }

        return "Wrote gameoptions.json to the Asphalt 8 files folder and verified its size." +
            (backup?.let { " Previous file backed up as ${it.name}." } ?: if (originValue == "CREATED") {
                " This was a new config file; rollback will remove it."
            } else {
                " The original backup from the first UltraScale install was preserved."
            }) +
            " Fully close and relaunch the game. The game accepting this external override is not yet runtime-verified."
    }

    override fun restoreAsphalt8Config(): String {
        requireAsphalt8Stopped()
        val directory = File(
            "/storage/emulated/0/Android/data/com.gameloft.android.ANMP.GloftA8HM/files"
        ).canonicalFile
        require(directory.isDirectory && directory.canWrite()) {
            "Asphalt 8 files folder is unavailable or not writable through Shizuku."
        }
        val destination = File(directory, "gameoptions.json")
        val originMarker = File(directory, ".ultrascale-asphalt8-config-origin")
        val origin = if (originMarker.isFile) originMarker.readText().trim() else null
        if (origin == "CREATED") {
            if (destination.exists()) {
                require(destination.isFile && destination.delete()) {
                    "Could not remove the UltraScale-created gameoptions.json."
                }
            }
            originMarker.delete()
            return "Removed the gameoptions.json created by UltraScale; no pre-existing config was present."
        }

        val backup = if (origin != null) {
            require(Regex("""^gameoptions\.json\.ultrascale-\d+\.bak$""").matches(origin)) {
                "The backup marker is invalid; refusing to guess which backup to restore."
            }
            File(directory, origin).canonicalFile.also {
                require(it.parentFile == directory && it.isFile) {
                    "The original backup recorded by UltraScale is missing; nothing was restored."
                }
            }
        } else {
            directory.listFiles()
                ?.filter {
                    it.isFile && it.name.matches(Regex("""^gameoptions\.json\.ultrascale-\d+\.bak$"""))
                }
                ?.maxByOrNull { it.lastModified() }
                ?: error("No UltraScale backup was found; nothing to restore.")
        }

        val temporary = File(directory, ".gameoptions.json.restore.tmp")
        try {
            Files.copy(backup.toPath(), temporary.toPath(), StandardCopyOption.REPLACE_EXISTING)
            try {
                Files.move(
                    temporary.toPath(),
                    destination.toPath(),
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING
                )
            } catch (_: AtomicMoveNotSupportedException) {
                Files.move(
                    temporary.toPath(),
                    destination.toPath(),
                    StandardCopyOption.REPLACE_EXISTING
                )
            }
        } finally {
            temporary.delete()
        }
        check(destination.isFile && destination.length() == backup.length()) {
            "Restored file failed verification."
        }
        originMarker.delete()
        return "Restored gameoptions.json from ${backup.name}."
    }

    override fun readRendererConfig(): String {
        val directory = File(
            "/storage/emulated/0/Android/data/com.gameloft.android.ANMP.GloftA8HM/files"
        ).canonicalFile
        require(directory.isDirectory && directory.canRead()) {
            "Asphalt 8 files folder is unavailable."
        }
        val config = File(directory, "ultrascale-renderer.ini").canonicalFile
        require(config.parentFile == directory) { "Invalid renderer config path." }
        return if (config.isFile) config.readText(Charsets.UTF_8) else "cas=0\nsharpness=0.5\n"
    }

    override fun deployRendererConfig(configText: String): String {
        requireAsphalt8Stopped()
        require(configText.toByteArray(Charsets.UTF_8).size <= 1024) {
            "Renderer config exceeds the 1 KiB safety limit."
        }
        val lines = configText.lineSequence().map { it.trim() }.filter { it.isNotEmpty() }.toList()
        val values = lines.mapNotNull { line ->
            val split = line.indexOf('=')
            if (split <= 0) null else line.substring(0, split).trim() to line.substring(split + 1).trim()
        }.toMap()
        require(values.keys.all { it == "cas" || it == "sharpness" }) {
            "Only the supported final-frame CAS settings can be written."
        }
        require(values["cas"] in setOf("0", "1")) { "cas must be 0 or 1." }
        val sharpness = values["sharpness"]?.toFloatOrNull()
            ?: error("sharpness must be a number from 0.0 to 1.0.")
        require(sharpness.isFinite() && sharpness in 0.0f..1.0f) {
            "sharpness must be from 0.0 to 1.0."
        }
        val canonical = "cas=${values["cas"]}\nsharpness=${sharpness}\n"
        val directory = File(
            "/storage/emulated/0/Android/data/com.gameloft.android.ANMP.GloftA8HM/files"
        ).canonicalFile
        require(directory.isDirectory && directory.canWrite()) {
            "Asphalt 8 files folder is unavailable or not writable through Shizuku."
        }
        val destination = File(directory, "ultrascale-renderer.ini").canonicalFile
        require(destination.parentFile == directory) { "Invalid renderer config destination." }
        val temporary = File(directory, ".ultrascale-renderer.ini.tmp")
        try {
            FileOutputStream(temporary, false).use { output ->
                output.write(canonical.toByteArray(Charsets.UTF_8))
                output.flush()
                output.fd.sync()
            }
            try {
                Files.move(temporary.toPath(), destination.toPath(),
                    StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
            } catch (_: AtomicMoveNotSupportedException) {
                Files.move(temporary.toPath(), destination.toPath(),
                    StandardCopyOption.REPLACE_EXISTING)
            }
        } finally {
            temporary.delete()
        }
        check(destination.isFile && destination.readText(Charsets.UTF_8) == canonical) {
            "Renderer config failed verification."
        }
        return "Applied final-frame CAS config (cas=${values["cas"]}, sharpness=${sharpness}). Relaunch Asphalt 8; the layer reads the sidecar while rendering."
    }

    override fun restoreRendererConfig(): String {
        requireAsphalt8Stopped()
        val directory = File(
            "/storage/emulated/0/Android/data/com.gameloft.android.ANMP.GloftA8HM/files"
        ).canonicalFile
        val destination = File(directory, "ultrascale-renderer.ini").canonicalFile
        require(destination.parentFile == directory) { "Invalid renderer config path." }
        if (destination.exists()) {
            require(destination.isFile && destination.delete()) {
                "Could not remove UltraScale renderer config."
            }
        }
        return "Removed UltraScale renderer config. The GLES layer is pass-through until CAS is configured again."
    }

    private fun requireAsphalt8Stopped() {
        val process = ProcessBuilder(
            "pidof",
            "com.gameloft.android.ANMP.GloftA8HM"
        ).redirectErrorStream(true).start()
        val output = process.inputStream.bufferedReader().use { it.readText() }.trim()
        val exitCode = process.waitFor()
        require(exitCode != 0 || output.isBlank()) {
            "Asphalt 8 is still running. Close the game fully, then retry."
        }
    }

    /**
     * Apply global GPU settings as a small transaction. Android settings are not
     * transactional, so restore the exact previous values if any write fails.
     */
    private fun writeSettingsTransaction(changes: Map<String, String?>) {
        val before = changes.keys.associateWith(::readSetting)
        try {
            changes.forEach { (key, value) ->
                if (value == null) runSettings("delete", key, null)
                else runSettings("put", key, value)
            }
        } catch (failure: Throwable) {
            before.forEach { (key, value) ->
                runCatching {
                    if (value.isBlank()) runSettings("delete", key, null)
                    else runSettings("put", key, value)
                }
            }
            throw failure
        }
    }

    private fun readSetting(key: String): String {
        val process = ProcessBuilder("settings", "get", "global", key).redirectErrorStream(true).start()
        val output = process.inputStream.bufferedReader().readText().trim()
        val code = process.waitFor()
        check(code == 0) { output.ifBlank { "settings get failed: $key" } }
        return output.takeUnless { it == "null" } ?: ""
    }

    private fun runSettings(action: String, key: String, value: String?) {
        val command = mutableListOf("settings", action, "global", key)
        if (value != null) command += value
        val process = ProcessBuilder(command).redirectErrorStream(true).start()
        val output = process.inputStream.bufferedReader().readText()
        val code = process.waitFor()
        check(code == 0) { output.ifBlank { "settings command failed: $key" } }
    }

    @Throws(RemoteException::class)
    override fun onTransact(code: Int, data: Parcel, reply: Parcel?, flags: Int): Boolean {
        if (code == 16777115) {
            System.exit(0)
            return true
        }
        return super.onTransact(code, data, reply, flags)
    }
}
