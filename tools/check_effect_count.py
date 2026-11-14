from pathlib import Path


def main() -> None:
    source = (Path(__file__).resolve().parents[1] / "src/main.cpp").read_text(encoding="utf-8")
    assert "auto const hasPostProcessAa =" in source
    assert "aa == AntiAliasingMethod::Fxaa || aa == AntiAliasingMethod::SmaaHigh ||" in source
    assert "aa == AntiAliasingMethod::SmaaUltra;" in source
    assert "return hasPostProcessAa + cas + bloom + grayscale + pixelate + invert + flip +" in source
    assert "return (aa != AntiAliasingMethod::Off) + cas + bloom" not in source
    print("SSAA presentation-stage counting self-check passed.")


if __name__ == "__main__":
    main()
