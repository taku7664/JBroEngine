#include "pch.h"
#include "AssetMetaFile.h"

#include "yaml-cpp/yaml.h"

#include <sstream>

namespace
{
	constexpr const char* YAML_KEY_VERSION = "Version";
	constexpr const char* YAML_KEY_GUID = "Guid";
	constexpr const char* YAML_KEY_LEGACY_ID = "Id";
	constexpr const char* YAML_KEY_TYPE = "Type";
	constexpr const char* YAML_KEY_DISPLAY_NAME = "DisplayName";
	constexpr const char* YAML_KEY_IMPORTER = "Importer";
	constexpr const char* YAML_KEY_IMPORT_OPTIONS = "ImportOptions";
	constexpr const char* LEGACY_HEADER = "JBroAssetMeta\t1";

	template<typename T>
	bool ReadYamlValue(const YAML::Node& node, const char* key, T& outValue)
	{
		if (!node[key])
		{
			return true;
		}

		try
		{
			outValue = node[key].as<T>();
			return true;
		}
		catch (const YAML::Exception&)
		{
			return false;
		}
	}
}

bool CAssetMetaFile::Load(const File::Path& path, AssetMetaData& outMetaData)
{
	if (path.empty())
	{
		return false;
	}

	std::ifstream file(path);
	if (false == file.is_open())
	{
		return false;
	}

	std::string line;
	if (false == static_cast<bool>(std::getline(file, line)))
	{
		return false;
	}

	file.clear();
	file.seekg(0, std::ios::beg);

	if (line != LEGACY_HEADER)
	{
		return LoadYaml(file, outMetaData);
	}

	AssetMetaData metaData;
	while (std::getline(file, line))
	{
		std::istringstream lineStream(line);
		std::string key;
		std::string value;
		std::getline(lineStream, key, '\t');
		std::getline(lineStream, value);

		if (key == "Id")
		{
			metaData.Guid = File::Guid(value);
		}
		else if (key == "Type")
		{
			metaData.Type = ParseType(value);
		}
		else if (key == "Version")
		{
			try
			{
				metaData.Version = static_cast<std::uint32_t>(std::stoul(value));
			}
			catch (const std::exception&)
			{
				return false;
			}
		}
		else if (key == "DisplayName")
		{
			metaData.DisplayName = value;
		}
		else if (key == "Importer")
		{
			metaData.Importer = value;
		}
	}

	outMetaData = std::move(metaData);
	return true;
}

bool CAssetMetaFile::Save(const File::Path& path, const AssetMetaData& metaData)
{
	if (path.empty())
	{
		return false;
	}

	const std::filesystem::path filePath(path);
	if (filePath.has_parent_path())
	{
		std::error_code errorCode;
		std::filesystem::create_directories(filePath.parent_path(), errorCode);
	}

	std::ofstream file(path, std::ios::out | std::ios::trunc);
	if (false == file.is_open())
	{
		return false;
	}

	YAML::Emitter emitter;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << YAML_KEY_VERSION << YAML::Value << metaData.Version;
	emitter << YAML::Key << YAML_KEY_GUID << YAML::Value << metaData.Guid.generic_string();
	emitter << YAML::Key << YAML_KEY_TYPE << YAML::Value << ToString(metaData.Type);
	emitter << YAML::Key << YAML_KEY_DISPLAY_NAME << YAML::Value << metaData.DisplayName;
	emitter << YAML::Key << YAML_KEY_IMPORTER << YAML::Value << metaData.Importer;
	emitter << YAML::Key << YAML_KEY_IMPORT_OPTIONS << YAML::Value;
	if (metaData.ImportOptionsYaml.empty())
	{
		emitter << YAML::BeginMap << YAML::EndMap;
	}
	else
	{
		try
		{
			emitter << YAML::Load(metaData.ImportOptionsYaml);
		}
		catch (const YAML::Exception&)
		{
			emitter << YAML::BeginMap << YAML::EndMap;
		}
	}
	emitter << YAML::EndMap;

	file << emitter.c_str() << '\n';
	return true;
}

bool CAssetMetaFile::LoadYaml(std::istream& stream, AssetMetaData& outMetaData)
{
	YAML::Node root;
	try
	{
		root = YAML::Load(stream);
	}
	catch (const YAML::Exception&)
	{
		return false;
	}

	if (!root || false == root.IsMap())
	{
		return false;
	}

	AssetMetaData metaData;
	std::string guidText;
	if (root[YAML_KEY_GUID])
	{
		if (false == ReadYamlValue(root, YAML_KEY_GUID, guidText))
		{
			return false;
		}
	}
	else if (root[YAML_KEY_LEGACY_ID])
	{
		if (false == ReadYamlValue(root, YAML_KEY_LEGACY_ID, guidText))
		{
			return false;
		}
	}
	else
	{
		return false;
	}
	metaData.Guid = File::Guid(guidText);

	if (false == ReadYamlValue(root, YAML_KEY_VERSION, metaData.Version)
		|| false == ReadYamlValue(root, YAML_KEY_DISPLAY_NAME, metaData.DisplayName)
		|| false == ReadYamlValue(root, YAML_KEY_IMPORTER, metaData.Importer))
	{
		return false;
	}

	std::string typeText;
	if (false == ReadYamlValue(root, YAML_KEY_TYPE, typeText))
	{
		return false;
	}
	metaData.Type = ParseType(typeText);

	// 자가 복구 — 과거에 EAssetType::Audio 가 ToString/ParseType 분기에 빠져
	// 디스크에 "Type: Unknown" 으로 저장된 .Jmeta 가 있을 수 있다. Importer 문자열로
	// 추정 가능하면 Type 을 회복해 인스펙터 등 dispatch 가 정상 작동하도록.
	if (EAssetType::Unknown == metaData.Type && false == metaData.Importer.empty())
	{
		const EAssetType inferred = ParseType(metaData.Importer);
		if (EAssetType::Unknown != inferred)
		{
			metaData.Type = inferred;
		}
	}

	if (root[YAML_KEY_IMPORT_OPTIONS])
	{
		metaData.ImportOptionsYaml = YAML::Dump(root[YAML_KEY_IMPORT_OPTIONS]);
	}

	outMetaData = std::move(metaData);
	return true;
}

const char* CAssetMetaFile::ToString(EAssetType type)
{
	switch (type)
	{
	case EAssetType::Sprite:
		return "Sprite";
	case EAssetType::Mesh:
		return "Mesh";
	case EAssetType::Material:
		return "Material";
	case EAssetType::Shader:
		return "Shader";
	case EAssetType::Scene:
		return "Scene";
	case EAssetType::Prefab:
		return "Prefab";
	case EAssetType::Script:
		return "Script";
	case EAssetType::Audio:
		return "Audio";
	case EAssetType::Custom:
		return "Custom";
	default:
		return "Unknown";
	}
}

EAssetType CAssetMetaFile::ParseType(const std::string& value)
{
	// Texture 폐기 — 기존 .Jmeta 호환을 위해 Sprite 로 자동 매핑.
	if (value == "Texture") return EAssetType::Sprite;
	if (value == "Sprite") return EAssetType::Sprite;
	if (value == "Mesh") return EAssetType::Mesh;
	if (value == "Material") return EAssetType::Material;
	if (value == "Shader") return EAssetType::Shader;
	if (value == "Scene") return EAssetType::Scene;
	if (value == "Prefab") return EAssetType::Prefab;
	if (value == "Script") return EAssetType::Script;
	if (value == "Audio")  return EAssetType::Audio;
	if (value == "Custom") return EAssetType::Custom;
	return EAssetType::Unknown;
}
