#!/usr/bin/env python3
"""Static checks for SGSR1 alpha-safe capture and restored quality presets."""

import json
from pathlib import Path

pipeline = Path("src/render/PostProcessPipeline.cpp").read_text(encoding="utf-8")
sgsr = Path("src/render/SgsrRenderer.cpp").read_text(encoding="utf-8")
main = Path("src/main.cpp").read_text(encoding="utf-8")
settings = json.loads(Path("mod.json").read_text(encoding="utf-8"))["settings"]

assert "glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor.data())" in pipeline
assert "glClearColor(0.f, 0.f, 0.f, 0.f)" in pipeline
assert "previousClearColor[3]" in pipeline
assert "outColor = vec4(color, sourceSample.a)" in sgsr
assert "glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA)" in sgsr

# SGSR1 and FSR1 presets must be separate and bind to their own upscaler.
assert "sgsr-quality-preset" in settings
assert settings["sgsr-quality-preset"]["default"] == "Custom"
assert settings["sgsr-quality-preset"]["one-of"] == settings["fsr-quality-preset"]["one-of"]
assert "g_sgsrPresetScale" in main
assert "updateSgsrQualityPreset" in main
assert 'bindStringSetting("sgsr-quality-preset", updateSgsrQualityPreset)' in main
assert "config.upscaling == UpscaleMethod::Sgsr" in main
assert "auto const presetScale = config.upscaling == UpscaleMethod::Fsr ?" in main

# Avoid repeating a synchronous capability query on every frame-history swap.
assert "GL_MAX_TEXTURE_SIZE is invariant for a live context" in main
assert "GLint m_maxTextureSize = 0;" in main

print("SGSR1 alpha, independent quality presets, and FG context-cache checks passed.")
