# Asphalt 8 9.2.0i — original XAPK vs UltraScale XAPK comparison

Date: 2026-11-24
Package: `com.gameloft.android.ANMP.GloftA8HM`
Version: `9.2.0i` / versionCode `92009`

## Critical source limitation

The so-called original reference XAPK is an APKPure-acquired XAPK, not a Google Play-delivered install or an authenticated Play-generated APK set. Therefore this is a valid binary comparison between the two available XAPKs, but it is **not** a comparison against the actual Play Store delivery artifact.

## Artifacts inspected

- Reference outer ZIP: `asphalt8-original-reference.zip`, SHA-256 `0a6590f2aff24d2f20d13d375fb7db90906858958742de960fd84b08b85a978f`.
- Modified outer ZIP: `ultrascale-modified-package.zip`, SHA-256 `222790c3dce0dc211953eadb0c8c27194946314bb8e063d8ba874666259c7fb0`.
- Both XAPKs declare the same package, versionCode 92009, versionName 9.2.0i, min SDK 24, target SDK 36, and the same three split roles: base, config.arm64_v8a, config.hdpi.
- Neither XAPK includes an external PAD Fast-Follow/On-Demand asset pack archive.

## High-confidence comparison findings

1. **All 11 base-APK DEX files are byte-for-byte identical.** SHA-256 of every `classes*.dex` file matches between the two APKs. This includes the game's Java/Kotlin code and the bundled Play Core Asset Delivery integration. The modified build did not delete or rewrite the game's PAD Java/Kotlin downloader.
2. The `asset-delivery.properties` file is byte-identical in both APKs:
   ```
   version=2.1.0
   client=asset-delivery
   asset-delivery_client=2.1.0
   ```
3. Both DEX sets contain the same PAD symbols/messages, including `AssetPackManager`, `getPackStates`, `getPackLocation`, `AssetPackState`, `AssetPackStatus`, `assetPackManager.getPackStates failed`, and `Pack not found with pack name`. The literal `requestFetch` does not occur; the public Java API commonly uses `fetch(List<String>)` in this SDK generation.
4. Both configuration split APKs have identical payload entries; only APK signing metadata differs.
5. The base APK manifest differs in three meaningful places:
   - Modified build adds `android:isSplitRequired=true`.
   - One deep-link `android:scheme` value is emitted as the literal `discord-778212061490053120` instead of remaining a resource reference; the resolved value is the same.
   - Modified build adds `com.android.graphics.injectLayers.enable=true` to enable Android graphics debug-layer loading.
6. The modified base APK adds `lib/arm64-v8a/libGLES_ultrascale.so`; the game DEX files are unchanged.
7. The only semantic edits to `assets/releaseminigame/gameoptions.json` are `GPU_5/FPSLimit: 60 -> 240` and the existing `Specific/*/scaleDisplay` values changed to 70. All other parsed JSON values are identical. These are graphics/performance settings, not PAD downloader code.

## What this means

There is no evidence that UltraScale removed or changed the Java/Kotlin PAD downloader. Adding another PAD SDK copy would duplicate an integration already present and is not a justified repair.

The strongest explanation for a Play-delivery pack staying at 0% is installation provenance/signing: Google defines `APP_NOT_OWNED (-13)` for apps not installed through Play and `UNRECOGNIZED_INSTALLATION (-15)` for an installation/version Play does not recognize. Both available XAPKs are sideloaded APKPure/test-signed packages, so neither proves how the real Play-installed build behaves. This is a strong, evidence-supported hypothesis, not a confirmed device error until the runtime PAD error code is captured.

The extra `isSplitRequired` manifest attribute is a packaging difference worth tracking, but it is not by itself proof of a PAD failure. The game launches with its supplied ABI/density splits, and the PAD DEX/API code is unchanged. The graphics debug-layer metadata is unrelated to the Java PAD calls; disabling it can be an isolation experiment, not a proven fix.

## Required next test (without uninstalling or clearing game data)

Capture device logcat while opening the in-game download screen. Look for:
- `AssetPack`
- `PlayCore`
- `AssetPackService`
- `GloftA8HM`
- `APP_NOT_OWNED`
- `UNRECOGNIZED_INSTALLATION`
- `PACK_UNAVAILABLE`
- `NETWORK_ERROR`

Record pack name, status, and errorCode. A state stuck at `PENDING` or `WAITING_FOR_WIFI` has a different remedy from `FAILED/-13`, `FAILED/-15`, or `FAILED/-2`.

Do not call `showConfirmationDialog` automatically: Google's API docs warn that for a non-Play-installed app it may trigger a Play update, restart the app and cancel the asset requests. This user wants to preserve the modified installation.

## Official references

- Gameloft's documented Asphalt 8 migration from its proprietary CDN to Play Asset Delivery Fast-Follow: https://developer.android.com/stories/games/gameloft-pad
- PAD Java integration, `fetch()`, listeners, statuses and errors: https://developer.android.com/guide/playcore/asset-delivery/integrate-java
- AssetPackErrorCode definitions: https://developer.android.com/reference/com/google/android/play/core/assetpacks/model/AssetPackErrorCode
