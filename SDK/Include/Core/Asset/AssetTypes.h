#pragma once

#include "File/FilePath.h"

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
	Scene,
	Prefab,
	Script,
	Custom
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
	// .Jmeta stores stable Guid/import data only, not filesystem locator fields.
	File::Path Path;
	File::Path MetaPath;
	std::string DisplayName;
	std::string Importer;
	std::string ImportOptionsYaml;

	// true 면 UnloadNonPersistentAssets() 시 보존된다.
	// 등록 방식(.Jmeta / path-only) 과는 직교 — 어느 방식이든 플래그로 라이프사이클 제어.
	bool IsPersistent = false;
};

struct AssetLoadDesc
{
	AssetGuid Guid = INVALID_ASSET_GUID;
	EAssetType Type = EAssetType::Unknown;
	File::Path Path;
	File::Path ResolvedPath;
	const AssetMetaData* MetaData = nullptr;
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
	File::Path OutputManifestPath;
	File::Path OutputBlobPath;
};
