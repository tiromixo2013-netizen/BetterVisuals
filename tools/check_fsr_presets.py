from __future__ import annotations

import json
from pathlib import Path


def main() -> None:
    settings = json.loads(Path("mod.json").read_text(encoding="utf-8"))["settings"]
    preset = settings["fsr-quality-preset"]
    expected = [
        "Custom",
        "Ultra Quality",
        "Quality",
        "Balanced",
        "Performance",
        "Ultra Performance",
    ]
    assert preset["default"] == "Custom"
    assert preset["one-of"] == expected

    render_scale = settings["render-scale"]
    assert render_scale["default"] == 1
    assert render_scale["min"] == 0.25
    assert render_scale["max"] == 1.2

    source = Path("src/main.cpp").read_text(encoding="utf-8")
    expected_scales = {
        "Ultra Quality": "0.77f",
        "Quality": "0.67f",
        "Balanced": "0.59f",
        "Performance": "0.50f",
        "Ultra Performance": "0.33f",
    }
    for label, scale in expected_scales.items():
        assert f'value == "{label}"' in source
        assert f"scale = {scale}" in source

    assert "config.upscaling == UpscaleMethod::Fsr" in source
    assert "g_fsrPresetScale.load" in source
    assert "(fsrPresetScale > 0.f ? fsrPresetScale : 1.f) * configuredScale" in source
    assert "std::clamp(requestedScale, 0.25f, 1.f)" in source
    assert 'bindDoubleSetting("render-scale", g_renderScale, 0.25f, 1.2f, 1.f)' in source
    assert "GL_MAX_TEXTURE_SIZE" in source
    print("FSR preset base × Render Scale multiplier checks passed.")


if __name__ == "__main__":
    main()
