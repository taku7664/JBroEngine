# Game Build Pipeline Design Note

## Goal
- Editor, Windows game build, Web build, and future mobile builds must share one build model.
- The editor may request a build, but the build result must be produced by an independent builder pipeline.
- Runtime builds must not include editor-only systems: ImGui, editor windows/tools, live compile, SDK, and editor localization.
- Web and mobile must stay visible in every Windows build decision.

## Non-negotiable Direction
- Treat `.Jproject` as an editor/project-authoring file.
- Treat `build_manifest.json` as the runtime contract.
- Treat cooked assets as the cross-platform input.
- Treat platform packaging as the final platform-specific step.
- Do not let Windows DLL/live-compile assumptions leak into Web or mobile.

## Proposed Pipeline
```text
Editor
  -> save project
  -> request build
  -> show builder log

Builder
  -> load .Jproject
  -> validate build settings
  -> reconcile/import assets
  -> cook assets
  -> build scripts for target platform
  -> build/copy runtime executable
  -> stage platform files
  -> verify output
  -> write build report

Runtime
  -> load build_manifest.json
  -> mount cooked assets
  -> load or bind game scripts
  -> open startup scene
```

## First Implementation Target
- Add `BuildScripts/BuildGame.ps1`.
- Support Windows first:
  - `-Project <path-to-.Jproject>`
  - `-Platform Windows`
  - `-Configuration Release`
  - `-OutputRoot Dist/Games`
- Produce:
```text
Dist/Games/<ProjectName>-Windows-Release/
  <ProjectName>.exe
  GameScript.dll
  Content/
    build_manifest.json
    Packs/
      base.jbp
  ThirdParty/
    ...
```

## Build Manifest
Runtime should read a manifest instead of reading `.Jproject` directly.

Minimum fields:
- `version`
- `productName`
- `targetPlatform`
- `configuration`
- `startupScene`
- `resolution`
- `assetMounts`
- `scriptMode`
- `scriptModule`
- `engineVersion`
- `buildTimeUtc`

Example:
```json
{
  "version": 1,
  "productName": "SampleGame",
  "targetPlatform": "Windows",
  "configuration": "Release",
  "startupScene": "Scenes/Main.jscene",
  "resolution": { "width": 1920, "height": 1080 },
  "assetMounts": [
    { "type": "Pack", "path": "Content/Packs/base.jbp" }
  ],
  "scriptMode": "DynamicLibrary",
  "scriptModule": "GameScript.dll"
}
```

## Asset Build Direction
- Default game builds must produce real asset packs, not loose file staging.
- Loose staging may exist only as an explicit debug/development option.
- Phase 1: pack file without compression.
  - Add `.jbp` pack format.
  - Add pack index: asset id, guid, type, offset, size, hash, compression, encryption.
  - Store runtime asset registry inside or next to the pack.
  - Runtime loads assets through pack offsets, not through source file paths.
- Phase 2: integrity and tamper detection.
  - Hash every packed asset payload.
  - Hash the pack index.
  - Sign the pack header/index or use authenticated encryption.
  - Fail load when hash/signature verification fails.
- Phase 3: confidentiality.
  - Encrypt asset payloads.
  - Prefer authenticated encryption so corruption/tampering is also detected.
  - Consider encrypting the index when logical asset names should not be exposed.
- Phase 4: platform profiles.
  - Windows: encrypted `.jbp` file beside executable.
  - Web: packed assets can be downloaded/preloaded, but embedded keys are not secret.
  - Mobile: pack may live inside the app bundle or expansion/storage container.
- Phase 5: incremental cook cache.
  - Use source hash + importer version + platform profile as cache key.
- Add `IAssetMount` implementations:
  - `PackAssetMount`
  - `LooseAssetMount` for debug only
  - later `MobileBundleAssetMount`
  - later `WebPreloadAssetMount`

## Pack Protection Goals
- Resource privacy:
  - Users should not be able to browse the build folder and directly open normal PNG/WAV/YAML files.
  - Pack entries should not expose raw project folder structure unless debug symbols/options request it.
  - Payload encryption is required for this goal.
