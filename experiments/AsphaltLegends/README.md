# UltraScale — Asphalt Legends (Android) research branch

This folder is deliberately separate from the Asphalt 8 and Geometry Dash work.

## What this branch is

A version-scoped research and adapter scaffold for Asphalt Legends Android. It records how historical community mods exposed graphics settings, provides a conservative mapper for the legacy external-config format, and defines the full UltraScale sidecar profile.

It is **not** a modified Asphalt Legends APK, an APK downloader, or proof of runtime injection into the current retail game.

## Target and compatibility

- Current retail target researched: package `com.gameloft.android.ANMP.GloftA9HM`, version `53.5.3a` (verified 2026-11-25 from public release listings).
- Legacy Urban GT19 external-config target: Gameloft Store `1.0.8`.
- Leia Loft Premium `1.2.1` is a separate, old offline build and is not a compatibility base for current online Asphalt Legends.
- No official Asphalt Legends source repository or source APK was found on GitHub. A source-code repository is not the same as a game APK.

## Files

- `MODDING_METHOD_RESEARCH.md`: source-backed research on Urban GT19, Leia Loft, and historical GameOptions keys.
- `profiles/ultrascale-asphalt-legends.schema.json`: the agreed unified UltraScale profile; shader/FG options are marked as requiring a renderer adapter.
- `profiles/legacy-1.0.8-gameoptions.example.json`: a small example of historically documented GameOptions fields, not a replacement for the game's complete config.
- `src/LegacyGameOptionsAdapter.kt`: a version-gated mapper. It only changes known keys already present in an existing legacy config and refuses to run unless an external-config reader has been verified.

## Integration boundary

The Urban GT19 mod appears to have changed the APK so it reads an external `GameOptions_debug.json`; users then placed that file in the game files directory after cache download. The public posts show the behavior and file placement, but not the APK patch source or exact implementation. Therefore we do not pretend the JSON file alone installs a reader.

The historical JSON controls native game options such as 3D scene render scale, UI render scale, car LOD, shadows and texture filtering. It does **not** provide arbitrary shader injection, motion vectors, a frame-history buffer, or presentation-queue control.

The full UltraScale menu and shader pipeline therefore require an authorized in-game adapter / game-owned rendering hook. Current retail 53.5.3a is not marked supported until its exact APK/split set and runtime rendering path can be inspected with authorization.

## What is and is not active

| Feature | Legacy GameOptions mapping | Needs renderer / presentation adapter |
|---|---:|---:|
| 3D Render Scale | Candidate, only for existing legacy key | Also required to guarantee the live target is the 3D scene |
| UI 2D resolution scale | Candidate, separate key | Keep at 100% by default |
| Car model LOD | Candidate, only if key exists | No shader hook |
| Car shadow quality | Candidate, only if key exists | No shader hook |
| SGSR1 / SGSR2 / MWSR / FSR Lite | No | Yes |
| Nearest / Bilinear | Not an arbitrary GameOptions flag | Yes, unless engine already exposes a matching scaler |
| AMD CAS | No | Yes |
| Low Latency / Frame Pacing | No | Yes, needs queue/presentation control |
| Frame Generation x2/x3/x4 | No | Yes, needs motion data/history and controlled presentation |
| FPS / 1% low / frame time / CPU/GPU HUD | No | Yes, needs trusted telemetry hooks |

## Build and verification

No Gradle build or device validation is claimed by this research-only commit. The mapper is intentionally not wired into the main app's APK patch flow. That requires a verified version-specific integration and a deliberate UI path first.
