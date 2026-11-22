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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.horizontalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
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
private const val DEFAULT_CONFIG_TARGET = "Default"

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

    data class EditableSetting(
        val key: String,
        val value: Any?,
        val description: String,
        val range: String,
        val overridden: Boolean,
        val hasDefault: Boolean
    )

    private data class Definition(
        val key: String,
        val value: Any?,
        val description: String,
        val range: String
    )

    fun extract(context: Context, apkUri: Uri): Result<Report> = runCatching {
        val extracted = context.contentResolver.openInputStream(apkUri)?.use { input ->
            ZipInputStream(input).use { zip ->
                var entryName: String? = null
                var bytes: ByteArray? = null
                while (true) {
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
                        entryName = entry.name
                        bytes = output.toByteArray()
                        zip.closeEntry()
                        break
                    }
                    zip.closeEntry()
                }
                val resultBytes = bytes ?: error("No gameoptions.json entry found in this APK")
                (entryName ?: "gameoptions.json") to resultBytes
            }
        } ?: error("Cannot open selected APK")

        val root = JSONObject(extracted.second.toString(Charsets.UTF_8))
        require(root.optJSONObject("header") != null) { "Missing gameoptions header" }
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
                if (value != null && value != JSONObject.NULL) resolution[name] = value.toString()
            }
        }
        Report(
            entryName = extracted.first,
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

    fun targets(jsonText: String, section: String): List<String> {
        val profile = JSONObject(jsonText).getJSONObject("profiles").getJSONObject(section)
        val overrides = profile.optJSONObject("overrides") ?: JSONObject()
        val names = mutableListOf(DEFAULT_CONFIG_TARGET)
        val iterator = overrides.keys()
        while (iterator.hasNext()) names += iterator.next()
        return names
    }

    fun settings(jsonText: String, section: String, target: String): List<EditableSetting> {
        val profile = JSONObject(jsonText).getJSONObject("profiles").getJSONObject(section)
        val definitions = readDefinitions(profile.optJSONArray("defaults")).associateBy { it.key }
        if (target == DEFAULT_CONFIG_TARGET) {
            return definitions.values.map {
                EditableSetting(it.key, it.value, it.description, it.range, overridden = false, hasDefault = true)
            }
        }

        val overrides = profile.optJSONObject("overrides") ?: error("No override table for $section")
        val values = overrides.optJSONObject(target) ?: error("Unknown $section override: $target")
        val keys = LinkedHashSet<String>()
        keys.addAll(definitions.keys)
        val iterator = values.keys()
        while (iterator.hasNext()) keys += iterator.next()

        return keys.mapNotNull { key ->
            val definition = definitions[key]
            val hasOverride = values.has(key)
            val value = if (hasOverride) values.opt(key) else definition?.value
            if (value == null || value == JSONObject.NULL) return@mapNotNull null
            EditableSetting(
                key = key,
                value = value,
                description = definition?.description ?: "Existing override value",
                range = definition?.range.orEmpty(),
                overridden = hasOverride,
                hasDefault = definition != null
            )
        }
    }

    fun update(
        jsonText: String,
        section: String,
        target: String,
        key: String,
        rawValue: String?
    ): Result<String> = runCatching {
        val root = JSONObject(jsonText)
        val profile = root.getJSONObject("profiles").getJSONObject(section)
        val definitions = readDefinitions(profile.optJSONArray("defaults")).associateBy { it.key }
        val definition = definitions[key]
        val overrides = profile.optJSONObject("overrides") ?: error("Missing override table")
        val currentOverride = if (target == DEFAULT_CONFIG_TARGET) null else
            overrides.optJSONObject(target) ?: error("Unknown override profile: $target")

        if (rawValue == null) {
            require(target != DEFAULT_CONFIG_TARGET) { "Default values cannot be removed" }
            require(definition != null) { "This setting has no default to inherit" }
            currentOverride?.remove(key)
        } else {
            val currentValue = if (target == DEFAULT_CONFIG_TARGET) {
                definition?.value ?: error("Unknown default setting: $key")
            } else if (currentOverride?.has(key) == true) {
                currentOverride.opt(key)
            } else {
                definition?.value ?: error("Setting has no existing value: $key")
            }
            val parsed = parseValue(rawValue, currentValue, definition?.range.orEmpty())
            if (target == DEFAULT_CONFIG_TARGET) {
                val defaults = profile.optJSONArray("defaults") ?: error("Missing defaults array")
                var updated = false
                for (i in 0 until defaults.length()) {
                    val tier = defaults.optJSONObject(i) ?: continue
                    val pairs = tier.optJSONArray(DEFAULT_CONFIG_TARGET) ?: continue
                    for (j in 0 until pairs.length()) {
                        val pair = pairs.optJSONArray(j) ?: continue
                        if (pair.optString(0) == key) {
                            pair.put(1, parsed)
                            updated = true
                            break
                        }
                    }
                    if (updated) break
                }
                require(updated) { "Could not locate default pair for $key" }
            } else {
                currentOverride?.put(key, parsed)
            }
        }
        root.toString(2)
    }

    fun export(context: Context, destination: Uri, jsonText: String): Result<Unit> = runCatching {
        context.contentResolver.openOutputStream(destination, "wt")?.use {
            it.write(jsonText.toByteArray(Charsets.UTF_8))
            it.flush()
        } ?: error("Cannot write exported JSON")
    }

    private fun readDefaults(defaults: JSONArray?): Map<String, String> =
        readDefinitions(defaults).associate { it.key to (it.value?.toString() ?: "null") }

    private fun readDefinitions(defaults: JSONArray?): List<Definition> {
        val result = mutableListOf<Definition>()
        if (defaults == null) return result
        for (i in 0 until defaults.length()) {
            val tier = defaults.optJSONObject(i) ?: continue
            val pairs = tier.optJSONArray(DEFAULT_CONFIG_TARGET) ?: continue
            for (j in 0 until pairs.length()) {
                val pair = pairs.optJSONArray(j) ?: continue
                if (pair.length() < 2) continue
                result += Definition(
                    key = pair.optString(0),
                    value = pair.opt(1).takeUnless { it == JSONObject.NULL },
                    description = pair.optString(2, pair.optString(0)),
                    range = pair.optString(3, "")
                )
            }
        }
        return result
    }

    private fun parseValue(raw: String, current: Any?, range: String): Any {
        return when (current) {
            is Boolean -> raw.toBooleanStrictOrNull() ?: error("Enter true or false")
            is Int -> raw.toIntOrNull()?.also { validateRange(it.toDouble(), range) }
                ?: error("Enter a whole number")
            is Long -> raw.toLongOrNull()?.also { validateRange(it.toDouble(), range) }
                ?: error("Enter a whole number")
            is Float -> raw.toFloatOrNull()?.also { validateRange(it.toDouble(), range) }
                ?: error("Enter a number")
            is Number -> raw.toDoubleOrNull()?.also { validateRange(it, range) }
                ?: error("Enter a number")
            is String -> raw
            else -> error("Unsupported setting type")
        }
    }

    private fun validateRange(value: Double, range: String) {
        val match = Regex("""^\[\s*(-?\d+(?:\.\d+)?)?\s*;\s*(-?\d+(?:\.\d+)?)?\s*\]$""")
            .matchEntire(range.trim()) ?: return
        val minimum = match.groupValues[1].toDoubleOrNull()
        val maximum = match.groupValues[2].toDoubleOrNull()
        require(minimum == null || value >= minimum) { "Value is below the allowed minimum $minimum" }
        require(maximum == null || value <= maximum) { "Value is above the allowed maximum $maximum" }
    }
}

