#include "pch.h"
#include "SpriteAsset.h"

#include "yaml-cpp/yaml.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>

namespace
{
	constexpr std::array<char, 8> COOKED_SPRITE_MAGIC = { 'J', 'B', 'S', 'P', 'R', '8', '1', '\0' };
	constexpr std::uint8_t OPAQUE_ALPHA_THRESHOLD = 0;

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

	template<typename T>
	bool ReadPod(const std::vector<std::uint8_t>& bytes, std::size_t& offset, T& outValue)
	{
		if (offset + sizeof(T) > bytes.size())
		{
			return false;
		}
		std::memcpy(&outValue, bytes.data() + offset, sizeof(T));
		offset += sizeof(T);
		return true;
	}

	bool ReadString(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::string& outValue)
	{
		std::uint32_t size = 0;
		if (false == ReadPod(bytes, offset, size) || offset + size > bytes.size())
		{
			return false;
		}
		outValue.assign(reinterpret_cast<const char*>(bytes.data() + offset), size);
		offset += size;
		return true;
	}

	bool TryLoadCookedSpritePayload(
		const AssetLoadDesc& desc,
		std::uint32_t& outWidth,
		std::uint32_t& outHeight,
		std::vector<std::uint8_t>& outPixels,
		std::string& outImportOptionsYaml)
	{
		outWidth = 0;
		outHeight = 0;
		outPixels.clear();
		outImportOptionsYaml.clear();

		if (desc.MemoryPayload.size() < COOKED_SPRITE_MAGIC.size()
			|| 0 != std::memcmp(desc.MemoryPayload.data(), COOKED_SPRITE_MAGIC.data(), COOKED_SPRITE_MAGIC.size()))
		{
			return false;
		}

		std::size_t offset = COOKED_SPRITE_MAGIC.size();
		std::uint32_t version = 0;
		std::uint32_t channels = 0;
		std::uint32_t pixelSize = 0;
		if (false == ReadPod(desc.MemoryPayload, offset, version)
			|| 1 != version
			|| false == ReadPod(desc.MemoryPayload, offset, outWidth)
			|| false == ReadPod(desc.MemoryPayload, offset, outHeight)
			|| false == ReadPod(desc.MemoryPayload, offset, channels)
			|| 4 != channels
			|| false == ReadString(desc.MemoryPayload, offset, outImportOptionsYaml)
			|| false == ReadPod(desc.MemoryPayload, offset, pixelSize)
			|| 0 == outWidth
			|| 0 == outHeight)
		{
			return false;
		}

		const std::uint64_t expectedSize = static_cast<std::uint64_t>(outWidth) * static_cast<std::uint64_t>(outHeight) * 4ull;
		if (expectedSize != pixelSize || offset + pixelSize != desc.MemoryPayload.size())
		{
			return false;
		}

		outPixels.assign(desc.MemoryPayload.begin() + static_cast<std::ptrdiff_t>(offset), desc.MemoryPayload.end());
		return true;
	}

	Rect ComputeLocalBounds(const SpriteFrame& frame, float pixelsPerUnit)
	{
		if (frame.Width == 0 || frame.Height == 0 || pixelsPerUnit <= 0.0f)
		{
			return {};
		}

		const float left = -frame.PivotX * static_cast<float>(frame.Width) / pixelsPerUnit;
		const float top = frame.PivotY * static_cast<float>(frame.Height) / pixelsPerUnit;
		const float right = (1.0f - frame.PivotX) * static_cast<float>(frame.Width) / pixelsPerUnit;
		const float bottom = -(1.0f - frame.PivotY) * static_cast<float>(frame.Height) / pixelsPerUnit;
		return Rect(left, bottom, right, top);
	}

	Rect ComputeLocalOpaqueBounds(const SpriteFrame& frame, float pixelsPerUnit)
	{
		if (!frame.HasOpaquePixels || frame.Width == 0 || frame.Height == 0 || pixelsPerUnit <= 0.0f)
		{
			return {};
		}

		const SpritePixelBounds& opaque = frame.OpaqueBoundsPixels;
		const float left = (static_cast<float>(opaque.X) - frame.PivotX * static_cast<float>(frame.Width)) / pixelsPerUnit;
		const float right = (static_cast<float>(opaque.X + opaque.Width) - frame.PivotX * static_cast<float>(frame.Width)) / pixelsPerUnit;
		const float bottom = (frame.PivotY * static_cast<float>(frame.Height) - static_cast<float>(opaque.Y + opaque.Height)) / pixelsPerUnit;
		const float top = (frame.PivotY * static_cast<float>(frame.Height) - static_cast<float>(opaque.Y)) / pixelsPerUnit;
		return Rect(left, bottom, right, top);
	}

