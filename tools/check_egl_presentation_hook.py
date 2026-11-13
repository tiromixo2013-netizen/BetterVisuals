from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    source = (root / "src/main.cpp").read_text(encoding="utf-8")
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")

    assert "#ifdef GEODE_IS_ANDROID\n    #include <EGL/egl.h>\n    #include <EGL/eglext.h>\n#endif" in source
    assert "EGLBoolean betterVisualsEglSwapBuffers(EGLDisplay display, EGLSurface surface)" in source
    assert "eglGetCurrentContext()" in source
    assert "eglGetCurrentDisplay()" in source
    assert "eglGetCurrentSurface(EGL_DRAW)" in source
    assert "frame processing remains disabled" in source
    detour = source.split("EGLBoolean betterVisualsEglSwapBuffers", 1)[1].split("\n    }", 1)[0]
    assert detour.count("eglSwapBuffers(display, surface)") == 2
    assert "presentMidpoint()" in detour
    assert "presentNativeFrame()" in detour
    assert "per-thread chain index" in detour
    assert "Mod::get()->hook(" in source
    assert "tulip::hook::TulipConvention::Cdecl" in source
    assert "installAndroidEglSwapHook();" in source
    assert "if (ANDROID)\n    target_link_libraries(${PROJECT_NAME} EGL)\nendif()" in cmake

    # The hook now presents one generated midpoint and then restores the native frame.
    assert "generated midpoint" in detour.lower()
    print("Android EGL presentation-hook source checks passed.")


if __name__ == "__main__":
    main()
