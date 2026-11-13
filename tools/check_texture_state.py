from __future__ import annotations

from pathlib import Path


def main() -> None:
    source = Path("src/main.cpp").read_text(encoding="utf-8")
    assert "glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &previousMinFilter)" in source
    assert "glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &previousMagFilter)" in source
    assert "glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, previousMinFilter)" in source
    assert "glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, previousMagFilter)" in source
    assert "glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);" not in source
    assert "glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);" not in source
    print("Texture sampling state restoration self-check passed.")


if __name__ == "__main__":
    main()
