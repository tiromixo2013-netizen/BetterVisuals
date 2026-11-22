package dev.ultrascale.mobile

import android.app.Activity
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Spinner
import android.widget.ArrayAdapter
import android.view.ViewGroup
import android.graphics.Color
import android.view.Gravity
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Controlled, app-owned GLES2 render-scale test.
 * Scene rendering happens into a lower-resolution RGBA texture/FBO; a second pass
 * samples that texture across the full native surface. This is not a game injector.
 */
class RenderScaleHarnessActivity : Activity() {
    private lateinit var surface: GLSurfaceView
    private lateinit var renderer: HarnessRenderer
    private lateinit var scaleLabel: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        renderer = HarnessRenderer()
        surface = GLSurfaceView(this).apply {
            setEGLContextClientVersion(2)
            setRenderer(renderer)
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
            preserveEGLContextOnPause = false
        }

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.rgb(16, 17, 26))
            setPadding(18, 12, 18, 12)
        }
        val title = TextView(this).apply {
            text = "ULTRASCALE · RENDER SCALE TEST"
            setTextColor(Color.rgb(183, 161, 255))
            textSize = 17f
            setPadding(0, 8, 0, 4)
        }
        val subtitle = TextView(this).apply {
            text = "Low-resolution off-screen render → full-size output"
            setTextColor(Color.LTGRAY)
            textSize = 13f
        }
        scaleLabel = TextView(this).apply {
            setTextColor(Color.WHITE)
            textSize = 15f
            setPadding(0, 12, 0, 4)
            text = "Render scale: 70%"
        }
        val scale = SeekBar(this).apply {
            max = 75
            progress = 45 // 25%..100%, default 70%
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(bar: SeekBar?, value: Int, fromUser: Boolean) {
                    val factor = (25 + value) / 100f
                    scaleLabel.text = "Render scale: ${(factor * 100).roundToInt()}%"
                    renderer.setScale(factor)
                }
                override fun onStartTrackingTouch(bar: SeekBar?) = Unit
                override fun onStopTrackingTouch(bar: SeekBar?) = Unit
            })
        }
        root.addView(title)
        root.addView(subtitle)
        root.addView(scaleLabel)
        root.addView(scale)

        val upscalerLabel = TextView(this).apply {
            text = "Upscaler: Linear"
            setTextColor(Color.WHITE)
            textSize = 14f
            setPadding(0, 10, 0, 4)
        }
        val upscalerPicker = Spinner(this).apply {
            adapter = ArrayAdapter(
                this@RenderScaleHarnessActivity,
                android.R.layout.simple_spinner_dropdown_item,
                listOf("Linear (fast)", "Catmull-Rom 4×4 (sharper)")
            )
            setSelection(0)
            onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
                override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                    renderer.setUpscaler(position)
                    upscalerLabel.text = if (position == 0) "Upscaler: Linear" else "Upscaler: Catmull-Rom 4×4"
                }
                override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
            }
        }
        root.addView(upscalerLabel)
        root.addView(upscalerPicker)

        val sharpnessLabel = TextView(this).apply {
            text = "CAS sharpening: Off"
            setTextColor(Color.WHITE)
            textSize = 14f
            setPadding(0, 10, 0, 4)
        }
        val sharpness = SeekBar(this).apply {
            max = 100
            progress = 0
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(bar: SeekBar?, value: Int, fromUser: Boolean) {
                    renderer.setSharpness(value / 100f)
                    sharpnessLabel.text = if (value == 0) "CAS sharpening: Off" else "CAS sharpening: $value%"
                }
                override fun onStartTrackingTouch(bar: SeekBar?) = Unit
                override fun onStopTrackingTouch(bar: SeekBar?) = Unit
            })
        }
        root.addView(sharpnessLabel)
        root.addView(sharpness)

        val presetLabel = TextView(this).apply {
            text = "Preset"
            setTextColor(Color.WHITE)
            textSize = 14f
            setPadding(0, 10, 0, 4)
        }
        val presetPicker = Spinner(this).apply {
            adapter = ArrayAdapter(
                this@RenderScaleHarnessActivity,
                android.R.layout.simple_spinner_dropdown_item,
                listOf("Custom", "Performance · 50%", "Balanced · 70%", "Quality · 85%", "Native · 100%")
            )
            setSelection(0)
            onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
                override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                    when (position) {
                        1 -> { scale.progress = 25; upscalerPicker.setSelection(0); sharpness.progress = 0 }
                        2 -> { scale.progress = 45; upscalerPicker.setSelection(1); sharpness.progress = 25 }
                        3 -> { scale.progress = 60; upscalerPicker.setSelection(1); sharpness.progress = 45 }
                        4 -> { scale.progress = 75; upscalerPicker.setSelection(0); sharpness.progress = 0 }
                    }
                }
                override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
            }
        }
        root.addView(presetLabel)
        root.addView(presetPicker)

        root.addView(surface, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f
        ))
        val note = TextView(this).apply {
            text = "Test harness only. It does not modify other apps or prove arbitrary-game compatibility."
            setTextColor(Color.LTGRAY)
            textSize = 12f
            gravity = Gravity.CENTER
            setPadding(0, 8, 0, 8)
        }
        root.addView(note)
        setContentView(root)
    }

    override fun onResume() { super.onResume(); surface.onResume() }
    override fun onPause() { surface.onPause(); super.onPause() }
}

