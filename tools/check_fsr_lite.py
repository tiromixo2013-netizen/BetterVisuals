#!/usr/bin/env python3
"""Static checks for the optional six-read FSR Lite path."""
import json
from pathlib import Path

shader = Path("src/shaders/upscale/FsrLiteShader.cpp").read_text(encoding="utf-8")
header = Path("src/shaders/PostProcessShaders.hpp").read_text(encoding="utf-8")
main = Path("src/main.cpp").read_text(encoding="utf-8")
settings = json.loads(Path("mod.json").read_text(encoding="utf-8"))["settings"]

assert shader.count("texture2D(u_texture,") == 6
assert "bilinearFallback" in shader
assert "orientationConfidence" in shader
assert "gl_FragColor = vec4(result, center.a)" in shader
assert "kFsrLiteShader" in header
assert "UpscaleMethod::FsrLite" in main
assert "g_fsrLiteRenderer.apply" in main
assert "FSR Lite (6-Tap)" in settings["upscale-method"]["one-of"]
assert "FSR 1" in settings["upscale-method"]["one-of"]
print("FSR Lite six-read, alpha-preserving, fallback and integration checks passed.")
