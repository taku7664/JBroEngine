#include "pch.h"
#include "SpriteAsset.h"

#include "Core/RHI/IRHIDevice.h"

#include "yaml-cpp/yaml.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace
{
	template<typename T>
	T ReadOption(const YAML::Node& node, const char* key, const T& defaultValue)
	{
		if (!node[key]) return defaultValue;
		try { return node[key].as<T>(); }
		catch (const YAML::Exception&) { return defaultValue; }
	}

	ESpriteSliceType ParseSliceType(const std::string& s)
	{
		if (s == "Automatic") return ESpriteSliceType::Automatic;
		if (s == "CellSize")  return ESpriteSliceType::CellSize;
		if (s == "CellCount") return ESpriteSliceType::CellCount;
		return ESpriteSliceType::None;
	}

	const char* SliceTypeToString(ESpriteSliceType t)
	{
		switch (t)
		{
		case ESpriteSliceType::Automatic: return "Automatic";
		case ESpriteSliceType::CellSize:  return "CellSize";
		case ESpriteSliceType::CellCount: return "CellCount";
		default:                          return "None";
		}
	}
}

// ── CSpriteAsset ─────────────────────────────────────────────────────────────

CSpriteAsset::CSpriteAsset(const AssetMetaData& metaData,
                           std::uint32_t width,
                           std::uint32_t height,
                           std::vector<std::uint8_t>&& pixels)
	: m_metaData(metaData)
	, m_width(width)
	, m_height(height)
	, m_pixels(std::move(pixels))
{
}

CSpriteAsset::CSpriteAsset(const AssetMetaData& metaData)
	: m_metaData(metaData)
{
}

AssetGuid       CSpriteAsset::GetGuid()      const { return m_metaData.Guid; }
EAssetType      CSpriteAsset::GetAssetType() const { return m_metaData.Type; }
EAssetLoadState CSpriteAsset::GetLoadState() const { return m_loadState; }
const AssetMetaData& CSpriteAsset::GetMetaData() const { return m_metaData; }

std::uint32_t CSpriteAsset::GetWidth()  const { return m_width; }
std::uint32_t CSpriteAsset::GetHeight() const { return m_height; }
const std::vector<std::uint8_t>& CSpriteAsset::GetPixels() const { return m_pixels; }

SafePtr<IRHITexture> CSpriteAsset::GetGpuTexture() const
{
	return m_gpuTexture.GetSafePtr();
}

bool CSpriteAsset::EnsureGpuTexture(IRHIDevice& device)
{
	if (m_gpuTexture)
	{
		return true;
	}
	if (m_pixels.empty() || 0 == m_width || 0 == m_height)
	{
		return false;
	}

	RHITexture2DDesc desc;
	desc.Width  = m_width;
	desc.Height = m_height;
	desc.Format = ERHITextureFormat::RGBA8;
	m_gpuTexture = device.CreateTexture2D(desc, m_pixels.data());
	return static_cast<bool>(m_gpuTexture);
}

const SpriteImportOptions& CSpriteAsset::GetImportOptions() const { return m_importOptions; }
const std::vector<SpriteFrame>& CSpriteAsset::GetFrames() const { return m_frames; }

void CSpriteAsset::SetImportData(const SpriteImportOptions& options, std::vector<SpriteFrame>&& frames)
{
	m_importOptions = options;
	m_frames = std::move(frames);
}

// ── CSpriteImportOptions ─────────────────────────────────────────────────────

