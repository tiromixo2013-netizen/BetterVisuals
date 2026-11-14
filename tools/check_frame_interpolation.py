#!/usr/bin/env python3
"""Static regression checks for Android and Windows forward-predictive frame generation.

These checks validate source wiring and prediction direction. They do not
execute GLSL, measure GPU cost, or establish visual quality on a device.
"""
from pathlib import Path

main = Path("src/main.cpp").read_text(encoding="utf-8")
header = Path("src/render/FrameInterpolationRenderer.hpp").read_text(encoding="utf-8")
renderer = Path("src/render/FrameInterpolationRenderer.cpp").read_text(encoding="utf-8")

for token in (
    "FrameInterpolationRenderer m_interpolator",
    "m_gpuTimer.shouldInterpolate()",
    "m_lastAverageNs > 4'000'000ULL",
    "m_framesUntilProbe = 120",
    "m_probeQuery = m_active",
    "A disjoint timer result is invalid",
    "m_interpolator.abandonContext()",
    "m_interpolator.prepare(m_width, m_height)",
    "m_interpolator.generate(m_textures[previousIndex], m_textures[currentIndex])",
    "bool presentGeneratedFrame()",
    "if (!generated) m_gpuTimer.end()",
    "supportsAndroidPresentationTime(display)",
    "shouldGenerateOnThisInterval",
    "m_realFrameSequence % 2",
    "EGLBoolean const realSwapResult = eglSwapBuffers(display, surface)",
    "generatedSwapResult",
    "keeping the already-presented real frame",
    "Generated frame missed its midpoint deadline",
    "result = realSwapResult;",
    "bool scheduleGeneratedFrame(",
    "if (target <= earliest) return false;"
):
    assert token in main, f"missing Android integration: {token}"

for token in (
    "uniform sampler2D u_previous",
    "uniform sampler2D u_current",
    "vec2 searchStep = 4.0 * u_invResolution",
    "float second = 1.0",
    "reverseBest > 0.105 || inconsistent",
    "float predictionFraction = (motionMagnitude > 0.72 || confidence < 0.55) ? 0.25 : 0.5",
    "vec2 predictedCoord = clamp(v_uv - offset * predictionFraction, 0.0, 1.0)",
    "gl_FragColor = texture2D(u_current, predictedCoord)",
    "gl_FragColor = currentSample",
    "best > 0.105",
    "float matchCost(vec4 a, vec4 b)",
    "bool photometricTransition",
    "bool fadeFromBlack",
    "bool alphaTransition",
    "alphaRgbCost < 0.05",
    "if (best > 0.055)",
    "if (reverseBest > 0.055)",
    "confidence < 0.30",
    "protectedPrediction",
    "blockIndex * 16.0",
    "refineBlockIndex * 8.0",
    "refineUv",
    "u_reusedMotion",
    "reuseValid",
    "directionReversal",
    "m_motionTextures",
    "auto const writeIndex = 1 - m_motionReadIndex",
    "never simultaneously sampled as reuse history",
    "m_hasMotionHistory",
    "outputWidth = std::max<GLsizei>(1, (width + 1) / 2)",
    "outputHeight = std::max<GLsizei>(1, (height + 1) / 2)",
    "glViewport(0, 0, m_surfaceWidth, m_surfaceHeight)",
    "previousFrame == currentFrame",
    "glDisable(GL_CULL_FACE)",
    "glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE)",
):
    assert token in renderer, f"missing predictive reconstruction stage: {token}"

for token in (
    "bool prepare(GLsizei width, GLsizei height)",
    "bool generate(GLuint previousFrame, GLuint currentFrame)",
    "GLuint outputTexture() const",
    "bool presentTexture(GLuint texture)",
    "m_surfaceWidth",
    "m_surfaceHeight",
    "void abandonContext()",
):
    assert token in header, f"missing renderer API: {token}"

assert "return glGetError()" not in renderer, "renderer must not consume caller GL errors"
prepare_body = renderer.split("bool FrameInterpolationRenderer::prepare", 1)[1].split(
    "bool FrameInterpolationRenderer::generate", 1
)[0]
assert prepare_body.index("m_motionProgram && m_program && m_presentProgram") < prepare_body.index("GlStateGuard state"), (
    "prepared frames should skip the expensive GL state snapshot"
)

# The EGL hook must submit the real frame before drawing/swapping the prediction.
hook = main.split("EGLBoolean betterVisualsEglSwapBuffers", 1)[1].split(
    "void installAndroidEglSwapHook", 1
)[0]
native_swap = hook.index("EGLBoolean const realSwapResult = eglSwapBuffers(display, surface)")
generated_draw = hook.index("g_androidFrameHistory.presentGeneratedFrame()")
assert native_swap < generated_draw, "the real frame must be presented before the predicted frame"
assert "if (realSwapResult == EGL_TRUE && frameReady && generateOnThisInterval)" in hook
assert "presentNativeFrame" not in main
assert "m_pendingTexture" not in main

# Sign and adaptive-step checks for the shader's coordinate convention:
# if an object moved right, its matching previous location is left of current.
current_x = 0.50
previous_offset_x = -0.02
stable_fraction = 0.5
fast_fraction = 0.25
stable_x = current_x - previous_offset_x * stable_fraction
fast_x = current_x - previous_offset_x * fast_fraction
assert stable_x > current_x and fast_x > current_x
assert abs(stable_x - current_x - 0.01) < 1e-9, "stable motion must use half displacement"
assert abs(fast_x - current_x - 0.005) < 1e-9, "fast motion must use quarter displacement"

print("Android forward-predictive frame-generation source checks passed (static only).")


# Windows uses the same interpolation renderer with native WGL presentation.
windows = main.split("#ifdef GEODE_IS_WINDOWS\nnamespace {\n    std::atomic<bool> g_windowsFrameHistoryEnabled", 1)[1].split("\n#endif", 1)[0]
for token in (
    "class WindowsFrameHistory final",
    "m_interpolator.prepare(m_width, m_height)",
    "m_interpolator.generate(m_textures[previousIndex], m_textures[currentIndex])",
    "m_timer.shouldInterpolate()",
    "m_fence.retire()",
    "glCopyTexSubImage2D",
    "wglGetCurrentDC()",
    "GetClientRect(window, &client)",
    "loadWindowsGlProc",
):
    assert token in windows, f"missing Windows frame-history path: {token}"

windows_hook = main.split("class $modify(BetterVisualsEGLView, CCEGLView)", 1)[1].split("\n};", 1)[0]
assert "void swapBuffers()" in windows_hook
assert "bool const frameReady = g_windowsFrameHistory.capture()" in windows_hook
assert windows_hook.index("bool const frameReady") < windows_hook.index("CCEGLView::swapBuffers(); // Always submit the real frame first.")
assert windows_hook.index("CCEGLView::swapBuffers(); // Always submit the real frame first.") < windows_hook.index("g_windowsFrameHistory.presentGeneratedFrame()")
assert "CCEGLView::swapBuffers(); // Submit one generated frame for each real interval (2x target)." in windows_hook

print("Windows WGL frame-generation source checks passed (static only).")
