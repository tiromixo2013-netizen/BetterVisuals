package dev.ultrascale.mobile

import android.content.Intent
import android.os.Bundle
import android.content.pm.PackageManager
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
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                var refresh by remember { mutableIntStateOf(0) }
                val games = remember(refresh) { discoverLaunchables() }
                LazyColumn(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    item {
                        Text("UltraScale Mobile — API Lab", style = MaterialTheme.typography.headlineSmall)
                        Text("Android 15+ • Vulkan / OpenGL ES • ANGLE not required")
                        Text("Shizuku: ${ShizukuBridge.status()}")
                        Button(onClick = { ShizukuBridge.requestPermission(); refresh++ }) { Text("Request Shizuku permission") }
                    }
                    item { Text("Installed launchable apps (Play Store games are included when installed)", style = MaterialTheme.typography.titleMedium) }
                    items(games, key = { it.first.packageName }) { (app, label) ->
                        val report = GraphicsApiScanner.inspect(this@MainActivity, app)
                        Card(Modifier.fillMaxWidth()) {
                            Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                                Text(label, style = MaterialTheme.typography.titleMedium)
                                Text(app.packageName, style = MaterialTheme.typography.labelSmall)
                                Text("Engine: ${report.engine} • Type: ${report.gameKind}")
                                Text("Candidate APIs: ${report.candidateApis.joinToString()}")
                                Text("Evidence: ${report.confidence} — ${report.evidence}")
                                Text("Vulkan layer: ${report.vulkanLayer}")
                                Text("GLES layer: ${report.glesLayer}")
                            }
                        }
                    }
                }
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun discoverLaunchables(): List<Pair<android.content.pm.ApplicationInfo, String>> {
        val intent = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
        return packageManager.queryIntentActivities(intent, 0)
            .mapNotNull { it.activityInfo?.applicationInfo }
            .filter { it.packageName != packageName }
            .distinctBy { it.packageName }
            .map { it to packageManager.getApplicationLabel(it).toString() }
            .sortedBy { it.second.lowercase() }
    }
}
