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
    detour = source.split("EGLBoolean betterVisualsEglSwapBuffers", 1)[1].split(
        "void installAndroidEglSwapHook", 1
    )[0]
    assert detour.count("eglSwapBuffers(display, surface)") == 4
    assert "presentGeneratedFrame()" in detour
    assert "scheduleRealFrame" in detour
    assert "scheduleGeneratedFrame" in detour
    assert "EGL_ANDROID_presentation_time" in source
    assert "m_period / 2" in source
    assert "The current real frame is always swapped first" in detour
    assert detour.index("result = eglSwapBuffers(display, surface)") < detour.index(
        "presentGeneratedFrame()"
    )
    assert "presentNativeFrame()" not in detour
    assert "per-thread chain index" in detour
    assert "Mod::get()->hook(" in source
    assert "tulip::hook::TulipConvention::Cdecl" in source
    assert "installAndroidEglSwapHook();" in source
    assert "if (ANDROID)\n    target_link_libraries(${PROJECT_NAME} EGL)\nendif()" in cmake

    print("Android EGL real-first predictive presentation checks passed (static only).")


if __name__ == "__main__":
    main()
