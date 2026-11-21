package dev.ultrascale.mobile

import android.content.pm.PackageManager
import rikka.shizuku.Shizuku

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
}