- Tamper protection:
  - Modified pack bytes must be detected before the asset is used.
  - A plain checksum is not enough because an attacker can recompute it.
  - Use authenticated encryption or a signature/HMAC over the index and payload hashes.
- Limits:
  - Packing is protection, not perfect DRM.
  - Client-side keys can eventually be extracted.
  - Web cannot keep asset keys secret from a determined user.
  - This still raises the cost of casual extraction and catches accidental or simple malicious modification.

## Pack Format Sketch
```text
JBP Header
  magic
  version
  flags
  indexOffset
  indexSize
  indexHash
  signatureOrMac

Encrypted/Authenticated Index
  assetCount
  entries[]

Entry
  assetId
  guid
  type
  logicalPathHash
  offset
  compressedSize
  uncompressedSize
  compression
  encryption
  payloadHash

Payload Blocks
  encrypted/compressed asset bytes
```

## Pack Build Order
1. Load `.Jproject`.
2. Reconcile assets and ensure `.Jmeta` consistency.
3. Resolve build scene list and startup scene.
4. Build dependency graph from scene `ReferencedAssets`.
5. Add explicit include rules: always-include assets, labels, addressable assets.
6. Validate all referenced GUIDs.
7. Normalize logical asset ids and reject collisions.
8. Import/cook assets into platform-ready bytes.
9. Compress only when the asset type benefits.
10. Encrypt payload blocks.
11. Write pack payloads and index.
12. Sign or MAC header/index/payload hashes.
13. Write `build_manifest.json`.
14. Verify by reopening the pack through the runtime reader.

## Pack Runtime Direction
- `CAssetManager::LoadPackedAssetManifest` should become real pack registration.
- `AssetMetaData::Path` should not mean source filesystem path in a packaged build.
- Add a resolver layer:
```text
AssetGuid -> AssetRecord -> IAssetMount -> byte stream
```
- Existing asset loaders should eventually accept byte streams, not only filesystem paths.
- Transitional implementation can extract a payload to memory/temp only for loaders that cannot read memory yet.

## Pack Counterexamples
- "Just zip the Assets folder."
  - This hides folder clutter but does not protect data or prevent tampering.
- "Encrypt each file separately and keep names in JSON."
  - Payload is protected, but asset names/project structure may still leak.
- "Hash files in manifest."
  - Detects accidental corruption, but an attacker can modify both file and hash unless the manifest is signed/MACed.
- "One huge pack is enough."
  - Simple for v1, but bad for patching, DLC, and scene-based loading. Keep the format able to support multiple packs.
- "Compression is protection."
  - Compression is not protection. It only changes representation.
- "Web pack encryption protects assets."
  - Web can use the same pack reader for consistency, but the key is recoverable from the client.

## Pack Entry Identity Options
There are three candidate shapes for how packed asset entries identify and describe assets.

| Option | Entry name | Entry body | Summary |
| --- | --- | --- | --- |
| A | opaque id/hash from pack index | raw/cooked data only; metadata lives in encrypted index | Strongest hiding, but requires a mature index and stream-oriented loader path. |
| B | asset GUID | import options + raw/cooked data | Good balance: stable identity, no original filename exposure, simpler lookup by GUID. |
| C | original filename/path | GUID + import options + raw/cooked data | Easiest to debug and migrate from loose files, but leaks project/resource structure. |

Comparison:

| Metric | A. Index/opaque id | B. GUID name | C. Original filename |
| --- | --- | --- | --- |
| Resource privacy | High | High | Low |
| Tamper protection compatibility | High | High | High |
| Original project structure exposure | Very low | Low | High |
| Rename resilience | High | High | Low to medium |
| Duplicate filename handling | Strong | Strong | Weak unless full path is preserved |
| Runtime GUID lookup | Requires index lookup | Direct | Requires metadata read or index lookup |
| Debuggability | Low | Medium | High |
| Initial implementation cost | High | Medium | Low |
| Loader migration pressure | High | Medium | Low |
| Mobile/Web suitability | High | High | Medium |
| Patch/DLC friendliness | High | High | Medium |

