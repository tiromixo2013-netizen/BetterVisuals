#!/usr/bin/env python3
"""Static integration checks for the MWSR + SGSR1 bake-off path."""
import json
from pathlib import Path

main = Path("src/main.cpp").read_text(encoding="utf-8")
renderer = Path("src/render/MwsrRenderer.cpp").read_text(encoding="utf-8")
header = Path("src/render/MwsrRenderer.hpp").read_text(encoding="utf-8")
shader_header = Path("src/shaders/PostProcessShaders.hpp").read_text(encoding="utf-8")
settings = json.loads(Path("mod.json").read_text(encoding="utf-8"))["settings"]

assert "UpscaleMethod::Mwsr" in main
assert "g_mwsrRenderer.apply(" in main
assert "sourceTexture, internalWidth, internalHeight, width, height" in main
assert 'bindStringSetting("mwsr-quality-preset", updateMwsrQualityPreset)' in main
assert "g_aqr4Renderer" not in main and "UpscaleMethod::Aqr4" not in main
assert "textureLod(uTex, uv, 0.0).a" in renderer
assert "glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA" in renderer
assert "CHROMA444 = true" in renderer
assert "mwsrLaw(" in renderer
assert "class MwsrRenderer final" in header
assert "kAqr4Shader" not in shader_header
assert "MWSR" in settings["upscale-method"]["one-of"]
assert "AQR-4" not in settings["upscale-method"]["one-of"]
assert settings["mwsr-quality-preset"]["default"] == "Balanced"
assert settings["mwsr-quality-preset"]["one-of"] == [
    "Custom", "Native", "Ultra Quality", "Quality", "Balanced", "Performance", "Ultra Performance"
]
print("MWSR registration, preset, alpha-compositing and AQR-4 removal checks passed.")
