from pathlib import Path
import re


def main() -> None:
    source = (Path(__file__).resolve().parents[1] / "src/main.cpp").read_text(encoding="utf-8")

    marker = "#ifdef GEODE_IS_ANDROID\n    // Diagnostic only: measure actual scene-draw cadence on-device."
    assert marker in source, "Android-only frame-pacing diagnostic is missing"
    start = source.index(marker)
    end = source.index("\n#endif", start)
    block = source[start:end]

    assert re.search(r"void drawScene\(\)\s*\{", block), "drawScene hook is missing"
    assert "std::chrono::steady_clock" in block, "Use a monotonic clock for cadence"
    assert "frameCount" in block and "callsPerSecond" in block
    assert "minIntervalMs" in block and "maxIntervalMs" in block
    assert "[FramePacing] Android drawScene:" in block
    assert block.count("CCDirector::drawScene();") == 1, "Call the original drawScene exactly once"
    assert "mainLoop(" not in block, "Do not hook the virtual mainLoop path here"
    assert "swapBuffers" not in block, "Do not add a swap-buffer hook here"
    assert "setDeltaTime" not in block and "skip" not in block.lower()
    assert source.count("void drawScene()") == 1, "Avoid duplicate drawScene hooks"

    print("Android frame-pacing hook self-check passed.")


if __name__ == "__main__":
    main()
