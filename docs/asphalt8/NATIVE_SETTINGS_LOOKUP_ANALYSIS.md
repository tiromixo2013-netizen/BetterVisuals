# Asphalt 8 Android 9.2.0i — native settings lookup analysis

## Scope
Read-only AArch64 disassembly of the acquired `libAsphalt8.so` from Asphalt 8 Android 9.2.0i. No APK or game resource was modified.

## Confirmed string references

| Identifier | String VA | Code reference |
|---|---:|---:|
| `visual_quality_container` | `0x4469bd` | `0x165944c` |
| `visual_quality_container_android` | `0x415d4e` | `0x1659578` |
| `profile_quality_label` | `0x4988c3` | `0x165966c` |
| `quality_slider` | `0x4988d9` | `0x16597a0` |

Other related strings previously located include `options_menu/screen_options_sound_and_display_ads`, `highframerate_enabled`, `highframerate_disabled`, `GameOptionsManager`, `GameOptionsGPUProfile`, `GetDefaultGFXOption`, `SetGFXOptionIndex`, and `GetGFXOption`.

## What the disassembly shows
At `0x165944c` and `0x1659578`, the code materializes the generic and Android-specific container names as strings. It then calls a virtual method through the object vtable at byte offset `0x178`, passing the name and an output slot. The returned object is checked through its own virtual interface before being retained. The later references to `profile_quality_label` and `quality_slider` follow the same named-object lookup pattern. A separate virtual call at vtable offset `0xc8` and another at `0x148` occur in the same setup path.

This is strong evidence that the native code resolves already-registered/named UI objects from a UI manager/container. It is **not** evidence that the lookup method registers new objects, accepts external plugins, or exposes a safe extension API. The disassembly inspected so far does not establish an insertion/registration call for arbitrary UI nodes.

## Consequence for UltraScale
Do not append guessed objects to BGIN files or patch the commercial APK on this evidence. The shipped BGIN resources do not contain the four settings identifiers above, and the UI code resolves them through native runtime objects. A valid next step is to identify the concrete implementation behind vtable slot `0x178` and trace the creation/registration lifecycle of the Android visual-quality container. Only if a supported registration point is found should a non-invasive integration design be considered.

## Limitations
- Analysis is static and read-only; no runtime instrumentation or in-game verification was performed.
- The exact C++ class and semantic name of the virtual method at vtable offset `0x178` remain unconfirmed.
- No UltraScale setting has been injected into Asphalt 8.
- Frame Generation and unsupported upscalers remain unimplemented for this target.