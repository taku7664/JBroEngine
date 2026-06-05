# Engine-wide Code Audit

Date: 2026-06-05

## Scope

- Read target: `Application/`, `Engine/`, `SDK/Include/`, `BuildScripts/`, `SampleProject/`, `Samples/`
- Excluded: `Build/`, `.git/`, `Engine/ThirdParty/`
- Method: broad `rg` scan for suspicious patterns, then targeted source reads around each candidate.
- This document is a review artifact. It does not change runtime behavior.

## Summary

The most important issues are not syntax-level dead code. They are contract drift between build/package/runtime layers.

1. Asset pack still carries raw-source oriented metadata and can materialize decrypted payloads back to disk.
2. Build manifest/package rules are implemented in multiple places with overlapping but not identical contracts.
3. Runtime still has legacy loose-path fallback for startup scene and YAML manifest fallback.
4. Vulkan rendering works as a baseline, but descriptor updates and layout handling are still early-stage and likely to become bottlenecks or correctness traps.
5. Several editor helpers compute expensive pixel-level data every interaction frame.

## Resolved After Audit

### R-001: Removed stale asset package JSON manifest writer

- Related finding: F-003
- Date: 2026-06-05
- Files:
  - `Engine/Core/Asset/AssetManager.cpp`
  - `Engine/Core/Asset/AssetTypes.h`
  - `SDK/Include/Core/Asset/AssetTypes.h`
  - `Application/Editor/Build/GameBuildManager.cpp`

What changed:
- Removed the unused `BuildAssetPackage()` JSON sidecar manifest writer.
- Removed `AssetPackageBuildDesc::OutputManifestPath` from the engine and SDK public mirror.
- Kept asset package output as a single `OutputBlobPath` `.jbpack` contract.

Why:
- Current runtime/package manifest is already `Content/build_manifest.jbmanifest`.
- Keeping a second stale JSON writer made the package contract look split even though no current caller used it.

## Findings

### F-001: Pack reader can write decrypted payloads back to `.packcache`

- Severity: High
- Category: asset protection / package contract
- Files:
  - `Engine/Core/Asset/AssetPackage.cpp:418-420`
  - `Engine/Core/Asset/AssetPackage.cpp:491-527`
  - `Engine/Core/Asset/AssetManager.cpp:650-665`

What I read:
- `CAssetPackReader::Open()` sets `m_cacheRoot` to `<pack parent>/.packcache/<pack stem>`.
- `CAssetPackReader::MaterializePayload()` decrypts/reads a payload and writes it to that cache path.
- `CAssetManager::LoadAssetInternal()` uses `MaterializePayload()` when the loader cannot load directly from `MemoryPayload`.

What I think:
- This is useful as a compatibility bridge for file-only loaders, but it weakens the user's stated pack goals: personal resource protection and mutation resistance.
- The cache is deleted on `Close()`, but a process crash, debugger pause, permission failure, or external copy can leave readable cooked/raw payloads on disk.

Counterexample checked:
- This is not always hit. Sprite, FileAsset, and AudioEffect loaders support memory payload. Audio supports memory for decompressed mode, but streaming mode requires a path.

Recommendation:
- Treat disk materialization as an explicit debug/development escape hatch, not a default runtime fallback.
- Add a loader capability flag such as `RequiresFilePathForRuntime()` and make package validation fail for release/mobile/web when an asset would need materialization.
- For audio streaming, choose a platform-specific streaming pack reader instead of extracting the file.

### F-002: Current asset pack stores raw source payload and debug/import metadata in runtime index

- Severity: High
- Category: asset pipeline / release hygiene
- Files:
  - `Engine/Core/Asset/AssetPackage.h:8-27`
  - `Engine/Core/Asset/AssetPackage.h:36-52`
  - `Engine/Core/Asset/AssetPackage.cpp:177-190`
  - `Engine/Core/Asset/AssetPackage.cpp:292-317`

