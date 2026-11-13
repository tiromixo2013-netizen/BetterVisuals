from pathlib import Path


def main() -> None:
    source = (Path(__file__).resolve().parents[1] / "src/main.cpp").read_text(encoding="utf-8")

    # Geode 5.9.0's 2.2081 Android bindings do not provide an address for
    # CCDirector::drawScene. A $modify override therefore cannot instrument
    # this method on Android; keep this unsupported diagnostic out of the mod.
    assert "void drawScene()" not in source, "Unsupported CCDirector::drawScene hook was reintroduced"
    assert "[FramePacing] Android drawScene:" not in source
    assert "betterVisualsFrameGenerationSwapBuffers" not in source

    print("Unsupported Android drawScene hook is absent.")


if __name__ == "__main__":
    main()
