# Asphalt 8 9.2.0i — Galaxy M14 4G external graphics profile candidate (corrected)

Target: package `com.gameloft.android.ANMP.GloftA8HM`, version 9.2.0i, Samsung Galaxy M14 4G model `SM-M145F`, 4 GB RAM variant.

## Hardware correction

The SM-M145F 4G model uses Qualcomm Snapdragon 680 and Adreno 610, not Mali-G68. The earlier candidate that added a Mali-G68 matcher was incorrect and should not be used.

## Corrected config

Starting from the exact `gameoptions.json` extracted from the acquired 9.2.0i package (source SHA-256 `703d9d62291822058157dd627e151ef589d978c0434acba4d67d1a209bea18e1`), the corrected external candidate adds only the exact model matcher `MODEL$$SM-M145F` to the existing `GPU_5` matcher list. It preserves the existing `GPU$$Adreno` + `GPU$$610` matcher and all original profile overrides. The pre-existing `GPU_5` override requests `GFXOption=HIGH` and `FPSLimit=60.0`; this is a configured cap, not a promise of sustained 60 FPS.

Corrected candidate SHA-256: `952f6e1e0fde2dc999a450f071b6b01511a8c48d92c781563d48769c58b8d854`.

## External-file route and limits

Community instructions have shown placing `gameoptions.json` under:

`Android/data/com.gameloft.android.ANMP.GloftA8HM/files/`

That report is from an older build and does not prove that 9.2.0i reads the external file on every Android version/device. The corrected file is a sidecar test candidate, not an APK patch and not confirmed runtime injection.

This does not add UltraScale menu controls, FSR/EASU, CAS, SGSR, MWSR, or frame generation. No APK bytes, signing block, native library, BGIN resource, saved game data, or anti-cheat component were changed. No runtime test was possible on the user's phone.

Back up any existing external config before testing and restore it if the game ignores the file or becomes unstable.
