#pragma once

#include "Core/Asset/AssetTypes.h"

#include <iosfwd>

class CAssetMetaFile final
{
public:
	static bool Load(const char* path, AssetMetaData& outMetaData);
	static bool Save(const char* path, const AssetMetaData& metaData);

private:
	static bool LoadYaml(std::istream& stream, AssetMetaData& outMetaData);
	static const char* ToString(EAssetType type);
	static EAssetType ParseType(const std::string& value);
};
