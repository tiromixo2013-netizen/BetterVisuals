# Asphalt 8 UltraScale integration status

## What the inspected build establishes

The acquired Asphalt 8 Android build is version 9.2.0i (versionCode 92009), ARM64, and its inspected native game library links against EGL/OpenGL ES. The inspection does not establish that Android will load a third-party graphics layer into this production package, nor does it expose the game's internal render targets or frame submission queue.

## Settings model added to the UltraScale manager

The manager now stores a local profile with:
- Render scale (50–100%)
- Upscaler selection (Off, nearest, bilinear, Catmull-Rom, FSR Lite, FSR1 EASU, MWSR, SGSR1, SGSR2)
- Reconstruction sharpness
- CAS-style post-upscale sharpening
- Integer scaling toggle
- Low-latency mode
- Presets: Ultra Performance, Performance, Balanced, Quality, Ultra Quality, Custom

These are profile controls and not proof that Asphalt 8 consumes the settings. The current Asphalt 8 adapter remains config-only for existing keys such as scaleDisplay; no undocumented graphics keys are written.

## Frame Generation decision

Frame Generation is deliberately not exposed as an active switch for Asphalt 8. The inspected build and current adapter provide no supported motion vectors, depth, per-frame history, jitter data, or a controllable present queue. A generic post-process frame interpolator without those inputs would be an unvalidated optical-flow approximation, not a reliable game Frame Generation implementation, and could add latency or visual artifacts. Revisit only after a game-owned render hook can provide the required inputs and the result is validated on-device.

## Activation boundary

The new settings card persists values inside the UltraScale manager. It does not inject code into Asphalt 8, alter the official APK signature, or modify the game's own settings screen. Applying these options to the game requires a permitted, version-tested render-pass adapter and a successful device test.
