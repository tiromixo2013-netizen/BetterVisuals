from __future__ import annotations

from pathlib import Path


def main() -> None:
    source = Path("src/main.cpp").read_text(encoding="utf-8")
    marker = "// Apply the hardware texture limit as one uniform scale factor."
    assert marker in source
    start = source.index(marker)
    end = source.index("auto const needsPresentation", start)
    block = source[start:end]

    assert "static_cast<float>(maxTextureSize) / static_cast<float>(width)" in block
    assert "static_cast<float>(maxTextureSize) / static_cast<float>(height)" in block
    assert "std::min(effectiveScale, maxDimensionScale)" in block
    assert "scaledDimension(width, boundedScale)" in block
    assert "scaledDimension(height, boundedScale)" in block
    assert "std::min(\n            scaledDimension(width, effectiveScale)" not in block
    assert "std::min(\n            scaledDimension(height, effectiveScale)" not in block

    print("Render-target texture-limit aspect-ratio self-check passed.")


if __name__ == "__main__":
    main()
