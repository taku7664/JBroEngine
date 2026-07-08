#pragma once

#include "Core/Asset/AssetTypes.h"

#include <string>

class CAssetTypeRules final
{
public:
	static EAssetType DetectTypeFromPath(const File::Path& path);
	static EAssetType ResolveType(EAssetType declaredType, const File::Path& path);
	static bool IsAssignableTo(EAssetType expectedType, EAssetType declaredType, const File::Path& path);
	static bool IsExtensionAllowed(EAssetType type, const File::Path& path);
	static std::string GetAllowedExtensionsText(EAssetType type);
	static const char* GetTypeName(EAssetType type);
};
