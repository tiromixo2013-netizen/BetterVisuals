package dev.ultrascale.mobile

import android.content.Context
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

private const val ULTRASCALE_PREFS = "ultrascale_graphics_settings"

private fun readUltraScaleSettings(context: Context): UltraScaleSettings {
    val p = context.getSharedPreferences(ULTRASCALE_PREFS, Context.MODE_PRIVATE)
    fun enumValue(key: String, fallback: String) = p.getString(key, fallback) ?: fallback
    return UltraScaleSettings(
        renderScalePercent = p.getInt("renderScale", 70),
        upscaler = runCatching { UpscaleMode.valueOf(enumValue("upscaler", UpscaleMode.BILINEAR.name)) }.getOrDefault(UpscaleMode.BILINEAR),
        upscaleSharpnessPercent = p.getInt("upscaleSharpness", 20),
        preset = runCatching { UpscalePreset.valueOf(enumValue("preset", UpscalePreset.CUSTOM.name)) }.getOrDefault(UpscalePreset.CUSTOM),
        casSharpnessPercent = p.getInt("casSharpness", 0),
        lowLatency = runCatching { LowLatencyMode.valueOf(enumValue("lowLatency", LowLatencyMode.OFF.name)) }.getOrDefault(LowLatencyMode.OFF),
        integerScaling = p.getBoolean("integerScaling", false)
    ).normalized()
}

private fun writeUltraScaleSettings(context: Context, settings: UltraScaleSettings) {
    context.getSharedPreferences(ULTRASCALE_PREFS, Context.MODE_PRIVATE).edit()
        .putInt("renderScale", settings.renderScalePercent)
        .putString("upscaler", settings.upscaler.name)
        .putInt("upscaleSharpness", settings.upscaleSharpnessPercent)
        .putString("preset", settings.preset.name)
        .putInt("casSharpness", settings.casSharpnessPercent)
        .putString("lowLatency", settings.lowLatency.name)
        .putBoolean("integerScaling", settings.integerScaling)
        .apply()
}

@Composable
fun UltraScaleControlsCard() {
    val context = LocalContext.current
    var settings by remember(context) { mutableStateOf(readUltraScaleSettings(context)) }
    fun update(next: UltraScaleSettings) {
        settings = next.normalized()
        writeUltraScaleSettings(context, settings)
    }

    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)
    ) {
        Column(
            Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Text("ULTRASCALE · GRAPHICS PROFILE", color = MaterialTheme.colorScheme.primary, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold)
            Text("Saved locally on this device", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)

            Text("Quality preset", fontWeight = FontWeight.SemiBold)
            EnumPicker(
                label = settings.preset.title,
                values = UpscalePreset.entries,
                title = { it.title },
                onSelect = { update(UltraScaleSettings.forPreset(it)) }
            )

            HorizontalDivider()
            SettingValue("Render scale", "${settings.renderScalePercent}% · before upscaling")
            Slider(
                value = settings.renderScalePercent.toFloat(),
                onValueChange = { update(settings.copy(renderScalePercent = it.toInt(), preset = UpscalePreset.CUSTOM)) },
                valueRange = 50f..100f,
                steps = 9
            )

            Text("Upscaler", fontWeight = FontWeight.SemiBold)
            EnumPicker(
                label = settings.upscaler.title,
                values = UpscaleMode.entries,
                title = { it.title },
                onSelect = { update(settings.copy(upscaler = it, preset = UpscalePreset.CUSTOM)) }
            )
            Text(
                when (settings.upscaler) {
                    UpscaleMode.OFF, UpscaleMode.NEAREST, UpscaleMode.BILINEAR, UpscaleMode.CATMULL_ROM, UpscaleMode.FSR_LITE -> "Available in the app-owned render test only; not yet connected to Asphalt 8's render target."
                    else -> "Shader/reference is bundled, but this technique still needs a validated game render-pass backend."
                },
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodySmall
            )

            SettingValue("Upscaling sharpness", "${settings.upscaleSharpnessPercent}% · reconstruction stage")
            Slider(
                value = settings.upscaleSharpnessPercent.toFloat(),
                onValueChange = { update(settings.copy(upscaleSharpnessPercent = it.toInt(), preset = UpscalePreset.CUSTOM)) },
                valueRange = 0f..100f
            )

            SettingValue("CAS / post-upscale sharpening", "${settings.casSharpnessPercent}% · 0 disables")
            Slider(
                value = settings.casSharpnessPercent.toFloat(),
                onValueChange = { update(settings.copy(casSharpnessPercent = it.toInt(), preset = UpscalePreset.CUSTOM)) },
                valueRange = 0f..100f
            )

            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Column(Modifier.weight(1f)) {
                    Text("Integer scaling", fontWeight = FontWeight.SemiBold)
                    Text("Nearest-neighbour integer-ratio output; adapter required", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                Switch(
                    checked = settings.integerScaling,
                    onCheckedChange = { update(settings.copy(integerScaling = it, preset = UpscalePreset.CUSTOM)) }
                )
            }

            Text("Low latency", fontWeight = FontWeight.SemiBold)
            EnumPicker(
                label = settings.lowLatency.title,
                values = LowLatencyMode.entries,
                title = { it.title },
                onSelect = { update(settings.copy(lowLatency = it, preset = UpscalePreset.CUSTOM)) }
            )
            Text("Requires control of the game's frame queue. Not active in Asphalt 8 until a compatible adapter is available.", color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)

            HorizontalDivider()
            Text("Frame Generation", fontWeight = FontWeight.SemiBold)
            Text("Not enabled for Asphalt 8: the inspected build exposes no supported motion-vector / frame-history interface to this app. No fake ON switch is shown.", color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)

            Button(
                onClick = { update(UltraScaleSettings()) },
                modifier = Modifier.fillMaxWidth()
            ) { Text("Reset graphics profile") }
        }
    }
}

@Composable
private fun SettingValue(title: String, value: String) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
        Text(title, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
        Text(value, color = MaterialTheme.colorScheme.primary, style = MaterialTheme.typography.labelMedium)
    }
}

@Composable
private fun <T> EnumPicker(
    label: String,
    values: List<T>,
    title: (T) -> String,
    onSelect: (T) -> Unit
) {
    var expanded by remember { mutableStateOf(false) }
    Column {
        Button(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth()) { Text(label) }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            values.forEach { value ->
                DropdownMenuItem(
                    text = { Text(title(value)) },
                    onClick = {
                        expanded = false
                        onSelect(value)
                    }
                )
            }
        }
    }
}
