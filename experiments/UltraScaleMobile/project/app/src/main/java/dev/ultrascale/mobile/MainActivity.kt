package dev.ultrascale.mobile

import android.content.Intent
import android.os.Bundle
import android.content.pm.ApplicationInfo
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

class MainActivity : ComponentActivity() {
    private var refreshPermissionUi: (() -> Unit)? = null
    private val permissionListener = Shizuku.OnRequestPermissionResultListener { _, _ ->
        runOnUiThread { refreshPermissionUi?.invoke() }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                var refresh by remember { mutableIntStateOf(0) }
                var showAllApps by remember { mutableStateOf(false) }
                var shizukuRefresh by remember { mutableIntStateOf(0) }
                DisposableEffect(Unit) {
                    refreshPermissionUi = { shizukuRefresh++ }
                    ShizukuBridge.statusListener = { runOnUiThread { shizukuRefresh++ } }
                    Shizuku.addRequestPermissionResultListener(permissionListener)
                    onDispose {
                        refreshPermissionUi = null
                        ShizukuBridge.statusListener = null
                        Shizuku.removeRequestPermissionResultListener(permissionListener)
                    }
                }
                val shizukuStatus = remember(shizukuRefresh) { ShizukuBridge.status() }
                val games = remember(refresh, showAllApps) { discoverGames(showAllApps) }
                LazyColumn(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    item {
                        Text("UltraScale Mobile — API Lab", style = MaterialTheme.typography.headlineSmall)
                        Text("Android 15+ • Vulkan / OpenGL ES • no ANGLE dependency")
                        Text("Shizuku: $shizukuStatus")
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Button(onClick = { ShizukuBridge.requestPermission(); refresh++ }) { Text("Request permission") }
                            Button(enabled = ShizukuBridge.granted(), onClick = {
                                runCatching { ShizukuBridge.bindUserService() }
                            }) { Text("Connect shell service") }
                        }
                        OutlinedButton(enabled = ShizukuBridge.serviceConnected(), onClick = {
                            ShizukuBridge.stopUserService(); refresh++
                        }) { Text("Stop shell service") }
                        val deviceApis = remember { NativeGraphicsProbe.report() }
                        Text("Device graphics APIs", style = MaterialTheme.typography.titleMedium)
                        Text(deviceApis.first)
                        Text(deviceApis.second)
                        Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                            Text("Show all launchable apps")
                            Switch(checked = showAllApps, onCheckedChange = { showAllApps = it })
                        }
                        Text("Installed ${if (showAllApps) "apps" else "games"}: ${games.size}")
                        Text("Vulkan: target manifest opt-in (API 30+) or debuggable; otherwise root. GLES: debuggable target or root. Shizuku ADB UID 2000 is not root.")
                    }
                    items(games, key = { it.first.packageName }) { (app, label) ->
                        val report = GraphicsApiScanner.inspect(this@MainActivity, app)
                        var feedback by remember(app.packageName) { mutableStateOf("") }
                        Card(Modifier.fillMaxWidth()) {
                            Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(5.dp)) {
                                Text(label, style = MaterialTheme.typography.titleMedium)
                                Text(app.packageName, style = MaterialTheme.typography.labelSmall)
                                Text("Engine: ${report.engine} • Type: ${report.gameKind}")
                                Text("Possible APIs: ${report.candidateApis.joinToString()}")
                                Text("Evidence: ${report.confidence} — ${report.evidence}")
                                val vulkanEligibility = when (report.vulkanLayer) {
                                    LayerReadiness.ELIGIBLE_OPT_IN -> "manifest opt-in"
                                    LayerReadiness.ELIGIBLE_DEBUG -> "debuggable target"
                                    LayerReadiness.ELIGIBLE_ROOT -> "root"
                                    else -> "not eligible — needs opt-in, debuggable target, or root"
                                }
                                val glesEligibility = when (report.glesLayer) {
                                    LayerReadiness.ELIGIBLE_DEBUG -> "debuggable target"
                                    LayerReadiness.ELIGIBLE_ROOT -> "root"
                                    else -> "not eligible — GLES needs debuggable target or root"
                                }
                                Text("Vulkan layer: $vulkanEligibility")
                                Text("GLES layer: $glesEligibility")
                                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    Button(enabled = report.targetAllowsVulkan && ShizukuBridge.granted() && ShizukuBridge.serviceConnected(),
                                        onClick = {
                                            feedback = ShizukuBridge.configureDebugLayer(app.packageName, packageName, LayerApi.VULKAN, true)
                                                .fold({ "Vulkan: $it" }, { "Vulkan failed: ${it.message}" })
                                        }) { Text("Enable Vulkan") }
                                    Button(enabled = report.targetAllowsGles && ShizukuBridge.granted() && ShizukuBridge.serviceConnected(),
                                        onClick = {
                                            feedback = ShizukuBridge.configureDebugLayer(app.packageName, packageName, LayerApi.OPENGL_ES, true)
                                                .fold({ "GLES: $it" }, { "GLES failed: ${it.message}" })
                                        }) { Text("Enable GLES") }
                                }
                                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    OutlinedButton(enabled = ShizukuBridge.granted() && ShizukuBridge.serviceConnected(),
                                        onClick = {
                                            feedback = ShizukuBridge.configureDebugLayer(app.packageName, packageName, LayerApi.VULKAN, false)
                                                .fold({ "Vulkan: $it" }, { "Disable Vulkan failed: ${it.message}" })
                                        }) { Text("Disable Vulkan") }
                                    OutlinedButton(enabled = ShizukuBridge.granted() && ShizukuBridge.serviceConnected(),
                                        onClick = {
                                            feedback = ShizukuBridge.configureDebugLayer(app.packageName, packageName, LayerApi.OPENGL_ES, false)
                                                .fold({ "GLES: $it" }, { "Disable GLES failed: ${it.message}" })
                                        }) { Text("Disable GLES") }
                                }
                                if (feedback.isNotBlank()) Text(feedback, style = MaterialTheme.typography.bodySmall)
                                Text("Restart the game after changing layer settings.", style = MaterialTheme.typography.labelSmall)
                            }
                        }
                    }
                }
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun discoverGames(showAllApps: Boolean): List<Pair<ApplicationInfo, String>> {
        val intent = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
        return packageManager.queryIntentActivities(intent, 0)
            .mapNotNull { it.activityInfo?.applicationInfo }
            .filter { it.packageName != packageName }
            .distinctBy { it.packageName }
            .filter { app ->
                showAllApps || app.category == ApplicationInfo.CATEGORY_GAME ||
                    (app.flags and ApplicationInfo.FLAG_IS_GAME) != 0
            }
            .map { it to packageManager.getApplicationLabel(it).toString() }
            .sortedBy { it.second.lowercase() }
    }
}