What I read:
- `AssetRecord` includes `Importer`, `ImportOptionsYaml`, and `SourceExtension`.
- `CAssetPackWriter::Write()` reads `entry.SourcePath` directly and sets `record.PayloadType = EAssetPayloadType::RawSource`.
- `SourceExtension` is serialized into the pack index.

What I think:
- This is still closer to "encrypted loose source bundle" than the final cooked-payload package described in B+.
- It protects folder paths better than loose assets, but it does not yet meet the stronger runtime-cooked contract.

Counterexample checked:
- Some metadata can be useful for diagnostics and forward compatibility, but B+ explicitly said release pack/index should not carry source path/name structure or debug metadata by default.

Recommendation:
- Split `AssetRecordRuntime` and `AssetRecordDebugMetadata`.
- Runtime pack index should keep only: `AssetGuid`, `EntryLocator`, `PackId`, `PayloadType`, `Offset`, `StoredSize`, `UncompressedSize`, `PayloadHash`, flags, dependency ids if needed.
- Move importer/import option/source extension to editor cache or optional debug sidecar.
- Replace raw source payload with cooked texture/audio/scene/prefab payloads before expanding mobile/web packaging.

### F-003: Dead/stale JSON manifest writer remains inside `BuildAssetPackage`

- Severity: Medium
- Category: dead code / design drift
- Status: Resolved by R-001.
- Files:
  - `Engine/Core/Asset/AssetManager.cpp:536-562`
  - `Application/Editor/Build/GameBuildManager.cpp:892-895`

What I read:
- `BuildAssetPackage()` writes a JSON manifest only if `OutputManifestPath` is not empty and differs from `packPath`.
- The only current caller sets both `OutputManifestPath` and `OutputBlobPath` to the same `game_assets.jbpack` path.

What I think:
- This branch looks like a leftover from the earlier `build_manifest.json` direction.
- It also violates the later project preference to use YAML for text manifests and binary for shipped manifests.

Counterexample checked:
- No other source caller currently passes different manifest/blob paths.

Recommendation:
- Remove the JSON writer or replace it with an explicit editor-only/debug-only YAML sidecar writer.
- Keep shipped package manifest in `Content/build_manifest.jbmanifest`.

### F-004: Build manifest contract is duplicated between C++ and PowerShell/C# writer

- Severity: High
- Category: build pipeline / cross-platform correctness
- Files:
  - `BuildScripts/BuildGame.ps1:566-657`
  - `Engine/Core/Build/BuildManifest.cpp:167-249`
  - `Engine/Core/Build/BuildManifest.cpp:481-529`
  - `Application/Editor/Build/GameBuildManager.cpp:901-912`

What I read:
- Editor Windows packaging calls `CBuildManifestLoader::WriteBinaryFile()` in C++.
- Script/Web packaging uses `BuildGame.ps1`, which embeds a C# `JBroBuildManifestWriterV2` that writes a parallel binary format.
- The C++ reader then decodes the same format.

What I think:
- This is a classic drift point. One new manifest field or order change must be updated in at least two writers.
- The earlier PPU issue is exactly the kind of bug this structure creates.

Counterexample checked:
- Current fields appear aligned: width, height, startup scene guid, PPU, target platform, script mode, script module.

Recommendation:
- Make one writer authoritative.
- Best direction: a small engine-owned command line tool, or make `BuildGame.ps1` call a shared binary manifest writer executable.
- If keeping PowerShell, add a manifest binary round-trip test that writes with both writers and reads with C++.

### F-005: Runtime still supports legacy YAML manifest and loose scene path fallback

- Severity: Medium
- Category: release contract / dead compatibility
- Files:
  - `Engine/Core/Build/BuildManifest.cpp:13-14`
  - `Engine/Core/Build/BuildManifest.cpp:334-345`
  - `Engine/Core/Build/BuildManifest.cpp:380-468`
  - `Application/Application.cpp:295-344`

What I read:
- Runtime searches both `Content/build_manifest.jbmanifest` and `Content/build_manifest.yaml`.
- If `StartupSceneGuid` is null but `StartupScene` path exists, runtime resolves and loads the scene by path.

