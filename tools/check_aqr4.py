#!/usr/bin/env python3
"""Static integration checks for the standalone AQR-4 shader path."""
import json
from pathlib import Path

shader = Path("src/shaders/upscale/Aqr4Shader.cpp").read_text(encoding="utf-8")
header = Path("src/shaders/PostProcessShaders.hpp").read_text(encoding="utf-8")
main = Path("src/main.cpp").read_text(encoding="utf-8")
settings = json.loads(Path("mod.json").read_text(encoding="utf-8"))["settings"]

assert shader.count("texture2D(u_texture,") == 4
assert 'uniform float u_edgeStrength;' in shader
assert '"u_edgeStrength"' in shader
assert "kAqr4Shader" in header
assert "UpscaleMethod::Aqr4" in main
assert "g_aqr4Renderer.prepare(bv::shaders::kAqr4Shader" in main
assert "g_aqr4Renderer.apply(sourceTexture, g_aqr4EdgeStrength" in main
assert 'bindStringSetting("aqr4-quality-preset", updateAqr4QualityPreset)' in main
assert 'bindDoubleSetting("aqr4-edge-strength", g_aqr4EdgeStrength' in main
assert "aqr4Upscaling" in main and "g_aqr4PresetScale" in main
assert "config.upscaling == UpscaleMethod::Aqr4;" in main
assert "AQR-4" in settings["upscale-method"]["one-of"]
assert settings["aqr4-quality-preset"]["default"] == "Native"
assert settings["aqr4-quality-preset"]["one-of"] == [
    "Native", "Ultra Quality", "Quality", "Balanced", "Performance", "Ultra Performance"
]
print("AQR-4 shader, renderer, scaling presets and settings integration checks passed.")