	void BuildFrameBounds(
		const std::vector<std::uint8_t>& pixels,
		std::uint32_t textureWidth,
		std::uint32_t textureHeight,
		float pixelsPerUnit,
		std::vector<SpriteFrame>& frames,
		Rect& outMaximumLocalBounds,
		Rect& outMaximumLocalOpaqueBounds,
		bool& outHasAnyOpaquePixels)
	{
		outMaximumLocalBounds = {};
		outMaximumLocalOpaqueBounds = {};
		outHasAnyOpaquePixels = false;

		const bool canScanPixels =
			textureWidth > 0 &&
			textureHeight > 0 &&
			pixels.size() >= static_cast<std::size_t>(textureWidth) * static_cast<std::size_t>(textureHeight) * 4ull;
		const float effectivePixelsPerUnit = pixelsPerUnit > 0.0f ? pixelsPerUnit : 1.0f;

		bool hasAnyFrameBounds = false;
		for (SpriteFrame& frame : frames)
		{
			frame.HasOpaquePixels = false;
			frame.OpaqueBoundsPixels = {};

			frame.LocalBounds = ComputeLocalBounds(frame, effectivePixelsPerUnit);

			if (canScanPixels && frame.Width > 0 && frame.Height > 0)
			{
				const std::uint32_t frameRight = std::min(textureWidth, frame.X + frame.Width);
				const std::uint32_t frameBottom = std::min(textureHeight, frame.Y + frame.Height);
				if (frame.X < frameRight && frame.Y < frameBottom)
				{
					std::uint32_t minX = frameRight - frame.X;
					std::uint32_t minY = frameBottom - frame.Y;
					std::uint32_t maxX = 0;
					std::uint32_t maxY = 0;

					for (std::uint32_t y = frame.Y; y < frameBottom; ++y)
					{
						for (std::uint32_t x = frame.X; x < frameRight; ++x)
						{
							const std::size_t alphaIndex =
								(static_cast<std::size_t>(y) * textureWidth + static_cast<std::size_t>(x)) * 4 + 3;
							if (alphaIndex < pixels.size() && pixels[alphaIndex] > OPAQUE_ALPHA_THRESHOLD)
							{
								const std::uint32_t localX = x - frame.X;
								const std::uint32_t localY = y - frame.Y;
								minX = std::min(minX, localX);
								minY = std::min(minY, localY);
								maxX = std::max(maxX, localX);
								maxY = std::max(maxY, localY);
								frame.HasOpaquePixels = true;
							}
						}
					}

					if (frame.HasOpaquePixels)
					{
						frame.OpaqueBoundsPixels.X = minX;
						frame.OpaqueBoundsPixels.Y = minY;
						frame.OpaqueBoundsPixels.Width = maxX - minX + 1;
						frame.OpaqueBoundsPixels.Height = maxY - minY + 1;
					}
				}
			}

			frame.LocalOpaqueBounds = ComputeLocalOpaqueBounds(frame, effectivePixelsPerUnit);

			if (!frame.LocalBounds.IsEmpty())
			{
				outMaximumLocalBounds = hasAnyFrameBounds
					? outMaximumLocalBounds.Union(frame.LocalBounds)
					: frame.LocalBounds;
				hasAnyFrameBounds = true;
			}

			if (frame.HasOpaquePixels && !frame.LocalOpaqueBounds.IsEmpty())
			{
				outMaximumLocalOpaqueBounds = outHasAnyOpaquePixels
					? outMaximumLocalOpaqueBounds.Union(frame.LocalOpaqueBounds)
					: frame.LocalOpaqueBounds;
				outHasAnyOpaquePixels = true;
			}
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
std::uint32_t CSpriteAsset::GetPixelGeneration() const { return m_pixelGeneration; }

const SpriteImportOptions& CSpriteAsset::GetImportOptions() const { return m_importOptions; }
const std::vector<SpriteFrame>& CSpriteAsset::GetFrames() const { return m_frames; }
const Rect& CSpriteAsset::GetMaximumLocalBounds() const { return m_maximumLocalBounds; }
const Rect& CSpriteAsset::GetMaximumLocalOpaqueBounds() const { return m_maximumLocalOpaqueBounds; }
bool CSpriteAsset::HasAnyOpaquePixels() const { return m_hasAnyOpaquePixels; }

void CSpriteAsset::SetImportData(const SpriteImportOptions& options, std::vector<SpriteFrame>&& frames)
{
	m_importOptions = options;
	m_frames = std::move(frames);
	BuildFrameBounds(
		m_pixels,
		m_width,
		m_height,
		GetEffectivePixelsPerUnit(1.0f),
		m_frames,
		m_maximumLocalBounds,
		m_maximumLocalOpaqueBounds,
		m_hasAnyOpaquePixels);
}

void CSpriteAsset::ReplacePixels(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t>&& pixels)
{
	m_width  = width;
	m_height = height;
	m_pixels = std::move(pixels);
	++m_pixelGeneration;
	BuildFrameBounds(
		m_pixels,
		m_width,
		m_height,
		GetEffectivePixelsPerUnit(1.0f),
		m_frames,
		m_maximumLocalBounds,
		m_maximumLocalOpaqueBounds,
		m_hasAnyOpaquePixels);
	// GPU 텍스처는 RenderResourceCache 가 따로 invalidate 한다 (자산이 GPU 를 소유하지 않음).
	// reload 경로(CAssetManager::ReloadAsset)가 cache->InvalidateSpriteTexture(guid) 호출.
}

float CSpriteAsset::GetEffectivePixelsPerUnit(float fallback) const
{
	if (m_importOptions.PixelsPerUnit > 0.0f) return m_importOptions.PixelsPerUnit;
	if (fallback > 0.0f) return fallback;
	return 1.0f;
}

void CSpriteAsset::ApplyImportOptions(const std::string& importOptionsYaml)
{
	// 픽셀 데이터/GPU 텍스처는 그대로. 옵션 + frames 만 in-place 갱신.
	// 향후 GPU 텍스처 재생성이 필요한 옵션(Premultiply/ColorSpace/Mipmap 등) 도입 시 분기 추가.
	SpriteImportOptions newOptions = CSpriteImportOptions::FromYaml(importOptionsYaml);
	std::vector<SpriteFrame> newFrames = CSpriteImportOptions::BuildFrames(m_width, m_height, newOptions);
	SetImportData(newOptions, std::move(newFrames));
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
	    && (false == desc.ResolvedPath.empty() || desc.HasMemoryPayload())
	    && nullptr != desc.MetaData;
}

OwnerPtr<IAsset> CSpriteAssetLoader::Load(const AssetLoadDesc& desc)
{
	if (false == CanLoad(desc)) return nullptr;

	int w = 0, h = 0, channels = 0;
	std::vector<std::uint8_t> data;
	std::string cookedImportOptionsYaml;
	if (desc.HasMemoryPayload())
	{
		std::uint32_t cookedWidth = 0;
		std::uint32_t cookedHeight = 0;
		if (TryLoadCookedSpritePayload(desc, cookedWidth, cookedHeight, data, cookedImportOptionsYaml))
		{
			w = static_cast<int>(cookedWidth);
			h = static_cast<int>(cookedHeight);
		}
	}

	if (data.empty())
	{
		stbi_uc* pixels = nullptr;
		if (desc.HasMemoryPayload())
		{
			pixels = stbi_load_from_memory(
				desc.MemoryPayload.data(),
				static_cast<int>(desc.MemoryPayload.size()),
				&w,
				&h,
				&channels,
				4);
		}
		else
		{
			const std::string resolved = desc.ResolvedPath.string();
			pixels = stbi_load(resolved.c_str(), &w, &h, &channels, 4);
		}
		if (nullptr == pixels || w <= 0 || h <= 0)
		{
			if (pixels) stbi_image_free(pixels);
			return nullptr;
		}

		const std::size_t byteCount = static_cast<std::size_t>(w) * h * 4;
		data.resize(byteCount);
		std::memcpy(data.data(), pixels, byteCount);
		stbi_image_free(pixels);
	}

	AssetMetaData metaData = *desc.MetaData;
	if (false == cookedImportOptionsYaml.empty())
	{
		metaData.ImportOptionsYaml = cookedImportOptionsYaml;
	}

	OwnerPtr<CSpriteAsset> sprite = MakeOwnerPtr<CSpriteAsset>(
		metaData,
		static_cast<std::uint32_t>(w),
		static_cast<std::uint32_t>(h),
		std::move(data));

	const SpriteImportOptions options = CSpriteImportOptions::FromYaml(metaData.ImportOptionsYaml);
	sprite->SetImportData(options, CSpriteImportOptions::BuildFrames(
		sprite->GetWidth(), sprite->GetHeight(), options));

	return sprite;
}

void CSpriteAssetLoader::Unload(IAsset& asset)
{
	(void)asset;
}

bool CSpriteAssetLoader::ReloadInto(IAsset& existing, const AssetMetaData& metaData)
{
	if (EAssetType::Sprite != existing.GetAssetType()) return false;
	CSpriteAsset& sprite = static_cast<CSpriteAsset&>(existing);

	// 1) raw 파일 재로드. 디스크에 파일이 없거나 디코드 실패면 옵션만 갱신하고 픽셀은 보존.
	bool pixelsReplaced = false;
	if (false == metaData.Path.empty())
	{
		const std::string resolved = metaData.Path.string();
		int w = 0, h = 0, channels = 0;
		stbi_uc* pixels = stbi_load(resolved.c_str(), &w, &h, &channels, 4);
		if (pixels && w > 0 && h > 0)
		{
			const std::size_t byteCount = static_cast<std::size_t>(w) * h * 4;
			std::vector<std::uint8_t> data(byteCount);
			std::memcpy(data.data(), pixels, byteCount);
			sprite.ReplacePixels(
				static_cast<std::uint32_t>(w),
				static_cast<std::uint32_t>(h),
				std::move(data));
			pixelsReplaced = true;
		}
		if (pixels) stbi_image_free(pixels);
	}

	// 2) ImportOptions + frames 재계산. 픽셀 폭/높이가 바뀌었으면 frames 도 새 크기에 맞춰짐.
	const SpriteImportOptions options = CSpriteImportOptions::FromYaml(metaData.ImportOptionsYaml);
	sprite.SetImportData(options, CSpriteImportOptions::BuildFrames(
		sprite.GetWidth(), sprite.GetHeight(), options));

	(void)pixelsReplaced;
	return true;
}