Decision:
- Prefer option B for the first protected pack implementation.
- Keep the original filename/path only as optional encrypted debug metadata, not as the public entry name.
- Keep option A as the long-term optimized format after asset loaders support byte streams cleanly.
- Avoid option C for release builds because it conflicts with the resource privacy goal.
- Migration direction: implement B first, but keep the pack reader/writer APIs shaped so A can replace the public entry identity later without changing game-facing asset APIs.

Option B entry body should be self-describing enough for transitional loaders:
```text
Entry name: <guid>
Entry body:
  entryHeader
    guid
    type
    importer
    importOptionsSize
    payloadSize
    payloadHash
  importOptions
  rawOrCookedPayload
```

Runtime loading path for option B:
```text
AssetGuid -> PackIndex[Guid] -> decrypt/verify entry -> parse entry header -> loader
```

This reduces the need to expose source paths while still allowing a transitional path where a loader receives a temporary file or file-like adapter until all loaders can consume memory streams.

## Option B Hardening Notes
- Do not store raw original paths in the public index.
- If original paths are needed for debug builds, store them in an encrypted debug metadata block.
- Use a generated `AssetRecordId` internally even when the external entry name is the GUID.
  - This lets option A replace the GUID entry name later without changing loader-facing records.
- Store import options beside the payload, but version them independently from the pack format.
  - `packVersion` and `importerVersion` should not be the same field.
- Store a cooked payload type:
  - `RawSource`
  - `CookedTexture`
  - `CookedAudio`
  - `SerializedScene`
  - `BinaryBlob`
- Release builds should prefer cooked payloads over original raw files.
- Add a per-entry flags field:
  - compressed
  - encrypted
  - streamed
  - memoryOnly
  - debugNamePresent
- Keep pack index records sorted deterministically by GUID or AssetRecordId.
- Add a pack-level build id and content hash so clean rebuild differences are explainable.
- Keep one manifest-level list of mounted packs:
```text
build_manifest.json
  packs:
    - id: base
      path: Content/Packs/base.jbp
      required: true
    - id: dlc_01
      path: Content/Packs/dlc_01.jbp
      required: false
```

Recommended B implementation layers:
```text
PackWriter
  -> receives AssetCookRecord[]
  -> writes GUID-named encrypted entries
  -> writes encrypted/signed index

PackReader
  -> verifies header/index
  -> resolves GUID to AssetRecord
  -> returns AssetBytes or AssetStream

AssetManager
  -> asks IAssetMount for GUID
  -> never knows whether the entry name was GUID or opaque id
```

Do not expose pack entry names above `PackReader`. This is the key rule that keeps option A reachable.

## 2026-06-01 Implementation Pass
- Added B+ runtime/building primitives:
  - `AssetEntryLocator`
  - `AssetRecord`
  - `CAssetPackWriter`
  - `CAssetPackReader`
- `AssetManager` now keeps pack entry details below the pack reader layer.
  - Upper-level loading still starts from `AssetGuid`.
  - Packed assets register as synthetic runtime records, not source file paths.
- `AssetLoadDesc` can carry an in-memory payload.
  - File and sprite loaders use memory payloads directly.
  - Audio loader can decode memory payloads for decompressed mode.
  - Streaming audio can still fall back to a materialized cache file because the current backend path stores a stream path.
- Current `.jbp` pass writes:
  - fixed binary header
  - GUID entry locator
  - binary index
  - payload bytes
  - FNV-based payload/index hashes
- Current `.jbp` pass does not yet provide cryptographic protection.
  - The header/index shape reserves the right place for authenticated encryption/MAC, but FNV hash only detects accidental or simple corruption.
  - Real release protection still requires an authenticated encryption or MAC provider.
- Web source list must include `Engine/Core/Asset/AssetPackage.cpp` whenever the engine-side pack layer is compiled for Web.

## Script Build Direction
- Windows:
  - Build `GameScript.dll` in Release.
  - Stage only the DLL and required runtime dependency files.
  - LiveCompile remains editor-only.
- Web:
  - Do not rely on DLL loading.
  - Prefer static compilation into WASM or generated registration sources.
- Mobile:
  - Assume dynamic native code loading is unavailable or not store-compliant.
  - Prefer static script binding or ahead-of-time generated registration.
