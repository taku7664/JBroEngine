#pragma once

#include "Engine/Core/Asset/AssetTypes.h"

class CAssetPath final
{
public:
	static bool NormalizeRelativePath(const char* path, std::string& outPath);
	// Registry/Manager 키용 정규화.
	// 절대경로(드라이브 접두사 포함) → 슬래시 정규화 + 소문자만 적용해 그대로 사용.
	// 상대경로 → NormalizeRelativePath 규칙 적용.
	// path-based 등록(.Jmeta 없이 외부 절대경로 자산)을 위해 추가됨.
	static bool NormalizeAssetKey(const char* path, std::string& outPath);
	static bool IsAbsoluteAssetPath(const char* path);
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
