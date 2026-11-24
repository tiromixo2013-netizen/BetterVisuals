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

internal fun readUltraScaleSettings(context: Context): UltraScaleSettings {
    val p = context.getSharedPreferences(ULTRASCALE_PREFS, Context.MODE_PRIVATE)
    fun enumValue(key: String, fallback: String) = p.getString(key, fallback) ?: fallback
    return UltraScaleSettings(
        renderScalePercent = p.getInt("renderScale", 70),
        fpsLimit = p.getInt("fpsLimit", 240),
        upscaler = runCatching {
            UpscaleMode.valueOf(enumValue("upscaler", UpscaleMode.BILINEAR.name))
        }.getOrDefault(UpscaleMode.BILINEAR),
        upscaleSharpnessPercent = p.getInt("upscaleSharpness", 20),
        preset = runCatching {
            UpscalePreset.valueOf(enumValue("preset", UpscalePreset.CUSTOM.name))
        }.getOrDefault(UpscalePreset.CUSTOM),
        casSharpnessPercent = p.getInt("casSharpness", 0),
        lowLatency = runCatching {
            LowLatencyMode.valueOf(enumValue("lowLatency", LowLatencyMode.OFF.name))
        }.getOrDefault(LowLatencyMode.OFF),
        integerScaling = p.getBoolean("integerScaling", false),
        frameGeneration = runCatching {
            FrameGenerationMode.valueOf(enumValue("frameGeneration", FrameGenerationMode.OFF.name))
        }.getOrDefault(FrameGenerationMode.OFF),
        framePacing = p.getBoolean("framePacing", true),
        vsyncAligned = p.getBoolean("vsyncAligned", true),
        lowQueue = p.getBoolean("lowQueue", true),
        queueDepth = p.getInt("queueDepth", 1),
        bidirectionalFlowCheck = p.getBoolean("bidirectionalFlowCheck", true),
        sceneCutDetection = p.getBoolean("sceneCutDetection", true),
        disocclusionRejection = p.getBoolean("disocclusionRejection", true),
        edgeAwareWarping = p.getBoolean("edgeAwareWarping", true),
        hudGhostingSuppression = p.getBoolean("hudGhostingSuppression", true)
    ).normalized()
}

