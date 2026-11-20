from __future__ import annotations

import json
from pathlib import Path


def main() -> None:
    settings = json.loads(Path("mod.json").read_text(encoding="utf-8"))["settings"]
    methods = settings["upscale-method"]["one-of"]
    assert "Integer Scaling" in methods
    assert "UltraScale2D" in methods

    preset = settings["integer-scaling-quality-preset"]
    assert preset["default"] == "Performance (2x)"
    assert preset["one-of"] == [
        "Native (1x)",
        "Performance (2x)",
        "Ultra Performance (3x)",
    ]

    source = Path("src/main.cpp").read_text(encoding="utf-8")
    assert "Integer," in source
    assert "SmartInteger," in source
    assert 'value == "UltraScale2D"' in source
    assert "auto const smartIntegerScaling" in source
    assert "UpscaleMethod::SmartInteger" in source
    assert "kSmartIntegerShader" in source
    assert "g_smartIntegerRenderer.apply" in source
    assert 'value == "Integer Scaling"' in source
    assert 'bindStringSetting("integer-scaling-quality-preset", updateIntegerScalingPreset)' in source
    assert 'if (value == "Native (1x)") factor = 1;' in source
    assert 'else if (value == "Performance (2x)") factor = 2;' in source
    assert 'else if (value == "Ultra Performance (3x)") factor = 3;' in source

    # Verify exact integer source dimensions and exact N×N output pixel coverage.
    assert "width / integerFactor" in source
    assert "height / integerFactor" in source
    assert "integerOutputWidth = internalWidth * integerFactor" in source
    assert "integerOutputHeight = internalHeight * integerFactor" in source
    assert "glViewport(" in source and "integerOutputWidth" in source
    assert "glScissor(" in source
    assert "glClearColor(0.f, 0.f, 0.f, 1.f)" in source

    # The existing presentation path selects GL_NEAREST unless a linear upscaler is active.
    assert "linearSampling ? GL_LINEAR : GL_NEAREST" in source
    assert "config.upscaling == UpscaleMethod::Bilinear" in source
    assert "config.upscaling == UpscaleMethod::FsrLite" in source
    assert "config.upscaling == UpscaleMethod::Sgsr" in source
    assert "config.upscaling == UpscaleMethod::Integer" in source
    assert "config.upscaling == UpscaleMethod::SmartInteger" in source
    assert "slider and SSAA multiplier are intentionally bypassed" in source
    print("Integer Scaling 1x/2x/3x integration checks passed (static checks only).")


if __name__ == "__main__":
    main()
