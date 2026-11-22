@file:Suppress("ProduceStateDoesNotAssignValue")
package dev.ultrascale.mobile

import android.content.Intent
import android.os.Bundle
import android.content.pm.ApplicationInfo
import android.widget.ImageView
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import rikka.shizuku.Shizuku

private val Ink = Color(0xFF10111A)
private val Panel = Color(0xFF1B1D2A)
private val Violet = Color(0xFFB7A1FF)
private val Muted = Color(0xFFA9A9BC)

class MainActivity : ComponentActivity() {
    private var refreshPermissionUi: (() -> Unit)? = null
    private val permissionListener = Shizuku.OnRequestPermissionResultListener { _, _ ->
        runOnUiThread { refreshPermissionUi?.invoke() }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme(
                primary = Violet,
                onPrimary = Ink,
                secondary = Color(0xFF8BD8C7),
                background = Ink,
                surface = Panel,
                surfaceVariant = Color(0xFF292B3A),
                onSurface = Color(0xFFF4F2FF),
                onSurfaceVariant = Muted
            )) {
                var refresh by remember { mutableIntStateOf(0) }
                var showAllApps by remember { mutableStateOf(false) }
                var query by remember { mutableStateOf("") }
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
                val allGames = remember(refresh, showAllApps) { discoverGames(showAllApps) }
                val games = remember(allGames, query) {
                    allGames.filter { (app, label) ->
                        label.contains(query, ignoreCase = true) ||
                            app.packageName.contains(query, ignoreCase = true)
                    }.sortedWith(
                        compareBy<Pair<ApplicationInfo, String>> { if (it.first.packageName == "com.gameloft.android.ANMP.GloftA8HM") 0 else 1 }
                            .thenBy { it.second.lowercase() }
                    )
                }
                val deviceApis = remember { NativeGraphicsProbe.report() }

                LazyColumn(
                    modifier = Modifier.fillMaxSize().background(Ink),
                    contentPadding = PaddingValues(start = 18.dp, end = 18.dp, top = 22.dp, bottom = 32.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    item {
                        Column(verticalArrangement = Arrangement.spacedBy(5.dp)) {
                            Text("ULTRASCALE", color = Violet, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold, letterSpacing = 2.sp)
                            Text("Game Center", style = MaterialTheme.typography.headlineLarge, fontWeight = FontWeight.Bold)
                            Text("Graphics compatibility & layer diagnostics", color = Muted, style = MaterialTheme.typography.bodyMedium)
                        }
                    }
                    item {
                        Card(
                            colors = CardDefaults.cardColors(containerColor = Panel),
                            shape = RoundedCornerShape(24.dp),
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Column(
                                Modifier.background(Brush.linearGradient(listOf(Color(0xFF29233F), Panel)))
                                    .padding(18.dp),
                                verticalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                                    Box(Modifier.size(42.dp).clip(CircleShape).background(Violet.copy(alpha = .16f)), contentAlignment = Alignment.Center) {
                                        Text("US", color = Violet, fontWeight = FontWeight.Black)
                                    }
                                    Column(Modifier.weight(1f)) {
                                        Text("Device & service", fontWeight = FontWeight.SemiBold)
                                        Text("Android graphics environment", color = Muted, style = MaterialTheme.typography.bodySmall)
                                    }
                                    StatusPill(if (ShizukuBridge.granted() && ShizukuBridge.serviceConnected()) "CONNECTED" else shizukuStatus.uppercase().take(18))
                                }
                                Text("Shizuku · $shizukuStatus", style = MaterialTheme.typography.bodyMedium)
                                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                                    Button(
                                        onClick = { ShizukuBridge.requestPermission(); refresh++ },
                                        modifier = Modifier.weight(1f)
                                    ) { Text("Grant access", maxLines = 1) }
                                    OutlinedButton(
                                        enabled = ShizukuBridge.granted() && !ShizukuBridge.serviceConnected(),
                                        onClick = { runCatching { ShizukuBridge.bindUserService() } },
                                        modifier = Modifier.weight(1f)
                                    ) { Text("Connect", maxLines = 1) }
                                }
                                OutlinedButton(
                                    enabled = ShizukuBridge.serviceConnected(),
                                    onClick = { ShizukuBridge.stopUserService(); refresh++ },
                                    modifier = Modifier.fillMaxWidth()
                                ) { Text("Disconnect service") }
                                HorizontalDivider(color = Color.White.copy(alpha = .08f))
                                Text("GRAPHICS APIs", color = Violet, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                                Text(deviceApis.first, style = MaterialTheme.typography.bodyMedium)
                                Text(deviceApis.second, style = MaterialTheme.typography.bodySmall, color = Muted)
                            }
                        }
                    }
                    item { UpscalerLibraryCard() }
                    item {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.weight(1f)) {
                                Text("Installed games", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
                                Text("${games.size} ${if (showAllApps) "launchable apps" else "games"} found", color = Muted, style = MaterialTheme.typography.bodySmall)
                            }
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text("All apps", color = Muted, style = MaterialTheme.typography.labelMedium)
                                Switch(checked = showAllApps, onCheckedChange = { showAllApps = it })
                            }
                        }
                    }
                    item {
                        OutlinedTextField(
                            value = query,
                            onValueChange = { query = it },
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true,
                            placeholder = { Text("Search game or package") },
                            shape = RoundedCornerShape(16.dp)
                        )
                    }
                    if (games.isEmpty()) {
                        item {
                            Card(colors = CardDefaults.cardColors(containerColor = Panel), shape = RoundedCornerShape(18.dp)) {
                                Text("No matching apps. Try another search or enable All apps.", Modifier.padding(18.dp), color = Muted)
                            }
                        }
                    }
                    items(games, key = { it.first.packageName }) { (app, label) ->
                        val report by produceState<ApiReport?>(initialValue = null, app.packageName) {
                            value = withContext(Dispatchers.IO) {
                                GraphicsApiScanner.inspect(this@MainActivity, app)
                            }
                        }
                        if (report != null) {
                            GameCard(app, label, report!!)
                        } else {
                            Card(
                                colors = CardDefaults.cardColors(containerColor = Panel),
                                shape = RoundedCornerShape(18.dp),
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Row(
                                    Modifier.padding(18.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                                ) {
                                    CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                                    Text("Analyzing APK and engine signatures…", color = Muted)
                                }
                            }
                        }
                    }
                    item {
                        Text(
                            "Important: Shizuku runs as ADB shell, not root. Android graphics-layer eligibility depends on the target app and device policy. APK signatures are only candidate API evidence; they do not prove which API the game is using right now.",
                            color = Muted,
                            style = MaterialTheme.typography.bodySmall
                        )
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


@Composable
private fun UpscalerLibraryCard() {
    val context = androidx.compose.ui.platform.LocalContext.current
    var selectedTechnique by remember { mutableStateOf<UpscaleTechnique?>(null) }
    var sourceText by remember { mutableStateOf("") }
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
        colors = CardDefaults.cardColors(containerColor = Panel)
    ) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(11.dp)) {
            Text("BUILT-IN SHADER LIBRARY", color = Violet, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
            Text("${UpscaleTechniqueCatalog.all.size} packaged techniques", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
            UpscaleTechniqueCatalog.all.forEach { technique ->
                Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text(technique.title, modifier = Modifier.weight(1f), fontWeight = FontWeight.SemiBold)
                        StatusPill(if (technique.role == TechniqueRole.TEMPORAL_REFERENCE) "REFERENCE" else "BUNDLED")
                    }
                    Text("${technique.role} · ${technique.graphicsProfile} · ${technique.license}", color = Muted, style = MaterialTheme.typography.labelSmall)
                    Text(technique.note, color = Muted, style = MaterialTheme.typography.bodySmall)
                    TextButton(
                        onClick = {
                            selectedTechnique = technique
                            sourceText = runCatching { UpscaleTechniqueCatalog.source(context, technique) }
                                .getOrElse { "Could not load shader asset: ${it.message}" }
                        },
                        contentPadding = PaddingValues(horizontal = 0.dp, vertical = 2.dp)
                    ) { Text("Inspect bundled source") }
                }
            }
            HorizontalDivider(color = Color.White.copy(alpha = .08f))
            Text(
                "Shader sources are packaged and loadable by the app. They are not yet applied to a game's render target; source capture, render-pass integration, output scaling and synchronization remain required.",
                color = Muted,
                style = MaterialTheme.typography.bodySmall
            )
        }
    }
    selectedTechnique?.let { technique ->
        AlertDialog(
            onDismissRequest = { selectedTechnique = null },
            title = { Text(technique.title) },
            text = {
                Text(
                    sourceText,
                    modifier = Modifier.heightIn(max = 420.dp).verticalScroll(rememberScrollState()),
                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                    style = MaterialTheme.typography.bodySmall
                )
            },
            confirmButton = {
                TextButton(onClick = { selectedTechnique = null }) { Text("Close") }
            }
        )
    }
}

