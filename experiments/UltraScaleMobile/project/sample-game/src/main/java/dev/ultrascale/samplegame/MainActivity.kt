package dev.ultrascale.samplegame

import android.app.Activity
import android.content.res.AssetManager
import android.app.AlertDialog
import android.opengl.GLES30
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import javax.microedition.khronos.egl.EGL10
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.egl.EGLContext
import javax.microedition.khronos.egl.EGLDisplay
import javax.microedition.khronos.opengles.GL10

private object NativeGlesBridge {
    external fun process(
        sourceTexture: Int, destinationTexture: Int,
        inputWidth: Int, inputHeight: Int,
        outputWidth: Int, outputHeight: Int,
        sharpness: Float
    ): Int
}

class MainActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var glView: GLSurfaceView
    private lateinit var renderer: UpscaleTestRenderer
    private external fun runVulkanProbe(): String

    companion object {
        init { System.loadLibrary("sample_vulkan_probe") }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(18, 18, 18, 18)
        }
        status = TextView(this).apply {
            text = "UltraScale graphics integration test. Open Advanced to change the live GLES render path."
        }
        val vulkan = Button(this).apply {
            text = "Run Vulkan API test"
            setOnClickListener {
                status.text = runCatching { runVulkanProbe() }.getOrElse { "Vulkan test failed: ${it.message}" }
            }
        }
        val advanced = Button(this).apply {
            text = "Graphics Settings — Advanced"
            setOnClickListener { showAdvancedSettings() }
        }
        renderer = UpscaleTestRenderer(assets)
        val diagnostics = Button(this).apply {
            text = "Refresh GLES diagnostics"
            setOnClickListener {
                glView.queueEvent {
                    val report = renderer.runtimeReport()
                    runOnUiThread { status.text = report }
                }
            }
        }
        glView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(3)
            // Request GLES 3.1 explicitly so SGSR1's GLSL ES 3.10 textureGather
            // shader can compile. Fall back to GLES 3.0 only when the EGL driver
            // does not expose EGL_KHR_create_context or rejects the 3.1 context.
            setEGLContextFactory(object : GLSurfaceView.EGLContextFactory {
                override fun createContext(egl: EGL10, display: EGLDisplay, config: EGLConfig): EGLContext {
                    val extensions = egl.eglQueryString(display, EGL10.EGL_EXTENSIONS).orEmpty()
                        .split(Regex("\\s+"))
                    if ("EGL_KHR_create_context" in extensions) {
                        val context31 = egl.eglCreateContext(
                            display, config, EGL10.EGL_NO_CONTEXT,
                            intArrayOf(0x3098, 3, 0x30FB, 1, EGL10.EGL_NONE)
                        )
                        if (context31 != null && context31 != EGL10.EGL_NO_CONTEXT) return context31
                    }
                    return egl.eglCreateContext(
                        display, config, EGL10.EGL_NO_CONTEXT,
                        intArrayOf(0x3098, 3, EGL10.EGL_NONE)
                    )
                }

                override fun destroyContext(egl: EGL10, display: EGLDisplay, context: EGLContext) {
                    egl.eglDestroyContext(display, context)
                }
            })
            setRenderer(renderer)
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
        root.addView(status, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        root.addView(advanced, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        root.addView(diagnostics, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        root.addView(vulkan, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        root.addView(glView, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)
    }

    private fun showAdvancedSettings() {
        val panel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 12, 24, 4)
        }
        val enabled = Switch(this).apply {
            text = "Enable render-scale + upscale pass"
            isChecked = renderer.upscalingEnabled
        }
        val sharpen = Switch(this).apply {
            text = "Extra sharpening (Bilinear / Nearest only)"
            isChecked = renderer.sharpeningEnabled
        }
        val cas = Switch(this).apply {
            text = "AMD FidelityFX CAS post-sharpen"
            isChecked = renderer.casEnabled
        }
        val casStrengthLabel = TextView(this).apply { text = "CAS sharpness: 50%" }
        val casStrength = SeekBar(this).apply {
            max = 100
            progress = (renderer.casSharpness * 100).toInt()
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    casStrengthLabel.text = "CAS sharpness: $progress%"
                }
                override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit
                override fun onStopTrackingTouch(seekBar: SeekBar?) = Unit
            })
        }
        val presetLabel = TextView(this).apply { text = "Quality preset" }
        val preset = Spinner(this).apply {
            adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item,
                listOf("Ultra Quality — 90%", "Quality — 82%", "Balanced — 75%", "Performance — 67%", "Ultra Performance — 59%", "Custom"))
            val currentPercent = (renderer.renderScale * 100).toInt()
            val presetValues = listOf(90, 82, 75, 67, 59)
            val matching = presetValues.indexOfFirst { kotlin.math.abs(it - currentPercent) <= 1 }
            setSelection(if (matching >= 0) matching else 5)
        }
        val methodLabel = TextView(this).apply { text = "Upscale method" }
        val method = Spinner(this).apply {
            adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item,
                listOf("Bilinear", "Nearest", "SGSR1", "FSR Lite (6-tap)", "MWSR (upstream lightweight)", "AMD FSR1 EASU", "Native GLES3 (bilinear + edge sharpen)"))
            setSelection(renderer.upscaleMethod)
        }
        val scaleLabel = TextView(this).apply {
            text = "Internal render scale: ${(renderer.renderScale * 100).toInt()}%"
        }
        val scale = SeekBar(this).apply {
            max = 50
            progress = ((renderer.renderScale - 0.5f) * 100).toInt().coerceIn(0, 50)
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    scaleLabel.text = "Internal render scale: ${50 + progress}%"
                    if (fromUser && preset.selectedItemPosition != 5) preset.setSelection(5)
                }
                override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit
                override fun onStopTrackingTouch(seekBar: SeekBar?) = Unit
            })
        }
        preset.onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
            override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                val percent = listOf(90, 82, 75, 67, 59).getOrNull(position)
                if (percent != null) scale.progress = percent - 50
            }
        }
        panel.addView(enabled)
        panel.addView(presetLabel)
        panel.addView(preset)
        panel.addView(methodLabel)
        panel.addView(method)
        panel.addView(scaleLabel)
        panel.addView(scale)
        panel.addView(sharpen)
        panel.addView(cas)
        panel.addView(casStrengthLabel)
        panel.addView(casStrength)
        AlertDialog.Builder(this)
            .setTitle("Advanced Graphics")
            .setView(panel)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Apply") { _, _ ->
                val newEnabled = enabled.isChecked
                val newSharpen = sharpen.isChecked
                val newCas = cas.isChecked
                val newCasStrength = casStrength.progress / 100f
                val newMethod = method.selectedItemPosition
                val newScale = (50 + scale.progress) / 100f
                glView.queueEvent {
                    renderer.configure(newEnabled, newMethod, newSharpen, newScale, newCas, newCasStrength)
                    runOnUiThread { status.text = renderer.statusMessage }
                }
            }
            .show()
    }

    override fun onPause() { super.onPause(); if (::glView.isInitialized) glView.onPause() }
    override fun onResume() { super.onResume(); if (::glView.isInitialized) glView.onResume() }
}

