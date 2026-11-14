from __future__ import annotations

from pathlib import Path


def main() -> None:
    source = Path("src/main.cpp").read_text(encoding="utf-8")
    assert "glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &previousMinFilter)" in source
    assert "glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &previousMagFilter)" in source
    assert "glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, previousMinFilter)" in source
    assert "glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, previousMagFilter)" in source
    # Frame-history textures intentionally use linear sampling; the scene upscaler
    # path must still preserve and restore the caller texture parameters (checked above).
    assert "glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);" in source
    print("Texture sampling state restoration self-check passed.")


if __name__ == "__main__":
    main()
