package dev.ultrascale.mobile

object NativeGraphicsProbe {
    val available: Boolean = runCatching { System.loadLibrary("ultrascale_probe"); true }.getOrDefault(false)
    external fun vulkanLoaderInfo(): String
    external fun eglLoaderInfo(): String
    fun report(): Pair<String, String> = if (available) {
        runCatching { vulkanLoaderInfo() }.getOrDefault("Vulkan probe failed") to
            runCatching { eglLoaderInfo() }.getOrDefault("EGL probe failed")
    } else "Native probe unavailable" to "Native probe unavailable"
}
