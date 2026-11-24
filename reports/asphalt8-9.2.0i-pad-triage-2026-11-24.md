# Asphalt 8 9.2.0i — Play Asset Delivery triage

Date: 2026-11-24
Package: com.gameloft.android.ANMP.GloftA8HM
Acquisition source: APKPure XAPK workflow artifact (run 45261333997), **not a verified Google Play download**.

## Evidence inspected

- XAPK SHA-256: bcb114a9cb1df2a454a9caaaf7f67942fef3532c56e1b630bd3a3761cebee32f
- XAPK contains exactly:
  - com.gameloft.android.ANMP.GloftA8HM.apk
  - config.arm64_v8a.apk
  - config.hdpi.apk
  - manifest.json
- No separately delivered asset-pack APK/archive is embedded in this XAPK.
- Base APK contains `asset-delivery.properties`:
  ```
  version=2.1.0
  client=asset-delivery
  asset-delivery_client=2.1.0
  ```
- Base APK DEX string tables contain Play Core Asset Delivery classes and API references including `AssetPackManager`, `getPackStates`, `getPackLocation`, `AssetPackState`, and `AssetPackLocation`.
- DEX strings also include Asset Delivery failure messages such as `assetPackManager.getPackStates failed`, `Pack not found with pack name`, and `The requested pack is not available`.

## Interpretation (evidence-bounded)

The package is not missing the Play Asset Delivery client marker or the Play Core Asset Delivery API surface. Therefore, simply adding another copy of the PAD library is not a justified fix.

The APKPure XAPK is only the base and configuration splits; it does not include separately delivered Fast-Follow/On-Demand content. That is expected for Play-delivered packs and does not by itself prove a packaging defect.

The exact pack names, request call site, runtime status/error code, and whether the 0% progress UI is waiting on PAD or a separate Gameloft downloader are **not yet established**. DEX string presence is not a call-graph proof.

The modified build is re-signed with a temporary test key. A Play-served PAD request can depend on Play recognizing the installed package/version/signing identity and delivery provenance. This is a strong suspect for a sideloaded/re-signed build, not a confirmed diagnosis without device logs.

## Safe next diagnostic

Capture logcat while opening the game's download screen and waiting 30–60 seconds. Filter for:
- `AssetPack`
- `PlayCore`
- `assetpack`
- `GloftA8HM`
- `GooglePlay`
- `Download`

Record the first Asset Delivery error/status code and the pack name if logged. Do not clear Asphalt 8 app data or uninstall the modified build for this test.

## What this does not claim

- This is not a verified Google Play-signed APK.
- No Google Play-delivered asset pack was acquired.
- No missing downloader was patched.
- No SGSR2 temporal integration or real frame generation was added by this analysis.
## Official Play error codes relevant to this case

Google's Asset Delivery API defines:
- `APP_NOT_OWNED` (-13): the app is not owned on the device; Google defines ownership as having installed it through Play.
- `UNRECOGNIZED_INSTALLATION` (-15): the installed app version is not recognized by Play.
- `APP_UNAVAILABLE` (-1): the app/version is not published or the account lacks access.
- `PLAY_STORE_NOT_FOUND` (-11): Play Store is missing or not the official app.

The same API documents that a confirmation flow for an app not installed by Play may trigger an update to a valid Play-installed version and restart the app. That is **not** a safe action for this user because it could replace the modified build. Do not automatically launch this flow on the user's device.

Sources:
- https://developer.android.com/reference/com/google/android/play/core/assetpacks/model/AssetPackErrorCode
- https://developer.android.com/reference/com/google/android/play/core/assetpacks/AssetPackManager
