# Asphalt Legends Android — mod integration research

Research date: 2026-11-25

## Executive finding

There are at least two historical Android mod approaches, but neither supplies public source code for a reusable, current-version plugin:

1. **Urban GT19 / KTOPlay external-config mod** — reported for the Gameloft Store 1.0.8 build. The modified APK reads a separate `GameOptions_debug.json` from the game's external files directory. The user downloads the game's cache first, then places the JSON under `Android/data/<game-folder>/files`. The published package is described as an APK around 69.9 MB plus a JSON archive around 7.6 KB; the extracted JSON was reposted as about 77.2 KB. The author is Urban GT19; the forum post is by ktoplay.
2. **Leia Loft Premium 1.2.1 / XaView** — a different, old offline/paid game build. The distribution notes describe ten graphics presets in the game folder, a DebugActivity/config-based debug activation path, and rootless access to app data through DocumentProvider or MT Manager. Another 4PDA post reports the graphics page could be opened from the in-game chat button, while other settings were unavailable. This is not the current online retail build.

No public GitHub source for either mod's APK patch was found. The evidence describes the finished behavior, file layout and settings; it does not reveal the exact bytecode/native patch used to add the reader or menu. Any claim that the exact implementation is known would be speculation.

## Version identity matters

| Artifact | Version / platform | What is evidenced | Compatibility status |
|---|---|---|---|
| Current retail Asphalt Legends | Android package `com.gameloft.android.ANMP.GloftA9HM`, listing version 53.5.3a | Current public release listing | Target for new research; no community-mod compatibility demonstrated |
| Urban GT19 external config | Gameloft Store 1.0.8 | Modified APK + external `GameOptions_debug.json` | Legacy only; do not copy its APK/config blindly to 53.5.3a |
| Leia Loft Premium | 1.2.1, offline/paid local build | DebugActivity/config route, 10 graphics presets, graphics-only settings menu | Separate legacy build; not a current online-game adapter |
| Asphalt 9 Demo | Historical 1.1.0e | Old profile archives with several graphics tiers | Historical reference only |

The current 53.5.3a listing was visible on public Android app trackers as updated 2026-10-16. The Android package name is stable in those listings. The game is distributed as a large app plus downloaded game data; that is not available as an official source-code repository on GitHub.

## How Urban GT19's method appears to work

The available posts state:

- The mod author “integrated an external config”.
- The file is named `GameOptions_debug.json`.
- The file is placed under `Android/data/<game-folder>/files` after the game cache is downloaded.
- Hardware/SoC identification appears in the config: the post recommends replacing a nearby processor identifier (for example, 725) to make the preset selector choose maximum graphics.

The safe technical interpretation is:

1. The **APK has to contain a reader or resolver** for the external file. Android does not make the game consume an arbitrary JSON just because it is copied into its folder.
2. The reader likely runs before/while the game's GameOptions system constructs its graphics settings, then merges the external values with defaults/device capability selection. The public sources do not reveal whether this was implemented in Java/Kotlin, native C/C++, or a patched asset-loading path.
3. The SoC identifier change most likely manipulates the game's device-tier selection / whitelist path. It is not a new upscaling algorithm and it does not prove the GPU can safely render every maximum setting.
4. The game cache must already exist because the external config location is within the created game files tree.
5. A robust implementation must preserve unknown keys and the game's own generated defaults. The example adapter in this branch only updates a small allowlist of known historical keys already present in the source JSON.

## How Leia Loft / XaView differs

The Leia Loft 1.2.1 release notes describe a packaged offline APK with:

- A maximum-graphics configuration tuned by KTOPlay.
- Ten graphics config files selectable from the game folder.
- A DebugActivity route and a config flag to activate debugging without Root.
- A rootless DocumentProvider / MT Manager route for its app data.
- A modified offline/local game release.

A separate forum post says its added in-game graphics settings were opened via the chat button and that graphics could be configured there, but other settings were not exposed. This suggests the author modified the APK's activity/UI wiring as well as configuration handling. The APK patch itself is not public, so the exact activity registration, menu callback and internal settings object are unknown.

This is a closed/legacy example and should be treated as behavioral research, not a reusable library.

## Historical GameOptions fields found in community documentation

These names are historical candidates only. They are not certified for 53.5.3a.

| Key / key family | Reported purpose | Candidate use |
|---|---|---|
| `render_scene3DResolutionScale` | 3D scene render scale; documented as fractional scale | Render Scale candidate |
| `render_scene2DResolutionScale` | 2D/UI render scale | Keep 100% unless explicitly testing UI resolution |
| `models_carHighestQualityLODIngame` | In-game car model LOD, numeric range 0–7 | Separate car detail selector |
| `models_carHighestQualityLODMenu` | Menu/garage car model LOD | Separate garage detail selector |
| `render_focusedVehicleShadowQuality` | Focused vehicle shadow quality; documented 0 off / 1 low / 2 medium / 3 high | Car shadow quality |
| `textures_highestAnisotropy`, `gpu_texturesAnisotropyCapacity` | Texture anisotropy | Texture filtering |
| `postFX_useSSRInGameplay` | Screen-space reflections | Optional post effect |
| `postFX_useDOFInGameplay` | Depth of field | Optional post effect |
| bloom / lens flare / film grain / motion blur keys | Post effects | Optional post effects |
| `postFX_numAASamples` | MSAA sample count | Anti-aliasing |
| `textures_streamerBudgetMB` | Texture-streaming memory budget | Memory-pressure candidate |
| `textures_streamerHighestQualityMipmap`, `textures_bestMipmapLevel`, `gpu_mipmapLevelCapacity` | Mipmap limits / streaming quality | Texture quality |

