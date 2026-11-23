# Asphalt 8 native UI lookup — follow-up trace

## Additional call-site comparison
Static disassembly of `libAsphalt8.so` at `0x1658130` constructs the string `speed_unit_container` and invokes the same virtual slot at vtable byte offset `0x178` on the object loaded from `[x19 + 0x2b0]`. The call passes the constructed string as its first explicit argument and an address of a stack output slot in `x8`; the returned object is then null-checked and queried through its own first vtable entry.

The same call pattern is used for `visual_quality_container`, `visual_quality_container_android`, and `profile_quality_label`. This repeated pattern supports the interpretation that slot `0x178` performs a named-child/object lookup on a UI manager or container-like object, returning a reference-counted object handle. It is still a behavioral inference from call-site data flow, not a recovered C++ declaration.

## Important distinction
The subsequent calls through offsets `0xc8` and `0x148` act on the retrieved objects and appear to configure/resolve them; they are not evidence of a general-purpose registration API. No call site found in this trace passes a newly constructed UI object into the manager at slot `0x178`.

## Current safe conclusion
- We can identify a recurring named-object lookup operation and the manager field used by this setup routine (`[x19 + 0x2b0]`).
- The exact class, method signature, and implementation address behind slot `0x178` remain unresolved.
- No safe extension/registration API has been demonstrated.
- The next static-analysis target is the construction and initialization of the object stored at `[x19 + 0x2b0]`, plus the vtable address installed on it. That may identify the owning UI manager and its supported lifecycle.
- No game files were changed and no runtime injection was performed.