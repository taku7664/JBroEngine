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

## Remaining Work Queue

### 1. Release asset pack contract hardening

- Source findings: F-001, F-002
- Priority: High
- Work:
  - Cook texture/audio/scene/prefab payloads before pack write.
  - Move remaining runtime import option data into cooked payload headers where practical.
  - Add platform pack streaming for assets that intentionally stream.
- Reason:
  - This is the largest gap between the current pack and the intended B+ / release protection contract.

### 2. Build manifest writer unification

- Source finding: F-004
- Priority: High
- Work:
  - Make one binary manifest writer authoritative.
  - Prefer an engine-owned tool or shared library over duplicated C++ and PowerShell/C# format writers.
  - Add round-trip verification for writer/reader compatibility.
- Reason:
  - Manifest drift already caused platform/runtime mismatches such as PPU handling.

### 3. Runtime fallback tightening

- Source finding: F-005
- Priority: Medium
- Work:
  - Gate `build_manifest.yaml` fallback and loose scene path fallback behind development/debug only.
  - In release game packages, require binary manifest, startup scene guid, and mounted pack.
- Reason:
  - Loose fallback can hide broken packages by loading files that should not exist in release output.

### 4. Editor-only output hygiene

- Source finding: F-006
- Priority: Medium
- Work:
  - Stop copying `Localization` into game build output from `Application.vcxproj`.
  - Keep localization sync editor-only.
  - Keep package verifier checking that `SDK`, `Editor`, and `Localization` are absent.
- Reason:
  - Localization is editor-only in this project and should not appear in game intermediates.

### 5. Scene serialization side-effect cleanup

- Source finding: F-007
- Priority: Medium
- Work:
  - Replace `const_cast` mutation in `SerializeToText(const CScene&)`.
  - Either make the mutation explicit with `SerializeToText(CScene&)`, or split referenced-asset collection from serialization.
- Reason:
  - The current API signature claims read-only behavior but updates scene metadata.

### 6. RHI/render performance parity

- Source findings: F-008, F-009, F-010
- Priority: Medium
- Work:
  - Add Vulkan descriptor reuse/cache or dynamic uniform strategy.
  - Keep D3D11, WebGPU, Vulkan improvements parallel.
  - Evaluate optional batching improvements without breaking transparent ordering.
- Reason:
  - Vulkan currently has a baseline path, but per-draw descriptor work will become a real bottleneck.

### 7. Editor interaction hot-path cleanup

- Source finding: F-011
- Priority: Low to Medium
- Work:
  - Avoid full render target alpha scan during frequent SceneView picking/drag interactions.
  - Prefer object-id picking, cached masks, or bounded sampling where practical.
- Reason:
  - This is editor-only, but it can become visibly expensive on high-resolution views.

### 8. Mobile package completion

- Source finding: F-012
- Priority: Medium
- Work:
  - Implement Android/iOS package staging after the release pack contract is hardened.
  - Keep platform package differences explicit; do not hide them behind vague `Mobile` behavior.
- Reason:
  - Current mobile package code intentionally fails fast instead of producing a misleading partial package.

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
- Work:
  - Normalize ProjectManager YAML emitter indentation and helper usage.
  - Keep this separate from behavior-changing project save work.
- Reason:
  - Low risk, but it improves future reviewability of build settings serialization.
