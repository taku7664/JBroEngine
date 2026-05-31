#include "pch.h"
#include "AssetDatabase.h"

#include "Core/Asset/AssetMetaFile.h"
#include "yaml-cpp/yaml.h"

#include <fstream>

namespace
{
	constexpr std::uint32_t ASSET_DB_VERSION = 1;
	constexpr std::uint64_t FNV_OFFSET_BASIS = 1469598103934665603ull;
	constexpr std::uint64_t FNV_PRIME        = 1099511628211ull;
}

bool CAssetDatabase::Load(const File::Path& dbFilePath)
{
	Clear();
	if (dbFilePath.empty())
	{
		return false;
	}

	std::error_code errorCode;
	if (false == std::filesystem::exists(dbFilePath, errorCode))
	{
		return true;   // 캐시 없음 — 정상(첫 로드).
	}

	YAML::Node root;
	try
	{
		root = YAML::LoadFile(dbFilePath.string());
	}
	catch (const YAML::Exception&)
	{
		return false;  // 손상 — 빈 상태로 진행.
	}

	if (!root || false == root.IsMap())
	{
		return false;
	}

	const YAML::Node assets = root["Assets"];
	if (assets && assets.IsSequence())
	{
		for (const YAML::Node& node : assets)
		{
			if (false == node.IsMap())
			{
				continue;
			}
			AssetDbEntry entry;
			entry.Guid = AssetGuid(node["Guid"].as<std::string>(""));
			if (entry.Guid.IsNull())
			{
				continue;
			}
			entry.RelativePath      = node["Path"].as<std::string>("");
			entry.ContentHash       = node["Hash"].as<std::uint64_t>(0);
			entry.FileSize          = node["Size"].as<std::uint64_t>(0);
			entry.ModifiedTime      = node["Mtime"].as<std::int64_t>(0);
			entry.Type              = CAssetMetaFile::ParseType(node["Type"].as<std::string>("Unknown"));
			entry.Version           = node["Version"].as<std::uint32_t>(1);
			entry.DisplayName       = node["DisplayName"].as<std::string>("");
			entry.Importer          = node["Importer"].as<std::string>("");
			entry.ImportOptionsYaml = node["ImportOptions"].as<std::string>("");
			m_byGuid.emplace(entry.Guid, std::move(entry));
		}
	}

	RebuildSecondaryIndexes();
	return true;
}

bool CAssetDatabase::Save(const File::Path& dbFilePath) const
{
	if (dbFilePath.empty())
	{
		return false;
	}

	const std::filesystem::path filePath(dbFilePath);
	if (filePath.has_parent_path())
	{
		std::error_code errorCode;
		std::filesystem::create_directories(filePath.parent_path(), errorCode);
	}

	YAML::Emitter emitter;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << "Version" << YAML::Value << ASSET_DB_VERSION;
	emitter << YAML::Key << "Assets" << YAML::Value << YAML::BeginSeq;
	for (const auto& pair : m_byGuid)
	{
		const AssetDbEntry& entry = pair.second;
		emitter << YAML::BeginMap;
		emitter << YAML::Key << "Guid"          << YAML::Value << entry.Guid.generic_string();
		emitter << YAML::Key << "Path"          << YAML::Value << entry.RelativePath;
		emitter << YAML::Key << "Hash"          << YAML::Value << entry.ContentHash;
		emitter << YAML::Key << "Size"          << YAML::Value << entry.FileSize;
		emitter << YAML::Key << "Mtime"         << YAML::Value << entry.ModifiedTime;
		emitter << YAML::Key << "Type"          << YAML::Value << CAssetMetaFile::ToString(entry.Type);
		emitter << YAML::Key << "Version"       << YAML::Value << entry.Version;
		emitter << YAML::Key << "DisplayName"   << YAML::Value << entry.DisplayName;
		emitter << YAML::Key << "Importer"      << YAML::Value << entry.Importer;
		emitter << YAML::Key << "ImportOptions" << YAML::Value << entry.ImportOptionsYaml;
		emitter << YAML::EndMap;
	}
	emitter << YAML::EndSeq;
	emitter << YAML::EndMap;

	std::ofstream file(dbFilePath, std::ios::out | std::ios::trunc);
	if (false == file.is_open())
	{
		return false;
	}
	file << emitter.c_str() << '\n';
	return true;
}

