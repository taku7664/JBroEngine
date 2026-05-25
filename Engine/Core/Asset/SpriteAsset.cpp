#include "pch.h"
#include "SpriteAsset.h"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <sstream>

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

CSpriteAsset::CSpriteAsset(const AssetMetaData& metaData)
	: m_metaData(metaData)
{
}

AssetGuid CSpriteAsset::GetGuid() const
{
	return m_metaData.Guid;
}

EAssetType CSpriteAsset::GetAssetType() const
{
	return m_metaData.Type;
}

EAssetLoadState CSpriteAsset::GetLoadState() const
{
	return m_loadState;
}

const AssetMetaData& CSpriteAsset::GetMetaData() const
{
	return m_metaData;
}

SpriteImportOptions CSpriteImportOptions::FromYaml(const std::string& yamlText)
{
	SpriteImportOptions options;
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

	const YAML::Node spriteNode = root["Sprite"] ? root["Sprite"] : root;
	options.RowCount = std::max<std::uint32_t>(1, ReadOption<std::uint32_t>(spriteNode, "RowCount", options.RowCount));
	options.ColumnCount = std::max<std::uint32_t>(1, ReadOption<std::uint32_t>(spriteNode, "ColumnCount", options.ColumnCount));
	options.MarginX = ReadOption<std::uint32_t>(spriteNode, "MarginX", options.MarginX);
	options.MarginY = ReadOption<std::uint32_t>(spriteNode, "MarginY", options.MarginY);
	options.GapX = ReadOption<std::uint32_t>(spriteNode, "GapX", options.GapX);
	options.GapY = ReadOption<std::uint32_t>(spriteNode, "GapY", options.GapY);
	options.PivotX = ReadOption<float>(spriteNode, "PivotX", options.PivotX);
	options.PivotY = ReadOption<float>(spriteNode, "PivotY", options.PivotY);
	options.PixelsPerUnit = ReadOption<float>(spriteNode, "PixelsPerUnit", options.PixelsPerUnit);
	return options;
}

std::string CSpriteImportOptions::ToYaml(const SpriteImportOptions& options)
{
	YAML::Emitter emitter;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << "Sprite" << YAML::Value;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << "RowCount" << YAML::Value << options.RowCount;
	emitter << YAML::Key << "ColumnCount" << YAML::Value << options.ColumnCount;
	emitter << YAML::Key << "MarginX" << YAML::Value << options.MarginX;
	emitter << YAML::Key << "MarginY" << YAML::Value << options.MarginY;
	emitter << YAML::Key << "GapX" << YAML::Value << options.GapX;
	emitter << YAML::Key << "GapY" << YAML::Value << options.GapY;
	emitter << YAML::Key << "PivotX" << YAML::Value << options.PivotX;
	emitter << YAML::Key << "PivotY" << YAML::Value << options.PivotY;
	emitter << YAML::Key << "PixelsPerUnit" << YAML::Value << options.PixelsPerUnit;
	emitter << YAML::EndMap;
	emitter << YAML::EndMap;
	return emitter.c_str();
}

std::vector<SpriteFrame> CSpriteImportOptions::BuildFrames(std::uint32_t textureWidth, std::uint32_t textureHeight, const SpriteImportOptions& options)
{
	std::vector<SpriteFrame> frames;
	const std::uint32_t rowCount = std::max<std::uint32_t>(1, options.RowCount);
	const std::uint32_t columnCount = std::max<std::uint32_t>(1, options.ColumnCount);
	if (0 == textureWidth || 0 == textureHeight
		|| textureWidth <= options.MarginX * 2
		|| textureHeight <= options.MarginY * 2)
	{
		return frames;
	}

	const std::uint32_t totalGapX = columnCount > 1 ? options.GapX * (columnCount - 1) : 0;
	const std::uint32_t totalGapY = rowCount > 1 ? options.GapY * (rowCount - 1) : 0;
	const std::uint32_t availableWidth = textureWidth - options.MarginX * 2;
	const std::uint32_t availableHeight = textureHeight - options.MarginY * 2;
	if (availableWidth <= totalGapX || availableHeight <= totalGapY)
	{
		return frames;
	}

	const std::uint32_t frameWidth = (availableWidth - totalGapX) / columnCount;
	const std::uint32_t frameHeight = (availableHeight - totalGapY) / rowCount;
	if (0 == frameWidth || 0 == frameHeight)
	{
		return frames;
	}

	frames.reserve(static_cast<std::size_t>(rowCount) * static_cast<std::size_t>(columnCount));
	for (std::uint32_t row = 0; row < rowCount; ++row)
	{
		for (std::uint32_t column = 0; column < columnCount; ++column)
		{
			SpriteFrame frame;
			frame.X = options.MarginX + column * (frameWidth + options.GapX);
			frame.Y = options.MarginY + row * (frameHeight + options.GapY);
			frame.Width = frameWidth;
			frame.Height = frameHeight;
			frame.PivotX = options.PivotX;
			frame.PivotY = options.PivotY;
			frames.push_back(frame);
		}
	}
	return frames;
}

EAssetType CSpriteAssetLoader::GetSupportedType() const
{
	return EAssetType::Sprite;
}

bool CSpriteAssetLoader::CanLoad(const AssetLoadDesc& desc) const
{
	return EAssetType::Sprite == desc.Type && nullptr != desc.ResolvedPath && nullptr != desc.MetaData;
}

OwnerPtr<IAsset> CSpriteAssetLoader::Load(const AssetLoadDesc& desc)
{
	if (false == CanLoad(desc))
	{
		return nullptr;
	}

	std::ifstream file(desc.ResolvedPath);
	if (false == file.is_open())
	{
		return nullptr;
	}

	OwnerPtr<CSpriteAsset> sprite = MakeOwnerPtr<CSpriteAsset>(*desc.MetaData);
	sprite->ImportOptions = CSpriteImportOptions::FromYaml(desc.MetaData->ImportOptionsYaml);
	std::string key;
	while (file >> key)
	{
		if (key == "TextureGuid")
		{
			std::string guid;
			file >> guid;
			sprite->TextureGuid = File::Guid(guid);
		}
		else if (key == "Pivot")
		{
			file >> sprite->PivotX >> sprite->PivotY;
		}
		else if (key == "PixelsPerUnit")
		{
			file >> sprite->PixelsPerUnit;
		}
	}
	sprite->Frames = CSpriteImportOptions::BuildFrames(0, 0, sprite->ImportOptions);

	return sprite;
}

void CSpriteAssetLoader::Unload(IAsset& asset)
{
	(void)asset;
}
