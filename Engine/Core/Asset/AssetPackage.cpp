#include "pch.h"
#include "AssetPackage.h"

#include "Core/Asset/AssetMetaFile.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

namespace
{
	constexpr std::array<char, 8> PACK_MAGIC = { 'J', 'B', 'P', 'A', 'C', 'K', '1', '\0' };
	constexpr std::uint32_t PACK_VERSION = 1;
	constexpr std::uint32_t HEADER_SIZE = 72;
	constexpr std::uint64_t FNV_OFFSET = 14695981039346656037ull;
	constexpr std::uint64_t FNV_PRIME = 1099511628211ull;
	constexpr std::uint64_t PACK_CRYPT_KEY = 0x9E3779B97F4A7C15ull;

	struct PackHeader
	{
		std::array<char, 8> Magic = PACK_MAGIC;
		std::uint32_t Version = PACK_VERSION;
		std::uint32_t HeaderSize = HEADER_SIZE;
		std::uint32_t Flags = 0;
		std::uint32_t EntryCount = 0;
		std::uint64_t PayloadOffset = 0;
		std::uint64_t PayloadSize = 0;
		std::uint64_t IndexOffset = 0;
		std::uint64_t IndexSize = 0;
		std::uint64_t IndexHash = 0;
		std::uint64_t PayloadHash = 0;
	};

	std::uint64_t HashBytes(const std::vector<std::uint8_t>& bytes)
	{
		std::uint64_t hash = FNV_OFFSET;
		for (std::uint8_t byte : bytes)
		{
			hash ^= byte;
			hash *= FNV_PRIME;
		}
		hash ^= static_cast<std::uint64_t>(bytes.size());
		hash *= FNV_PRIME;
		return 0 == hash ? FNV_OFFSET : hash;
	}

	void HashAppend(std::uint64_t& hash, const std::uint8_t* bytes, std::size_t size)
	{
		for (std::size_t i = 0; i < size; ++i)
		{
			hash ^= bytes[i];
			hash *= FNV_PRIME;
		}
	}

	void CryptBytes(std::vector<std::uint8_t>& bytes, std::uint64_t seed)
	{
		std::uint64_t state = seed ^ PACK_CRYPT_KEY;
		for (std::size_t i = 0; i < bytes.size(); ++i)
		{
			state ^= state << 13;
			state ^= state >> 7;
			state ^= state << 17;
			bytes[i] ^= static_cast<std::uint8_t>((state >> ((i & 7) * 8)) & 0xFF);
		}
	}

	template<typename T>
	void WritePod(std::ostream& stream, const T& value)
	{
		stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
	}

	template<typename T>
	bool ReadPod(std::istream& stream, T& value)
	{
		stream.read(reinterpret_cast<char*>(&value), sizeof(T));
		return static_cast<bool>(stream);
	}

	void WriteString(std::ostream& stream, const std::string& value)
	{
		const std::uint32_t size = static_cast<std::uint32_t>(value.size());
		WritePod(stream, size);
		if (0 != size)
		{
			stream.write(value.data(), size);
		}
	}

	bool ReadString(std::istream& stream, std::string& value)
	{
		std::uint32_t size = 0;
		if (false == ReadPod(stream, size))
		{
			return false;
		}
		value.clear();
		if (0 == size)
		{
			return true;
		}
		value.resize(size);
		stream.read(value.data(), size);
		return static_cast<bool>(stream);
	}

	bool WriteHeader(std::ostream& stream, const PackHeader& header)
	{
		stream.seekp(0, std::ios::beg);
		stream.write(header.Magic.data(), header.Magic.size());
		WritePod(stream, header.Version);
		WritePod(stream, header.HeaderSize);
		WritePod(stream, header.Flags);
		WritePod(stream, header.EntryCount);
		WritePod(stream, header.PayloadOffset);
		WritePod(stream, header.PayloadSize);
		WritePod(stream, header.IndexOffset);
		WritePod(stream, header.IndexSize);
		WritePod(stream, header.IndexHash);
		WritePod(stream, header.PayloadHash);
		return static_cast<bool>(stream);
	}

