#pragma once

#include "Core/Asset/AssetTypes.h"

#include <iosfwd>

class CAssetMetaFile final
{
public:
	static bool Load(const File::Path& path, AssetMetaData& outMetaData);
	static bool Save(const File::Path& path, const AssetMetaData& metaData);

private:
	static bool LoadYaml(std::istream& stream, AssetMetaData& outMetaData);
	static const char* ToString(EAssetType type);
	static EAssetType ParseType(const std::string& value);
};
