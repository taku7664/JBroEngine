# Engine Audit Follow-up Work

Date: 2026-06-05

This document reorganizes the remaining work from `tasks/EngineCodeAudit.md` after the first dead-code cleanup pass.

## Completed Cleanup

### Dead/stale manifest sidecar path

- Source finding: F-003
- Status: Done
- Result:
  - Removed `AssetPackageBuildDesc::OutputManifestPath`.
  - Removed the stale JSON manifest writer from `CAssetManager::BuildAssetPackage()`.
  - Kept package asset output as one `.jbpack` path.

### Pack cache materialization and source/debug index metadata

- Source findings: F-001, F-002
- Status: Done for default runtime materialization and source/debug metadata.
- Result:
  - Removed default `.packcache` materialization from `CAssetPackReader`.
  - Removed `Importer` and `SourceExtension` from the runtime `AssetRecord` index.
  - Removed the unused `DebugNamePresent` package entry flag.
  - Bumped pack index writer to version 2 and kept v1 read compatibility by reading and discarding removed fields.
  - Packed assets now require memory-load-capable loaders; unsupported file-path fallback fails explicitly.
- Remaining:
  - `ImportOptionsYaml` remains in the runtime record until cooked payload formats carry the resolved runtime options directly.
  - Audio streaming from pack still needs a platform streaming pack reader instead of file extraction.

### Build script asset pack writer parity

- Source findings: F-001, F-002, F-004
- Status: Done
- Result:
  - Updated `BuildScripts/BuildGame.ps1` embedded C# pack writer to write index version 2.
  - Removed `Importer` and `SourceExtension` from script-generated pack index records.
  - Renamed script-side writer/entry types to `JBroPackWriterV2` and `JBroPackEntryV2` to avoid stale loaded C# type reuse in long PowerShell sessions.
  - Verified the embedded C# pack writer compiles with `Add-Type`.

### Release runtime fallback tightening

- Source finding: F-005
- Status: Done
- Result:
  - Release runtime default manifest search no longer includes `Content/build_manifest.yaml`.
  - Release runtime rejects non-binary build manifests instead of parsing YAML fallback.
  - Release runtime scene loading rejects path fallback and requires GUID-backed package asset loading.
- Scope note:
  - Debug builds and editor/development builds still allow legacy fallback for diagnosis and migration.

### Editor-only localization output hygiene

- Source finding: F-006
- Status: Done
- Result:
  - `Application.vcxproj` localization post-build sync now runs only for editor configurations: `Debug` and `Release`.
  - Game configurations `Debug_Game` and `Release_Game` no longer copy editor-only `Localization` into their intermediate output folders.
  - Package verifier still rejects `SDK`, `Editor`, and `Localization` in final game packages.

### Scene serializer side-effect API cleanup

- Source finding: F-007
- Status: Done
- Result:
  - `CSceneSerializer::SerializeToText()` and `SaveToFile()` now take `CScene&`, making the referenced-asset cache update explicit.
  - Removed scene-target `const_cast` from serialization.
  - Object serialization still uses const object access.
  - SDK public mirror was updated to the same signature.

### Build manifest writer unification

- Source finding: F-004
- Status: Done
- Result:
  - Removed the embedded C# `JBroBuildManifestWriterV2` from `BuildScripts/BuildGame.ps1`.
  - Added `BuildTools/BuildManifestTool`, which calls `CBuildManifestLoader::WriteBinaryFile()`.
  - Script/web packaging now shares the engine-owned C++ manifest writer with editor Windows packaging.
  - The tool round-trip validates the generated manifest before returning success.

### Cooked sprite payload transition

- Source findings: F-001, F-002
- Status: Done for C++ Sprite pack/load path.
- Result:
  - C++ pack writer now stores Sprite assets as cooked RGBA8 payloads with a small binary header.
  - Sprite loader detects the cooked payload magic and builds `CSpriteAsset` directly from RGBA memory without decoding original image bytes again.
  - C++ pack writer records Scene/Prefab/BinaryBlob payload types instead of writing every record as `RawSource`.
  - Script/Web C# pack writer now classifies Scene/Prefab/BinaryBlob payload records, but keeps Sprite/Audio raw-compatible until an engine-owned asset pack tool replaces the C# writer.
- Remaining:
  - Move Web/script packaging from embedded C# pack writer to an engine-owned `AssetPackTool`.
  - Cook audio payloads, including a pack-backed streaming strategy.
  - Move remaining runtime import option data out of the pack index once each cooked payload carries its own runtime header.

### Release package smoke hardening

- Source findings: F-001, F-002, F-005
- Status: Done for current Windows/Web package contract checks.
- Result:
  - Added `BuildManifestTool --validate` so generated binary manifests are read back by the engine-owned manifest loader.
  - `BuildGame.ps1` release smoke now checks binary manifest magic/validation, startup scene GUID, asset pack magic, loose asset folder absence, legacy text manifest/index absence, and editor-only artifact absence.
  - yaml-cpp PDB warning handling was documented separately and then resolved by rebuilding matching vendor libs/PDBs.

### RHI/render performance parity pass

- Source findings: F-008, F-009, F-010
- Status: Done for the current sprite-render bottleneck pass.
- Result:
  - Reduced sprite constant-buffer churn.
  - Added RHI instancing contract and default sprite instanced batching.
  - Pre-resolved sprite draw resources in render items.
  - Reduced redundant sprite render state binds.
  - Reduced WebGPU/Vulkan per-draw descriptor churn.
  - Replaced linear WebGPU bind group cache lookup.
  - Added Vulkan descriptor reuse/cache.