SpriteImportOptions CSpriteImportOptions::FromYaml(const std::string& yamlText)
{
	SpriteImportOptions options;
	if (yamlText.empty()) return options;

	YAML::Node root;
	try { root = YAML::Load(yamlText); }
	catch (const YAML::Exception&) { return options; }

	if (!root || false == root.IsMap()) return options;
	const YAML::Node spriteNode = root["Sprite"] ? root["Sprite"] : root;

	options.SliceType     = ParseSliceType(ReadOption<std::string>(spriteNode, "SliceType", "None"));
	options.RowCount      = std::max<std::uint32_t>(1, ReadOption<std::uint32_t>(spriteNode, "RowCount",    options.RowCount));
	options.ColumnCount   = std::max<std::uint32_t>(1, ReadOption<std::uint32_t>(spriteNode, "ColumnCount", options.ColumnCount));
	options.CellWidth     = std::max<std::uint32_t>(1, ReadOption<std::uint32_t>(spriteNode, "CellWidth",   options.CellWidth));
	options.CellHeight    = std::max<std::uint32_t>(1, ReadOption<std::uint32_t>(spriteNode, "CellHeight",  options.CellHeight));
	options.MarginX       = ReadOption<std::uint32_t>(spriteNode, "MarginX", options.MarginX);
	options.MarginY       = ReadOption<std::uint32_t>(spriteNode, "MarginY", options.MarginY);
	options.GapX          = ReadOption<std::uint32_t>(spriteNode, "GapX",    options.GapX);
	options.GapY          = ReadOption<std::uint32_t>(spriteNode, "GapY",    options.GapY);
	options.PivotX        = ReadOption<float>        (spriteNode, "PivotX", options.PivotX);
	options.PivotY        = ReadOption<float>        (spriteNode, "PivotY", options.PivotY);
	options.PixelsPerUnit = ReadOption<float>        (spriteNode, "PixelsPerUnit", options.PixelsPerUnit);
	return options;
}

std::string CSpriteImportOptions::ToYaml(const SpriteImportOptions& options)
{
	YAML::Emitter emitter;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << "Sprite" << YAML::Value;
	emitter << YAML::BeginMap;
	emitter << YAML::Key << "SliceType"     << YAML::Value << SliceTypeToString(options.SliceType);
	emitter << YAML::Key << "RowCount"      << YAML::Value << options.RowCount;
	emitter << YAML::Key << "ColumnCount"   << YAML::Value << options.ColumnCount;
	emitter << YAML::Key << "CellWidth"     << YAML::Value << options.CellWidth;
	emitter << YAML::Key << "CellHeight"    << YAML::Value << options.CellHeight;
	emitter << YAML::Key << "MarginX"       << YAML::Value << options.MarginX;
	emitter << YAML::Key << "MarginY"       << YAML::Value << options.MarginY;
	emitter << YAML::Key << "GapX"          << YAML::Value << options.GapX;
	emitter << YAML::Key << "GapY"          << YAML::Value << options.GapY;
	emitter << YAML::Key << "PivotX"        << YAML::Value << options.PivotX;
	emitter << YAML::Key << "PivotY"        << YAML::Value << options.PivotY;
	emitter << YAML::Key << "PixelsPerUnit" << YAML::Value << options.PixelsPerUnit;
	emitter << YAML::EndMap;
	emitter << YAML::EndMap;
	return emitter.c_str();
}

