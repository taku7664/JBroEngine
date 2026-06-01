#pragma once

#include "Utillity/File/FilePath.h"

#include <string>
#include <vector>

enum class EBuildAssetMountType
{
	Unknown,
	Loose,
	Pack
};

struct BuildAssetMount
{
	EBuildAssetMountType Type = EBuildAssetMountType::Unknown;
	File::Path Path;
	bool Required = true;
};

struct BuildManifest
{
	int Version = 0;
	std::string ProductName;
	std::string TargetPlatform;
	std::string Configuration;
	std::string StartupScene;
	std::string StartupSceneGuid;
	std::vector<std::string> BuildScenes;
	int ResolutionWidth = 0;
	int ResolutionHeight = 0;
	std::vector<BuildAssetMount> AssetMounts;
	std::string ScriptMode;
	std::string ScriptModule;
	std::string EngineVersion;
	std::string BuildTimeUtc;

	File::Path ManifestPath;
	File::Path ContentRootPath;
	File::Path PackageRootPath;
};

class CBuildManifestLoader final
{
public:
	static bool FindDefaultManifest(File::Path& outManifestPath);
	static bool LoadFromFile(const File::Path& manifestPath, BuildManifest& outManifest, std::string* outError = nullptr);
	static bool WriteBinaryFile(const File::Path& manifestPath, const BuildManifest& manifest, std::string* outError = nullptr);
	static File::Path ResolvePackagePath(const BuildManifest& manifest, const File::Path& path);
};
