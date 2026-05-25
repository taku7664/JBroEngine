#include "pch.h"
#include "TextureAsset.h"

#include "Core/RHI/IRHIDevice.h"

#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

CTextureAsset::CTextureAsset(const AssetMetaData& metaData, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t>&& pixels)
	: m_metaData(metaData)
	, m_width(width)
	, m_height(height)
	, m_pixels(std::move(pixels))
{
}

AssetGuid CTextureAsset::GetGuid() const
{
	return m_metaData.Guid;
}

EAssetType CTextureAsset::GetAssetType() const
{
	return m_metaData.Type;
}

EAssetLoadState CTextureAsset::GetLoadState() const
{
	return m_loadState;
}

const AssetMetaData& CTextureAsset::GetMetaData() const
{
	return m_metaData;
}

std::uint32_t CTextureAsset::GetWidth() const
{
	return m_width;
}

std::uint32_t CTextureAsset::GetHeight() const
{
	return m_height;
}

const std::vector<std::uint8_t>& CTextureAsset::GetPixels() const
{
	return m_pixels;
}

const SpriteImportOptions& CTextureAsset::GetSpriteImportOptions() const
{
	return m_spriteImportOptions;
}

const std::vector<SpriteFrame>& CTextureAsset::GetSpriteFrames() const
{
	return m_spriteFrames;
}

void CTextureAsset::SetSpriteImportData(const SpriteImportOptions& options, std::vector<SpriteFrame>&& frames)
{
	m_spriteImportOptions = options;
	m_spriteFrames = std::move(frames);
}

SafePtr<IRHITexture> CTextureAsset::GetGpuTexture() const
{
	return m_gpuTexture.GetSafePtr();
}

bool CTextureAsset::EnsureGpuTexture(IRHIDevice& device)
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
	desc.Width = m_width;
	desc.Height = m_height;
	desc.Format = ERHITextureFormat::RGBA8;
	m_gpuTexture = device.CreateTexture2D(desc, m_pixels.data());
	return static_cast<bool>(m_gpuTexture);
}

EAssetType CTextureAssetLoader::GetSupportedType() const
{
	return EAssetType::Texture;
}

bool CTextureAssetLoader::CanLoad(const AssetLoadDesc& desc) const
{
	return EAssetType::Texture == desc.Type && nullptr != desc.ResolvedPath && nullptr != desc.MetaData;
}

OwnerPtr<IAsset> CTextureAssetLoader::Load(const AssetLoadDesc& desc)
{
	if (false == CanLoad(desc))
	{
		return nullptr;
	}

	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_uc* pixels = stbi_load(desc.ResolvedPath, &width, &height, &channels, 4);
	if (nullptr == pixels || width <= 0 || height <= 0)
	{
		if (pixels)
		{
			stbi_image_free(pixels);
		}
		return nullptr;
	}

	const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
	std::vector<std::uint8_t> data(byteCount);
	std::memcpy(data.data(), pixels, byteCount);
	stbi_image_free(pixels);

	OwnerPtr<CTextureAsset> texture = MakeOwnerPtr<CTextureAsset>(*desc.MetaData, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), std::move(data));
	const SpriteImportOptions options = CSpriteImportOptions::FromYaml(desc.MetaData->ImportOptionsYaml);
	texture->SetSpriteImportData(options, CSpriteImportOptions::BuildFrames(texture->GetWidth(), texture->GetHeight(), options));
	return texture;
}

void CTextureAssetLoader::Unload(IAsset& asset)
{
	(void)asset;
}