- Shared abstraction:
```text
IScriptBindingProvider
  WindowsDynamicLibraryScriptProvider
  StaticScriptProvider
```

## Platform Profiles
The builder should not branch on scattered `if platform == ...` checks. Add a platform profile object.

Profile fields:
- executable kind
- script mode
- asset mount mode
- compression policy
- symbol policy
- required runtime libraries
- forbidden output patterns
- package layout
- post-build command

Suggested profiles:
- `WindowsDesktopProfile`
- `WebProfile`
- `AndroidProfile`
- `IOSProfile`

## Exception Handling Checklist
- Project file missing or invalid.
- Project version unsupported.
- Project path contains non-ASCII characters.
- Output path is inside source `Assets` or `Contents`.
- Output path delete target resolves outside expected build root.
- Startup scene is empty, missing, or not under the project asset root.
- Build scene list contains missing files.
- Duplicate asset GUIDs exist.
- Missing `.Jmeta` files exist after reconcile.
- Orphan `.Jmeta` files exist and cannot be quarantined.
- Asset import fails.
- Asset references point to missing GUIDs.
- Asset case differs only by letter case. This matters on mobile/Web file systems.
- Two assets normalize to the same package path.
- Unsupported asset type for target platform.
- Unsupported texture/audio format for target platform.
- Cook output exceeds target size limits.
- Pack file cannot be reopened after writing.
- Pack header/index signature or MAC verification fails.
- Pack index references bytes outside the payload range.
- Pack entry offset/size overlaps another entry unexpectedly.
- Pack key id is missing or unsupported.
- Pack encryption is disabled for a release build.
- Script source project is missing.
- Script build command is empty.
- Script build fails.
- Script output DLL or static registry file is missing after build.
- Script ABI/version mismatch with engine.
- Runtime DLL dependency is missing.
- Editor-only file is found in output.
- SDK is found in game output.
- Localization is found in game output unless explicitly allowed by a runtime localization feature.
- PDB/symbol files are included when symbol policy says no.
- Existing output directory cannot be cleaned.
- File copy fails because the executable is running.
- Build report cannot be written.

## Mobile-Specific Risks
- No arbitrary dynamic DLL loading.
- File system is usually sandboxed and case-sensitive behavior may differ.
- Assets may need to live in platform package storage first, then copy/extract to writable storage only when needed.
- Startup scene and config must be available before writable storage is initialized.
- Native code signing and bundle identifiers become part of the build contract.
- Runtime permissions must be declared at package time, not requested ad hoc by engine subsystems.
- Texture/audio compression should be target-profile driven.
- Long path and Unicode path behavior must be tested on device.
- Hot reload/live compile should be treated as editor-only and unavailable.

## Verification Gates
- `Release_Game|x64` builds.
- GameScript sample or generated user script builds in Release.
- Output folder has no editor-only files.
- Output folder has no SDK.
- Output folder has no editor localization.
- `build_manifest.json` is valid JSON and all referenced files exist.
- Pack file opens through the runtime reader.
- Pack verification fails when one byte is modified.
- Pack verification fails when index entries are reordered or edited.
- Release build does not output loose source assets unless explicitly requested by a development option.
- Startup scene loads from staged output.
- Game can launch from outside the repository root.
- Clean rebuild produces the same manifest except timestamps/build id.
- Web build pipeline still uses the same cook manifest shape.

## Deferred Work
1. Add `BuildSettings` to project data.
2. Add `BuildScripts/BuildGame.ps1`.
3. Add runtime `build_manifest.json` loader.
4. Add `.jbp` pack writer and reader.
5. Add output verifier.
6. Add Windows script DLL staging.
7. Add pack integrity/signature check.
8. Add platform profile interface.
9. Add static script binding path for Web/mobile.
10. Add editor Build window that calls the builder and shows logs.

## Current Design Decision
- Start with actual pack files for game builds.
- Keep loose asset staging only as an explicit debug/development option.
- Start with PowerShell builder, not a new C++ builder executable.
- Keep editor build UI thin.
- Keep cooked asset contract platform-neutral.
- Keep script loading platform-specific behind one provider interface.
