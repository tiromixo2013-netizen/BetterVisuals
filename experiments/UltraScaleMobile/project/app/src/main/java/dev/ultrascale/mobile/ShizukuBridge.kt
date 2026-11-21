package dev.ultrascale.mobile

import android.content.pm.PackageManager
import rikka.shizuku.Shizuku

enum class LayerApi { VULKAN, OPENGL_ES }

object ShizukuBridge {
    fun connected(): Boolean = runCatching { Shizuku.pingBinder() }.getOrDefault(false)
    fun granted(): Boolean = runCatching {
        connected() && Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
    }.getOrDefault(false)
    fun uid(): Int? = runCatching { if (granted()) Shizuku.getUid() else null }.getOrNull()
    fun status(): String = when {
        !connected() -> "Not connected"
        !granted() -> "Permission required"
        uid() == 2000 -> "ADB shell (UID 2000)"
        uid() == 0 -> "Root (UID 0)"
        else -> "Connected with limited identity"
    }
    fun requestPermission() {
        if (connected() && !granted()) Shizuku.requestPermission(9021)
    }

    fun configureDebugLayer(targetPackage: String, layerPackage: String, api: LayerApi, enable: Boolean): Result<String> = runCatching {
        check(granted()) { "Shizuku permission is not granted" }
        require(targetPackage.matches(Regex("[A-Za-z0-9_.]+"))) { "Invalid target package" }
        require(layerPackage.matches(Regex("[A-Za-z0-9_.]+"))) { "Invalid layer package" }
        val commands = mutableListOf(
            "settings put global enable_gpu_debug_layers ${if (enable) 1 else 0}",
            "settings put global gpu_debug_app $targetPackage",
            "settings put global gpu_debug_layer_app $layerPackage"
        )
        when (api) {
            LayerApi.VULKAN -> commands += "settings put global gpu_debug_layers VK_LAYER_ULTRASCALE_api_lab"
            LayerApi.OPENGL_ES -> commands += "settings put global gpu_debug_layers_gles libGLES_ultrascale.so"
        }
        if (!enable) {
            commands += "settings delete global gpu_debug_app"
            commands += "settings delete global gpu_debug_layer_app"
            commands += if (api == LayerApi.VULKAN) "settings delete global gpu_debug_layers" else "settings delete global gpu_debug_layers_gles"
            commands += "settings put global enable_gpu_debug_layers 0"
        }
        val process = Shizuku.newProcess(arrayOf("sh", "-c", commands.joinToString(" && ")), null, null)
        val output = process.inputStream.bufferedReader().readText()
        val errors = process.errorStream.bufferedReader().readText()
        val code = process.waitFor()
        check(code == 0) { errors.ifBlank { "ADB shell command failed ($code)" } }
        output.ifBlank { "Debug-layer settings updated. Fully restart the target game." }
    }
}
