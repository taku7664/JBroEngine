#include "pch.h"
#include "FontAsset.h"

#include <algorithm>
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace
{
	std::vector<std::uint8_t> ReadBytes(const AssetLoadDesc& desc)
	{
		if (desc.HasMemoryPayload())
		{
			return desc.MemoryPayload;
		}
		std::ifstream stream(desc.ResolvedPath, std::ios::binary | std::ios::ate);
		if (false == stream.is_open())
		{
			return {};
		}
		const std::streamsize size = stream.tellg();
		if (size <= 0)
		{
			return {};
		}
		stream.seekg(0, std::ios::beg);
		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
		if (false == stream.read(reinterpret_cast<char*>(bytes.data()), size).good())
		{
			return {};
		}
		return bytes;
	}

	AssetGuid ReadGuid(const YAML::Node& node, const char* name)
	{
		return node[name] ? AssetGuid(node[name].as<std::string>(std::string())) : INVALID_ASSET_GUID;
	}
}

CFontFaceAsset::CFontFaceAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& bytes, std::uint32_t faceIndex)
	: m_metaData(metaData), m_bytes(std::move(bytes)), m_faceIndex(faceIndex)
{
}

AssetGuid CFontFaceAsset::GetGuid() const { return m_metaData.Guid; }
EAssetType CFontFaceAsset::GetAssetType() const { return EAssetType::FontFace; }
EAssetLoadState CFontFaceAsset::GetLoadState() const { return m_loadState; }
const AssetMetaData& CFontFaceAsset::GetMetaData() const { return m_metaData; }
const std::vector<std::uint8_t>& CFontFaceAsset::GetBytes() const { return m_bytes; }
std::uint32_t CFontFaceAsset::GetFaceIndex() const { return m_faceIndex; }
std::uint32_t CFontFaceAsset::GetGeneration() const { return m_generation; }

CFontFamilyAsset::CFontFamilyAsset(const AssetMetaData& metaData, FontFamilyData data)
	: m_metaData(metaData), m_data(std::move(data))
{
}

AssetGuid CFontFamilyAsset::GetGuid() const { return m_metaData.Guid; }
EAssetType CFontFamilyAsset::GetAssetType() const { return EAssetType::FontFamily; }
EAssetLoadState CFontFamilyAsset::GetLoadState() const { return m_loadState; }
const AssetMetaData& CFontFamilyAsset::GetMetaData() const { return m_metaData; }
const FontFamilyData& CFontFamilyAsset::GetData() const { return m_data; }
void CFontFamilyAsset::SetData(FontFamilyData data) { m_data = std::move(data); }

AssetGuid CFontFamilyAsset::ResolveFace(EFontStyle style, bool& usedRegularFallback) const
{
	usedRegularFallback = false;
	AssetGuid result = INVALID_ASSET_GUID;
	switch (style)
	{
	case EFontStyle::Bold: result = m_data.Bold; break;
	case EFontStyle::Italic: result = m_data.Italic; break;
	case EFontStyle::BoldItalic: result = m_data.BoldItalic; break;
	default: result = m_data.Regular; break;
	}
	if (result.IsNull() && EFontStyle::Regular != style)
	{
		result = m_data.Regular;
		usedRegularFallback = false == result.IsNull();
	}
	return result;
}

std::vector<AssetGuid> CFontFamilyAsset::ReadDependencyGuids(const File::Path& path)
{
	std::vector<AssetGuid> result;

	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	if (false == stream.is_open())
	{
		return result;
	}
	const std::streamsize size = stream.tellg();
	if (size <= 0)
	{
		return result;
	}
	stream.seekg(0, std::ios::beg);
	std::string text(static_cast<std::size_t>(size), '\0');
	if (false == stream.read(text.data(), size).good())
	{
		return result;
	}

	auto append = [&result](const AssetGuid& guid)
	{
		if (false == guid.IsNull() &&
		    std::find(result.begin(), result.end(), guid) == result.end())
		{
			result.push_back(guid);
		}
	};

	try
	{
		const YAML::Node root = YAML::Load(text);
		append(ReadGuid(root, "Regular"));
		append(ReadGuid(root, "Bold"));
		append(ReadGuid(root, "Italic"));
		append(ReadGuid(root, "BoldItalic"));
		if (const YAML::Node fallbacks = root["FallbackFamilies"]; fallbacks && fallbacks.IsSequence())
		{
			for (const YAML::Node& node : fallbacks)
			{
				append(AssetGuid(node.as<std::string>(std::string())));
			}
		}
	}
	catch (const YAML::Exception&)
	{
		return {};
	}

	return result;
}