@Composable
private fun GameCard(app: ApplicationInfo, label: String, report: ApiReport) {
    var expanded by remember(app.packageName) { mutableStateOf(app.packageName == "com.gameloft.android.ANMP.GloftA8HM") }
    var feedback by remember(app.packageName) { mutableStateOf("") }
    val context = androidx.compose.ui.platform.LocalContext.current
    val isAsphalt = app.packageName == "com.gameloft.android.ANMP.GloftA8HM"
    val vulkanReady = RenderApi.VULKAN in report.candidateApis &&
        report.vulkanLayer in setOf(LayerReadiness.ELIGIBLE_OPT_IN, LayerReadiness.ELIGIBLE_DEBUG, LayerReadiness.ELIGIBLE_ROOT)
    val glesReady = RenderApi.OPENGL_ES in report.candidateApis &&
        report.glesLayer in setOf(LayerReadiness.ELIGIBLE_OPT_IN, LayerReadiness.ELIGIBLE_DEBUG, LayerReadiness.ELIGIBLE_ROOT)

    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
        colors = CardDefaults.cardColors(containerColor = Panel)
    ) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                AndroidView(
                    factory = { ImageView(it).apply { scaleType = ImageView.ScaleType.FIT_CENTER } },
                    update = { image -> image.setImageDrawable(runCatching { context.packageManager.getApplicationIcon(app) }.getOrNull()) },
                    modifier = Modifier.size(52.dp).clip(RoundedCornerShape(14.dp)).background(Color.White.copy(alpha = .06f))
                )
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(3.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(7.dp)) {
                        Text(label, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        if (isAsphalt) StatusPill("TARGET")
                    }
                    Text(app.packageName, style = MaterialTheme.typography.labelSmall, color = Muted, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                StatusPill(report.gameKind.toString().replace('_', ' '))
                StatusPill(report.confidence.toString())
            }
            Text("Engine: ${report.engine}", color = Muted, style = MaterialTheme.typography.bodySmall)
            Text("Version: ${report.versionName} (${report.versionCode}) · Target SDK ${report.targetSdk}", color = Muted, style = MaterialTheme.typography.bodySmall)
            Text("Native ABI: ${report.nativeAbis.joinToString().ifBlank { "No packaged native libraries" }}", color = Muted, style = MaterialTheme.typography.bodySmall)
            Text("Native libraries: ${report.nativeLibraryCount}", color = Muted, style = MaterialTheme.typography.bodySmall)
            Text("Candidate APIs: ${report.candidateApis.joinToString().ifBlank { "Not detected" }}", style = MaterialTheme.typography.bodyMedium)
            Text(
                "Runtime API is not confirmed by APK signature scanning.",
                color = Muted,
                style = MaterialTheme.typography.bodySmall
            )
            OutlinedButton(onClick = { expanded = !expanded }, modifier = Modifier.fillMaxWidth()) {
                Text(if (expanded) "Hide technical details" else "Technical details")
            }
            if (expanded) {
                Column(verticalArrangement = Arrangement.spacedBy(9.dp)) {
                    Text("LAYER ELIGIBILITY", color = Violet, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                    EligibilityLine("Vulkan", vulkanReady, eligibilityText(report.vulkanLayer, true))
                    EligibilityLine("OpenGL ES", glesReady, eligibilityText(report.glesLayer, false))
                    Text("Vulkan target graphics-layer opt-in: ${if (report.manifestLayerOptIn) "YES" else "NO"}", color = Muted, style = MaterialTheme.typography.bodySmall)
                    Text(report.evidence, color = Muted, style = MaterialTheme.typography.bodySmall)
                    Text("A candidate API is not runtime confirmation. Layer switches configure Android debug-layer settings only; they do not inject an upscaler or modify the game's own settings menu.", color = Muted, style = MaterialTheme.typography.bodySmall)
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                        Button(
                            enabled = vulkanReady && ShizukuBridge.granted() && ShizukuBridge.serviceConnected(),
                            onClick = {
                                feedback = ShizukuBridge.configureDebugLayer(app.packageName, context.packageName, LayerApi.VULKAN, true)
                                    .fold({ "Vulkan setting: $it" }, { "Vulkan failed: ${it.message}" })
                            },
                            modifier = Modifier.weight(1f)
                        ) { Text("Enable Vulkan", maxLines = 1) }
                        Button(
                            enabled = glesReady && ShizukuBridge.granted() && ShizukuBridge.serviceConnected(),
                            onClick = {
                                feedback = ShizukuBridge.configureDebugLayer(app.packageName, context.packageName, LayerApi.OPENGL_ES, true)
                                    .fold({ "GLES setting: $it" }, { "GLES failed: ${it.message}" })
                            },
                            modifier = Modifier.weight(1f)
                        ) { Text("Enable GLES", maxLines = 1) }
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                        OutlinedButton(
                            enabled = ShizukuBridge.granted() && ShizukuBridge.serviceConnected(),
                            onClick = {
                                feedback = ShizukuBridge.configureDebugLayer(app.packageName, context.packageName, LayerApi.VULKAN, false)
                                    .fold({ "Vulkan setting: $it" }, { "Vulkan failed: ${it.message}" })
                            },
                            modifier = Modifier.weight(1f)
                        ) { Text("Disable Vulkan", maxLines = 1) }
                        OutlinedButton(
                            enabled = ShizukuBridge.granted() && ShizukuBridge.serviceConnected(),
                            onClick = {
                                feedback = ShizukuBridge.configureDebugLayer(app.packageName, context.packageName, LayerApi.OPENGL_ES, false)
                                    .fold({ "GLES setting: $it" }, { "GLES failed: ${it.message}" })
                            },
                            modifier = Modifier.weight(1f)
                        ) { Text("Disable GLES", maxLines = 1) }
                    }
                    Button(
                        enabled = (vulkanReady || glesReady) && ShizukuBridge.granted() && ShizukuBridge.serviceConnected(),
                        onClick = {
                            val results = mutableListOf<String>()
                            if (vulkanReady) {
                                ShizukuBridge.configureDebugLayer(app.packageName, context.packageName, LayerApi.VULKAN, true)
                                    .onSuccess { results += "Vulkan: configured" }
                                    .onFailure { results += "Vulkan: ${it.message}" }
                            }
                            if (glesReady) {
                                ShizukuBridge.configureDebugLayer(app.packageName, context.packageName, LayerApi.OPENGL_ES, true)
                                    .onSuccess { results += "GLES: configured" }
                                    .onFailure { results += "GLES: ${it.message}" }
                            }
                            feedback = results.joinToString(" · ") +
                                " — Android debug-layer settings only; this is not forced injection."
                            val launch = context.packageManager.getLaunchIntentForPackage(app.packageName)
                            if (results.any { it.endsWith("configured") } && launch != null) {
                                launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                                context.startActivity(launch)
                            }
                        },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text("Apply compatible layer & launch game", maxLines = 1)
                    }
                    if (feedback.isNotBlank()) Text(feedback, color = MaterialTheme.colorScheme.secondary, style = MaterialTheme.typography.bodySmall)
                    Text(
                        "This button configures Android's documented GPU debug-layer settings for eligible targets, then launches the game. The game must permit layer loading; it cannot force-load into protected production apps.",
                        color = Muted,
                        style = MaterialTheme.typography.labelSmall
                    )
                    Text("If settings change, fully stop and relaunch the game.", color = Muted, style = MaterialTheme.typography.labelSmall)
                }
            }
        }
    }
}

