# UltraScale Render Scale Harness

The app-owned GLES2 harness is available from the UltraScale Mobile Game Center card named **RENDER SCALE LAB**.

## What it executes

- Allocates an RGBA texture at `round(surfaceWidth * scale)` by `round(surfaceHeight * scale)`.
- Attaches the texture to an owned framebuffer and checks `glCheckFramebufferStatus`.
- Draws a procedural diagnostic scene into that lower-resolution framebuffer.
- Binds the default framebuffer and draws a full-screen textured quad at the unchanged surface dimensions.
- Reconstructs the output with Linear or Catmull-Rom 4×4 sampling.
- Optionally applies an AMD CAS-derived sharpen-only filter after reconstruction, fused into the output pass.
- Recreates the target when the selected scale or surface dimensions change.

The slider covers 25% through 100%, with 70% as its initial value. A red output indicates that target creation or framebuffer completeness failed.

## What this proves

When built and run on a GLES2-capable device, this harness exercises a genuine off-screen lower-resolution render followed by a full-size sampling pass in an application controlled by this project.

## What it does not prove

- It does not inject into or modify another application.
- It does not demonstrate that an arbitrary game's scene color target can safely be intercepted.
- It does not include FSR, SGSR, frame generation, multisample resolve, HDR, or game-specific UI separation.
- It is not yet a production-ready render pipeline or a verified APK.

## Validation sequence

1. Build and install the debug app.
2. Open **RENDER SCALE LAB**.
3. Confirm the moving diagnostic pattern renders at 100%.
4. Reduce scale to 70%, 50%, and 25%; the output surface should remain full size while the intermediate target dimensions shrink.
5. Rotate/resize the surface and confirm the image recovers.
6. Return to 100% and confirm the bypass-equivalent full-resolution target still renders.

No build or device run is claimed by the source change alone.

## Upscaling modes added

The composite pass now exposes two explicit reconstruction modes:

- **Linear (fast):** standard GLES texture filtering; lower sample cost.
- **Catmull-Rom 4×4:** a separable cubic reconstruction evaluated from a 4×4 neighborhood (16 texture samples) with edge clamping and output clamping.

The Catmull-Rom path uses a fragment precision fallback for GLES2 devices without fragment `highp`. It is a spatial reconstruction filter, not AI upscaling and not FSR/FSR 2/3. It may cost more GPU time than linear filtering and can produce different edge behavior. No performance or image-quality claim is made until measured on-device.

## CAS sharpening pass

The output pass now has an adjustable **CAS sharpening** control (0–100%, default off). It ports the sharpen-only neighborhood math from the bundled AMD FidelityFX CAS-derived asset into the GLES2-compatible fragment path:

- Reconstructs the center output pixel and its four axial neighbors.
- Computes the local cross-neighborhood min/max range.
- Derives the adaptive green-channel amplitude and shared RGB filter weight.
- Applies the CAS sharpen-only weighted result after the selected reconstruction mode.

The pass is fused with the output composite to avoid allocating another full-resolution intermediate target. This means Catmull-Rom plus CAS repeats the 4×4 reconstruction for neighboring output samples and can be substantially more expensive; start with Linear when measuring cost. This is an app-owned harness adaptation of the bundled CAS-derived math, not evidence of CAS being injected into any game. The original bundled shader and its MIT notice remain unchanged.
