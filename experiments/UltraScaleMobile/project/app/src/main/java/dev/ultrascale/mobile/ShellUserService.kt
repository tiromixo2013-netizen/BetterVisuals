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

        if (!enable) {
            listOf("enable_gpu_debug_layers", "gpu_debug_app", "gpu_debug_layers",
                "gpu_debug_layers_gles", "gpu_debug_layer_app").forEach { setting ->
                runSettings("delete", setting, null)
            }
            return "All GPU debug-layer settings cleared. Restart the game."
        }

        runSettings("put", "enable_gpu_debug_layers", "1")
        runSettings("put", "gpu_debug_app", targetPackage)
        runSettings("put", "gpu_debug_layer_app", layerPackage)
        if (api == "vulkan") {
            runSettings("put", "gpu_debug_layers", "VK_LAYER_ULTRASCALE_api_lab")
            runSettings("delete", "gpu_debug_layers_gles", null)
        } else {
            runSettings("put", "gpu_debug_layers_gles", "libGLES_ultrascale.so")
            runSettings("delete", "gpu_debug_layers", null)
        }
        return "Configured $api debug layer for $targetPackage. Fully stop and relaunch the game."
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