- Scope note:
  - Further batching or renderer architecture changes should be tracked as new performance work, not as this audit item.

### Editor interaction hot-path cleanup

- Source finding: F-011
- Status: Done for SceneView box-selection hot path.
- Result:
  - `PickBox()` no longer repeatedly scans full sprite frame alpha data per candidate.
  - Sprite import/load now caches frame-local opaque pixel bounds and local opaque bounds in `CSpriteAsset`.
  - SceneView box selection uses `SpriteFrame::LocalOpaqueBounds` instead of a local legacy alpha-bounds cache.
- Scope note:
  - Single-click `Pick()` still performs a one-pixel alpha test intentionally for click precision.
  - Object-id picking/GPU picking/cached mask picking are future feature-level upgrades, not remaining audit cleanup.

### Project YAML emitter/style cleanup

- Source finding: F-014
- Status: Done.
- Result:
  - Project build-settings YAML emission was consolidated through a shared helper.
  - New-project creation and `SaveProject()` now use the same build-settings key emission path.
  - The cleanup was kept behavior-preserving for `.Jproject` load/save contracts.

## Remaining Work Queue

### 1. Release asset pack contract hardening

- Source findings: F-001, F-002
- Priority: High
- Status: Partially done; Sprite cooked payload, runtime record hardening, and release smoke checks are done.
- Work:
  - Replace script/web C# pack writer with an engine-owned pack tool so Web gets the same cooked Sprite payload.
  - Cook audio payloads, including a pack-backed streaming strategy.
  - Move remaining runtime import option data out of the pack index once each cooked payload carries its own runtime header.
- Reason:
  - This is the largest remaining gap between the current pack and the intended B+ / release protection contract.

### 2. Build manifest writer unification

- Source finding: F-004
- Priority: High
- Status: Done
- Work:
  - Keep future fields in `Engine/Core/Build/BuildManifest.*` first.
  - Keep script/web packaging on `BuildManifestTool` instead of adding a second writer.
- Reason:
  - Manifest drift already caused platform/runtime mismatches such as PPU handling.

### 3. Runtime fallback tightening

- Source finding: F-005
- Priority: Medium
- Status: Done for release runtime gate.
- Work:
  - Keep release package smoke tests covering missing binary manifest and missing startup scene guid.
- Reason:
  - Loose fallback can hide broken packages by loading files that should not exist in release output.

### 4. Editor-only output hygiene

- Source finding: F-006
- Priority: Medium
- Status: Done
- Work:
  - Keep package verifier checks in sync with new platform package outputs.
- Reason:
  - Localization is editor-only in this project and should not appear in game intermediates.

### 5. Scene serialization side-effect cleanup

- Source finding: F-007
- Priority: Medium
- Status: Done
- Work:
  - Keep future scene save call sites aware that serialization refreshes `ReferencedAssets`.
- Reason:
  - The current API signature claims read-only behavior but updates scene metadata.

### 6. RHI/render performance parity

- Source findings: F-008, F-009, F-010
- Priority: Medium
- Status: Done for the current descriptor/batching parity pass.
- Work:
  - Track future renderer performance work as new scoped items.
  - Keep D3D11, WebGPU, Vulkan improvements parallel when adding new renderer features.
- Reason:
  - Vulkan descriptor reuse/cache and current sprite batching improvements have been implemented; future work should stay parallel across RHIs.

### 7. Editor interaction hot-path cleanup

- Source finding: F-011
- Priority: Low to Medium
- Status: Done for the known SceneView box-selection hot path.
- Work:
  - Keep one-pixel alpha test for single-click picking unless object-id/GPU picking is introduced.
  - Track object-id picking, cached masks, or bounded sampling as separate feature upgrades if needed.
- Reason:
  - The repeated full-frame alpha scan issue was resolved by cached sprite opaque bounds.

### 8. Mobile package completion

- Source finding: F-012
- Priority: Medium
- Status: In progress; Android package staging foundation is connected.
- Work:
  - Add Android NDK native target that produces `libJBroGame.so`.
  - Connect Android native entry/lifecycle/surface callbacks to the common runtime bootstrap.
  - Add Android Vulkan runtime smoke after the native target exists.
  - Implement Gradle signing/output polish for Debug APK and Release APK/AAB.
  - Keep iOS explicitly unsupported until Xcode signing and MoltenVK/Metal direction is implemented.
  - Keep platform package differences explicit; do not hide them behind vague `Mobile` behavior.
- Done:
  - `BuildGame.ps1 -Platform Android` now reaches package staging instead of immediate mobile unsupported.
  - Android Gradle project skeleton is generated under the package output.
  - Binary manifest and asset pack are staged into `app/src/main/assets/Content`.
  - Android package verification rejects loose assets and editor/script DLL artifacts.
  - Missing `libJBroGame.so` fails before Gradle with the generated project path and checked candidate paths.
- Reason:
  - Mobile package work must move toward real Android/iOS outputs without pretending Android and iOS have the same packaging constraints.

### 9. Build icon asset workflow polish

- Source finding: F-013
- Priority: Low to Medium
- Work:
  - Keep icon selection asset-guid based.
  - Avoid fixed overwrite paths for imported icon copies.
  - Reuse existing asset import/copy conventions instead of inventing a separate build asset store.
- Reason:
  - Build settings should reference project assets, not unmanaged build-side files.

### 10. YAML emitter/style cleanup

- Source finding: F-014
- Priority: Low
- Status: Done
- Work:
  - Keep future `.Jproject` fields on shared emitter helpers where possible.
  - Keep future formatting-only cleanup separate from behavior-changing project save work.
- Reason:
  - Shared emitter usage reduces future build-settings serialization drift.
