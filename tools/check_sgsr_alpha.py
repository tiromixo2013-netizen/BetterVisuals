#!/usr/bin/env python3
"""Static checks for SGSR1 alpha-safe capture and isolated preset scaling."""

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

# SGSR1 must not silently change the scene-capture resolution through a
# preset until the Android viewport/scissor path is validated. FSR presets
# remain supported; SGSR uses the explicit Render Scale only.
assert "sgsr-quality-preset" not in settings
assert "g_sgsrPresetScale" not in main
assert "updateSgsrQualityPreset" not in main
assert "auto const presetScale = fsrPresetScale;" in main

print("SGSR1 alpha preservation and preset isolation checks passed.")
