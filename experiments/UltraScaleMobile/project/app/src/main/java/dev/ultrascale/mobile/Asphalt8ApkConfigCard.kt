package dev.ultrascale.mobile

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.util.zip.ZipInputStream

private val AsphaltMuted = Color(0xFFA9A9BC)
private val AsphaltViolet = Color(0xFFB7A1FF)
private val AsphaltPanel = Color(0xFF1B1D2A)

/** Extracts the game's own gameoptions.json from a user-selected APK. Read-only; no APK rewrite. */
object Asphalt8ApkConfig {
    data class Report(
        val entryName: String,
        val rawJson: String,
        val profileNames: List<String>,
        val gpuDefaults: Map<String, String>,
        val specificDefaults: Map<String, String>,
        val gpuOverrideCount: Int,
        val resolutionScales: Map<String, String>
    )

    fun extract(context: Context, apkUri: Uri): Result<Report> = runCatching {
        val jsonBytes = context.contentResolver.openInputStream(apkUri)?.use { input ->
            ZipInputStream(input).use { zip ->
                var found: ByteArray? = null
                var foundName = ""
                while (found == null) {
                    val entry = zip.nextEntry ?: break
                    if (!entry.isDirectory && entry.name.substringAfterLast('/').equals("gameoptions.json", true)) {
                        val output = ByteArrayOutputStream()
                        val buffer = ByteArray(8192)
                        var total = 0
                        while (true) {
                            val count = zip.read(buffer)
                            if (count < 0) break
                            total += count
                            require(total <= 2 * 1024 * 1024) { "gameoptions.json exceeds 2 MB" }
                            output.write(buffer, 0, count)
                        }
                        found = output.toByteArray()
                        foundName = entry.name
                    }
                    zip.closeEntry()
                }
                require(found != null) { "No gameoptions.json entry found in this APK" }
                foundName to found!!
            }
        } ?: error("Cannot open selected APK")

        val root = JSONObject(jsonBytes.second.toString(Charsets.UTF_8))
        val header = root.optJSONObject("header") ?: error("Missing gameoptions header")
        val profiles = root.optJSONObject("profiles") ?: error("Missing profiles object")
        require(profiles.has("GPU") && profiles.has("Specific")) {
            "JSON does not match the expected Asphalt 8 profile layout"
        }
        val gpu = profiles.getJSONObject("GPU")
        val specific = profiles.getJSONObject("Specific")
        val gpuOverrides = gpu.optJSONObject("overrides") ?: JSONObject()
        val specificOverrides = specific.optJSONObject("overrides") ?: JSONObject()
        val resolution = linkedMapOf<String, String>()
        val keys = specificOverrides.keys()
        while (keys.hasNext()) {
            val name = keys.next()
            if (name.startsWith("RES_")) {
                val value = specificOverrides.optJSONObject(name)?.opt("scaleDisplay")
                if (value != null) resolution[name] = value.toString()
            }
        }
        Report(
            entryName = jsonBytes.first,
            rawJson = root.toString(2),
            profileNames = buildList {
                val names = profiles.keys()
                while (names.hasNext()) add(names.next())
            }.sorted(),
            gpuDefaults = readDefaults(gpu.optJSONArray("defaults")),
            specificDefaults = readDefaults(specific.optJSONArray("defaults")),
            gpuOverrideCount = gpuOverrides.length(),
            resolutionScales = resolution.toSortedMap()
        )
    }

    private fun readDefaults(defaults: JSONArray?): Map<String, String> {
        val result = linkedMapOf<String, String>()
        if (defaults == null) return result
        for (i in 0 until defaults.length()) {
            val tier = defaults.optJSONObject(i) ?: continue
            val names = tier.keys()
            while (names.hasNext()) {
                val tierName = names.next()
                val pairs = tier.optJSONArray(tierName) ?: continue
                for (j in 0 until pairs.length()) {
                    val pair = pairs.optJSONArray(j) ?: continue
                    if (pair.length() >= 2) result[pair.optString(0)] = pair.get(1).toString()
                }
            }
        }
        return result
    }

    fun export(context: Context, destination: Uri, report: Report): Result<Unit> = runCatching {
        context.contentResolver.openOutputStream(destination, "wt")?.use {
            it.write(report.rawJson.toByteArray(Charsets.UTF_8))
            it.flush()
        } ?: error("Cannot write exported JSON")
    }
}

