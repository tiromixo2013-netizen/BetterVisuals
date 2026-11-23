# Asphalt 8 Android 9.2.0i — owner-field and vtable cross-check

## Verified call-site data flow
At `0x1658130`, the settings-related routine loads `x21 = *(x19 + 0x2b0)`. It then loads the object's vptr from `[x21]`, loads the function pointer at vtable byte offset `0x178`, and calls it with `x0=x21`, `x1` pointing to the temporary name string, and `x8` pointing to an output slot. This confirms the indirect dispatch shape and the receiver object.

## Cross-check of apparent field writers
Two other routines write to an object field at offset `0x2b0`, but they cannot safely be assumed to initialize the same C++ class merely because the offset matches:

- At `0xc12f88`, a 56-byte allocation is initialized by `0x1db541c`; that initializer writes ordinary fields at offsets `+0x8` and `+0x30` and zeroes a 32-byte region. It does not install a vptr. The subsequent `0x1db54c4` call is a separate wrapper/registration sequence. This is not sufficient evidence that this stored pointer is the polymorphic receiver used by the settings lookup.
- At `0xd24168–0xd24178`, a 0x128-byte allocation is initialized by `0xd5b01c` and stored at `[x19+0x2b0]`. That initializer installs vptr address `0x2b39370`. The relocation-backed function entries for that vtable do not include a slot at `0x2b394e8` (`vptr + 0x178`); therefore this candidate vtable does not explain the live call at offset `0x178` and must not be labeled as the settings manager.

## Updated conclusion
The exact object assigned to `[x19+0x2b0]` in the settings screen's owning class remains unidentified. Matching a member offset across unrelated classes is not enough to identify a constructor. The previous idea that one of the visible `+0x2b0` stores directly identifies the lookup manager is therefore unproven and has been rejected as a working identification.

## Next static-analysis step
Trace the owning class of the routine containing `0x1658130` from its constructor and lifecycle. Specifically, identify writes to its `+0x2b0` field within that class's construction path, then resolve the exact vptr installed on the object stored there. Use relocation-backed vtable entries to map slot `0x178` to its actual implementation. Until that chain is complete, no extension API or injection point is established.

## Safety and verification
All work remains read-only static analysis of the acquired shared library. No APK, BGIN resource, or installed game was modified. No UltraScale setting has been injected or tested in Asphalt 8.