#!/usr/bin/env python3
"""Static regression checks for the opt-in Android EGL frame-history capture stage."""

from pathlib import Path

source = Path("src/main.cpp").read_text(encoding="utf-8")
manifest = Path("mod.json").read_text(encoding="utf-8")

required_source = (
    "class AndroidFrameHistory final",
    "eglQuerySurface(display, surface, EGL_WIDTH, &width)",
    "eglQuerySurface(display, surface, EGL_HEIGHT, &height)",
    "GL_MAX_TEXTURE_SIZE",
    "m_context != context",
    "glTexImage2D(",
    "glCopyTexSubImage2D(",
    "glBindFramebuffer(GL_FRAMEBUFFER, 0)",
    "previousFramebuffer",
    "previousActiveTexture",
    "previousTexture",
    "g_frameHistoryEnabled.load(std::memory_order_relaxed)",
    "g_androidFrameHistory.capture(display, surface, context)",
    "g_androidFrameHistory.generate()",
    "Forward prediction generated after the real-frame swap",
    "bool hasEglExtension(char const* extensions, std::string_view name)",
    "EGL_ANDROID_presentation_time",
    "EGL_ANDROID_get_frame_timestamps",
    'eglGetProcAddress("eglPresentationTimeANDROID")',
    'eglGetProcAddress("eglGetFrameTimestampsANDROID")',
    "logAndroidPresentationCapabilities(display)",
    "Display pacing is separate.",
)
for token in required_source:
    assert token in source, f"missing frame-history safeguard or stage: {token}"

assert '"frame-history-capture"' in manifest
assert '"default": false' in manifest
assert "g_frameHistoryEnabled" in source
assert "opt-in" in source
print("Android EGL frame-history source checks passed (static checks only).")