What I think:
- This makes older packages more tolerant, but it weakens the pack-only contract and can mask packaging mistakes.
- If a package accidentally includes a loose scene, the game may work for the wrong reason.

Counterexample checked:
- During transition, YAML fallback may have helped avoid breaking old packages. But current design notes consistently point to binary manifest and pack.

Recommendation:
- Gate legacy YAML/path fallback behind a debug/development define.
- In release game builds, require `StartupSceneGuid` and pack mount.

### F-006: Game build output still receives editor localization as a post-build side effect

- Severity: Medium
- Category: build hygiene / editor-runtime separation
- Files:
  - `Application/Application.vcxproj:310-318`
  - `BuildScripts/BuildGame.ps1:1138`

What I read:
- `Application.vcxproj` has an unconditional post-build event that copies `Application/Localization` to `$(OutDir)` and `$(SolutionDir)/Localization`.
- The game package verifier later asserts `Localization` is not present in the final package root.

What I think:
- Final package may still be clean, but intermediate game build output contains editor-only localization files.
- This conflicts with the user's rule that localization is editor-only, and it creates a path where packaging might accidentally re-include it later.

Counterexample checked:
- Current `BuildGame.ps1` explicitly checks package root for `SDK`, `Localization`, `Editor`, so final package is defended.

Recommendation:
- Condition the post-build event on `JBRO_EDITOR` configurations or move editor localization sync into editor packaging only.

### F-007: `SceneSerializer::SerializeToText(const CScene&)` mutates the scene through `const_cast`

- Severity: Medium
- Category: API correctness / surprising side effect
- Files:
  - `Engine/GameFramework/Scene/SceneSerializer.cpp:68-110`

What I read:
- Serialization takes `const CScene&`, then uses `const_cast<CScene&>(scene).ForEachObject(...)`.
- It also calls `SetReferencedAssets()` through `const_cast` before returning.

What I think:
- The name and signature promise read-only serialization, but the function updates scene metadata.
- That can be valid if "save updates referenced asset cache" is a required side effect, but it should be explicit in the API.

Counterexample checked:
- `ReferencedAssets` is useful project metadata, and serialization is a natural place to recalculate it.

Recommendation:
- Either change API to `SerializeToText(CScene& scene, ...)` or split into `CollectReferencedAssets(const CScene&)` and `UpdateReferencedAssets(...)`.
- Add a comment at call sites if the side effect is intentional.

### F-008: Vulkan descriptor sets are allocated and updated per draw

- Severity: Medium
- Category: rendering performance
- Files:
  - `Engine/Core/RHI/Vulkan/VulkanCommandContext.cpp:399-434`
  - `Engine/Core/RHI/Vulkan/VulkanCommandContext.cpp:521-561`
  - `Engine/Core/RHI/Vulkan/VulkanCommandContext.cpp:564-640`

What I read:
- Every draw calls `BindPendingDescriptors()`.
- That allocates a descriptor set from a pool and calls `vkUpdateDescriptorSets()`.
- There is no equivalent cache to the WebGPU bind group cache.

What I think:
- This is acceptable for smoke/parity, but it is a likely bottleneck once sprite count rises.
- It also creates many descriptor sets per frame and relies on pool reset timing.

Counterexample checked:
- Forward2D now batches contiguous sprites sharing texture/sampler, so the draw count is lower than per-sprite in common cases.

Recommendation:
- Add a Vulkan descriptor cache keyed by `(pipeline layout, constant buffer, texture view, sampler)`, or move to descriptor indexing / dynamic uniform buffers later.
- Keep D3D11/WebGPU/Vulkan renderer improvements parallel so RHI behavior does not diverge.

### F-009: `Forward2DRenderer` batching only groups contiguous items with the same texture/sampler

- Severity: Medium
- Category: rendering performance
- Files:
  - `Engine/Core/Renderer/RenderScene.cpp:30-48`
  - `Engine/Core/Renderer/Forward2DRenderer.cpp:721-793`
  - `Engine/Core/Renderer/Forward2DRenderer.cpp:796-857`