	bool ReadHeader(std::istream& stream, PackHeader& header)
	{
		stream.seekg(0, std::ios::beg);
		stream.read(header.Magic.data(), header.Magic.size());
		return static_cast<bool>(stream)
			&& ReadPod(stream, header.Version)
			&& ReadPod(stream, header.HeaderSize)
			&& ReadPod(stream, header.Flags)
			&& ReadPod(stream, header.EntryCount)
			&& ReadPod(stream, header.PayloadOffset)
			&& ReadPod(stream, header.PayloadSize)
			&& ReadPod(stream, header.IndexOffset)
			&& ReadPod(stream, header.IndexSize)
			&& ReadPod(stream, header.IndexHash)
			&& ReadPod(stream, header.PayloadHash);
	}

	bool ReadFileBytes(const File::Path& path, std::vector<std::uint8_t>& outBytes)
	{
		outBytes.clear();
		std::ifstream file(path, std::ios::binary);
		if (false == file.is_open())
		{
			return false;
		}

		file.seekg(0, std::ios::end);
		const std::streamoff size = file.tellg();
		if (size < 0)
		{
			return false;
		}

		file.seekg(0, std::ios::beg);
		outBytes.resize(static_cast<std::size_t>(size));
		if (false == outBytes.empty())
		{
			file.read(reinterpret_cast<char*>(outBytes.data()), size);
		}
		return static_cast<bool>(file) || file.eof();
	}

	std::vector<std::uint8_t> SerializeIndex(const std::vector<AssetRecord>& records)
	{
		std::ostringstream stream(std::ios::binary);
		const std::uint32_t indexVersion = 2;
		const std::uint64_t count = static_cast<std::uint64_t>(records.size());
		WritePod(stream, indexVersion);
		WritePod(stream, count);
		for (const AssetRecord& record : records)
		{
			WriteString(stream, record.Guid.generic_string());
			WriteString(stream, record.EntryLocator.Value);
			WritePod(stream, static_cast<std::uint32_t>(record.Type));
			WritePod(stream, static_cast<std::uint32_t>(record.PayloadType));
			WritePod(stream, record.Version);
			WritePod(stream, record.Flags);
			WritePod(stream, record.Offset);
			WritePod(stream, record.StoredSize);
			WritePod(stream, record.UncompressedSize);
			WritePod(stream, record.PayloadHash);
			WriteString(stream, record.PackId);
			WriteString(stream, record.ImportOptionsYaml);
		}

		const std::string text = stream.str();
		return std::vector<std::uint8_t>(text.begin(), text.end());
	}

	bool DeserializeIndex(const std::vector<std::uint8_t>& bytes, std::vector<AssetRecord>& outRecords)
	{
		outRecords.clear();
		std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		std::istringstream stream(text, std::ios::binary);
		std::uint32_t indexVersion = 0;
		std::uint64_t count = 0;
		if (false == ReadPod(stream, indexVersion)
			|| (1 != indexVersion && 2 != indexVersion)
			|| false == ReadPod(stream, count))
		{
			return false;
		}

		outRecords.reserve(static_cast<std::size_t>(count));
		for (std::uint64_t i = 0; i < count; ++i)
		{
			AssetRecord record;
			std::string guid;
			std::uint32_t type = 0;
			std::uint32_t payloadType = 0;
			if (false == ReadString(stream, guid)
				|| false == ReadString(stream, record.EntryLocator.Value)
				|| false == ReadPod(stream, type)
				|| false == ReadPod(stream, payloadType)
				|| false == ReadPod(stream, record.Version)
				|| false == ReadPod(stream, record.Flags)
				|| false == ReadPod(stream, record.Offset)
				|| false == ReadPod(stream, record.StoredSize)
				|| false == ReadPod(stream, record.UncompressedSize)
				|| false == ReadPod(stream, record.PayloadHash)
				|| false == ReadString(stream, record.PackId))
			{
				return false;
			}
			if (1 == indexVersion)
			{
				std::string ignoredImporter;
				std::string ignoredSourceExtension;
				if (false == ReadString(stream, ignoredImporter)
					|| false == ReadString(stream, record.ImportOptionsYaml)
					|| false == ReadString(stream, ignoredSourceExtension))
				{
					return false;
				}
			}
			else if (false == ReadString(stream, record.ImportOptionsYaml))
			{
				return false;
			}
			record.Guid = File::Guid(guid);
			record.Type = static_cast<EAssetType>(type);
			record.PayloadType = static_cast<EAssetPayloadType>(payloadType);
			if (record.Guid.IsNull() || record.EntryLocator.IsEmpty() || EAssetType::Unknown == record.Type)
			{
				return false;
			}
			outRecords.push_back(std::move(record));
		}
		return true;
	}
}

