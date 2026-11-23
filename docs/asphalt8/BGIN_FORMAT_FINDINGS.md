# Asphalt 8 Android 9.2.0i — BGIN structural findings

## Scope
Read-only examination of the 13 `.bgin` resources in the official Android 9.2.0i package. No game asset or APK was modified. The parser validates the header, hash/offset index, and common indexed-entity envelope. Unknown index tails and unindexed byte regions remain opaque.

## Confirmed layout
All 13 resources share:
1. `u16 marker = 0x1AAA`
2. `u16 version = 12`
3. `u32 constant = 0x9456A530`
4. `u16 resource_name_length` + UTF-8 resource name
5. `u32 index_size`
6. Index region
7. `u16 record_count`
8. Resource records / other Clara data

The index begins with `u16 bucket_count`, followed by `bucket_count` entries of `u32 name_hash` and `u32 absolute_file_offset`.

Each indexed entry points to a common record envelope:
- `u32 class_hash`
- `u32 body_size`
- body starts with `u16 object_name_length` + UTF-8 object name
- remaining body is opaque payload

The index can contain an opaque tail. The total record count can exceed the indexed entity count, so this is not a complete decoder for all Clara record kinds.

## Validation across this build

All 13 resources passed header and indexed-record bounds checks. Indexed records had valid names and body ranges, with no overlaps.

| Resource | Indexed entities | Total record count | Opaque unindexed bytes |
|---|---:|---:|---:|
| controllers_menu | 44 | 44 | 0 |
| debug_menu | 3 | 3 | 0 |
| download_minigame | 9 | 10 | 143 |
| end_race_screens | 93 | 105 | 5,103 |
| generic_templates | 17 | 17 | 0 |
| ingame_menu | 91 | 98 | 1,480 |
| medals_templates | 139 | 139 | 0 |
| popups | 357 | 359 | 493 |
| scheme_controller_specific | 9 | 9 | 0 |
| sequences | 0 | 84 | 25,021 |
| splash_menu | 6 | 7 | 269 |
| sprites | 27 | 27 | 0 |
| widgets | 401 | 401 | 0 |

## Implication for UltraScale settings

`ingame_menu.bgin` contains 91 indexed objects and seven additional unindexed records/regions. Its indexed names include `boss_icon_1`, `boss_icon_2`, `box_1`, and in-game controls/templates. It does **not** contain the native settings identifiers `screen_options_sound_and_display_ads`, `visual_quality_container_android`, `profile_quality_label`, or `quality_slider`.

A second scan of **every asset entry** in the main APK also found none of those four identifiers. They are referenced by executable native code in `libAsphalt8.so`. In the relevant ARM64 code region, the native routine materializes `visual_quality_container` and `visual_quality_container_android` strings and passes them through the same virtual lookup path used for named UI objects. This supports a native/runtime lookup path rather than a ready-made BGIN node that can simply be extended.

Therefore appending guessed BGIN objects is not a justified way to add UltraScale controls. The remaining route is to understand the native screen construction/lookup contract and determine whether the app exposes a legitimate extension point. No supported third-party registration API has been established.

## Limitations
- The parser is structural and read-only; it is not a BGIN editor or full writer.
- It does not decode opaque payloads or non-indexed records.
- Validation does not prove a modified resource would be accepted by the game.
- No menu injection, APK patch, or in-game rendering change has been performed.

Tool: `tools/asphalt8/bgin_structural_parser.py`
