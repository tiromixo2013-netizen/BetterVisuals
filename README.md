# <c-ffba30>Better</c> <c-ffce2b>Visuals</c> <c-ffe600>and</c> <c-fff424>FPS</c>

More FPS or better visuals, up to you!

Pro tip: You can change the settings in pause menus.

Recommended settings:

- `Max FPS`: 0.5x + Bilinear, AA off, Sharpening off.
- `Better FPS`: 0.7x + Bilinear, AA off, Sharpening off. (Or FSR 1)
- `Better Visuals`: 1x, SMAA High, Sharpening off.
- `Stream/Record`: 1x, SMAA High, Sharpening on.
- `YT Showcase`: 1x, SSAA 1.5x, Sharpening on. (Or SSAA 3x)

*May distort text and UI elements.*

---

## <c-47f0ff>Render Scale</c>

Render the game at lower resolution for faster rendering then upscale it.

Available methods:

- Nearest neighbour (Native)
- Bilinear interpolation (Native, Default)
- AMD FidelityFX FSR 1 (EASU)
- Qualcomm SGSR 1 (single-frame spatial upscaler)
- MWSR (single-frame spatial upscaler, experimental)

Nearest and Bilinear are the same speed (fastest), but Bilinear looks smoother.
FSR 1 produces sharper edges while upscaling with a small performance cost.
SGSR 1 and MWSR are separate spatial reconstruction methods. MWSR uses an edge-aware cubic reconstruction pass; compare both with the same Render Scale on your device. The winner depends on GPU, output scale, and game content.

*FSR 1 can have more downsides on older hardware.*

---

## <c-47f0ff>MWSR Quality Presets</c>

MWSR is an experimental single-frame spatial upscaler with a GLSL ES 3.00 implementation. It reconstructs from the current RGB frame only; it does not use motion vectors, depth, temporal history, or neural inference. The mod preserves the game's premultiplied alpha when compositing the reconstructed frame.

- Native: 100% per dimension (no upscaling).
- Ultra Quality: 90% per dimension.
- Quality: 82% per dimension.
- Balanced: 75% per dimension.
- Performance: 67% per dimension.
- Ultra Performance: 59% per dimension.
- Custom: uses the general Render Scale slider.

Use the same source-resolution preset for SGSR 1 and MWSR when comparing. GPU timings must be measured on the target device; published desktop and other-phone results do not predict Adreno 610 performance.

## <c-c247ff>Anti-aliasing</c>

Makes the game look better while playing levels.

Available methods:

- FXAA
- SMAA High (Default)
- SMAA Ultra
- SSAA 1.5x, 2x, 3x

FXAA is the fastest.
SMAA High adds better quality with minimal performance cost.
SMAA Ultra is overkill except for extreme resolutions like 720p or 4K on large screens.
SSAA renders at higher resolution, then downsamples for maximum quality at maximum cost.
It multiplies with Render Scale, so a combined factor of 1x or below has no effect.

---

## <c-ff0000>AMD FidelityFX CAS</c>

A fast, high-quality sharpening filter that improves image clarity.
Also makes streams or videos look sharper at low to medium bitrates.

---

## <c-ff8ab1>Fun stuff</c>

Extra shaders you can mix and customize:

- Bloom
- Grayscale
- Pixelate
- Dithering Filter
- VHS Filter
- CRT Filter

*These may cause lag on mobile or low-end devices.*

---

## <c-4dafff>Want more AA methods and shaders?</c>

Join the Discord server or open a GitHub issue/feature request!

Framebuffer and VBO handling referenced from [DevTools](https://github.com/geode-sdk/DevTools).

Other credits, citations, and licenses can be found in the mod's source code

---

## <c-00ffa6>Quotes I got while making the mod</c>

- Finally I can play at 1000 FPS (I use a 200Hz screen)
- works perfectly thanks !! /Unlisted YouTube GD showcase/
- No phone overheat after the upscale update
- no wait i made it work LMAO from 400fps to 900fps :fire:


<!-- Build validation trigger: Android repair branch -->