private class UpscaleTestRenderer(private val assets: AssetManager) : GLSurfaceView.Renderer {
    @Volatile var upscalingEnabled = true
        private set
    @Volatile var upscaleMethod = 0
        private set
    private val nearestMode: Boolean get() = upscaleMethod == 1
    @Volatile var sharpeningEnabled = true
        private set
    @Volatile var casEnabled = false
        private set
    @Volatile var casSharpness = 0.5f
        private set
    @Volatile var renderScale = 0.75f
        private set
    @Volatile var statusMessage = "Renderer ready."
        private set

    private var sceneProgram = 0
    private var upscaleProgram = 0
    private var sgsr1Program = 0
    private var fsrLiteProgram = 0
    private var mwsrProgram = 0
    private var fsr1Program = 0
    private var casProgram = 0
    private var framebuffer = 0
    private var sceneTexture = 0
    private var postFramebuffer = 0
    private var postTexture = 0
    private var postWidth = 0
    private var postHeight = 0
    private var mwsrParamsDirty = true
    private var sceneWidth = 0
    private var sceneHeight = 0
    private var outputWidth = 0
    private var outputHeight = 0
    private var sceneTargetStatus = "not created"
    private var postTargetStatus = "not created"
    private var sgsr1Capability = "not checked"
    private val shaderDiagnostics = mutableListOf<String>()
    private lateinit var triangleBuffer: FloatBuffer
    private lateinit var quadBuffer: FloatBuffer

