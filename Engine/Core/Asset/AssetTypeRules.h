#pragma once

#include "Core/Asset/AssetTypes.h"

#include <string>

class CAssetTypeRules final
{
public:
	// Extension-based detection is a bootstrap/fallback path for files that do not
	// have registered .Jmeta yet. Once metadata exists, AssetMetaData::Type is the
	// source of truth.
	static EAssetType DetectTypeFromPath(const File::Path& path);
	static EAssetType ResolveType(EAssetType declaredType, const File::Path& path);
	static bool IsAssignableTo(EAssetType expectedType, EAssetType declaredType, const File::Path& path);
	static bool IsExtensionAllowed(EAssetType type, const File::Path& path);
	static std::string GetAllowedExtensionsText(EAssetType type);
	static EAssetType ParseTypeName(const std::string& name);
	static const char* GetTypeName(EAssetType type);
};
