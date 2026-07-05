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
	// BuildScenes 와 인덱스 1:1 매칭되는 각 씬의 에셋 GUID. 릴리즈 런타임은 경로 대신 이 GUID 로
	// 씬 노드를 선로드한다(경로 폴백은 release 에서 금지). 비어 있거나 GUID 가 없는 항목은 스킵.
	std::vector<std::string> BuildSceneGuids;
	int ResolutionWidth = 0;
	int ResolutionHeight = 0;
	float PixelsPerUnit = 100.0f;
	std::vector<BuildAssetMount> AssetMounts;
	std::string ScriptMode;
	std::string ScriptModule;
	std::string Orientation; // "Landscape" / "Portrait" / "Auto"(또는 빈 문자열 = Auto)
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