    fun configure(enabled: Boolean, method: Int, sharpen: Boolean, scale: Float, cas: Boolean, casAmount: Float) {
        upscalingEnabled = enabled
        val requestedMethod = method.coerceIn(0, 6)
        val available = when (requestedMethod) {
            2 -> sgsr1Program != 0
            3 -> fsrLiteProgram != 0
            4 -> mwsrProgram != 0
            5 -> fsr1Program != 0
            else -> true
        }
        upscaleMethod = if (available) requestedMethod else 0
        sharpeningEnabled = sharpen
        casEnabled = cas && casProgram != 0
        casSharpness = casAmount.coerceIn(0f, 1f)
        renderScale = scale.coerceIn(0.5f, 1f)
        statusMessage = buildString {
            append("Sample GLES: ")
            append(listOf("Bilinear", "Nearest", "SGSR1", "FSR Lite (6-tap)", "MWSR", "AMD FSR1 EASU", "Native GLES3")[upscaleMethod])
            if (!available) append(" (shader unavailable; Bilinear fallback)")
            if (cas) append(if (casEnabled) " + AMD CAS" else " (CAS shader unavailable)")
        }
    }

    fun runtimeReport(): String {
        val vendor = GLES30.glGetString(GLES30.GL_VENDOR) ?: "unknown"
        val renderer = GLES30.glGetString(GLES30.GL_RENDERER) ?: "unknown"
        val version = GLES30.glGetString(GLES30.GL_VERSION) ?: "unknown"
        val method = listOf("Bilinear", "Nearest", "SGSR1", "FSR Lite (6-tap)", "MWSR", "AMD FSR1 EASU", "Native GLES3 (bilinear + edge sharpen)")[upscaleMethod]
        val majorMinor = IntArray(2)
        GLES30.glGetIntegerv(0x821B, majorMinor, 0) // GL_MAJOR_VERSION
        GLES30.glGetIntegerv(0x821C, majorMinor, 1) // GL_MINOR_VERSION
        return buildString {
            append("GLES runtime: ").append(version)
            append(" (").append(majorMinor[0]).append(".").append(majorMinor[1]).append(")")
            append("\nSGSR1 textureGather: ").append(sgsr1Capability)
            append("\nGPU: ").append(vendor).append(" / ").append(renderer)
            append("\nActive method: ").append(method)
            append("\nRender target: ").append(sceneWidth).append("x").append(sceneHeight)
            append(" -> ").append(outputWidth).append("x").append(outputHeight)
            append("\nScene FBO: ").append(sceneTargetStatus)
            append("\nCAS output FBO: ").append(postTargetStatus)
            append("\nCAS: ").append(if (casEnabled) "enabled" else "disabled")
            if (shaderDiagnostics.isNotEmpty()) {
                append("\nShader diagnostics:")
                shaderDiagnostics.forEach { append("\n• ").append(it) }
            } else {
                append("\nShader diagnostics: all loaded shaders compiled and linked")
            }
        }
    }

    private fun detectTextureGatherSupport(): String {
        val version = IntArray(2)
        GLES30.glGetIntegerv(0x821B, version, 0) // GL_MAJOR_VERSION
        GLES30.glGetIntegerv(0x821C, version, 1) // GL_MINOR_VERSION
        if (version[0] > 3 || (version[0] == 3 && version[1] >= 1)) {
            return "supported (GLES ${version[0]}.${version[1]} core)"
        }
        return "unsupported (SGSR1 shader requires GLSL ES 3.10 / GLES 3.1 core; found GLES ${version[0]}.${version[1]})"
    }

    private fun loadShaderProgram(label: String, vertexSource: String, assetPath: String): Int =
        runCatching {
            val source = assets.open(assetPath).bufferedReader().use { it.readText() }
            linkProgram(vertexSource, source)
        }.onFailure {
            shaderDiagnostics += "$label failed: ${it.message ?: it.javaClass.simpleName}"
        }.getOrDefault(0)


