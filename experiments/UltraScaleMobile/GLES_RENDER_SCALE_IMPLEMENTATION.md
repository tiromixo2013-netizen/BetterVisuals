# GLES Render-Scale Implementation Gate

Status: design contract only. The current GLES layer is telemetry-only; no render-scale, upscaling, or sharpening pass is active.

## Required execution order

1. Render Scale: render the scene into an owned off-screen color target whose dimensions are derived from the drawable surface and a validated scale factor.
2. Upscaling: sample that off-screen target into the original drawable surface. The selected upscaler must match the shader bundle and the device's GLES capabilities.
3. Sharpening: apply CAS only after the upscaling pass, with bounded strength and a bypass at zero.
4. Presets: presets are parameter bundles over the three stages above; they must not reorder the passes.

## Why viewport interception is not Render Scale

Changing application calls to `glViewport` does not allocate a lower-resolution render target, redirect framebuffer attachments, resolve multisampling, or reconstruct the final image. It can simply leave unused pixels or clip the scene. Therefore the current `LayerViewport` wrapper must remain observational until a complete render-target path exists.

## First supported path: controlled GLES test harness

Implement and validate the pipeline in an app we control and can mark debuggable/opt-in before enabling any game adapter. The harness must:

- create an owned RGBA off-screen texture and framebuffer at the selected internal resolution;
- verify framebuffer completeness and preserve/restore GL state;
- render the test scene into that target;
- perform a full-screen pass into the original surface at its native dimensions;
- handle surface resize, context loss/recreation, and GL resource deletion;
- either resolve multisample targets correctly or explicitly reject that configuration;
- preserve alpha/color-space behavior and avoid sampling from the same image currently attached for drawing;
- provide a bypass path that reproduces the original-resolution output.

## Runtime acceptance gates

A stage is not considered enabled until all of these are true:

- the GLES layer is confirmed loaded in the target process (not merely configured through Android settings);
- the off-screen target dimensions match the requested scale within the documented rounding policy;
- framebuffer completeness succeeds;
- the final surface remains at its original dimensions;
- a captured frame demonstrates the expected low-resolution intermediate followed by the full-resolution output;
- resize/context-loss tests pass and the bypass path remains usable.

## Game-adapter policy

Do not enable generic interception for arbitrary engines. Each adapter must identify a stable scene-render boundary and prove that the intercepted target is the scene color target, not UI, shadow, post-processing, or an intermediate unrelated to final output. Unknown render graphs remain telemetry-only.

## Planned API and settings contract

Keep the settings order stable across UI, runtime config, and shader dispatch:

- `renderScale`: finite value in (0, 1], default 1.0;
- `upscaler`: explicit algorithm identifier, default `none`;
- `casSharpness`: finite bounded value, default 0 (disabled);
- `presetId`: optional named parameter bundle applied after the individual controls are loaded.

Invalid values must fall back to bypass. No stage may silently claim success when its prerequisite stage is unavailable.

## Current implementation boundary

The existing `GlesLayer.cpp` records framebuffer/texture metadata, viewport dimensions, draw counts, and swap timing. It does not yet own an off-screen target, redirect scene rendering, dispatch the packaged shaders, or composite a final frame. The Vulkan layer is also telemetry/pass-through. This document records the implementation gate; it does not claim that any graphics effect is active.
