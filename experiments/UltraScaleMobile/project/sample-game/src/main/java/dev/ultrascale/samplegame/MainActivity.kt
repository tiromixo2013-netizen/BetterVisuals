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
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

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
        glView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(3)
            setRenderer(renderer)
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
        root.addView(status, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        root.addView(advanced, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
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
                listOf("Bilinear", "Nearest", "SGSR1", "FSR Lite (6-tap)", "MWSR (upstream lightweight)"))
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
    private lateinit var triangleBuffer: FloatBuffer
    private lateinit var quadBuffer: FloatBuffer

    fun configure(enabled: Boolean, method: Int, sharpen: Boolean, scale: Float, cas: Boolean, casAmount: Float) {
        upscalingEnabled = enabled
        val requestedMethod = method.coerceIn(0, 4)
        val available = when (requestedMethod) {
            2 -> sgsr1Program != 0
            3 -> fsrLiteProgram != 0
            4 -> mwsrProgram != 0
            else -> true
        }
        upscaleMethod = if (available) requestedMethod else 0
        sharpeningEnabled = sharpen
        casEnabled = cas && casProgram != 0
        casSharpness = casAmount.coerceIn(0f, 1f)
        renderScale = scale.coerceIn(0.5f, 1f)
        statusMessage = buildString {
            append("Sample GLES: ")
            append(listOf("Bilinear", "Nearest", "SGSR1", "FSR Lite (6-tap)", "MWSR")[upscaleMethod])
            if (!available) append(" (shader unavailable; Bilinear fallback)")
            if (cas) append(if (casEnabled) " + AMD CAS" else " (CAS shader unavailable)")
        }
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
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
                color = vec4(0.08 + pulse * 0.12, 0.38 + pulse * 0.38, 0.72 + pulse * 0.25, 1.0);
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
            #version 300 es
            layout(location=0) in vec2 aPosition;
            layout(location=1) in vec2 aUv;
            layout(location=0) out vec4 in_TEXCOORD0;
            void main() {
                gl_Position = vec4(aPosition, 0.0, 1.0);
                in_TEXCOORD0 = vec4(aUv, 0.0, 1.0);
            }
        """.trimIndent()
        sgsr1Program = runCatching {
            val source = assets.open("shaders/sgsr1_mobile.frag").bufferedReader().use { it.readText() }
            linkProgram(sgsrVertex, source)
        }.getOrDefault(0)
        fsrLiteProgram = runCatching {
            val source = assets.open("shaders/fsr_lite_6tap.frag").bufferedReader().use { it.readText() }
            linkProgram(vertex, source)
        }.getOrDefault(0)
        mwsrProgram = runCatching {
            val source = assets.open("shaders/mwsr_up.frag").bufferedReader().use { it.readText() }
            linkProgram(vertex, source)
        }.getOrDefault(0)
        casProgram = runCatching {
            val source = assets.open("shaders/amd_cas_sharpen.frag").bufferedReader().use { it.readText() }
            linkProgram(vertex, source)
        }.getOrDefault(0)
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
            val useCas = casEnabled && casProgram != 0 && ensurePostTarget()
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, if (useCas) postFramebuffer else 0)
            GLES30.glViewport(0, 0, outputWidth, outputHeight)
            val selectedProgram = when {
                upscaleMethod == 2 && sgsr1Program != 0 -> sgsr1Program
                upscaleMethod == 3 && fsrLiteProgram != 0 -> fsrLiteProgram
                upscaleMethod == 4 && mwsrProgram != 0 -> mwsrProgram
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
            if (useCas) {
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
        } else {
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
            GLES30.glViewport(0, 0, outputWidth, outputHeight)
            drawScene()
        }
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
        val complete = GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER) == GLES30.GL_FRAMEBUFFER_COMPLETE
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
        val complete = GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER) == GLES30.GL_FRAMEBUFFER_COMPLETE
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
        val scale = minOf(outW.toFloat() / inW, outH.toFloat() / inH).coerceIn(1.07f, 4f)
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
        bindQuad(sceneProgram, triangleBuffer, GLES30.GL_TRIANGLES)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
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
        val fragment = compileShader(GLES30.GL_FRAGMENT_SHADER, fragmentSource)
        val program = GLES30.glCreateProgram()
        GLES30.glAttachShader(program, vertex)
        GLES30.glAttachShader(program, fragment)
        GLES30.glLinkProgram(program)
        val status = IntArray(1)
        GLES30.glGetProgramiv(program, GLES30.GL_LINK_STATUS, status, 0)
        GLES30.glDeleteShader(vertex)
        GLES30.glDeleteShader(fragment)
        check(status[0] == GLES30.GL_TRUE) { "GLES program link failed: " + GLES30.glGetProgramInfoLog(program) }
        return program
    }

    private fun compileShader(type: Int, source: String): Int {
        val shader = GLES30.glCreateShader(type)
        GLES30.glShaderSource(shader, source)
        GLES30.glCompileShader(shader)
        val status = IntArray(1)
        GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, status, 0)
        check(status[0] == GLES30.GL_TRUE) { "GLES shader compile failed: " + GLES30.glGetShaderInfoLog(shader) }
        return shader
    }
}
