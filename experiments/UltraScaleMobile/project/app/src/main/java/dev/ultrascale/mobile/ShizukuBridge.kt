package dev.ultrascale.mobile

import android.content.ComponentName
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.IBinder
import rikka.shizuku.Shizuku

enum class LayerApi { VULKAN, OPENGL_ES }

object ShizukuBridge {
    private val args by lazy {
        Shizuku.UserServiceArgs(ComponentName(BuildConfig.APPLICATION_ID, ShellUserService::class.java.name))
            .daemon(false)
            .processNameSuffix("ultrascale-shell")
            .tag("ultrascale-shell-service")
            .version(1)
    }
    @Volatile private var remote: ICommandService? = null
    @Volatile var statusListener: (() -> Unit)? = null
    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            remote = ICommandService.Stub.asInterface(service)
            statusListener?.invoke()
        }
        override fun onServiceDisconnected(name: ComponentName?) { remote = null; statusListener?.invoke() }
    }

    fun connected(): Boolean = runCatching { Shizuku.pingBinder() }.getOrDefault(false)
    fun granted(): Boolean = runCatching {
        connected() && Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
    }.getOrDefault(false)
    fun uid(): Int? = runCatching { if (granted()) Shizuku.getUid() else null }.getOrNull()
    fun serviceConnected(): Boolean = remote != null
    fun status(): String = when {
        !connected() -> "Not connected"
        !granted() -> "Permission required"
        uid() == 2000 && remote == null -> "ADB shell (UID 2000) • service not connected"
        uid() == 0 && remote == null -> "Root (UID 0) • service not connected"
        uid() == 2000 -> "ADB shell (UID 2000) • ready"
        uid() == 0 -> "Root (UID 0) • ready"
        else -> "Connected with limited identity"
    }
    fun requestPermission() {
        if (connected() && !granted()) Shizuku.requestPermission(9021)
    }
    fun bindUserService() {
        check(granted()) { "Grant Shizuku permission first" }
        if (remote == null) Shizuku.bindUserService(args, connection)
    }
    fun stopUserService() {
        runCatching { Shizuku.unbindUserService(args, connection, true) }
        remote = null
        statusListener?.invoke()
    }

    fun readLayerTelemetry(targetPackage: String): Result<String> = runCatching {
        check(granted()) { "Shizuku permission is not granted" }
        val service = remote ?: error("Shell service is not connected. Tap Connect Shizuku service first.")
        service.readLayerTelemetry(targetPackage)
    }

    fun deployAsphalt8Config(json: String): Result<String> = runCatching {
        check(granted()) { "Shizuku permission is not granted" }
        val service = remote ?: error("Shell service is not connected. Tap Connect Shizuku service first.")
        service.deployAsphalt8Config(json)
    }

    fun restoreAsphalt8Config(): Result<String> = runCatching {
        check(granted()) { "Shizuku permission is not granted" }
        val service = remote ?: error("Shell service is not connected. Tap Connect Shizuku service first.")
        service.restoreAsphalt8Config()
    }

    fun configureDebugLayer(targetPackage: String, layerPackage: String, api: LayerApi, enable: Boolean): Result<String> = runCatching {
        check(granted()) { "Shizuku permission is not granted" }
        val service = remote ?: error("Shell service is not connected. Tap Connect Shizuku service first.")
        service.configureLayer(targetPackage, layerPackage,
            if (api == LayerApi.VULKAN) "vulkan" else "gles", enable)
    }
}
