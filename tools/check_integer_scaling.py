from __future__ import annotations

import json
from pathlib import Path


def main() -> None:
    settings = json.loads(Path("mod.json").read_text(encoding="utf-8"))["settings"]
    methods = settings["upscale-method"]["one-of"]
    assert "Integer Scaling" not in methods
    assert "UltraScale2D" not in methods
    assert "integer-scaling-quality-preset" not in settings
    assert "ultrascale2d-quality-preset" not in settings

    source = Path("src/main.cpp").read_text(encoding="utf-8")
    shader_header = Path("src/shaders/PostProcessShaders.hpp").read_text(encoding="utf-8")
    assert "Integer," not in source
    assert "SmartInteger," not in source
    assert "UltraScale2D" not in source
    assert "integerScaling" not in source
    assert "kSmartIntegerShader" not in shader_header
    assert "kSmartIntegerShader" not in source
    assert "g_smartIntegerRenderer" not in source
    assert "g_ultraScale2DPresetScale" not in source
    assert "updateUltraScale2DQualityPreset" not in source
    assert "updateIntegerScalingPreset" not in source
    print("Removed Integer Scaling and UltraScale2D paths remain absent.")


if __name__ == "__main__":
    main()