bool CAssetPackWriter::Write(const File::Path& packPath, const std::vector<AssetPackageBuildEntry>& entries)
{
	if (packPath.empty())
	{
		return false;
	}

	std::vector<AssetPackageBuildEntry> sortedEntries = entries;
	std::sort(sortedEntries.begin(), sortedEntries.end(),
		[](const AssetPackageBuildEntry& lhs, const AssetPackageBuildEntry& rhs)
		{
			return lhs.MetaData.Guid.generic_string() < rhs.MetaData.Guid.generic_string();
		});

	const std::filesystem::path path(packPath);
	if (path.has_parent_path())
	{
		std::error_code errorCode;
		std::filesystem::create_directories(path.parent_path(), errorCode);
		if (errorCode)
		{
			return false;
		}
	}

	std::ofstream file(packPath, std::ios::binary | std::ios::trunc);
	if (false == file.is_open())
	{
		return false;
	}

	PackHeader header;
	file.seekp(HEADER_SIZE, std::ios::beg);
	header.PayloadOffset = HEADER_SIZE;

	std::uint64_t payloadHash = FNV_OFFSET;
	std::vector<AssetRecord> records;
	records.reserve(sortedEntries.size());

	for (const AssetPackageBuildEntry& entry : sortedEntries)
	{
		if (entry.MetaData.Guid.IsNull() || EAssetType::Unknown == entry.MetaData.Type || entry.SourcePath.empty())
		{
			return false;
		}

		std::vector<std::uint8_t> payload;
		if (false == ReadFileBytes(entry.SourcePath, payload))
		{
			return false;
		}

		AssetRecord record;
		record.Guid = entry.MetaData.Guid;
		record.Type = entry.MetaData.Type;
		record.PayloadType = EAssetPayloadType::RawSource;
		record.EntryLocator.Value = entry.MetaData.Guid.generic_string();
		record.PackId = packPath.stem().generic_string();
		record.ImportOptionsYaml = entry.MetaData.ImportOptionsYaml;
		record.Version = entry.MetaData.Version;
		record.Flags = 0;
		record.Offset = static_cast<std::uint64_t>(file.tellp());
		record.StoredSize = static_cast<std::uint64_t>(payload.size());
		record.UncompressedSize = static_cast<std::uint64_t>(payload.size());
		record.PayloadHash = HashBytes(payload);

		const std::vector<std::uint8_t> plainPayload = payload;
		CryptBytes(payload, record.PayloadHash ^ record.Offset);
		record.Flags = static_cast<std::uint32_t>(EAssetPackageEntryFlags::Encrypted);

		if (false == payload.empty())
		{
			file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
			HashAppend(payloadHash, plainPayload.data(), plainPayload.size());
		}
		if (false == static_cast<bool>(file))
		{
			return false;
		}

		records.push_back(std::move(record));
	}

	header.PayloadSize = static_cast<std::uint64_t>(file.tellp()) - header.PayloadOffset;
	header.PayloadHash = payloadHash;
	header.IndexOffset = static_cast<std::uint64_t>(file.tellp());
	std::vector<std::uint8_t> indexBytes = SerializeIndex(records);
	header.IndexSize = static_cast<std::uint64_t>(indexBytes.size());
	header.IndexHash = HashBytes(indexBytes);
	header.EntryCount = static_cast<std::uint32_t>(records.size());
	CryptBytes(indexBytes, header.PayloadHash ^ header.EntryCount);
	if (false == indexBytes.empty())
	{
		file.write(reinterpret_cast<const char*>(indexBytes.data()), static_cast<std::streamsize>(indexBytes.size()));
	}

	return WriteHeader(file, header);
}

