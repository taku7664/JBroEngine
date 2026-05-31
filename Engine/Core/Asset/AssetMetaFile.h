#pragma once

#include "Core/Asset/AssetTypes.h"

#include <iosfwd>

class CAssetMetaFile final
{
public:
	static bool Load(const File::Path& path, AssetMetaData& outMetaData);
	static bool Save(const File::Path& path, const AssetMetaData& metaData);

	// EAssetType ↔ .Jmeta 문자열 변환(순수 유틸 — DB/리컨실 등에서 재사용).
	static const char* ToString(EAssetType type);
	static EAssetType ParseType(const std::string& value);

private:
	static bool LoadYaml(std::istream& stream, AssetMetaData& outMetaData);
};
