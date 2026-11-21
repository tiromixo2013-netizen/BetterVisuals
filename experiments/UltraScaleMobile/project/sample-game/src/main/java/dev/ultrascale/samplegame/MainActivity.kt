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
            text = "CAS-style sharpening"
            isChecked = renderer.sharpeningEnabled
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
                listOf("Bilinear", "Nearest", "SGSR1", "FSR Lite (6-tap)"))
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
        AlertDialog.Builder(this)
            .setTitle("Advanced Graphics")
            .setView(panel)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Apply") { _, _ ->
                val newEnabled = enabled.isChecked
                val newSharpen = sharpen.isChecked
                val newMethod = method.selectedItemPosition
                val newScale = (50 + scale.progress) / 100f
                glView.queueEvent {
                    renderer.configure(newEnabled, newMethod, newSharpen, newScale)
                }
                status.text = "Applied to the sample game's GLES renderer: ${method.selectedItem}."
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
    @Volatile var renderScale = 0.75f
        private set

    private var sceneProgram = 0
    private var upscaleProgram = 0
    private var sgsr1Program = 0
    private var fsrLiteProgram = 0
    private var framebuffer = 0
    private var sceneTexture = 0
    private var sceneWidth = 0
    private var sceneHeight = 0
    private var outputWidth = 0
    private var outputHeight = 0
    private lateinit var triangleBuffer: FloatBuffer
    private lateinit var quadBuffer: FloatBuffer

    fun configure(enabled: Boolean, method: Int, sharpen: Boolean, scale: Float) {
        upscalingEnabled = enabled
        upscaleMethod = method.coerceIn(0, 3)
        sharpeningEnabled = sharpen
        renderScale = scale.coerceIn(0.5f, 1f)
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
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
            GLES30.glViewport(0, 0, outputWidth, outputHeight)
            val selectedProgram = when {
                upscaleMethod == 2 && sgsr1Program != 0 -> sgsr1Program
                upscaleMethod == 3 && fsrLiteProgram != 0 -> fsrLiteProgram
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
        return true
    }

    private fun drawScene() {
        GLES30.glClearColor(0.035f, 0.045f, 0.075f, 1f)
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        GLES30.glUseProgram(sceneProgram)
        bindQuad(sceneProgram, triangleBuffer, GLES30.GL_TRIANGLES)
    }

    private fun bindQuad(program: Int, buffer: FloatBuffer, mode: Int = GLES30.GL_TRIANGLE_STRIP) {
        buffer.position(0)
        GLES30.glEnableVertexAttribArray(0)
        GLES30.glVertexAttribPointer(0, 2, GLES30.GL_FLOAT, false, 16, buffer)
        buffer.position(2)
        GLES30.glEnableVertexAttribArray(1)
        GLES30.glVertexAttribPointer(1, 2, GLES30.GL_FLOAT, false, 16, buffer)
        GLES30.glDrawArrays(mode, 0, if (mode == GLES30.GL_TRIANGLES) 3 else 4)
        GLES30.glDisableVertexAttribArray(0)
        GLES30.glDisableVertexAttribArray(1)
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
