# UltraScale Mobile shader bundle

The Android app packages the following shader sources under
`app/src/main/assets/upscalers/` and exposes them through
`UpscaleTechniqueCatalog`.

| Technique | Role | Shader profile | Source/license notes |
|---|---|---|---|
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
No SGSR2 implementation is included in this bundle because no SGSR2 shader source
was present in the inspected project tree.

## Android support notes

- GLES 3.00 sources require an ES 3.0-capable context.
- SGSR1 Mobile declares GLSL ES 3.10 and therefore requires ES 3.1 shader support.
- The graphics layer remains pass-through/telemetry code. It does not yet replace
  game render targets or modify the game's own settings interface.