What I read:
- Render scene sorting only considers queue and `SortOrder`.
- `Forward2DRenderer` batches only adjacent items sharing texture/sampler.

What I think:
- If many sprites share a sort order but alternate textures, batching will fragment.
- Sorting by material/texture inside the same order bucket could improve batching, but it may change visible ordering for transparent sprites.

Counterexample checked:
- Transparent rendering often needs author-controlled order. Sorting by texture globally would be wrong.

Recommendation:
- Keep current behavior for correctness.
- Add an optional secondary sort key only within groups where order is explicitly equal and stable order is not required, or add sprite atlas/cook-time texture packing instead.

### F-010: `RenderFiltered` / `RenderExcluding` can ask the scene to sort repeatedly in one frame

- Severity: Low to Medium
- Category: rendering performance
- Files:
  - `Engine/Core/Renderer/RenderScene.cpp:30-38`
  - `Engine/Core/Renderer/Forward2DRenderer.cpp:733-736`
  - `Engine/Core/Renderer/Forward2DRenderer.cpp:802-803`

What I read:
- Both normal and filtered render paths call `CRenderScene::Sort()`.
- `CRenderScene::Sort()` is guarded by `m_needsSort`, so repeated calls after the first are cheap.

What I think:
- This is not currently a major bug because `m_needsSort` prevents repeated `std::sort`.
- It is still an architectural smell: render passes know concrete `CRenderScene` and pull sorting themselves.

Counterexample checked:
- Since the guard exists, this should not be prioritized before descriptor/cache/package work.

Recommendation:
- Move sorting to `RenderScene` finalization or engine frame preparation when render graph grows.

### F-011: SceneView selection scans sprite alpha pixels every box-select pass

- Severity: Medium
- Category: editor performance
- Files:
  - `Application/Editor/Main/SceneView/SceneViewEditContext.cpp:277-327`

What I read:
- Box selection attempts a tight AABB by scanning the sprite texture alpha pixels.
- It loops over frame width/height and checks alpha for every candidate sprite.

What I think:
- This gives accurate selection bounds, but it can become expensive with large textures or many selected/candidate sprites.
- It is editor-only, so it is not a runtime game bottleneck.

Counterexample checked:
- For small sprites it is fine, and the tight bound improves usability.

Recommendation:
- Cache tight alpha bounds per sprite asset and frame region, invalidated by `PixelGeneration` or import options.
- The selection code should use cached bounds and only fall back to scanning when cache is missing.

### F-012: Mobile package entry points are recognized but not implemented

- Severity: Medium
- Category: platform completeness
- Files:
  - `BuildScripts/BuildGame.ps1:991-993`
  - `Application/Editor/Build/GameBuildManager.cpp:519-528`

What I read:
- Android/iOS settings exist and are saved.
- Build path fails intentionally with "not implemented yet".

What I think:
- This is not a hidden bug; it is an explicit incomplete platform path.
- It matters because web/mobile are part of the engine direction, so package and asset contracts should not assume Windows DLL behavior.

Counterexample checked:
- Failing early is better than silently producing a fake mobile package.

Recommendation:
- Keep the fail-fast behavior until Android/iOS native package steps exist.
- Before implementing mobile package output, resolve F-001/F-002 so mobile does not inherit disk materialization/raw-source pack behavior.

### F-013: `BuildSettingsWindow` copies external icon assets into a fixed path

- Severity: Low to Medium
- Category: asset workflow / collision risk
- Files:
  - `Application/Editor/Main/BuildSettingsWindow.cpp:804-829`

What I read:
- If an icon selected in build settings is outside the asset root, it is copied to a fixed `WINDOWS_ICON_ASSET_PATH`.
- That path is then imported as a custom asset and GUID is stored.

What I think:
- This matches the agreed direction that build icons should live under `Assets`, but a fixed path can overwrite a previous icon asset without a version/history distinction.