void CAssetDatabase::Clear()
{
	m_byGuid.clear();
	m_byPath.clear();
	m_byHash.clear();
}

const AssetDbEntry* CAssetDatabase::FindByGuid(const AssetGuid& guid) const
{
	auto it = m_byGuid.find(guid);
	return it != m_byGuid.end() ? &it->second : nullptr;
}

const AssetDbEntry* CAssetDatabase::FindByPath(const std::string& relativePath) const
{
	auto it = m_byPath.find(relativePath);
	if (it == m_byPath.end())
	{
		return nullptr;
	}
	return FindByGuid(it->second);
}

const AssetDbEntry* CAssetDatabase::FindByHash(std::uint64_t contentHash) const
{
	if (0 == contentHash)
	{
		return nullptr;
	}
	auto it = m_byHash.find(contentHash);
	if (it == m_byHash.end())
	{
		return nullptr;
	}
	return FindByGuid(it->second);
}

void CAssetDatabase::Upsert(const AssetDbEntry& entry)
{
	if (entry.Guid.IsNull())
	{
		return;
	}
	m_byGuid[entry.Guid] = entry;
	if (false == entry.RelativePath.empty())
	{
		m_byPath[entry.RelativePath] = entry.Guid;
	}
	if (0 != entry.ContentHash)
	{
		m_byHash[entry.ContentHash] = entry.Guid;
	}
}

void CAssetDatabase::RemoveByGuid(const AssetGuid& guid)
{
	auto it = m_byGuid.find(guid);
	if (it == m_byGuid.end())
	{
		return;
	}
	if (false == it->second.RelativePath.empty())
	{
		auto pathIt = m_byPath.find(it->second.RelativePath);
		if (pathIt != m_byPath.end() && pathIt->second == guid)
		{
			m_byPath.erase(pathIt);
		}
	}
	if (0 != it->second.ContentHash)
	{
		auto hashIt = m_byHash.find(it->second.ContentHash);
		if (hashIt != m_byHash.end() && hashIt->second == guid)
		{
			m_byHash.erase(hashIt);
		}
	}
	m_byGuid.erase(it);
}

void CAssetDatabase::RebuildSecondaryIndexes()
{
	m_byPath.clear();
	m_byHash.clear();
	for (const auto& pair : m_byGuid)
	{
		const AssetDbEntry& entry = pair.second;
		if (false == entry.RelativePath.empty())
		{
			m_byPath[entry.RelativePath] = entry.Guid;
		}
		if (0 != entry.ContentHash)
		{
			m_byHash[entry.ContentHash] = entry.Guid;
		}
	}
}

bool CAssetDatabase::QueryStat(const File::Path& absolutePath, std::uint64_t& outSize, std::int64_t& outMtime)
{
	std::error_code errorCode;
	const std::uintmax_t size = std::filesystem::file_size(absolutePath, errorCode);
	if (errorCode)
	{
		return false;
	}
	const std::filesystem::file_time_type mtime = std::filesystem::last_write_time(absolutePath, errorCode);
	if (errorCode)
	{
		return false;
	}
	outSize  = static_cast<std::uint64_t>(size);
	outMtime = static_cast<std::int64_t>(mtime.time_since_epoch().count());
	return true;
}

bool CAssetDatabase::HashFile(const File::Path& absolutePath, std::uint64_t& outHash, std::uint64_t& outSize, std::int64_t& outMtime)
{
	if (false == QueryStat(absolutePath, outSize, outMtime))
	{
		return false;
	}

	std::ifstream file(absolutePath, std::ios::binary);
	if (false == file.is_open())
	{
		return false;
	}

	std::uint64_t hash = FNV_OFFSET_BASIS;
	char buffer[64 * 1024];
	while (file.good())
	{
		file.read(buffer, sizeof(buffer));
		const std::streamsize read = file.gcount();
		for (std::streamsize i = 0; i < read; ++i)
		{
			hash ^= static_cast<std::uint8_t>(buffer[i]);
			hash *= FNV_PRIME;
		}
	}
	// 빈 파일도 0 이 아닌 안정적 해시를 갖도록 size 를 섞는다(0 은 "없음" 의미로 예약).
	hash ^= outSize;
	hash *= FNV_PRIME;
	if (0 == hash)
	{
		hash = FNV_OFFSET_BASIS;
	}
	outHash = hash;
	return true;
}
