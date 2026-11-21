package dev.ultrascale.samplegame

import android.app.Activity
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import android.view.ViewGroup
import android.opengl.GLES30
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class MainActivity : Activity() {
    private lateinit var status: TextView
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
        status = TextView(this).apply { text = "GLES triangle runs below. Tap Vulkan to test Vulkan instance/device creation." }
        val vulkan = Button(this).apply {
            text = "Run Vulkan API test"
            setOnClickListener {
                status.text = runCatching { runVulkanProbe() }.getOrElse { "Vulkan test failed: ${it.message}" }
            }
        }
        val gl = GLSurfaceView(this).apply {
            setEGLContextClientVersion(3)
            setRenderer(TriangleRenderer())
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
        root.addView(status, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        root.addView(vulkan, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        root.addView(gl, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)
    }

    override fun onPause() { super.onPause(); if (::status.isInitialized) (status.parent as? LinearLayout)?.let { (it.getChildAt(2) as? GLSurfaceView)?.onPause() } }
    override fun onResume() { super.onResume(); if (::status.isInitialized) (status.parent as? LinearLayout)?.let { (it.getChildAt(2) as? GLSurfaceView)?.onResume() } }
}

private class TriangleRenderer : GLSurfaceView.Renderer {
    private var program = 0
    private val vertices = floatArrayOf(0f, 0.7f, -0.7f, -0.6f, 0.7f, -0.6f)
    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        val vertex = compile(GLES30.GL_VERTEX_SHADER, "#version 300 es\nlayout(location=0) in vec2 p; void main(){ gl_Position=vec4(p,0.0,1.0); }")
        val fragment = compile(GLES30.GL_FRAGMENT_SHADER, "#version 300 es\nprecision mediump float; out vec4 c; void main(){ c=vec4(0.15,0.75,1.0,1.0); }")
        program = GLES30.glCreateProgram()
        GLES30.glAttachShader(program, vertex); GLES30.glAttachShader(program, fragment); GLES30.glLinkProgram(program)
    }
    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) { GLES30.glViewport(0, 0, width, height) }
    override fun onDrawFrame(gl: GL10?) {
        GLES30.glClearColor(0.04f, 0.05f, 0.08f, 1f); GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        GLES30.glUseProgram(program)
        val buffer = java.nio.ByteBuffer.allocateDirect(vertices.size * 4).order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer()
        buffer.put(vertices).position(0)
        GLES30.glEnableVertexAttribArray(0); GLES30.glVertexAttribPointer(0, 2, GLES30.GL_FLOAT, false, 0, buffer)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3); GLES30.glDisableVertexAttribArray(0)
    }
    private fun compile(type: Int, source: String): Int {
        val shader = GLES30.glCreateShader(type); GLES30.glShaderSource(shader, source); GLES30.glCompileShader(shader); return shader
    }
}