Counterexample checked:
- For one application icon, a stable path is simple and user-friendly.

Recommendation:
- Keep the path stable if the product has only one icon.
- If multiple platform icons are added, use `Assets/BuildSettings/<Platform>/...` and store GUIDs per platform.

### F-014: Some project creation YAML emit code indentation is visibly inconsistent

- Severity: Low
- Category: maintainability / review friction
- Files:
  - `Engine/Editor/Project/ProjectManager.cpp:546-567`

What I read:
- Several `out <<` lines in the new-project build settings block are misindented relative to the surrounding emitter code.

What I think:
- This is not a runtime bug, but it makes future review more error-prone in an area that already had build-setting serialization issues.

Counterexample checked:
- The generated YAML output is not determined by source indentation.

Recommendation:
- Clean this when touching project creation/serialization next time, but do not prioritize it as a standalone behavior fix.

## Not Findings / Lower Priority

### SafePtr allocation internals

`OwnerPtr` and `SafePtr` allocate control blocks manually. This is intentional engine infrastructure and not automatically a problem. Do not replace it with `std::shared_ptr` without a dedicated design pass.

### `RenderScene::Sort()` repeated calls

The code looks suspicious from the call sites, but `m_needsSort` prevents repeated sort work after the first pass. Treat it as a future render-graph cleanup, not an immediate bug.

### Editor localization service in runtime

`CEngine` only allocates and exposes localization under `JBRO_EDITOR`. That part matches the rule that localization is editor-only. The issue is the unconditional post-build copy in `Application.vcxproj`, not the service lifetime.

## Recommended Order

1. Remove or gate pack materialization, and validate release packs do not need file extraction.
2. Replace raw-source pack entries with cooked payloads and split runtime/debug metadata.
3. Collapse binary manifest writer duplication into one authoritative writer or add round-trip tests.
4. Gate legacy YAML/loose scene fallback behind debug/development.
5. Condition localization copy on editor builds only.
6. Add Vulkan descriptor caching after correctness gates are stable.
7. Cache SceneView tight alpha bounds.
8. Clean stale JSON manifest writer and project YAML indentation during related work.

## Verification Performed

- `rg --files` scan over the requested source roots, excluding build artifacts and third-party code.
- Broad suspicious-pattern scans for TODO/stub/fallback/raw file IO/delete/new/thread/render/create-buffer/localization/build-manifest references.
- Targeted reads of:
  - `Application/Application.cpp`
  - `Application/Application.vcxproj`
  - `Application/Editor/Build/GameBuildManager.cpp`
  - `Application/Editor/Main/BuildSettingsWindow.cpp`
  - `Application/Editor/Main/SceneView/SceneViewEditContext.cpp`
  - `BuildScripts/BuildGame.ps1`
  - `Engine/Core/Asset/AssetManager.cpp`
  - `Engine/Core/Asset/AssetPackage.cpp`
  - `Engine/Core/Build/BuildManifest.cpp`
  - `Engine/Core/Engine.cpp`
  - `Engine/Core/Renderer/Forward2DRenderer.cpp`
  - `Engine/Core/Renderer/RenderScene.cpp`
  - `Engine/Core/RHI/Vulkan/VulkanCommandContext.cpp`
  - `Engine/Core/RHI/WebGPU/WebGPUCommandContext.cpp`
  - `Engine/GameFramework/Scene/SceneSerializer.cpp`

## Review Notes

- 코드를 읽었고: 빌드 산출, manifest, pack reader/writer, runtime startup, render path를 각각 소스 기준으로 확인했다.
- 생각했고: 현재 위험은 단일 파일 버그보다 "빌드는 됐지만 계약이 다르게 해석되는" 지점에 몰려 있다.
- 반례를 찾았고: 일부 의심 항목은 실제로 guard가 있거나 editor-only라 즉시 버그로 분류하지 않았다.
- 문서화했다: 즉시 수정할 항목과 나중에 구조화할 항목을 severity와 추천 순서로 분리했다.