std::vector<SpriteFrame> CSpriteImportOptions::BuildFrames(std::uint32_t textureWidth,
                                                          std::uint32_t textureHeight,
                                                          const SpriteImportOptions& options)
{
	std::vector<SpriteFrame> frames;
	if (0 == textureWidth || 0 == textureHeight) return frames;

	// None / Automatic — 전체 1 프레임. (Automatic 은 추후 알파 기반 분할 구현 예정.)
	if (ESpriteSliceType::None == options.SliceType
	    || ESpriteSliceType::Automatic == options.SliceType)
	{
		SpriteFrame f;
		f.X = 0; f.Y = 0;
		f.Width = textureWidth; f.Height = textureHeight;
		f.PivotX = options.PivotX; f.PivotY = options.PivotY;
		frames.push_back(f);
		return frames;
	}

	// 공통 — 여백 안의 유효 영역
	if (textureWidth <= options.MarginX * 2 || textureHeight <= options.MarginY * 2)
	{
		return frames;
	}
	const std::uint32_t availW = textureWidth  - options.MarginX * 2;
	const std::uint32_t availH = textureHeight - options.MarginY * 2;

	std::uint32_t cellW = 0, cellH = 0, cols = 0, rows = 0;

	if (ESpriteSliceType::CellCount == options.SliceType)
	{
		cols = std::max<std::uint32_t>(1, options.ColumnCount);
		rows = std::max<std::uint32_t>(1, options.RowCount);
		const std::uint32_t totalGapX = cols > 1 ? options.GapX * (cols - 1) : 0;
		const std::uint32_t totalGapY = rows > 1 ? options.GapY * (rows - 1) : 0;
		if (availW <= totalGapX || availH <= totalGapY) return frames;
		cellW = (availW - totalGapX) / cols;
		cellH = (availH - totalGapY) / rows;
	}
	else // CellSize
	{
		cellW = std::max<std::uint32_t>(1, options.CellWidth);
		cellH = std::max<std::uint32_t>(1, options.CellHeight);
		const std::uint32_t strideX = cellW + options.GapX;
		const std::uint32_t strideY = cellH + options.GapY;
		if (0 == strideX || 0 == strideY) return frames;
		cols = (availW + options.GapX) / strideX;
		rows = (availH + options.GapY) / strideY;
	}

	if (0 == cellW || 0 == cellH || 0 == cols || 0 == rows) return frames;

	frames.reserve(static_cast<std::size_t>(rows) * cols);
	for (std::uint32_t r = 0; r < rows; ++r)
	{
		for (std::uint32_t c = 0; c < cols; ++c)
		{
			SpriteFrame f;
			f.X = options.MarginX + c * (cellW + options.GapX);
			f.Y = options.MarginY + r * (cellH + options.GapY);
			f.Width  = cellW;
			f.Height = cellH;
			f.PivotX = options.PivotX;
			f.PivotY = options.PivotY;
			frames.push_back(f);
		}
	}
	return frames;
}

// ── CSpriteAssetLoader ───────────────────────────────────────────────────────

EAssetType CSpriteAssetLoader::GetSupportedType() const
{
	return EAssetType::Sprite;
}

bool CSpriteAssetLoader::CanLoad(const AssetLoadDesc& desc) const
{
	return EAssetType::Sprite == desc.Type
	    && false == desc.ResolvedPath.empty()
	    && nullptr != desc.MetaData;
}

OwnerPtr<IAsset> CSpriteAssetLoader::Load(const AssetLoadDesc& desc)
{
	if (false == CanLoad(desc)) return nullptr;

	int w = 0, h = 0, channels = 0;
	const std::string resolved = desc.ResolvedPath.string();
	stbi_uc* pixels = stbi_load(resolved.c_str(), &w, &h, &channels, 4);
	if (nullptr == pixels || w <= 0 || h <= 0)
	{
		if (pixels) stbi_image_free(pixels);
		return nullptr;
	}

	const std::size_t byteCount = static_cast<std::size_t>(w) * h * 4;
	std::vector<std::uint8_t> data(byteCount);
	std::memcpy(data.data(), pixels, byteCount);
	stbi_image_free(pixels);

	OwnerPtr<CSpriteAsset> sprite = MakeOwnerPtr<CSpriteAsset>(
		*desc.MetaData,
		static_cast<std::uint32_t>(w),
		static_cast<std::uint32_t>(h),
		std::move(data));

	const SpriteImportOptions options = CSpriteImportOptions::FromYaml(desc.MetaData->ImportOptionsYaml);
	sprite->SetImportData(options, CSpriteImportOptions::BuildFrames(
		sprite->GetWidth(), sprite->GetHeight(), options));

	return sprite;
}

void CSpriteAssetLoader::Unload(IAsset& asset)
{
	(void)asset;
}