CAssetPackReader::~CAssetPackReader()
{
	Close();
}

bool CAssetPackReader::Open(const File::Path& packPath)
{
	Close();
	if (packPath.empty())
	{
		return false;
	}

	std::ifstream file(packPath, std::ios::binary);
	if (false == file.is_open())
	{
		return false;
	}

	PackHeader header;
	if (false == ReadHeader(file, header)
		|| header.Magic != PACK_MAGIC
		|| PACK_VERSION != header.Version
		|| HEADER_SIZE != header.HeaderSize
		|| 0 == header.IndexOffset
		|| 0 == header.IndexSize)
	{
		return false;
	}

	file.seekg(0, std::ios::end);
	const std::uint64_t fileSize = static_cast<std::uint64_t>(file.tellg());
	if (header.IndexOffset + header.IndexSize > fileSize
		|| header.PayloadOffset + header.PayloadSize > fileSize)
	{
		return false;
	}

	std::vector<std::uint8_t> indexBytes(static_cast<std::size_t>(header.IndexSize));
	file.seekg(static_cast<std::streamoff>(header.IndexOffset), std::ios::beg);
	file.read(reinterpret_cast<char*>(indexBytes.data()), static_cast<std::streamsize>(indexBytes.size()));
	if (false == static_cast<bool>(file))
	{
		return false;
	}
	CryptBytes(indexBytes, header.PayloadHash ^ header.EntryCount);
	if (HashBytes(indexBytes) != header.IndexHash)
	{
		return false;
	}

	std::vector<AssetRecord> records;
	if (false == DeserializeIndex(indexBytes, records) || records.size() != header.EntryCount)
	{
		return false;
	}

	for (const AssetRecord& record : records)
	{
		if (record.Offset < header.PayloadOffset
			|| record.Offset + record.StoredSize > header.PayloadOffset + header.PayloadSize)
		{
			return false;
		}
		if (m_records.contains(record.Guid))
		{
			return false;
		}
		m_records.emplace(record.Guid, record);
	}

	m_packPath = packPath;
	return true;
}

void CAssetPackReader::Close()
{
	m_records.clear();
	m_packPath.clear();
}

const AssetRecord* CAssetPackReader::FindRecord(const AssetGuid& guid) const
{
	auto it = m_records.find(guid);
	return it == m_records.end() ? nullptr : &it->second;
}

void CAssetPackReader::BuildRecords(std::vector<AssetRecord>& outRecords) const
{
	outRecords.clear();
	outRecords.reserve(m_records.size());
	for (const auto& pair : m_records)
	{
		outRecords.push_back(pair.second);
	}
}

bool CAssetPackReader::ReadPayload(const AssetGuid& guid, std::vector<std::uint8_t>& outPayload, const AssetRecord** outRecord) const
{
	outPayload.clear();
	const AssetRecord* record = FindRecord(guid);
	if (nullptr == record || m_packPath.empty())
	{
		return false;
	}

	std::ifstream file(m_packPath, std::ios::binary);
	if (false == file.is_open())
	{
		return false;
	}

	outPayload.resize(static_cast<std::size_t>(record->StoredSize));
	file.seekg(static_cast<std::streamoff>(record->Offset), std::ios::beg);
	if (false == outPayload.empty())
	{
		file.read(reinterpret_cast<char*>(outPayload.data()), static_cast<std::streamsize>(outPayload.size()));
	}

	if (0 != (record->Flags & static_cast<std::uint32_t>(EAssetPackageEntryFlags::Encrypted)))
	{
		CryptBytes(outPayload, record->PayloadHash ^ record->Offset);
	}

	if ((false == static_cast<bool>(file) && false == file.eof()) || HashBytes(outPayload) != record->PayloadHash)
	{
		outPayload.clear();
		return false;
	}

	if (nullptr != outRecord)
	{
		*outRecord = record;
	}
	return true;
}