private class HarnessRenderer : GLSurfaceView.Renderer {
    @Volatile private var requestedScale = 0.70f
    @Volatile private var requestedUpscaler = 0
    @Volatile private var requestedSharpness = 0f
    private var appliedScale = -1f
    private var surfaceWidth = 1
    private var surfaceHeight = 1
    private var targetWidth = 1
    private var targetHeight = 1
    private var texture = 0
    private var framebuffer = 0
    private var sceneProgram = 0
    private var compositeProgram = 0
    private lateinit var vertices: FloatBuffer
    private var scenePosition = 0
    private var sceneUv = 0
    private var sceneTime = 0
    private var compositePosition = 0
    private var compositeUv = 0
    private var compositeSampler = 0
    private var compositeTexelSize = 0
    private var compositeUpscaler = 0
    private var compositeOutputTexelSize = 0
    private var compositeSharpness = 0
    private var startNanos = System.nanoTime()

    fun setScale(value: Float) {
        requestedScale = value.coerceIn(0.25f, 1f)
    }

    fun setUpscaler(value: Int) {
        requestedUpscaler = value.coerceIn(0, 1)
    }

    fun setSharpness(value: Float) {
        requestedSharpness = value.coerceIn(0f, 1f)
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        GLES20.glClearColor(0.035f, 0.04f, 0.07f, 1f)
        sceneProgram = linkProgram(VERTEX_SHADER, SCENE_SHADER)
        compositeProgram = linkProgram(VERTEX_SHADER, COMPOSITE_SHADER)
        scenePosition = GLES20.glGetAttribLocation(sceneProgram, "aPosition")
        sceneUv = GLES20.glGetAttribLocation(sceneProgram, "aUv")
        sceneTime = GLES20.glGetUniformLocation(sceneProgram, "uTime")
        compositePosition = GLES20.glGetAttribLocation(compositeProgram, "aPosition")
        compositeUv = GLES20.glGetAttribLocation(compositeProgram, "aUv")
        compositeSampler = GLES20.glGetUniformLocation(compositeProgram, "uTexture")
        compositeTexelSize = GLES20.glGetUniformLocation(compositeProgram, "uTexelSize")
        compositeUpscaler = GLES20.glGetUniformLocation(compositeProgram, "uUpscaler")
        compositeOutputTexelSize = GLES20.glGetUniformLocation(compositeProgram, "uOutputTexelSize")
        compositeSharpness = GLES20.glGetUniformLocation(compositeProgram, "uSharpness")
        val data = floatArrayOf(
            -1f, -1f, 0f, 0f,  1f, -1f, 1f, 0f,
            -1f,  1f, 0f, 1f,  1f,  1f, 1f, 1f
        )
        vertices = ByteBuffer.allocateDirect(data.size * 4)
            .order(ByteOrder.nativeOrder()).asFloatBuffer().apply { put(data); position(0) }
        releaseTarget()
        appliedScale = -1f
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        surfaceWidth = max(1, width)
        surfaceHeight = max(1, height)
        appliedScale = -1f
        GLES20.glViewport(0, 0, surfaceWidth, surfaceHeight)
    }

