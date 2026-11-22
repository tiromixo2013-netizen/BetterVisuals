# UltraScale Mobile APK Integration Pipeline

## Scope

This pipeline is an opt-in, user-authorized packaging workflow for APKs the user owns or is permitted to modify. It is not a universal runtime injector. Adding files to an APK does not make a game load them; a game-specific integration adapter must call the UltraScale renderer from the game's real graphics path.

## Stages

1. **Acquire**: accept a user-selected APK through Storage Access Framework. For split installs, collect the complete APK set (base + every config/feature split) and retain package/version metadata.
2. **Preflight**: verify ZIP readability, binary AndroidManifest presence, package ID, version, min/target SDK, DEX count, native ABIs, signing certificate, and split relationships. Reject truncated archives, duplicate ZIP paths, unsupported compression, and mismatched split sets.
3. **Adapter selection**: choose a tested adapter by exact package + version + ABI + engine signature. No generic adapter is assumed. If no adapter exists, stop and export a diagnostic report rather than patching executable code.
4. **Patch plan**: adapter declares exact files and transformations, expected hashes, required native ABI payloads, and a rollback manifest. Never overwrite unknown files or mutate anti-cheat/DRM components.
5. **Rebuild**: rebuild all affected APKs deterministically. Keep package name/version and split metadata consistent. Do not claim Play update compatibility after re-signing.
6. **Sign**: sign the complete APK set with a user-controlled key using Android apksig/apksigner. The original signature cannot be preserved after modification. The user must retain the signing key; losing it prevents future updates signed with that key.
7. **Verify**: run ZIP integrity, manifest/package consistency, DEX/native ABI presence, v2/v3 signature verification, split-set consistency, and installability checks. Install only after explicit user confirmation.
8. **Runtime validation**: report renderer integration as ACTIVE only after a game-specific adapter is observed executing in the target process and frame output is validated. File presence is not proof.

## Compatibility and safety rules

- Google Play apps may use split APKs, Play Asset Delivery, downloaded OBB/data, or server-delivered resources. A single base.apk is not a complete patch target.
- Re-signing commonly prevents installing as an update over the Play Store copy. A separate install may conflict on package name; changing package identity can break licensing, saves, services, and online features.
- Shizuku shell permission does not grant access to private app data or arbitrary process injection.
- Do not bypass anti-cheat, DRM, signature checks, SELinux, or Android app sandboxing.
- The first supported integration targets should be sideloaded, user-owned builds or games with an official mod/plugin API. Geometry Dash and Asphalt 8 are not considered supported until exact-version adapters are implemented and tested.

## Current implementation status

The existing GameRecon scanner is read-only. The APK overlay/rebuild/sign/verify pipeline described here is a design contract, not an implemented feature. The sample renderer demonstrates UltraScale shaders in a project-controlled app only. Do not expose a generic "Inject" button until a tested adapter exists.
