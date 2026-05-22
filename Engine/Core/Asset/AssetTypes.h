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
	Texture,
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
};

struct AssetLoadDesc
{
	AssetGuid Guid = INVALID_ASSET_GUID;
	EAssetType Type = EAssetType::Unknown;
	const char* Path = nullptr;
	const char* ResolvedPath = nullptr;
	const AssetMetaData* MetaData = nullptr;
};

struct AssetManagerDesc
{
	const char* AssetRootPath = "Assets";
};

struct AssetImportDesc
{
	EAssetType Type = EAssetType::Unknown;
	const char* Path = nullptr;
	const char* DisplayName = nullptr;
	const char* Importer = nullptr;
};

struct AssetRegistrySnapshot
{
	std::vector<AssetMetaData> Assets;
};

struct AssetPackageBuildDesc
{
	const char* OutputManifestPath = nullptr;
	const char* OutputBlobPath = nullptr;
};
