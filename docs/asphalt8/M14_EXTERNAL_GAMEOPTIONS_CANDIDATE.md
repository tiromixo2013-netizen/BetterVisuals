# Asphalt 8 9.2.0i — Galaxy M14 external graphics profile candidate

Target: package `com.gameloft.android.ANMP.GloftA8HM`, version 9.2.0i, Samsung model `SM-M145F`.

## Change

Starting from the exact `assets/releaseminigame/gameoptions.json` extracted from the acquired 9.2.0i package, the candidate adds two device matchers to the existing `GPU_5` selection list:

- `MODEL$$SM-M145F`
- `GPU$$Mali` AND `GPU$$G68`

The existing `GPU_5` override in this source already requests `GFXOption=HIGH` and `FPSLimit=60.0`. No new engine keys or unsupported rendering effects were added. All other profile definitions are preserved.

## External-file route

Community reports describe placing a `gameoptions.json` file in:

`Android/data/com.gameloft.android.ANMP.GloftA8HM/files/`

This route has not been verified against the acquired 9.2.0i build or on an SM-M145F device. The native package contains both asset and saved-config identifiers; downloaded/private saved settings may take precedence. Treat the external file as a test candidate, not a confirmed injection mechanism.

## Limits

This only attempts to select an existing graphics preset. It does not add UltraScale menu controls, FSR/EASU, CAS, SGSR, MWSR, or frame generation. No APK, native library, BGIN resource, signature, save data, or anti-cheat component was changed. No runtime test was possible in this environment.

The JSON candidate is provided as a separate artifact for manual testing. Back up any existing config and revert the file if the game ignores it, resets settings, overheats, or becomes unstable.
