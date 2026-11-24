# Asphalt 8 9.2.0i — untouched package baseline analysis

This branch is an isolated reference track. Do not merge its APK/package contents into the UltraScale modified branch.

## Reference input
- Package: `com.gameloft.android.ANMP.GloftA8HM`
- Version: `9.2.0i` (`version_code=92009`)
- Acquisition: existing GitHub Actions artifact `asphalt8-original-apkpure`, workflow run `45261333997`, artifact `14860272493`.
- Important provenance limit: this is the untouched APKPure-distributed XAPK captured by the project, **not a Play Store-downloaded / Play-signed APK**. It is a useful unmodified baseline, not proof of the exact Play Store delivery bundle.

## XAPK structure
- `com.gameloft.android.ANMP.GloftA8HM.apk` — 148,185,656 bytes
- `config.arm64_v8a.apk` — 27,240,407 bytes
- `config.hdpi.apk` — 142,162 bytes
- `manifest.json` — package/version/split metadata
- No standalone asset-pack archives or OBB files are included in this XAPK.

The absence of fast-follow/on-demand archives in this installer XAPK is not itself evidence of a broken package: Google Play Asset Delivery serves those packs separately after install and expands them into app-private storage.

## Asset Delivery evidence
The untouched base APK contains:
- `asset-delivery.properties`:
  - `version=2.1.0`
  - `client=asset-delivery`
  - `asset-delivery_client=2.1.0`
- DEX references to Google Play Core Asset Delivery classes, including:
  - `AssetPackManager`
  - `AssetPackManagerFactory`
  - `AssetPackStateUpdateListener`
  - `AssetPackState`
  - `AssetPackLocation`
  - `AssetPackStatus`
  - `AssetPackException`

This proves the original package embeds the Play Asset Delivery SDK/API classes. It does **not yet prove** which game code path invokes them, which pack names are requested, whether the game also retains a custom Gameloft CDN downloader, or whether the current 0% screen is waiting on PAD versus a separate game service. Those require call-site tracing and runtime state/log inspection.

Gameloft's Android Developers case study independently says Asphalt 8 moved from its earlier in-house secondary-download system to Play Asset Delivery Fast-Follow. Keep both the PAD path and any legacy/custom game download path in the investigation.

## Comparison against current UltraScale test package
Compared base APK DEX SHA-256 values between this untouched XAPK and the built UltraScale test XAPK (workflow run `45468792156`, artifact `14956806526`):
- All base APK DEX files `classes.dex` through `classes11.dex` match byte-for-byte.
- Both `assets/audience_network/classes.dex` and `classes2.dex` match byte-for-byte.
- `asset-delivery.properties` matches byte-for-byte.

Therefore the current UltraScale packaging workflow did not strip or rewrite the Play Asset Delivery SDK or Java/Kotlin DEX code. It adds the native GLES layer, changes the manifest to enable graphics layer injection, edits selected game-options values, and re-signs the APK/splits with the test key.

## Current diagnosis
1. There is already a Play Asset Delivery SDK in the unmodified base APK; blindly adding a second downloader library is not justified.
2. The current modified build's test signature is a credible compatibility risk: Google's PAD documentation says an app not recognized as Play-installed can enter `REQUIRES_USER_CONFIRMATION`; the confirmation flow can trigger a Play update/restart. This is a strong candidate to test, not a proven cause.
3. The user has also reported the original game exhibiting a 0% additional-content download in a separate test, so the signature cannot yet be treated as the sole explanation.
4. No extra PAD archive was present in either XAPK. That is consistent with Fast-Follow delivery, which is served separately by Play.

## Next analysis steps (keep isolated)
- Use jadx/decompiler on the untouched base APK to find game-owned call sites for `AssetPackManagerFactory.create`, `fetch` / native `AssetPackManager_requestDownload`, listener registration, pack-name constants, and handling for `REQUIRES_USER_CONFIRMATION`, `WAITING_FOR_WIFI`, `FAILED`, and `COMPLETED`.
- Trace the 0% UI to determine whether it is PAD progress or the legacy/custom downloader.
- Compare the exact Play-delivered package/signature and pack metadata if a genuine Play acquisition becomes available; do not label the APKPure baseline as Play-original.
- Do not add a second downloader until the call graph identifies a missing or broken request path.
- Keep Frame Generation and SGSR2 work on the modified branch and do not mix it into this reference branch. Their implementation remains separate from the asset-delivery diagnosis.