private fun writeUltraScaleSettings(context: Context, settings: UltraScaleSettings) {
    context.getSharedPreferences(ULTRASCALE_PREFS, Context.MODE_PRIVATE).edit()
        .putInt("renderScale", settings.renderScalePercent)
        .putInt("fpsLimit", settings.fpsLimit)
        .putString("upscaler", settings.upscaler.name)
        .putInt("upscaleSharpness", settings.upscaleSharpnessPercent)
        .putString("preset", settings.preset.name)
        .putInt("casSharpness", settings.casSharpnessPercent)
        .putString("lowLatency", settings.lowLatency.name)
        .putBoolean("integerScaling", settings.integerScaling)
        .putString("frameGeneration", settings.frameGeneration.name)
        .putBoolean("framePacing", settings.framePacing)
        .putBoolean("vsyncAligned", settings.vsyncAligned)
        .putBoolean("lowQueue", settings.lowQueue)
        .putInt("queueDepth", settings.queueDepth)
        .putBoolean("bidirectionalFlowCheck", settings.bidirectionalFlowCheck)
        .putBoolean("sceneCutDetection", settings.sceneCutDetection)
        .putBoolean("disocclusionRejection", settings.disocclusionRejection)
        .putBoolean("edgeAwareWarping", settings.edgeAwareWarping)
        .putBoolean("hudGhostingSuppression", settings.hudGhostingSuppression)
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
    val frameGenerationEnabled = settings.frameGeneration != FrameGenerationMode.OFF

    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)
    ) {
        Column(
            Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Text(
                "ULTRASCALE · GRAPHICS PROFILE",
                color = MaterialTheme.colorScheme.primary,
                style = MaterialTheme.typography.labelMedium,
                fontWeight = FontWeight.Bold
            )
            Text(
                "Saved locally on this device",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            // 1 — Render Scale
            SectionTitle("1 · RENDER SCALE")
            SettingValue("Render scale", "${settings.renderScalePercent}% · before upscaling")
            Slider(
                value = settings.renderScalePercent.toFloat(),
                onValueChange = {
                    update(settings.copy(
                        renderScalePercent = it.toInt(),
                        preset = UpscalePreset.CUSTOM
                    ))
                },
                valueRange = 50f..100f,
                steps = 9
            )

            SettingValue("FPS limit", "${settings.fpsLimit} FPS · selected build profile")
            EnumPicker(
                label = "${settings.fpsLimit} FPS",
                values = FpsLimitOption.entries,
                title = { "${it.value} FPS" },
                onSelect = { update(settings.copy(fpsLimit = it.value)) }
            )

            HorizontalDivider()

            // 2 — Upscaling
            SectionTitle("2 · UPSCALING")
            EnumPicker(
                label = settings.upscaler.title,
                values = UpscaleMode.entries,
                title = { it.title },
                onSelect = {
                    update(settings.copy(upscaler = it, preset = UpscalePreset.CUSTOM))
                }
            )
            Text(
                when (settings.upscaler) {
                    UpscaleMode.OFF -> "Pass-through mode; no spatial reconstruction is applied."
                    UpscaleMode.NEAREST, UpscaleMode.BILINEAR, UpscaleMode.FSR_LITE,
                    UpscaleMode.MWSR, UpscaleMode.SGSR1 ->
                        "Experimental GLES layer path is wired for qualifying low-resolution color blits; actual Asphalt 8 attachment and visual output still need device verification."
                    UpscaleMode.SGSR2 ->
                        "Temporal SGSR2 still requires game motion vectors, depth, jitter and history buffers; not active in Asphalt 8."
                },
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodySmall
            )
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth()
            ) {
                Column(Modifier.weight(1f)) {
                    Text("Integer scaling", fontWeight = FontWeight.SemiBold)
                    Text(
                        "Nearest-neighbour integer-ratio output; adapter required",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = settings.integerScaling,
                    onCheckedChange = {
                        update(settings.copy(integerScaling = it, preset = UpscalePreset.CUSTOM))
                    }
                )
            }

            // 3 — Upscaling Sharpness
            HorizontalDivider()
            SectionTitle("3 · UPSCALING SHARPNESS")
            SettingValue(
                "Upscaling sharpness",
                "${settings.upscaleSharpnessPercent}% · reconstruction stage"
            )
            Slider(
                value = settings.upscaleSharpnessPercent.toFloat(),
                onValueChange = {
                    update(settings.copy(
                        upscaleSharpnessPercent = it.toInt(),
                        preset = UpscalePreset.CUSTOM
                    ))
                },
                valueRange = 0f..100f
            )

            // 4 — Presets must be immediately after Upscaling Sharpness and before CAS.
            HorizontalDivider()
            SectionTitle("4 · UPSCALING PRESETS")
            EnumPicker(
                label = settings.preset.title,
                values = UpscalePreset.entries,
                title = { it.title },
                onSelect = { update(UltraScaleSettings.forPreset(it).copy(
                    lowLatency = settings.lowLatency,
                    integerScaling = settings.integerScaling,
                    frameGeneration = settings.frameGeneration,
                    framePacing = settings.framePacing,
                    vsyncAligned = settings.vsyncAligned,
                    lowQueue = settings.lowQueue,
                    queueDepth = settings.queueDepth,
                    bidirectionalFlowCheck = settings.bidirectionalFlowCheck,
                    sceneCutDetection = settings.sceneCutDetection,
                    disocclusionRejection = settings.disocclusionRejection,
                    edgeAwareWarping = settings.edgeAwareWarping,
                    hudGhostingSuppression = settings.hudGhostingSuppression
                )) }
            )

            // 5 — CAS
            HorizontalDivider()
            SectionTitle("5 · CAS")
            SettingValue(
                "CAS Sharpness",
                "${settings.casSharpnessPercent}% · 0 disables CAS"
            )
            Slider(
                value = settings.casSharpnessPercent.toFloat(),
                onValueChange = {
                    update(settings.copy(
                        casSharpnessPercent = it.toInt(),
                        preset = UpscalePreset.CUSTOM
                    ))
                },
                valueRange = 0f..100f
            )

            // 6 — Low Latency
            HorizontalDivider()
            SectionTitle("6 · LOW LATENCY")
            EnumPicker(
                label = settings.lowLatency.title,
                values = LowLatencyMode.entries,
                title = { it.title },
                onSelect = { update(settings.copy(lowLatency = it, preset = UpscalePreset.CUSTOM)) }
            )
            Text(
                "Requires control of the game's frame queue. Not active in Asphalt 8 until a compatible adapter is available.",
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodySmall
            )

            // 7 — All Frame Generation and Frame Pacing controls stay in this same card/window.
            HorizontalDivider()
            SectionTitle("7 · FRAME GENERATION")
            EnumPicker(
                label = settings.frameGeneration.title,
                values = FrameGenerationMode.entries,
                title = { it.title },
                onSelect = { update(settings.copy(frameGeneration = it)) }
            )
            Text(
                "Algorithm · ${FrameGenerationAlgorithm.PYRAMIDAL_LUCAS_KANADE.title}",
                fontWeight = FontWeight.SemiBold
            )
            Text(
                "One algorithm only. The x2, x3 and x4 choices are saved as profile settings.",
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodySmall
            )

            SwitchSettingRow(
                title = "Bidirectional Flow Check",
                description = "Reject inconsistent forward/backward motion estimates.",
                checked = settings.bidirectionalFlowCheck,
                enabled = frameGenerationEnabled,
                onCheckedChange = { update(settings.copy(bidirectionalFlowCheck = it)) }
            )
            SwitchSettingRow(
                title = "Scene Cut Detection",
                description = "Avoid interpolation across abrupt scene changes.",
                checked = settings.sceneCutDetection,
                enabled = frameGenerationEnabled,
                onCheckedChange = { update(settings.copy(sceneCutDetection = it)) }
            )
            SwitchSettingRow(
                title = "Disocclusion Rejection",
                description = "Reject newly exposed regions without reliable history.",
                checked = settings.disocclusionRejection,
                enabled = frameGenerationEnabled,
                onCheckedChange = { update(settings.copy(disocclusionRejection = it)) }
            )
            SwitchSettingRow(
                title = "Edge-Aware Warping",
                description = "Reduce edge tearing while warping the intermediate frame.",
                checked = settings.edgeAwareWarping,
                enabled = frameGenerationEnabled,
                onCheckedChange = { update(settings.copy(edgeAwareWarping = it)) }
            )
            SwitchSettingRow(
                title = "HUD Ghosting Suppression",
                description = "Reduce trails around static UI elements.",
                checked = settings.hudGhostingSuppression,
                enabled = frameGenerationEnabled,
                onCheckedChange = { update(settings.copy(hudGhostingSuppression = it)) }
            )

            HorizontalDivider()
            Text("FRAME PACING & PRESENTATION", color = MaterialTheme.colorScheme.primary, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold)
            SwitchSettingRow(
                title = "Frame Pacing",
                description = "Keep generated frames evenly spaced.",
                checked = settings.framePacing,
                enabled = frameGenerationEnabled,
                onCheckedChange = { update(settings.copy(framePacing = it)) }
            )
            SwitchSettingRow(
                title = "VSync Aligned",
                description = "Align generated-frame presentation with display refresh.",
                checked = settings.vsyncAligned,
                enabled = frameGenerationEnabled,
                onCheckedChange = { update(settings.copy(vsyncAligned = it)) }
            )
            SwitchSettingRow(
                title = "Low Queue",
                description = "Prefer a shorter presentation queue to limit latency.",
                checked = settings.lowQueue,
                enabled = frameGenerationEnabled,
                onCheckedChange = { update(settings.copy(lowQueue = it)) }
            )
            Text("Queue Depth", fontWeight = FontWeight.SemiBold)
            EnumPicker(
                label = "Depth ${settings.queueDepth}",
                values = listOf(1, 2, 3),
                enabled = frameGenerationEnabled,
                title = { "Depth $it" },
                onSelect = { update(settings.copy(queueDepth = it)) }
            )
            Text(
                "Saved profile only: Asphalt 8 still needs a compatible in-process adapter with frame history, motion estimation and presentation control before generated frames can be displayed.",
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodySmall
            )

            Button(
                onClick = { update(UltraScaleSettings()) },
                modifier = Modifier.fillMaxWidth()
            ) { Text("Reset graphics profile") }
        }
    }
}

@Composable
private fun SectionTitle(title: String) {
    Text(
        title,
        color = MaterialTheme.colorScheme.primary,
        style = MaterialTheme.typography.labelMedium,
        fontWeight = FontWeight.Bold
    )
}

@Composable
private fun SettingValue(title: String, value: String) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
        Text(title, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
        Text(value, color = MaterialTheme.colorScheme.primary, style = MaterialTheme.typography.labelMedium)
    }
}

@Composable
private fun SwitchSettingRow(
    title: String,
    description: String,
    checked: Boolean,
    enabled: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, fontWeight = FontWeight.SemiBold)
            Text(
                description,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            enabled = enabled
        )
    }
}

@Composable
private fun <T> EnumPicker(
    label: String,
    values: List<T>,
    title: (T) -> String,
    onSelect: (T) -> Unit,
    enabled: Boolean = true
) {
    var expanded by remember { mutableStateOf(false) }
    Column {
        Button(onClick = { expanded = true }, enabled = enabled, modifier = Modifier.fillMaxWidth()) { Text(label) }
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
