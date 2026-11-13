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
    print("FSR 1 quality preset self-check passed.")


if __name__ == "__main__":
    main()