EAssetType CFontFaceAssetLoader::GetSupportedType() const { return EAssetType::FontFace; }
bool CFontFaceAssetLoader::CanLoad(const AssetLoadDesc& desc) const
{
	return EAssetType::FontFace == desc.Type && nullptr != desc.MetaData;
}
OwnerPtr<IAsset> CFontFaceAssetLoader::Load(const AssetLoadDesc& desc)
{
	if (false == CanLoad(desc)) return nullptr;
	std::vector<std::uint8_t> bytes = ReadBytes(desc);
	if (bytes.empty()) return nullptr;
	std::uint32_t faceIndex = 0;
	if (desc.MetaData && false == desc.MetaData->ImportOptionsYaml.empty())
	{
		try
		{
			const YAML::Node options = YAML::Load(desc.MetaData->ImportOptionsYaml);
			faceIndex = options["FaceIndex"] ? options["FaceIndex"].as<std::uint32_t>(0) : 0;
		}
		catch (const YAML::Exception&) { return nullptr; }
	}
	return MakeOwnerPtr<CFontFaceAsset>(*desc.MetaData, std::move(bytes), faceIndex);
}
void CFontFaceAssetLoader::Unload(IAsset& asset) { (void)asset; }

EAssetType CFontFamilyAssetLoader::GetSupportedType() const { return EAssetType::FontFamily; }
bool CFontFamilyAssetLoader::CanLoad(const AssetLoadDesc& desc) const
{
	return EAssetType::FontFamily == desc.Type && nullptr != desc.MetaData;
}
OwnerPtr<IAsset> CFontFamilyAssetLoader::Load(const AssetLoadDesc& desc)
{
	if (false == CanLoad(desc)) return nullptr;
	const std::vector<std::uint8_t> bytes = ReadBytes(desc);
	if (bytes.empty()) return nullptr;
	FontFamilyData data;
	try
	{
		const YAML::Node root = YAML::Load(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
		if (!root || false == root.IsMap()) return nullptr;
		data.Regular = ReadGuid(root, "Regular");
		data.Bold = ReadGuid(root, "Bold");
		data.Italic = ReadGuid(root, "Italic");
		data.BoldItalic = ReadGuid(root, "BoldItalic");
		data.UseProjectFallbacks = root["UseProjectFallbacks"] ? root["UseProjectFallbacks"].as<bool>(true) : true;
		if (const YAML::Node fallbacks = root["FallbackFamilies"]; fallbacks && fallbacks.IsSequence())
		{
			for (const YAML::Node& node : fallbacks)
			{
				const AssetGuid guid(node.as<std::string>(std::string()));
				if (false == guid.IsNull()) data.FallbackFamilies.push_back(guid);
			}
		}
	}
	catch (const YAML::Exception&) { return nullptr; }
	return MakeOwnerPtr<CFontFamilyAsset>(*desc.MetaData, std::move(data));
}
void CFontFamilyAssetLoader::Unload(IAsset& asset) { (void)asset; }
bool CFontFamilyAssetLoader::ReloadInto(IAsset& existing, const AssetMetaData& metaData)
{
	if (existing.GetAssetType() != EAssetType::FontFamily) return false;
	AssetLoadDesc desc; desc.Guid = metaData.Guid; desc.Type = EAssetType::FontFamily;
	desc.Path = metaData.Path; desc.ResolvedPath = metaData.Path; desc.MetaData = &metaData;
	OwnerPtr<IAsset> loaded = Load(desc);
	if (!loaded) return false;
	auto& destination = static_cast<CFontFamilyAsset&>(existing);
	destination.SetData(static_cast<CFontFamilyAsset*>(loaded.Get())->GetData());
	return true;
}
