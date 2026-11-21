from __future__ import annotations

import json
from pathlib import Path


def main() -> None:
    settings = json.loads(Path("mod.json").read_text(encoding="utf-8"))["settings"]
    preset = settings["upscale-quality-preset"]
    expected = [
        "Custom",
        "Native",
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
    assert "Custom" in render_scale["description"]
    assert "override" in render_scale["description"]

    source = Path("src/main.cpp").read_text(encoding="utf-8")
    expected_scales = {
        "Native": "1.00f",
        "Ultra Quality": "0.90f",
        "Quality": "0.82f",
        "Balanced": "0.75f",
        "Performance": "0.67f",
        "Ultra Performance": "0.59f",
    }
    for label, scale in expected_scales.items():
        assert f'value == "{label}"' in source
        assert f"scale = {scale}" in source

    assert "g_upscaleQualityScale" in source
    assert "updateUpscaleQualityPreset" in source
    assert "presetScale > 0.f ? presetScale : configuredScale" in source
    assert "config.upscaling == UpscaleMethod::Fsr" in source
    assert "config.upscaling == UpscaleMethod::Sgsr" in source
    assert "config.upscaling == UpscaleMethod::Mwsr" in source
    assert 'bindStringSetting("upscale-quality-preset", updateUpscaleQualityPreset)' in source
    assert 'bindDoubleSetting("render-scale", g_renderScale, 0.25f, 1.2f, 1.f)' in source
    assert "g_fsrPresetScale" not in source
    assert "g_sgsrPresetScale" not in source
    assert "g_mwsrPresetScale" not in source
    for old_setting in ("fsr-quality-preset", "sgsr-quality-preset", "mwsr-quality-preset"):
        assert old_setting not in settings
    assert "GL_MAX_TEXTURE_SIZE" in source
    assert settings["fsr-sharpness"]["default"] == 1
    print("Shared upscaling presets and Custom Render Scale checks passed.")


if __name__ == "__main__":
    main()