@Composable
fun Asphalt8ApkConfigCard(selectedApkUri: Uri?) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val scope = rememberCoroutineScope()
    var report by remember { mutableStateOf<Asphalt8ApkConfig.Report?>(null) }
    var status by remember { mutableStateOf("Choose the Asphalt 8 Base APK in the APK Inspector above.") }
    var busy by remember { mutableStateOf(false) }
    var editedJson by remember { mutableStateOf("") }
    var selectedSection by remember { mutableStateOf("GPU") }
    var selectedTarget by remember { mutableStateOf(DEFAULT_CONFIG_TARGET) }
    var editStatus by remember { mutableStateOf("") }

    val exportPicker = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/json")
    ) { uri ->
        val currentJson = editedJson
        if (uri != null && currentJson.isNotBlank()) scope.launch {
            withContext(Dispatchers.IO) { Asphalt8ApkConfig.export(context, uri, currentJson) }
                .onSuccess { status = "Exported edited gameoptions.json. The APK and installed game were not changed." }
                .onFailure { status = "Export failed: ${it.message}" }
        }
    }

    val targets = remember(editedJson, selectedSection) {
        runCatching { Asphalt8ApkConfig.targets(editedJson, selectedSection) }.getOrDefault(listOf(DEFAULT_CONFIG_TARGET))
    }
    val activeTarget = selectedTarget.takeIf { it in targets } ?: DEFAULT_CONFIG_TARGET
    val settings = remember(editedJson, selectedSection, activeTarget) {
        runCatching { Asphalt8ApkConfig.settings(editedJson, selectedSection, activeTarget) }.getOrDefault(emptyList())
    }

    Card(
        colors = CardDefaults.cardColors(containerColor = AsphaltPanel),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(9.dp)) {
            Text("ASPHALT 8 · APK CONFIG EDITOR", color = AsphaltViolet, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold)
            Text(
                "Extracts the game's embedded gameoptions.json, lets you edit its existing defaults and override profiles, then exports a separate JSON file.",
                color = AsphaltMuted,
                style = MaterialTheme.typography.bodySmall
            )
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
                            editedJson = it.rawJson
                            selectedSection = "GPU"
                            selectedTarget = DEFAULT_CONFIG_TARGET
                            editStatus = ""
                            status = "Found ${it.entryName} · ${it.profileNames.size} profile groups · ${it.resolutionScales.size} resolution profiles."
                        }.onFailure {
                            report = null
                            editedJson = ""
                            status = "Extraction failed: ${it.message}"
                        }
                        busy = false
                    }
                },
                modifier = Modifier.fillMaxWidth()
            ) { Text(if (busy) "Scanning…" else "Extract and load gameoptions.json") }

            report?.let { data ->
                Text("Source entry: ${data.entryName}", color = AsphaltMuted, style = MaterialTheme.typography.bodySmall)
                Text("Groups: ${data.profileNames.joinToString()}", style = MaterialTheme.typography.bodySmall)
                Text("GPU override profiles: ${data.gpuOverrideCount}", style = MaterialTheme.typography.bodySmall)
                Text(
                    if (editedJson != data.rawJson) "Edited copy · original extracted data remains unchanged" else "Original extracted values loaded",
                    color = if (editedJson != data.rawJson) AsphaltViolet else AsphaltMuted,
                    style = MaterialTheme.typography.labelSmall
                )

                Text("CONFIG SECTION", color = AsphaltViolet, fontWeight = FontWeight.SemiBold)
                Row(
                    modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    listOf("GPU", "CPU", "MEM", "Specific", "ColorCorrection").forEach { section ->
                        OutlinedButton(
                            onClick = {
                                selectedSection = section
                                selectedTarget = DEFAULT_CONFIG_TARGET
                                editStatus = ""
                            }
                        ) { Text(section, maxLines = 1) }
                    }
                }

                Text("PROFILE / OVERRIDE", color = AsphaltViolet, fontWeight = FontWeight.SemiBold)
                ProfileTargetPicker(
                    targets = targets,
                    selected = activeTarget,
                    onSelected = {
                        selectedTarget = it
                        editStatus = ""
                    }
                )
                Text(
                    if (activeTarget == DEFAULT_CONFIG_TARGET) {
                        "Editing the section's Default values. Existing profile overrides remain separate."
                    } else {
                        "Editing only ${activeTarget}. The game applies this override only if its own profile selection rules choose it."
                    },
                    color = AsphaltMuted,
                    style = MaterialTheme.typography.labelSmall
                )

                if (settings.isEmpty()) {
                    Text("No editable values were found in this section/profile.", color = AsphaltMuted, style = MaterialTheme.typography.bodySmall)
                } else {
                    settings.forEach { setting ->
                        AsphaltSettingRow(
                            setting = setting,
                            isDefaultTarget = activeTarget == DEFAULT_CONFIG_TARGET,
                            onSet = { value ->
                                Asphalt8ApkConfig.update(editedJson, selectedSection, activeTarget, setting.key, value)
                                    .onSuccess {
                                        editedJson = it
                                        editStatus = "Updated ${setting.key} in ${activeTarget}."
                                    }
                                    .onFailure { editStatus = "Not changed: ${it.message}" }
                            },
                            onReset = {
                                Asphalt8ApkConfig.update(editedJson, selectedSection, activeTarget, setting.key, null)
                                    .onSuccess {
                                        editedJson = it
                                        editStatus = "Restored ${setting.key} to its default value."
                                    }
                                    .onFailure { editStatus = "Reset failed: ${it.message}" }
                            }
                        )
                    }
                }

                if (editStatus.isNotBlank()) {
                    Text(
                        editStatus,
                        color = if (editStatus.contains("failed", true) || editStatus.contains("Not changed", true)) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.secondary,
                        style = MaterialTheme.typography.bodySmall
                    )
                }
                OutlinedButton(
                    onClick = { exportPicker.launch("asphalt8-gameoptions-edited.json") },
                    enabled = editedJson.isNotBlank(),
                    modifier = Modifier.fillMaxWidth()
                ) { Text("Export edited gameoptions.json") }
            }
            Text(
                status,
                color = if (status.contains("failed", true)) MaterialTheme.colorScheme.error else AsphaltMuted,
                style = MaterialTheme.typography.bodySmall
            )
            Text(
                "Important: this edits and exports the APK's embedded JSON only. It does not inject it into the installed game or rewrite/sign the APK. Profile selection, accepted file location, and the meaning of scaleDisplay still require runtime verification. No FSR, CAS, or real render-scale effect is claimed.",
                color = AsphaltMuted,
                style = MaterialTheme.typography.labelSmall
            )
        }
    }
}