@Composable
fun Asphalt8ApkConfigCard(selectedApkUri: Uri?) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val scope = rememberCoroutineScope()
    var report by remember { mutableStateOf<Asphalt8ApkConfig.Report?>(null) }
    var status by remember { mutableStateOf("Choose Asphalt 8 Base APK in the APK Inspector above, then extract its game settings.") }
    var busy by remember { mutableStateOf(false) }
    val exportPicker = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/json")
    ) { uri ->
        val current = report
        if (uri != null && current != null) scope.launch {
            Asphalt8ApkConfig.export(context, uri, current)
                .onSuccess { status = "Exported the original APK config JSON. No game files were changed." }
                .onFailure { status = "Export failed: ${it.message}" }
        }
    }

    Card(
        colors = CardDefaults.cardColors(containerColor = AsphaltPanel),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(9.dp)) {
            Text("ASPHALT 8 · BASE APK CONFIG READER", color = AsphaltViolet, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold)
            Text("Reads the actual gameoptions.json entry from the selected APK and reports the default GPU settings and resolution profiles.", color = AsphaltMuted, style = MaterialTheme.typography.bodySmall)
            Text(
                if (selectedApkUri == null) "No APK selected yet." else "APK selected in the inspector above.",
                color = AsphaltMuted,
                style = MaterialTheme.typography.bodySmall
            )
            Button(
                enabled = selectedApkUri != null && !busy,
                onClick = {
                    val uri = selectedApkUri ?: return@Button
                    busy = true
                    status = "Scanning APK entries for gameoptions.json…"
                    scope.launch {
                        val result = withContext(Dispatchers.IO) { Asphalt8ApkConfig.extract(context, uri) }
                        result.onSuccess {
                            report = it
                            status = "Found ${it.entryName} · ${it.profileNames.size} profile groups · ${it.resolutionScales.size} resolution overrides."
                        }.onFailure {
                            report = null
                            status = "Extraction failed: ${it.message}"
                        }
                        busy = false
                    }
                },
                modifier = Modifier.fillMaxWidth()
            ) { Text(if (busy) "Scanning…" else "Extract gameoptions.json from APK") }

            report?.let { data ->
                Text("Found: ${data.entryName}", style = MaterialTheme.typography.bodySmall, color = AsphaltMuted)
                Text("Profile groups: ${data.profileNames.joinToString()}", style = MaterialTheme.typography.bodySmall)
                Text("GPU override profiles: ${data.gpuOverrideCount}", style = MaterialTheme.typography.bodySmall)
                Text("GPU defaults", color = AsphaltViolet, fontWeight = FontWeight.SemiBold)
                listOf("GFXOption", "FPSLimit", "useShadows", "postFX", "numPostEffectAASamples", "roadTextureAnisotropy").forEach { key ->
                    data.gpuDefaults[key]?.let { value ->
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                            Text(key, color = AsphaltMuted, style = MaterialTheme.typography.bodySmall)
                            Text(value, style = MaterialTheme.typography.bodySmall, fontWeight = FontWeight.SemiBold)
                        }
                    }
                }
                Text("Resolution overrides (scaleDisplay)", color = AsphaltViolet, fontWeight = FontWeight.SemiBold)
                if (data.resolutionScales.isEmpty()) {
                    Text("No RES_* scaleDisplay entries found.", color = AsphaltMuted, style = MaterialTheme.typography.bodySmall)
                } else {
                    data.resolutionScales.forEach { (name, value) ->
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                            Text(name, color = AsphaltMuted, style = MaterialTheme.typography.bodySmall)
                            Text(value, style = MaterialTheme.typography.bodySmall, fontWeight = FontWeight.SemiBold)
                        }
                    }
                }
                OutlinedButton(
                    onClick = { exportPicker.launch("asphalt8-gameoptions-extracted.json") },
                    modifier = Modifier.fillMaxWidth()
                ) { Text("Export extracted JSON") }
            }
            Text(status, color = if (status.contains("failed", true)) MaterialTheme.colorScheme.error else AsphaltMuted, style = MaterialTheme.typography.bodySmall)
            Text(
                "Read-only stage: this extracts and exports the APK's embedded config. It does not rewrite/sign the APK or prove the game accepts an external replacement. Render-scale semantics and profile selection must be verified before enabling edits.",
                color = AsphaltMuted,
                style = MaterialTheme.typography.labelSmall
            )
        }
    }
}
