from __future__ import annotations

from pathlib import Path


def main() -> None:
    source = Path("src/render/Gl.hpp").read_text(encoding="utf-8")
    save = source.index("glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment)")
    set_tight = source.index("glPixelStorei(GL_UNPACK_ALIGNMENT, 1)", save)
    upload = source.index("glTexImage2D(", set_tight)
    restore = source.index(
        "glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment)", upload
    )
    assert save < set_tight < upload < restore
    assert "auto const uploadError = glGetError();" in source
    assert "return uploadError == GL_NO_ERROR;" in source
    print("Texture unpack alignment self-check passed.")


if __name__ == "__main__":
    main()
