# Asphalt 8 Android 9.2.0i — resource resolver trace around 0x208CAEC

## Confirmed from the ARM64 disassembly

The previously identified helper at `0x208CAEC` is not a virtual UI-registration method.

- It reads a begin/end pair from `[x0 + 0x178]` and iterates the pointed-to entries. This is an ordinary data member access, not a vtable dispatch.
- It compares identifier-like vectors from those entries against the input vector passed in `x1`. The compared values are reached through the pointed-to objects' `+0x8` members.
- On a matching entry, it inspects another vector at `[entry + 0x48]`, performs a further integer-key comparison, and calls `0x20773DC` to resolve/build the matching typed resource object.
- The result is written through the out pointer in `x8`; the caller at `0x1656C40` reads the returned shared-pointer-like pair from stack offsets `+0x3A8/+0x3B0`.
- The caller then retains the returned object in its own `+0x2B0/+0x2B8` shared-pointer-like field.

The helper at `0x208C244`, called during the vector matching path, constructs a slash-separated string from path components. That is consistent with resource/path resolution, not insertion of a new UI child.

## What this establishes

The chain from the Sound & Display screen identifier into the retained object is a resource-resolution path: path construction, matching against registered resource entries, type-specific resolution, then retention by the screen owner.

The call at `0x20773DC` is a type-dispatch/factory-like routine: it asks the source object for a type identifier through virtual slot `+0x60`, then switches over that identifier to select a handler. Its broad switch and multiple callers indicate a shared typed-resource dispatch path, not a dedicated settings-screen extension point.

## Remaining unknowns

- The exact concrete C++ class and vptr of the retained object at the later child-lookup site are still not proven.
- This trace does not establish an API for adding arbitrary children to the Sound & Display screen.
- No BGIN file, APK, or runtime process was modified. No injection was attempted.

## Next useful target

Trace the concrete resource type returned by the matching branch of `0x20773DC` for the Sound & Display screen, then follow the later `+0x178` child-name lookup to its concrete implementation. Only a proven insertion/registration call on that screen's actual child container would justify designing an adapter around it.