    override fun onDrawFrame(gl: GL10?) {
        val scale = requestedScale
        if (scale != appliedScale || framebuffer == 0) {
            if (!createTarget(scale)) {
                GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0)
                GLES20.glViewport(0, 0, surfaceWidth, surfaceHeight)
                GLES20.glClearColor(0.25f, 0.02f, 0.02f, 1f)
                GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
                return
            }
            appliedScale = scale
        }

        // Pass 1: draw a moving diagnostic scene at reduced internal resolution.
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, framebuffer)
        GLES20.glViewport(0, 0, targetWidth, targetHeight)
        GLES20.glDisable(GLES20.GL_BLEND)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
        GLES20.glUseProgram(sceneProgram)
        bindQuad(scenePosition, sceneUv)
        GLES20.glUniform1f(sceneTime, (System.nanoTime() - startNanos) / 1_000_000_000f)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)

        // Pass 2: upscale the low-resolution target onto the unchanged native surface.
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0)
        GLES20.glViewport(0, 0, surfaceWidth, surfaceHeight)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
        GLES20.glUseProgram(compositeProgram)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture)
        GLES20.glUniform1i(compositeSampler, 0)
        GLES20.glUniform2f(compositeTexelSize, 1f / targetWidth.toFloat(), 1f / targetHeight.toFloat())
        GLES20.glUniform1i(compositeUpscaler, requestedUpscaler)
        GLES20.glUniform2f(compositeOutputTexelSize, 1f / surfaceWidth.toFloat(), 1f / surfaceHeight.toFloat())
        GLES20.glUniform1f(compositeSharpness, requestedSharpness)
        bindQuad(compositePosition, compositeUv)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
    }

    private fun createTarget(scale: Float): Boolean {
        releaseTarget()
        targetWidth = max(1, (surfaceWidth * scale).roundToInt())
        targetHeight = max(1, (surfaceHeight * scale).roundToInt())
        val names = IntArray(1)
        GLES20.glGenTextures(1, names, 0)
        texture = names[0]
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, targetWidth, targetHeight,
            0, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, null)
        GLES20.glGenFramebuffers(1, names, 0)
        framebuffer = names[0]
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, framebuffer)
        GLES20.glFramebufferTexture2D(GLES20.GL_FRAMEBUFFER, GLES20.GL_COLOR_ATTACHMENT0,
            GLES20.GL_TEXTURE_2D, texture, 0)
        val status = GLES20.glCheckFramebufferStatus(GLES20.GL_FRAMEBUFFER)
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
        return status == GLES20.GL_FRAMEBUFFER_COMPLETE
    }

    private fun releaseTarget() {
        if (framebuffer != 0) GLES20.glDeleteFramebuffers(1, intArrayOf(framebuffer), 0)
        if (texture != 0) GLES20.glDeleteTextures(1, intArrayOf(texture), 0)
        framebuffer = 0
        texture = 0
    }

    private fun bindQuad(position: Int, uv: Int) {
        vertices.position(0)
        GLES20.glEnableVertexAttribArray(position)
        GLES20.glVertexAttribPointer(position, 2, GLES20.GL_FLOAT, false, 16, vertices)
        vertices.position(2)
        GLES20.glEnableVertexAttribArray(uv)
        GLES20.glVertexAttribPointer(uv, 2, GLES20.GL_FLOAT, false, 16, vertices)
    }

    private fun linkProgram(vertex: String, fragment: String): Int {
        fun compile(type: Int, source: String): Int {
            val shader = GLES20.glCreateShader(type)
            GLES20.glShaderSource(shader, source)
            GLES20.glCompileShader(shader)
            val status = IntArray(1)
            GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, status, 0)
            check(status[0] == GLES20.GL_TRUE) { GLES20.glGetShaderInfoLog(shader) }
            return shader
        }
        val vs = compile(GLES20.GL_VERTEX_SHADER, vertex)
        val fs = compile(GLES20.GL_FRAGMENT_SHADER, fragment)
        val program = GLES20.glCreateProgram()
        GLES20.glAttachShader(program, vs)
        GLES20.glAttachShader(program, fs)
        GLES20.glLinkProgram(program)
        val status = IntArray(1)
        GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, status, 0)
        check(status[0] == GLES20.GL_TRUE) { GLES20.glGetProgramInfoLog(program) }
        GLES20.glDeleteShader(vs)
        GLES20.glDeleteShader(fs)
        return program
    }

    companion object {
        private const val VERTEX_SHADER = """
            attribute vec2 aPosition;
            attribute vec2 aUv;
            varying vec2 vUv;
            void main() { vUv = aUv; gl_Position = vec4(aPosition, 0.0, 1.0); }
        """
        private const val SCENE_SHADER = """
            precision mediump float;
            varying vec2 vUv;
            uniform float uTime;
            void main() {
                vec2 grid = abs(fract(vUv * 18.0) - 0.5);
                float lines = step(0.46, max(grid.x, grid.y));
                float pulse = 0.5 + 0.5 * sin(uTime * 1.7 + vUv.x * 8.0);
                vec3 base = mix(vec3(0.07, 0.10, 0.20), vec3(0.28, 0.12, 0.42), vUv.y);
                vec3 accent = vec3(0.20, 0.72, 0.68) * pulse;
                gl_FragColor = vec4(mix(base, accent, lines * 0.65), 1.0);
            }
        """
        private const val COMPOSITE_SHADER = """
            #ifdef GL_FRAGMENT_PRECISION_HIGH
            precision highp float;
            #else
            precision mediump float;
            #endif
            varying vec2 vUv;
            uniform sampler2D uTexture;
            uniform vec2 uTexelSize;
            uniform vec2 uOutputTexelSize;
            uniform int uUpscaler;
            uniform float uSharpness;

            float catmullWeight(int index, float t) {
                float t2 = t * t;
                float t3 = t2 * t;
                if (index == 0) return -0.5 * t + t2 - 0.5 * t3;
                if (index == 1) return 1.0 - 2.5 * t2 + 1.5 * t3;
                if (index == 2) return 0.5 * t + 2.0 * t2 - 1.5 * t3;
                return -0.5 * t2 + 0.5 * t3;
            }

            vec3 sampleCatmullRom(vec2 uv) {
                vec2 pixel = uv / uTexelSize - 0.5;
                vec2 base = floor(pixel);
                vec2 f = fract(pixel);
                vec3 result = vec3(0.0);
                for (int y = 0; y < 4; ++y) {
                    float wy = catmullWeight(y, f.y);
                    for (int x = 0; x < 4; ++x) {
                        float wx = catmullWeight(x, f.x);
                        vec2 sampleUv = (base + vec2(float(x - 1), float(y - 1)) + 0.5) * uTexelSize;
                        sampleUv = clamp(sampleUv, uTexelSize * 0.5, vec2(1.0) - uTexelSize * 0.5);
                        result += texture2D(uTexture, sampleUv).rgb * (wx * wy);
                    }
                }
                return clamp(result, 0.0, 1.0);
            }

            vec3 reconstruct(vec2 uv) {
                if (uUpscaler == 1) return sampleCatmullRom(uv);
                return texture2D(uTexture, clamp(uv, uTexelSize * 0.5, vec2(1.0) - uTexelSize * 0.5)).rgb;
            }

            vec3 casSample(vec2 uv) {
                return reconstruct(clamp(uv, uOutputTexelSize * 0.5, vec2(1.0) - uOutputTexelSize * 0.5));
            }

            vec3 applyCas(vec2 uv) {
                vec3 b = casSample(uv + vec2(0.0, -uOutputTexelSize.y));
                vec3 d = casSample(uv + vec2(-uOutputTexelSize.x, 0.0));
                vec3 e = casSample(uv);
                vec3 f = casSample(uv + vec2(uOutputTexelSize.x, 0.0));
                vec3 h = casSample(uv + vec2(0.0, uOutputTexelSize.y));

                vec3 mn = min(min(d, e), min(f, min(b, h)));
                vec3 mx = max(max(d, e), max(f, max(b, h)));
                float ampG = clamp(min(mn.g, 1.0 - mx.g) / max(mx.g, 1e-4), 0.0, 1.0);
                ampG = sqrt(ampG);
                float peak = -1.0 / mix(8.0, 5.0, clamp(uSharpness, 0.0, 1.0));
                float w = ampG * peak;
                float rcpWeight = 1.0 / (1.0 + 4.0 * w);
                return clamp((b * w + d * w + f * w + h * w + e) * rcpWeight, 0.0, 1.0);
            }

            void main() {
                vec3 color = reconstruct(vUv);
                if (uSharpness > 0.001) color = applyCas(vUv);
                gl_FragColor = vec4(color, 1.0);
            }
        """
    }
}
