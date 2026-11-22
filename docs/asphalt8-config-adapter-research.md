# Asphalt 8 configuration adapter research

## Concrete discovery: game-owned graphics configuration

Community reports document Asphalt 8 builds loading graphics options from game-owned external configuration files under the app-specific external files directory:

`Android/data/com.gameloft.android.ANMP.GloftA8HM/files/`

Observed names vary by game generation/build:
- `gameoptions.json`
- `gamesettings.json`
- older builds/reports: `GameOptions.jpk` or `GameOptions.json`
- historical external-config patches used `gameprofiles.txt` or an internal `GameOptions_saved.json` path.

Evidence:
- 4PDA discussion documents an external `GameOptions.jpk` / `GameOptions.json` config path and notes that an internal `GameOptions_saved.json` may take precedence: https://4pda.to/forum/index.php?showtopic=492661&st=46360
- A later 4PDA post documents copying `gameoptions.json` to the app-specific external files folder for a newer modded build: https://4pda.to/forum/index.php?showtopic=492661&st=62960
- A community report for Asphalt 8 v5.8.0k documents `gamesettings.json` in the same directory: https://www.reddit.com/r/Asphalt8/comments/m65puy/the_game_cant_load_higher_graphics_but_can_go/

These are community reports, not an official stable schema. The file name, schema, precedence and whether the current 9.1.0i build honors any external file must be verified against the exact installed build. A config from a different game generation must not be copied blindly.

## What this route can and cannot do

A game-owned configuration file can potentially control options the game already exposes internally, such as graphics tier, effects, frame-rate caps and quality presets, depending on the build's actual schema.

It does **not** by itself add new rendering algorithms. There is no evidence that Asphalt 8 understands UltraScale-specific keys for:
- Render Scale / off-screen scene capture
- FSR / other spatial upscalers
- AMD CAS sharpening
- UltraScale low-latency synchronization

Those require code running in the game's render process at the correct render target / frame boundary.

## Proposed safe adapter workflow

1. Read the installed package version and signing identity.
2. Back up any existing candidate config before writing.
3. Let the user export the current in-game config (or import a config extracted from that exact build).
4. Parse and validate JSON; preserve unknown keys and existing per-device/per-GPU sections.
5. Apply only fields confirmed in that exact version's schema. Never invent JSON keys.
6. Write through the user-selected Android Storage Access Framework folder tree; ask the user to select the game's own `Android/data/<package>/files` folder if Android permits it.
7. Keep restore/rollback and show exactly which file was changed.
8. Separately develop an in-process native render adapter for UltraScale Render Scale → Upscaling → CAS. Do not label config-file changes as a completed shader injection.

## Experimental game-native config deployment

The UltraScale Mobile card now has an opt-in Shizuku deployment path for the edited JSON:

- Target file: `/storage/emulated/0/Android/data/com.gameloft.android.ANMP.GloftA8HM/files/gameoptions.json`.
- It validates the expected `header` / `profiles.GPU` / `profiles.Specific` structure and caps the Binder payload at 512 KiB.
- It refuses to write while the game process is running, writes and syncs a temporary file, atomically replaces the target when supported, and verifies the final byte count.
- On the first apply, it preserves the original target in a timestamped backup, or records that no original file existed. Later applies keep that original baseline. Restore puts the original file back or removes the new file if none existed.
- The resolution slider edits only existing `Specific.overrides.RES_*` entries that already have `scaleDisplay`; it does not invent unknown fields.

**This is not an FSR/EASU/CAS injection.** It only writes the game's pre-existing `scaleDisplay` configuration. Community reports establish that similar config files have been placed in this folder by some Asphalt 8 builds, but the current 9.1.0i retail build's file precedence and the visual meaning of `scaleDisplay` have not been confirmed on-device. A successful file write is not proof the game accepted or applied it.

## Current decision

This is a promising additional route for Asphalt 8's **native game settings**, and is worth implementing as a profile/config adapter after the exact installed version's config is collected. It is not yet a proven injection path for UltraScale's custom shaders. For the previously inspected 9.1.0i build, do not assume the v5.x community `gamesettings.json` reports apply.