@Composable
private fun ProfileTargetPicker(
    targets: List<String>,
    selected: String,
    onSelected: (String) -> Unit
) {
    var expanded by remember { mutableStateOf(false) }
    Column {
        OutlinedButton(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth()) {
            Text(selected, maxLines = 1)
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            targets.forEach { target ->
                DropdownMenuItem(
                    text = { Text(target) },
                    onClick = {
                        onSelected(target)
                        expanded = false
                    }
                )
            }
        }
    }
}

@Composable
private fun AsphaltSettingRow(
    setting: Asphalt8ApkConfig.EditableSetting,
    isDefaultTarget: Boolean,
    onSet: (String) -> Unit,
    onReset: () -> Unit
) {
    var draft by remember(setting.key, setting.value) {
        mutableStateOf(setting.value?.toString().orEmpty())
    }
    Column(
        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
        verticalArrangement = Arrangement.spacedBy(5.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(setting.key, fontWeight = FontWeight.SemiBold, style = MaterialTheme.typography.bodyMedium)
                Text(
                    if (isDefaultTarget) "Default value" else if (setting.overridden) "Explicit override" else "Inherited from Default",
                    color = AsphaltMuted,
                    style = MaterialTheme.typography.labelSmall
                )
            }
            if (setting.value is Boolean) {
                Switch(
                    checked = setting.value,
                    onCheckedChange = { onSet(it.toString()) }
                )
            }
        }
        if (setting.value !is Boolean) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedTextField(
                    value = draft,
                    onValueChange = { draft = it },
                    label = { Text("Value") },
                    singleLine = true,
                    modifier = Modifier.weight(1f)
                )
                Button(onClick = { onSet(draft) }) { Text("Set") }
            }
        }
        if (setting.range.isNotBlank()) {
            Text("Declared range: ${setting.range}", color = AsphaltMuted, style = MaterialTheme.typography.labelSmall)
        }
        if (setting.description.isNotBlank()) {
            Text(setting.description, color = AsphaltMuted, style = MaterialTheme.typography.labelSmall)
        }
        if (!isDefaultTarget && setting.overridden && setting.hasDefault) {
            TextButton(onClick = onReset) { Text("Use Default") }
        }
    }
}