@Composable
private fun EligibilityLine(title: String, eligible: Boolean, detail: String) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(title, fontWeight = FontWeight.SemiBold, modifier = Modifier.width(92.dp))
        StatusPill(if (eligible) "ELIGIBLE" else "BLOCKED", positive = eligible)
        Text(detail, color = Muted, style = MaterialTheme.typography.bodySmall, modifier = Modifier.weight(1f))
    }
}

@Composable
private fun StatusPill(text: String, positive: Boolean = false) {
    Surface(
        color = if (positive) Color(0xFF173B36) else Violet.copy(alpha = .13f),
        shape = RoundedCornerShape(50)
    ) {
        Text(
            text,
            modifier = Modifier.padding(horizontal = 9.dp, vertical = 5.dp),
            color = if (positive) Color(0xFF8BD8C7) else Violet,
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold,
            maxLines = 1
        )
    }
}

private fun eligibilityText(state: LayerReadiness, vulkan: Boolean): String = when (state) {
    LayerReadiness.ELIGIBLE_OPT_IN -> "Target opted in"
    LayerReadiness.ELIGIBLE_DEBUG -> "Debuggable target"
    LayerReadiness.ELIGIBLE_ROOT -> "Root available"
    else -> if (vulkan) "Needs opt-in, debug target or root" else "Needs debug target or root"
}
