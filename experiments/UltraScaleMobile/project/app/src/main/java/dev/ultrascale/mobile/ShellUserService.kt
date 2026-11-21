package dev.ultrascale.mobile

import android.os.Parcel
import android.os.RemoteException
import java.util.regex.Pattern

class ShellUserService : ICommandService.Stub() {
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
            return "Configured $api layer for $targetPackage; existing layer entries were preserved. Fully stop and relaunch the game."
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
        return "Disabled UltraScale $api layer for $targetPackage; unrelated layer names and package search entries were preserved. Restart the game."

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
