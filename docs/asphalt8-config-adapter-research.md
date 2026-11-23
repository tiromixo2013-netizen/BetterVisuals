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


## Installed-config-first safety update

The editor now offers **Load installed Asphalt 8 config (recommended)** through the Shizuku service before the APK-extraction fallback. The live read is permitted only while the game is stopped, is limited to 512 KiB, and validates the expected header plus GPU/Specific profile objects before exposing the JSON to the editor. This prevents treating an APK's embedded defaults as if they were the user's current settings.

Backup handling validates that a previously recorded original backup still exists before another deployment, and restore uses the exact backup named by the marker rather than silently selecting an unrelated newest backup. Legacy restores without a marker may still select the newest matching UltraScale backup.

The app UI remains separate from Asphalt 8's own UI. It does not inherit the game's selected language, and no new game-language strings or in-game menu entries are inserted. The app can be localized independently in a later UI-resource pass. The live file read and deploy are code paths only; no on-device result or game acceptance is asserted.


## Verified APK acquisition: Asphalt 8 Android 9.2.0i (2026-11-23)

The acquisition workflow completed successfully for package `com.gameloft.android.ANMP.GloftA8HM`:

- Version: `9.2.0i`, versionCode `92009`, target SDK 36, minimum SDK 24.
- XAPK manifest contains a base APK plus `config.arm64_v8a.apk` and `config.hdpi.apk` splits; no separate OBB entry was present in this XAPK manifest.
- Base APK SHA-256: `dcb8510e7395f5288eba912fd246a2f5c9bd567fccaf46a3cd7f230292dd9e14`.
- ARM64 split SHA-256: `75cb1436dd06324c666e310f4b62ec12f0181a5e20f09f825ee18b6d2d93f610`.
- The ARM64 split contains `lib/arm64-v8a/libAsphalt8.so` (46,725,320 bytes). The base APK has no `lib/` entries; native code is delivered by the ABI split.
- The inspected base APK signer SHA-256 is `2173a248e2b11f9760b9167f398b183c92816cf8fc74afd3c64b22eddf50481c` (Gameloft certificate). No APK modification or re-signing occurred during acquisition.

### Embedded game-options schema found

The base APK contains `assets/releaseminigame/gameoptions.json` (149,335 bytes), with header `file_version=2`, `format_version=1`, `library_version=1` and profiles:

- `ColorCorrection`
- `CPU`
- `GPU`
- `MEM`
- `Specific`

Confirmed existing GPU keys include `GFXOption`, `FPSLimit`, `useShadows`, `postFX`, `useMotionBlur`, `useRoadReflection`, `useAA` (in a GPU override), `numPostEffectAASamples`, `roadTextureAnisotropy`, and `carTextureAnisotropy`. The embedded GPU default sets `GFXOption=VERYLOW`, `FPSLimit=30.0`, and `defaultTextureFiltering=1` (bilinear).

The `Specific` profile contains `scaleDisplay` and `scaleAABuffer`. Existing `RES_*` overrides set `scaleDisplay` to values including 50, 55, 56, 60, 70, 80, 85, 90 and 100. The default is `1.0`; the meaning/units and whether this embedded `releaseminigame` config governs the full live racing renderer still require runtime verification. The adapter must preserve the exact existing structure and should not infer that `scaleDisplay=50` is definitely a 50% render target without an on-device test.

### Native-library corroboration and UI boundary

Strings extracted from the ARM64 `libAsphalt8.so` include `GameOptionsManager`, `GS_GameOptions`, `GameOptions.json`, `GameOptions_saved.json`, `GameOptions_debug.json`, `GFXOption`, `scaleDisplay`, `textureBudgetMB`, `uiTextureQuality`, and `useShadows`. This confirms that the shipped native engine contains a game-options manager and recognizes these configuration concepts. It does not by itself prove which on-disk file wins precedence in 9.2.0i, or prove that every embedded `releaseminigame` option is consumed by the main racing mode.

The resource inventory found 620 assets under `assets/releaseminigame/`, including `gameoptions.json`, `.bgin` menu resources, `.tga` UI textures, `.xml` post-processing/material definitions and `.pvs` track data. The inventory did not find a main-game XML/JSON settings-screen definition; Android resource XML hits were predominantly library layouts. Therefore, this acquisition identifies a native config surface and custom menu asset family, but does **not** yet establish a safe insertion point for new UltraScale controls inside the full game's settings screen.

### Integration decision after acquisition

1. Update the adapter's version-aware parser against this exact embedded schema and preserve unknown profile/selection/override data.
2. Do not inject new keys for FSR, CAS, frame generation or latency: no evidence shows that the native game-options manager understands those UltraScale algorithms.
3. Verify the current build's effective config path and precedence on a device before deploying any external config. Keep the existing backup/restore path.
4. Continue reverse-engineering the custom menu resource format and native menu registration before claiming that controls can be added to the in-game settings screen.
5. Keep the official APK untouched and signed by Gameloft; no modified/re-signed APK has been produced.

Acquisition artifact: GitHub Actions run `45087768296`, artifact `Asphalt8-acquired-source-and-inspection` (250,926,304 bytes; expires 2026-11-26 UTC). The artifact contains the XAPK, split APKs, manifest and read-only inspection reports.


### Additional native menu symbol discovery

A read-only `strings -a` pass over the ARM64 `libAsphalt8.so` also found native UI identifiers including:

- `options_menu/screen_options_ads`
- `options_menu/template_control`
- `options_menu/template_gc_additional_button`
- `ingame_menu/template_ingame_options_controls`
- `visual_quality_container_android` and `visual_quality_container`
- `$STR_MENU_SOUND_AND_DISPLAY_OPTIONS`, `$STR_MENU_OPTIONS_HELP`, `$STR_MENU_OPTIONS_LANGUAGES`
- `OPTIONS_CONTROLS_CUSTOM` and `GameOptionsOverrideSlider`

This is stronger evidence that the game has native options-menu templates and a visual-quality control path. However, these are string references in a stripped native library, not the corresponding layout/resource files or event-registration code. The acquired XAPK did not contain the full `options_menu/` resource tree; its 620 game-specific assets were under `assets/releaseminigame/`. The next useful source is the installed game's downloaded resource pack(s), especially the data associated with the `releaseminigame.jpk` reference, or a complete installed-data export. Until those resources are available and mapped to the menu state machine, inserting a new in-game UltraScale page remains unproven.