    // GLSurfaceView may recreate its EGL context after pause/resume or surface loss.
    // Every GL object name from the previous context is invalid in the new one.
    private fun resetContextResources() {
        sceneProgram = 0
        upscaleProgram = 0
        sgsr1Program = 0
        fsrLiteProgram = 0
        mwsrProgram = 0
        fsr1Program = 0
        casProgram = 0
        framebuffer = 0
        sceneTexture = 0
        postFramebuffer = 0
        postTexture = 0
        postWidth = 0
        postHeight = 0
        sceneWidth = 0
        sceneHeight = 0
        sceneTargetStatus = "recreating after EGL context change"
        postTargetStatus = "recreating after EGL context change"
        sgsr1Capability = "not checked"
        mwsrParamsDirty = true
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        resetContextResources()
        shaderDiagnostics.clear()
        val vertex = """
            #version 300 es
            layout(location=0) in vec2 aPosition;
            layout(location=1) in vec2 aUv;
            out vec2 vUv;
            void main() { gl_Position = vec4(aPosition, 0.0, 1.0); vUv = aUv; }
        """.trimIndent()
        val sceneFragment = """
            #version 300 es
            precision mediump float;
            out vec4 color;
            void main() {
                vec2 p = gl_FragCoord.xy;
                float pulse = 0.5 + 0.5 * sin(p.x * 0.035) * cos(p.y * 0.035);
                float checker = mod(floor(p.x * 0.25) + floor(p.y * 0.25), 2.0);
                float fineLines = step(0.82, fract((p.x + p.y) * 0.075));
                float gridX = 1.0 - step(1.0, mod(p.x, 12.0));
                float gridY = 1.0 - step(1.0, mod(p.y, 12.0));
                float diagonal = 1.0 - step(1.5, abs(mod(p.x - p.y, 24.0) - 12.0));
                vec3 base = vec3(0.08 + pulse * 0.12, 0.32 + pulse * 0.30, 0.58 + pulse * 0.24);
                base = mix(base, vec3(0.92, 0.78, 0.18), checker * 0.22);
                base = mix(base, vec3(0.96, 0.96, 1.0), fineLines * 0.55);
                base = mix(base, vec3(0.04, 0.06, 0.09), max(gridX, gridY) * 0.72);
                base = mix(base, vec3(0.95, 0.22, 0.12), diagonal * 0.5);
                color = vec4(base, 1.0);
            }
        """.trimIndent()
        val upscaleFragment = """
            #version 300 es
            precision highp float;
            in vec2 vUv;
            uniform sampler2D uScene;
            uniform vec2 uTexel;
            uniform float uSharpen;
            uniform float uNearest;
            out vec4 color;
            vec4 sampleScene(vec2 uv) {
                if (uNearest > 0.5) {
                    vec2 snapped = (floor(uv / uTexel) + vec2(0.5)) * uTexel;
                    return texture(uScene, snapped);
                }
                return texture(uScene, uv);
            }
            void main() {
                vec4 center = sampleScene(vUv);
                vec3 north = sampleScene(vUv + vec2(0.0, uTexel.y)).rgb;
                vec3 south = sampleScene(vUv - vec2(0.0, uTexel.y)).rgb;
                vec3 east = sampleScene(vUv + vec2(uTexel.x, 0.0)).rgb;
                vec3 west = sampleScene(vUv - vec2(uTexel.x, 0.0)).rgb;
                vec3 sharpened = center.rgb + (center.rgb * 4.0 - north - south - east - west) * 0.18;
                color = vec4(clamp(mix(center.rgb, sharpened, uSharpen), 0.0, 1.0), center.a);
            }
        """.trimIndent()
        sceneProgram = linkProgram(vertex, sceneFragment)
        upscaleProgram = linkProgram(vertex, upscaleFragment)
        val sgsrVertex = """
            #version 310 es
            layout(location=0) in vec2 aPosition;
            layout(location=1) in vec2 aUv;
            layout(location=0) out vec4 in_TEXCOORD0;
            void main() {
                gl_Position = vec4(aPosition, 0.0, 1.0);
                in_TEXCOORD0 = vec4(aUv, 0.0, 1.0);
            }
        """.trimIndent()
        sgsr1Capability = detectTextureGatherSupport()
        sgsr1Program = if (sgsr1Capability.startsWith("supported")) {
            loadShaderProgram("SGSR1", sgsrVertex, "shaders/sgsr1_mobile.frag")
        } else {
            shaderDiagnostics += "SGSR1 skipped: textureGather is unavailable in this GLES context"
            0
        }
        fsrLiteProgram = loadShaderProgram("FSR Lite", vertex, "shaders/fsr_lite_6tap.frag")
        mwsrProgram = loadShaderProgram("MWSR", vertex, "shaders/mwsr_up.frag")
        fsr1Program = loadShaderProgram("AMD FSR1 EASU", vertex, "shaders/amd_fsr1_easu.frag")
        casProgram = loadShaderProgram("CAS", vertex, "shaders/amd_cas_sharpen.frag")
        if (upscaleMethod == 2 && sgsr1Program == 0) {
            upscaleMethod = 0
            statusMessage = "SGSR1 unavailable after graphics-context recreation; using Bilinear."
        } else if (upscaleMethod == 3 && fsrLiteProgram == 0) {
            upscaleMethod = 0
            statusMessage = "FSR Lite unavailable after graphics-context recreation; using Bilinear."
        } else if (upscaleMethod == 4 && mwsrProgram == 0) {
            upscaleMethod = 0
            statusMessage = "MWSR unavailable after graphics-context recreation; using Bilinear."
        } else if (upscaleMethod == 5 && fsr1Program == 0) {
            upscaleMethod = 0
            statusMessage = "AMD FSR1 EASU unavailable after graphics-context recreation; using Bilinear."
        }
        if (casEnabled && casProgram == 0) {
            casEnabled = false
            statusMessage = "CAS unavailable after graphics-context recreation; sharpening disabled."
        }
        mwsrParamsDirty = true
        triangleBuffer = floatBuffer(floatArrayOf(
            -0.72f, -0.62f, 0f, 0f,
             0.72f, -0.62f, 1f, 0f,
             0.00f,  0.76f, 0.5f, 1f
        ))
        quadBuffer = floatBuffer(floatArrayOf(
            -1f, -1f, 0f, 0f,
             1f, -1f, 1f, 0f,
            -1f,  1f, 0f, 1f,
             1f,  1f, 1f, 1f
        ))
        GLES30.glDisable(GLES30.GL_DEPTH_TEST)
        GLES30.glDisable(GLES30.GL_BLEND)
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        outputWidth = width.coerceAtLeast(1)
        outputHeight = height.coerceAtLeast(1)
        ensureSceneTarget()
        mwsrParamsDirty = true
    }

