# UltraScale Render Scale Harness

The app-owned GLES2 harness is available from the UltraScale Mobile Game Center card named **RENDER SCALE LAB**.

## What it executes

- Allocates an RGBA texture at `round(surfaceWidth * scale)` by `round(surfaceHeight * scale)`.
- Attaches the texture to an owned framebuffer and checks `glCheckFramebufferStatus`.
- Draws a procedural diagnostic scene into that lower-resolution framebuffer.
- Binds the default framebuffer and draws a full-screen textured quad at the unchanged surface dimensions.
- Uses linear texture filtering for the reconstruction pass.
- Recreates the target when the selected scale or surface dimensions change.

The slider covers 25% through 100%, with 70% as its initial value. A red output indicates that target creation or framebuffer completeness failed.

## What this proves

When built and run on a GLES2-capable device, this harness exercises a genuine off-screen lower-resolution render followed by a full-size sampling pass in an application controlled by this project.

## What it does not prove

- It does not inject into or modify another application.
- It does not demonstrate that an arbitrary game's scene color target can safely be intercepted.
- It does not include FSR, SGSR, CAS, frame generation, multisample resolve, HDR, or game-specific UI separation.
- It is not yet a production-ready render pipeline or a verified APK.

## Validation sequence

1. Build and install the debug app.
2. Open **RENDER SCALE LAB**.
3. Confirm the moving diagnostic pattern renders at 100%.
4. Reduce scale to 70%, 50%, and 25%; the output surface should remain full size while the intermediate target dimensions shrink.
5. Rotate/resize the surface and confirm the image recovers.
6. Return to 100% and confirm the bypass-equivalent full-resolution target still renders.

No build or device run is claimed by the source change alone.
