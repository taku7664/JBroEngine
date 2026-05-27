#pragma once

#include "Engine/Core/Asset/AssetTypes.h"

class CAssetPath final
{
public:
	static bool NormalizeRelativePath(const char* path, std::string& outPath);
	static bool IsValidRelativePath(const char* path);
	static std::string MakeMetaPath(const char* normalizedPath);
	static std::string StripMetaExtension(const char* normalizedMetaPath);
	static std::string GetDisplayNameFromPath(const char* normalizedPath);
	static AssetGuid GenerateAssetGuid();
	static bool IsMetaPath(const char* path);
	static const char* GetMetaExtension();
	static const char* GetLegacyMetaExtension();

private:
	static bool HasDrivePrefix(const std::string& path);
};