    override fun onDrawFrame(gl: GL10?) {
        if (upscalingEnabled && ensureSceneTarget()) {
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, sceneTexture)
            val filter = if (nearestMode) GLES30.GL_NEAREST else GLES30.GL_LINEAR
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, filter)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, filter)
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, framebuffer)
            GLES30.glViewport(0, 0, sceneWidth, sceneHeight)
            drawScene()
            if (upscaleMethod == 6 && drawNativeGlesUpscale()) return
            if (upscaleMethod == 6) {
                upscaleMethod = 0
                statusMessage = "Native GLES pass failed; using managed Bilinear fallback."
            }
            val useCas = prepareCasTarget()
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, if (useCas) postFramebuffer else 0)
            GLES30.glViewport(0, 0, outputWidth, outputHeight)
            val selectedProgram = when {
                upscaleMethod == 2 && sgsr1Program != 0 -> sgsr1Program
                upscaleMethod == 3 && fsrLiteProgram != 0 -> fsrLiteProgram
                upscaleMethod == 4 && mwsrProgram != 0 -> mwsrProgram
                upscaleMethod == 5 && fsr1Program != 0 -> fsr1Program
                else -> upscaleProgram
            }
            GLES30.glUseProgram(selectedProgram)
            bindQuad(selectedProgram, quadBuffer)
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, sceneTexture)
            when (selectedProgram) {
                sgsr1Program -> {
                    GLES30.glUniform1i(GLES30.glGetUniformLocation(sgsr1Program, "ps0"), 0)
                    val viewportLocation = GLES30.glGetUniformLocation(sgsr1Program, "ViewportInfo[0]")
                    GLES30.glUniform4f(viewportLocation, 1f / sceneWidth, 1f / sceneHeight,
                        sceneWidth.toFloat(), sceneHeight.toFloat())
                }
                fsrLiteProgram -> {
                    GLES30.glUniform1i(GLES30.glGetUniformLocation(fsrLiteProgram, "uScene"), 0)
                    GLES30.glUniform2f(GLES30.glGetUniformLocation(fsrLiteProgram, "uTexel"),
                        1f / sceneWidth.coerceAtLeast(1), 1f / sceneHeight.coerceAtLeast(1))
                }
                mwsrProgram -> applyMwsrUniforms()
                fsr1Program -> {
                    GLES30.glUniform1i(GLES30.glGetUniformLocation(fsr1Program, "u_texture"), 0)
                    GLES30.glUniform2f(GLES30.glGetUniformLocation(fsr1Program, "u_invResolution"),
                        1f / sceneWidth.coerceAtLeast(1), 1f / sceneHeight.coerceAtLeast(1))
                    GLES30.glUniform1f(GLES30.glGetUniformLocation(fsr1Program, "u_sharpness"), 1f)
                }
                else -> {
                    GLES30.glUniform1i(GLES30.glGetUniformLocation(upscaleProgram, "uScene"), 0)
                    GLES30.glUniform2f(GLES30.glGetUniformLocation(upscaleProgram, "uTexel"),
                        1f / sceneWidth.coerceAtLeast(1), 1f / sceneHeight.coerceAtLeast(1))
                    GLES30.glUniform1f(GLES30.glGetUniformLocation(upscaleProgram, "uSharpen"),
                        if (sharpeningEnabled) 1f else 0f)
                    GLES30.glUniform1f(GLES30.glGetUniformLocation(upscaleProgram, "uNearest"),
                        if (nearestMode) 1f else 0f)
                }
            }
            GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4)
            GLES30.glDisableVertexAttribArray(0)
            GLES30.glDisableVertexAttribArray(1)
            if (useCas) drawCasPass()
        } else {
            val useCas = prepareCasTarget()
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, if (useCas) postFramebuffer else 0)
            GLES30.glViewport(0, 0, outputWidth, outputHeight)
            drawScene()
            if (useCas) drawCasPass()
        }
    }

    private fun drawNativeGlesUpscale(): Boolean {
        if (!ensurePostTarget()) {
            statusMessage = "Native GLES output target unavailable; using managed fallback."
            return false
        }
        val result = runCatching {
            NativeGlesBridge.process(
                sceneTexture, postTexture, sceneWidth, sceneHeight,
                outputWidth, outputHeight,
                if (sharpeningEnabled) 0.65f else 0f
            )
        }.getOrElse { -100 }
        if (result != 0) {
            statusMessage = "Native GLES backend returned $result; using managed fallback."
            return false
        }
        if (casEnabled && casProgram != 0) {
            drawCasPass()
        } else {
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
            GLES30.glViewport(0, 0, outputWidth, outputHeight)
            GLES30.glUseProgram(upscaleProgram)
            bindQuad(upscaleProgram, quadBuffer)
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, postTexture)
            GLES30.glUniform1i(GLES30.glGetUniformLocation(upscaleProgram, "uScene"), 0)
            GLES30.glUniform2f(GLES30.glGetUniformLocation(upscaleProgram, "uTexel"),
                1f / outputWidth.coerceAtLeast(1), 1f / outputHeight.coerceAtLeast(1))
            GLES30.glUniform1f(GLES30.glGetUniformLocation(upscaleProgram, "uSharpen"), 0f)
            GLES30.glUniform1f(GLES30.glGetUniformLocation(upscaleProgram, "uNearest"), 0f)
            GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4)
            GLES30.glDisableVertexAttribArray(0)
            GLES30.glDisableVertexAttribArray(1)
        }
        if (!statusMessage.startsWith("Native GLES3 active"))
            statusMessage = "Native GLES3 active: bilinear upscale + edge-aware detail sharpen" +
                if (casEnabled && casProgram != 0) " + AMD CAS" else ""
        return true
    }

    private fun ensureSceneTarget(): Boolean {
        val wantedWidth = (outputWidth * renderScale).toInt().coerceAtLeast(1)
        val wantedHeight = (outputHeight * renderScale).toInt().coerceAtLeast(1)
        if (wantedWidth == sceneWidth && wantedHeight == sceneHeight && framebuffer != 0) return true
        if (framebuffer != 0) GLES30.glDeleteFramebuffers(1, intArrayOf(framebuffer), 0)
        if (sceneTexture != 0) GLES30.glDeleteTextures(1, intArrayOf(sceneTexture), 0)
        val textures = IntArray(1)
        GLES30.glGenTextures(1, textures, 0)
        sceneTexture = textures[0]
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, sceneTexture)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER,
            if (nearestMode) GLES30.GL_NEAREST else GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER,
            if (nearestMode) GLES30.GL_NEAREST else GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, GLES30.GL_RGBA8, wantedWidth, wantedHeight,
            0, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, null)
        val fbos = IntArray(1)
        GLES30.glGenFramebuffers(1, fbos, 0)
        framebuffer = fbos[0]
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, framebuffer)
        GLES30.glFramebufferTexture2D(GLES30.GL_FRAMEBUFFER, GLES30.GL_COLOR_ATTACHMENT0,
            GLES30.GL_TEXTURE_2D, sceneTexture, 0)
        val framebufferStatus = GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER)
        val complete = framebufferStatus == GLES30.GL_FRAMEBUFFER_COMPLETE
        sceneTargetStatus = if (complete) "complete" else "incomplete (0x${framebufferStatus.toString(16)})"
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
        if (!complete) {
            GLES30.glDeleteFramebuffers(1, intArrayOf(framebuffer), 0)
            GLES30.glDeleteTextures(1, intArrayOf(sceneTexture), 0)
            framebuffer = 0
            sceneTexture = 0
            sceneWidth = 0
            sceneHeight = 0
            return false
        }
        sceneWidth = wantedWidth
        sceneHeight = wantedHeight
        mwsrParamsDirty = true
        return true
    }

    private fun prepareCasTarget(): Boolean {
        if (!casEnabled || casProgram == 0) return false
        if (ensurePostTarget()) return true
        casEnabled = false
        statusMessage = "AMD CAS disabled: output framebuffer is unavailable on this device."
        return false
    }

    private fun drawCasPass() {
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
        GLES30.glViewport(0, 0, outputWidth, outputHeight)
        GLES30.glUseProgram(casProgram)
        bindQuad(casProgram, quadBuffer)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, postTexture)
        GLES30.glUniform1i(GLES30.glGetUniformLocation(casProgram, "uScene"), 0)
        GLES30.glUniform2f(GLES30.glGetUniformLocation(casProgram, "uInputSize"),
            outputWidth.toFloat(), outputHeight.toFloat())
        GLES30.glUniform1f(GLES30.glGetUniformLocation(casProgram, "uSharpness"), casSharpness)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4)
        GLES30.glDisableVertexAttribArray(0)
        GLES30.glDisableVertexAttribArray(1)
    }

    private fun ensurePostTarget(): Boolean {
        if (postWidth == outputWidth && postHeight == outputHeight && postFramebuffer != 0) return true
        if (postFramebuffer != 0) GLES30.glDeleteFramebuffers(1, intArrayOf(postFramebuffer), 0)
        if (postTexture != 0) GLES30.glDeleteTextures(1, intArrayOf(postTexture), 0)
        val textures = IntArray(1)
        GLES30.glGenTextures(1, textures, 0)
        postTexture = textures[0]
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, postTexture)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, GLES30.GL_RGBA8, outputWidth, outputHeight,
            0, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, null)
        val fbos = IntArray(1)
        GLES30.glGenFramebuffers(1, fbos, 0)
        postFramebuffer = fbos[0]
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, postFramebuffer)
        GLES30.glFramebufferTexture2D(GLES30.GL_FRAMEBUFFER, GLES30.GL_COLOR_ATTACHMENT0,
            GLES30.GL_TEXTURE_2D, postTexture, 0)
        val postFramebufferStatus = GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER)
        val complete = postFramebufferStatus == GLES30.GL_FRAMEBUFFER_COMPLETE
        postTargetStatus = if (complete) "complete" else "incomplete (0x${postFramebufferStatus.toString(16)})"
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
        if (!complete) {
            GLES30.glDeleteFramebuffers(1, intArrayOf(postFramebuffer), 0)
            GLES30.glDeleteTextures(1, intArrayOf(postTexture), 0)
            postFramebuffer = 0
            postTexture = 0
            postWidth = 0
            postHeight = 0
            return false
        }
        postWidth = outputWidth
        postHeight = outputHeight
        return true
    }

    private fun applyMwsrUniforms() {
        GLES30.glUniform1i(GLES30.glGetUniformLocation(mwsrProgram, "uTex"), 0)
        if (!mwsrParamsDirty) return
        val inW = sceneWidth.coerceAtLeast(1)
        val inH = sceneHeight.coerceAtLeast(1)
        val outW = outputWidth.coerceAtLeast(1)
        val outH = outputHeight.coerceAtLeast(1)
        // MWSR's reconstruction kernel also supports a 1:1 pass; do not
        // invent a 7% upscale when the selected render scale is already 100%.
        val scale = minOf(outW.toFloat() / inW, outH.toFloat() / inH).coerceIn(1f, 4f)
        val x = kotlin.math.ln(scale.toDouble()).toFloat()
        fun curve(c0: Float, c1: Float, c2: Float) = c0 + x * (c1 + x * c2)
        val kc = curve(0.63535834f, 0.021845682f, 0.12781268f)
        val kgBase = curve(-0.12884139f, 2.5844684f, -1.260092f)
        val kr = curve(1.0071546f, -1.0209067f, 0.5699606f)
        val klBase = curve(0.33416995f, -0.2962022f, 0.08628272f)
        val force = 1f + 0.35f * (kotlin.math.ln(scale / (4f / 3f)) /
            kotlin.math.ln(2f / (4f / 3f))).coerceIn(0f, 1f)
        val kg = kgBase * force
        val kl = klBase * force
        GLES30.glUniform4f(GLES30.glGetUniformLocation(mwsrProgram, "uTexelScale"),
            1f / inW, 1f / inH, outW.toFloat() / inW, outH.toFloat() / inH)
        GLES30.glUniform4f(GLES30.glGetUniformLocation(mwsrProgram, "uDimsAcross"),
            inW.toFloat(), inH.toFloat(), kr / inW, kr / inH)
        GLES30.glUniform4f(GLES30.glGetUniformLocation(mwsrProgram, "uKernel"),
            kc, 0.005f, kg, 0.1f)
        GLES30.glUniform4f(GLES30.glGetUniformLocation(mwsrProgram, "uZone"), 0f, 0f, 1f, 1f)
        GLES30.glUniform4f(GLES30.glGetUniformLocation(mwsrProgram, "uZoneBonus"), 0f, 0f, 0f, 0f)
        GLES30.glUniform4f(GLES30.glGetUniformLocation(mwsrProgram, "uBound"), kl, 0f, 0f, 0f)
        mwsrParamsDirty = false
    }

    private fun drawScene() {
        GLES30.glClearColor(0.035f, 0.045f, 0.075f, 1f)
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        GLES30.glUseProgram(sceneProgram)
        bindQuad(sceneProgram, quadBuffer)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4)
        GLES30.glDisableVertexAttribArray(0)
        GLES30.glDisableVertexAttribArray(1)
    }

    private fun bindQuad(program: Int, buffer: FloatBuffer, mode: Int = GLES30.GL_TRIANGLE_STRIP) {
        buffer.position(0)
        GLES30.glEnableVertexAttribArray(0)
        GLES30.glVertexAttribPointer(0, 2, GLES30.GL_FLOAT, false, 16, buffer)
        buffer.position(2)
        GLES30.glEnableVertexAttribArray(1)
        GLES30.glVertexAttribPointer(1, 2, GLES30.GL_FLOAT, false, 16, buffer)
    }

    private fun floatBuffer(values: FloatArray): FloatBuffer =
        ByteBuffer.allocateDirect(values.size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().apply { put(values); position(0) }

    private fun linkProgram(vertexSource: String, fragmentSource: String): Int {
        val vertex = compileShader(GLES30.GL_VERTEX_SHADER, vertexSource)
        val fragment = try {
            compileShader(GLES30.GL_FRAGMENT_SHADER, fragmentSource)
        } catch (failure: Throwable) {
            GLES30.glDeleteShader(vertex)
            throw failure
        }
        val program = GLES30.glCreateProgram()
        if (program == 0) {
            GLES30.glDeleteShader(vertex)
            GLES30.glDeleteShader(fragment)
            error("GLES could not allocate a program object")
        }
        try {
            GLES30.glAttachShader(program, vertex)
            GLES30.glAttachShader(program, fragment)
            GLES30.glLinkProgram(program)
            val status = IntArray(1)
            GLES30.glGetProgramiv(program, GLES30.GL_LINK_STATUS, status, 0)
            check(status[0] == GLES30.GL_TRUE) {
                "GLES program link failed: " + GLES30.glGetProgramInfoLog(program)
            }
            return program
        } catch (failure: Throwable) {
            GLES30.glDeleteProgram(program)
            throw failure
        } finally {
            GLES30.glDeleteShader(vertex)
            GLES30.glDeleteShader(fragment)
        }
    }

    private fun compileShader(type: Int, source: String): Int {
        val shader = GLES30.glCreateShader(type)
        if (shader == 0) error("GLES could not allocate a shader object")
        GLES30.glShaderSource(shader, source)
        GLES30.glCompileShader(shader)
        val status = IntArray(1)
        GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, status, 0)
        if (status[0] != GLES30.GL_TRUE) {
            val log = GLES30.glGetShaderInfoLog(shader)
            GLES30.glDeleteShader(shader)
            error("GLES shader compile failed: $log")
        }
        return shader
    }
}