Community docs also mention DPI/render limits and particle/breakable distances. They are omitted from the initial mapper until the exact key/value semantics are verified against an actual stock config. Historical forum posts describe configs being regenerated or moved into private app storage on some older releases; rootless external-file placement is therefore not a universal behavior.

## Why JSON cannot host all UltraScale techniques

GameOptions are game-owned quality choices. They can select render scale or car/texture/shadow quality when the engine already supports those options. They cannot by themselves implement:

- SGSR1, SGSR2, MWSR, FSR Lite or CAS shader passes.
- Render-target capture and replacement at the correct point in the frame.
- Motion-vector/depth acquisition, camera jitter/reprojection and temporal history for SGSR2.
- Optical-flow frame synthesis (Pyramidal Lucas–Kanade), cadence scheduling, and x2/x3/x4 presentation.
- A low-latency frame queue or input-to-present changes.
- A reliable CPU/GPU/1% low/frame-time overlay.

Those require an engine/render integration or an authorized graphics-layer route that actually owns the appropriate target and presentation lifecycle. A profile value alone is not an active implementation.

## Android / official integration context

Gameloft has used Android Game Mode API mapping for game quality/performance behavior. Android exposes performance, battery, standard and unsupported modes to a game. In Gameloft's description, battery mode may reduce environmental detail, reflections, depth-of-field, motion-blur/weather shader complexity and frame-rate cap; performance mode selects quality based on device and thermal behavior. This is a **game-vendor integration**, not an external arbitrary shader injector.

## Recommended UltraScale integration plan

### Stage A — read-only exact-build fingerprint
- Obtain the installed official split APK set using an authorized user export path.
- Record package name, version name/code, signing certificate digest, target SDK, ABI set and split layout.
- Inspect accessible manifest/DEX/native-library metadata only; do not extract protected private data.
- Verify actual GLES/Vulkan API use with an authorized debuggable capture or vendor diagnostics. A packaged `libEGL` or `libvulkan` dependency alone is only a hint.

### Stage B — establish the real configuration contract
- Compare a stock config before and after each in-game graphics setting changes.
- Re-launch and re-check location to identify whether the game regenerates or copies the config.
- Change one known option at a time and verify actual scene output (not only the UI).
- Record supported keys and legal value ranges for the exact build.
- Preserve unrelated keys; do not replace the entire config with a historical file.

### Stage C — implement a safe menu/config adapter
- Add a game/version-scoped adapter.
- Expose existing GameOptions-backed controls (3D scale, vehicle LOD, car shadow quality) only when the exact key and behavior are verified.
- Keep the complete UltraScale settings in a separate namespaced profile.
- Mark shader, latency and frame-generation features unavailable until the renderer reports the needed capabilities.
- Provide rollback and retain the original settings snapshot.

### Stage D — renderer integration
- Locate the actual 3D scene render target and its post-processing chain.
- Integrate one spatial upscaler first (Bilinear/Nearest baseline, then FSR Lite, MWSR, SGSR1) and measure GPU cost and image quality.
- Add CAS as a separate pass.
- SGSR2 requires temporal inputs and proper ping-pong history; it is not enabled from a standalone spatial shader.
- Frame Generation needs motion vectors/optical flow, disocclusion handling, UI composition policy, and presentation pacing. Do not expose working x2/x3/x4 until those are implemented and measured.
- Low Latency and Frame Pacing require ownership of queue/present synchronization; changing a JSON integer is not sufficient.

### Stage E — acceptance
- Verify attachment to the correct process/build.
- Verify that a selected scaler changes a game-owned render target.
- Compare screenshots and text/UI integrity at each scale.
- Measure median and p95/p99 frame time, dropped frames, GPU time, thermals and memory.
- Measure input-to-present before/after for latency claims.
- Test rollback, relaunch and repeated cache/download cycle.

## References

- 4PDA: Asphalt Legends / external config mod, Urban GT19, version 1.0.8 (Gameloft Store): https://4pda.to/forum/index.php?showtopic=901833&st=20880
- Telegram repost by AGMOD with APK/config sizes and external-config note: https://t.me/s/andoridgames66?before=7423
- Telegram/TECH J5 Games note for Leia Loft 1.2.1 and its DebugActivity / 10 configs: https://t.me/s/techj5games?before=8928
- 4PDA historical notes about graphics-only menu opened through chat button and config lifecycle: https://4pda.to/forum/index.php?showtopic=901833&st=20860
- Android Developers / Gameloft Android Game Mode API case study: https://developer.android.com/games/optimize/adpf/gamemode
- Current Android release listing (package/version): https://www.appbrain.com/app/Asphalt%20Legends%20-%20Racing%20Game/com.gameloft.android.ANMP.GloftA9HM

## Confidence summary

- High: Urban GT19's published 1.0.8 package uses an external JSON config file placed in game files after cache download.
- High: Leia Loft Premium 1.2.1 is a separate old offline build with DebugActivity/config and ten graphics presets.
- Medium: historical GameOptions key names and their described intent; actual values should be checked against the specific config version.
- Low / unverified: exact APK patch mechanism, exact menu code and compatibility with modern 53.5.3a.
- Not implemented: runtime adapter for current 53.5.3a, shader interception into the retail game, real frame generation or low-latency queue control.
