# Asphalt 8 Android 9.2.0i — settings screen owner and virtual dispatch trace

## New confirmed chain

### 1. Owning object constructor and vtable
- Constructor at `0x1655c60` installs vptr `0x2bc1670` at the object base (`str x9, [x0]`).
- Relocation-backed entries for this vtable include `0x2bc1768 -> 0x1656634` and `0x2bc1870 -> 0x1656bc0`.
- The vtable entry at `0x2bc17e8` (address-point offset `+0x178`) resolves to `0x17662c0`.
- The method at `0x17662c0` receives an object and a name-like argument, calls helper `0x1762670` with constant `261`, then tail-dispatches through another virtual slot at `+0xb0`. Its exact source-level signature and semantic name remain unknown. The target appears in many vtables, so it is likely shared framework behavior rather than a unique settings hook.

### 2. Screen creation path
- Virtual method body at `0x1656bc0` references the exact identifier `options_menu/screen_options_sound_and_display_ads` (string VA `0x498891`).
- It builds a screen/resource descriptor around that identifier, calls into the resource/object path, and then updates a two-word shared-pointer-like member at offsets `+0x2b0` and `+0x2b8` on its receiver.
- Another routine in the same native UI area later loads the object pointer from `[x19 + 0x2b0]`, constructs child names such as `visual_quality_container_android`, and invokes the virtual slot at `+0x178` on that loaded object.

Taken together, this links the named Sound & Display screen resource to a retained runtime screen object, and links later UI setup to name-based virtual dispatch on a screen/container object. This is a substantially better-supported chain than matching an unrelated `+0x2b0` field in another class.

## What is not yet proven
- The exact dynamic type/vptr of the retained object at `[x19 + 0x2b0]` at the point of the child lookup is not yet resolved. The owner class vtable is known, but the field stores a separate object.
- The exact semantics of virtual slot `+0x178` are inferred from its arguments and use; its implementation and downstream `+0xb0` dispatch still need full type recovery.
- No safe public or third-party UI registration API has been found. A named lookup call is not itself an insertion API.
- No APK/resource mutation or runtime injection has been performed.

## Next trace target
Follow the return/out-object produced by the resource path in `0x1656bc0` (including the helper call around `0x208caec`) to determine the retained object's actual vptr. Then map that vptr's `+0x178` slot and trace whether its `+0xb0` dispatch performs a pure lookup or can register new children. Until that is established, do not patch BGIN or the commercial APK.