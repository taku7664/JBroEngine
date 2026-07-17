#pragma once

// File::Guid / File::NULL_GUID 정의 — SDK 사용자가 PCH 없이도 빌드 가능하도록
// transitive 가 아닌 직접 include 로 보장.
// 경로는 Engine($(SolutionDir) 기준) / SDK(SDK/Include 기준) 양쪽에서 모두 통하도록
// "Utillity/..." prefix 를 사용.
#include "Utillity/File/FilePath.h"

#include <cstdint>
#include <string>
#include <vector>

using AssetGuid = File::Guid;

inline const AssetGuid INVALID_ASSET_GUID = File::NULL_GUID;

enum class EAssetType
{
	Unknown,
	Sprite,
	Mesh,
	Material,
	Shader,
	Canvas,      // .jcanvas — 정수값(5)은 그대로 둘 것(BuildGame.ps1 이 하드코딩).
	             // 리네임 이전에 저장된 `.jmeta` 는 "Scene" 을 적고 있고, ParseType 이 계속 받아 준다.
	Prefab,
	Script,
	Audio,
	Custom,
	// Custom 뒤에 추가 — 기존 타입의 정수값(빌드 스크립트/직렬화) 보존.
	AudioEffect,
	FontFace,
	FontFamily,
	Layer        // .jlayer — 레이어 파일(컴포짓 속성 + 소속 오브젝트)
};

enum class EAssetLoadState
{
	Unloaded,
	Loading,
	Loaded,
	Failed
};

struct AssetMetaData
{
	AssetGuid Guid = INVALID_ASSET_GUID;
	EAssetType Type = EAssetType::Unknown;
	std::uint32_t Version = 1;
	// Path/MetaPath are transient registry locators filled while scanning AssetRoot.
	// .jmeta stores stable Guid/import data only, not filesystem locator fields.
	File::Path Path;
	File::Path MetaPath;
	std::string DisplayName;
	std::string Importer;
	std::string ImportOptionsYaml;

	// true 면 UnloadNonPersistentAssets() 시 보존된다.
	// 등록 방식(.jmeta / path-only) 과는 직교 — 어느 방식이든 플래그로 라이프사이클 제어.
	bool IsPersistent = false;
};

struct AssetLoadDesc
{
	AssetGuid Guid = INVALID_ASSET_GUID;
	EAssetType Type = EAssetType::Unknown;
	File::Path Path;
	File::Path ResolvedPath;
	const AssetMetaData* MetaData = nullptr;
	std::vector<std::uint8_t> MemoryPayload;

	bool HasMemoryPayload() const { return false == MemoryPayload.empty(); }
};

struct AssetManagerDesc
{
	File::Path AssetRootPath = "Assets";
};

struct AssetImportDesc
{
	EAssetType Type = EAssetType::Unknown;
	File::Path Path;
	const char* DisplayName = nullptr;
	const char* Importer = nullptr;
};

struct AssetRegistrySnapshot
{
	std::vector<AssetMetaData> Assets;
};

struct AssetPackageBuildDesc
{
	File::Path OutputBlobPath;
	// 패키지에 포함할 자산 GUID 집합(참조 기반 패키징). 빌드 파이프라인이 빌드 캔버스 →
	// 참조 자산 → 프리팹/폰트 전이 의존 → Always Include 를 전개해 채운다.
	// 비어 있으면 패키징은 실패로 처리한다(전체 레지스트리 덤프 금지).
	std::vector<AssetGuid> IncludedAssets;
};
