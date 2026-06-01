#pragma once

#include "Core/Asset/AssetTypes.h"

#include <cstdint>
#include <unordered_map>

enum class EAssetPayloadType : std::uint32_t
{
	Unknown = 0,
	RawSource,
	CookedTexture,
	CookedAudio,
	SerializedScene,
	SerializedPrefab,
	BinaryBlob
};

enum class EAssetPackageEntryFlags : std::uint32_t
{
	None = 0,
	Compressed = 1 << 0,
	Encrypted = 1 << 1,
	Streamed = 1 << 2,
	MemoryOnly = 1 << 3,
	DebugNamePresent = 1 << 4
};

struct AssetEntryLocator
{
	std::string Value;

	bool IsEmpty() const { return Value.empty(); }
};

struct AssetRecord
{
	AssetGuid Guid = INVALID_ASSET_GUID;
	EAssetType Type = EAssetType::Unknown;
	EAssetPayloadType PayloadType = EAssetPayloadType::Unknown;
	AssetEntryLocator EntryLocator;
	std::string PackId;
	std::string Importer;
	std::string ImportOptionsYaml;
	std::string SourceExtension;
	std::uint32_t Version = 1;
	std::uint32_t Flags = 0;
	std::uint64_t Offset = 0;
	std::uint64_t StoredSize = 0;
	std::uint64_t UncompressedSize = 0;
	std::uint64_t PayloadHash = 0;
};

struct AssetPackageBuildEntry
{
	AssetMetaData MetaData;
	File::Path SourcePath;
};

class CAssetPackWriter final
{
public:
	static bool Write(const File::Path& packPath, const std::vector<AssetPackageBuildEntry>& entries);
};

class CAssetPackReader final
{
public:
	CAssetPackReader() = default;
	~CAssetPackReader();

	CAssetPackReader(const CAssetPackReader&) = delete;
	CAssetPackReader& operator=(const CAssetPackReader&) = delete;

	bool Open(const File::Path& packPath);
	void Close();

	const AssetRecord* FindRecord(const AssetGuid& guid) const;
	void BuildRecords(std::vector<AssetRecord>& outRecords) const;
	bool ReadPayload(const AssetGuid& guid, std::vector<std::uint8_t>& outPayload, const AssetRecord** outRecord = nullptr) const;
	bool MaterializePayload(const AssetGuid& guid, File::Path& outPath) const;

private:
	File::Path MakeCachePath(const AssetRecord& record) const;

private:
	File::Path m_packPath;
	File::Path m_cacheRoot;
	std::unordered_map<AssetGuid, AssetRecord> m_records;
};
