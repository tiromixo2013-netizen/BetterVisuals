#!/usr/bin/env python3
"""Static checks for the Android GPU midpoint reconstruction path."""

from pathlib import Path

main = Path("src/main.cpp").read_text(encoding="utf-8")
header = Path("src/render/FrameInterpolationRenderer.hpp").read_text(encoding="utf-8")
renderer = Path("src/render/FrameInterpolationRenderer.cpp").read_text(encoding="utf-8")

for token in (
    "FrameInterpolationRenderer m_interpolator",
    "m_interpolator.abandonContext()",
    "m_interpolator.prepare(width, height)",
    "m_interpolator.generate(m_textures[previousIndex], m_textures[currentIndex])",
):
    assert token in main, f"missing Android integration: {token}"

for token in (
    "uniform sampler2D u_previous",
    "uniform sampler2D u_current",
    "bestOffset",
    "bestCost",
    "u_invResolution",
    "fromPrevious",
    "fromCurrent",
    "gl_FragColor = vec4((fromPrevious + fromCurrent) * 0.5, 1.0)",
):
    assert token in renderer, f"missing motion reconstruction stage: {token}"

for token in (
    "bool prepare(GLsizei width, GLsizei height)",
    "bool generate(GLuint previousFrame, GLuint currentFrame)",
    "GLuint outputTexture() const",
    "void abandonContext()",
):
    assert token in header, f"missing renderer API: {token}"

print("Android GPU midpoint reconstruction source checks passed (static checks only).")
