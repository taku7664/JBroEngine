#include "pch.h"
#include "MaterialAsset.h"

#include "yaml-cpp/yaml.h"

namespace
{
	template<typename T>
	T ReadOption(const YAML::Node& node, const char* key, const T& defaultValue)
	{
		if (!node[key])
		{
			return defaultValue;
		}
		try
		{
			return node[key].as<T>();
		}
		catch (const YAML::Exception&)
		{
			return defaultValue;
		}
	}
}

CMaterialAsset::CMaterialAsset(const AssetMetaData& metaData)
	: m_metaData(metaData)
{
	SetImportOptions(CMaterialImportOptions::FromYaml(metaData.ImportOptionsYaml));
}

AssetGuid CMaterialAsset::GetGuid() const
{
	return m_metaData.Guid;
}

EAssetType CMaterialAsset::GetAssetType() const
{
	return m_metaData.Type;
}

EAssetLoadState CMaterialAsset::GetLoadState() const
{
	return m_loadState;
}

const AssetMetaData& CMaterialAsset::GetMetaData() const
{
	return m_metaData;
}

void CMaterialAsset::ApplyImportOptions(const std::string& importOptionsYaml)
{
	SetImportOptions(CMaterialImportOptions::FromYaml(importOptionsYaml));
}

const MaterialImportOptions& CMaterialAsset::GetImportOptions() const
{
	return m_importOptions;
}

void CMaterialAsset::SetImportOptions(const MaterialImportOptions& options)
{
	m_importOptions = options;
	Queue = options.Queue;
}

MaterialImportOptions CMaterialImportOptions::FromYaml(const std::string& yamlText)
{
	MaterialImportOptions options;
	if (yamlText.empty())
	{
		return options;
	}

	YAML::Node root;
	try
	{
		root = YAML::Load(yamlText);
	}
	catch (const YAML::Exception&)
	{
		return options;
	}

	if (!root || false == root.IsMap())
	{
		return options;
	}

	const YAML::Node materialNode = root["Material"] ? root["Material"] : root;
	options.Queue = ParseQueue(ReadOption<std::string>(materialNode, "Queue", QueueToString(options.Queue)));
	return options;
}

std::string CMaterialImportOptions::ToYaml(const MaterialImportOptions& options)
{
	YAML::Emitter emitter;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << "Material" << YAML::Value;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << "Queue" << YAML::Value << QueueToString(options.Queue);
	emitter << YAML::EndMap;
	emitter << YAML::EndMap;
	return emitter.c_str();
}

const char* CMaterialImportOptions::QueueToString(ERenderQueue queue)
{
	switch (queue)
	{
	case ERenderQueue::Background:
		return "Background";
	case ERenderQueue::Opaque:
		return "Opaque";
	case ERenderQueue::Overlay:
		return "Overlay";
	case ERenderQueue::Transparent:
	default:
		return "Transparent";
	}
}

ERenderQueue CMaterialImportOptions::ParseQueue(const std::string& text)
{
	if ("Background" == text)
	{
		return ERenderQueue::Background;
	}
	if ("Opaque" == text)
	{
		return ERenderQueue::Opaque;
	}
	if ("Overlay" == text)
	{
		return ERenderQueue::Overlay;
	}
	return ERenderQueue::Transparent;
}

EAssetType CMaterialAssetLoader::GetSupportedType() const
{
	return EAssetType::Material;
}

bool CMaterialAssetLoader::CanLoad(const AssetLoadDesc& desc) const
{
	return EAssetType::Material == desc.Type && nullptr != desc.MetaData;
}

OwnerPtr<IAsset> CMaterialAssetLoader::Load(const AssetLoadDesc& desc)
{
	if (false == CanLoad(desc))
	{
		return nullptr;
	}

	return MakeOwnerPtr<CMaterialAsset>(*desc.MetaData);
}

void CMaterialAssetLoader::Unload(IAsset& asset)
{
	(void)asset;
}
