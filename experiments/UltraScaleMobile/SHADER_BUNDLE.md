# UltraScale Mobile shader bundle

The Android app packages the following shader sources under
`app/src/main/assets/upscalers/` and exposes them through
`UpscaleTechniqueCatalog`.

| Technique | Role | Shader profile | Source/license notes |
|---|---|---|---|
| Qualcomm SGSR2 2-pass fragment | Temporal upscaling reference | Vulkan GLSL ES 3.20 | Official Qualcomm shader set, BSD-3-Clause; convert, upscale and vertex sources bundled |
| AMD FidelityFX FSR1 EASU | Spatial reconstruction | GLSL ES 1.00-style fragment source (GLES 2.0+) | AMD official-derived EASU shader source, MIT; full notice retained in asset |
| FSR Lite 6-tap | Spatial reconstruction | GLSL ES 3.00 | Project-authored EASU-inspired approximation; not official AMD FidelityFX FSR |
| MWSR | Spatial reconstruction | GLSL ES 3.00 | MoonlightWeb Super Resolution, MIT; original copyright and license notice retained in source |
| SGSR1 Mobile | Edge-adaptive reconstruction | GLSL ES 3.10 | Qualcomm Innovation Center source, BSD-3-Clause; notice retained in source |
| AMD CAS | Sharpening only | GLSL ES 3.00 | AMD FidelityFX CAS-derived sharpen-only path, MIT; notice retained in source |

## Integration boundary

These files are packaged source assets and can be loaded by the app with
`UpscaleTechniqueCatalog.source(context, technique)`. Packaging does not mean
that a shader is compiled, selected automatically, or applied to a third-party
game. A real render integration still needs:

1. A verified source image/render target and its dimensions, format and color space.
2. A compatible GLES/Vulkan render pass and shader interface/parameter mapping.
3. A distinct output image at the requested scale, framebuffer/swapchain integration,
   and correct synchronization/lifetime handling.
4. Device-side shader compilation and visual/performance validation for each technique.

CAS is a sharpening pass and does not upscale. FSR Lite is a custom lightweight
reconstruction shader, not AMD's official FSR implementation. MWSR and SGSR1
need their source-specific uniforms and viewport conventions mapped before use.
SGSR2 source is included as a reference set, not a working runtime option: it requires depth and motion-vector inputs, camera reprojection/jitter parameters, per-frame history ping-pong, and a Vulkan descriptor/pipeline setup. The current sample renderer does not provide those temporal inputs.

## Render-target discovery status

The GLES layer records the framebuffer bound at each observed draw call and snapshots the most recently drawn framebuffer plus its color-attachment texture metadata at the next sampled swap. This avoids the common false assumption that the framebuffer still bound at `eglSwapBuffers` is the scene target: many engines bind framebuffer 0 again before presenting. The captured target is still a candidate only. Multi-pass engines may draw UI, post-processing, or intermediate passes last; shared EGL contexts, non-2D textures, and engine-specific render graphs require additional handling before any target can be safely replaced. The GLES layer now also records `glBindRenderbuffer`, `glRenderbufferStorage`, `glFramebufferRenderbuffer`, and renderbuffer deletion, so color attachments backed by renderbuffers can be reported with dimensions and format instead of being silently missed. This remains metadata discovery, not proof that an attachment is the final scene or safe to sample.

## Android support notes

- GLES 3.00 sources require an ES 3.0-capable context.
- SGSR1 Mobile declares GLSL ES 3.10 and therefore requires ES 3.1 shader support.
- SGSR2 is Vulkan GLSL ES 3.20 with descriptor-set bindings; it is not directly compilable as an OpenGL ES fragment shader and is bundled for future Vulkan pipeline integration.
- The graphics layer remains pass-through/telemetry code. It does not yet replace
  game render targets or modify the game's own settings interface.
