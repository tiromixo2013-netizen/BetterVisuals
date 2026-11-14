#!/usr/bin/env python3
"""Static regression checks for Android forward-predictive frame generation.

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
    "m_interpolator.prepare(width, height)",
    "m_interpolator.generate(m_textures[previousIndex], m_textures[currentIndex])",
    "bool presentGeneratedFrame()",
):
    assert token in main, f"missing Android integration: {token}"

for token in (
    "uniform sampler2D u_previous",
    "uniform sampler2D u_current",
    "vec2 searchStep = 2.0 * u_invResolution",
    "float second = 1.0",
    "reverseBest > 0.075 || inconsistent",
    "vec2 predictedCoord = clamp(v_uv - offset, 0.0, 1.0)",
    "vec4 predictedSample = texture2D(u_current, predictedCoord)",
    "gl_FragColor = predictedSample",
    "gl_FragColor = currentSample",
    "best > 0.075",
    "confidence < 0.30",
    "refineEightPixelBlock",
    "blockIndex * 16.0",
    "u_reusedMotion",
    "reuseValid",
    "m_motionTextures",
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
native_swap = hook.index("result = eglSwapBuffers(display, surface)")
generated_draw = hook.index("g_androidFrameHistory.presentGeneratedFrame()")
assert native_swap < generated_draw, "the real frame must be presented before the predicted frame"
assert "presentNativeFrame" not in main
assert "m_pendingTexture" not in main

# Sign check for the shader's coordinate convention:
# if an object moved right, its matching previous location is left of current;
# the negative previous-frame offset is subtracted to predict farther right.
current_x = 0.50
previous_offset_x = -0.02
predicted_x = current_x - previous_offset_x
assert predicted_x > current_x

print("Android forward-predictive frame-generation source checks passed (static only).")
