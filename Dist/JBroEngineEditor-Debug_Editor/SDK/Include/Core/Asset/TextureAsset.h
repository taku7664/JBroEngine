#pragma once

#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"
#include "Core/Asset/SpriteAsset.h"
#include "Core/RHI/IRHITexture.h"

class IRHIDevice;

class CTextureAsset final : public IAsset
{
public:
	CTextureAsset(const AssetMetaData& metaData, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t>&& pixels);

	AssetGuid GetGuid() const override;
	EAssetType GetAssetType() const override;
	EAssetLoadState GetLoadState() const override;
	const AssetMetaData& GetMetaData() const override;

	std::uint32_t GetWidth() const;
	std::uint32_t GetHeight() const;
	const std::vector<std::uint8_t>& GetPixels() const;
	const SpriteImportOptions& GetSpriteImportOptions() const;
	const std::vector<SpriteFrame>& GetSpriteFrames() const;
	SafePtr<IRHITexture> GetGpuTexture() const;
	bool EnsureGpuTexture(IRHIDevice& device);
	void SetSpriteImportData(const SpriteImportOptions& options, std::vector<SpriteFrame>&& frames);

private:
	AssetMetaData m_metaData;
	std::uint32_t m_width = 0;
	std::uint32_t m_height = 0;
	std::vector<std::uint8_t> m_pixels;
	SpriteImportOptions m_spriteImportOptions;
	std::vector<SpriteFrame> m_spriteFrames;
	OwnerPtr<IRHITexture> m_gpuTexture;
	EAssetLoadState m_loadState = EAssetLoadState::Loaded;
};

class CTextureAssetLoader final : public IAssetLoader
{
public:
	EAssetType GetSupportedType() const override;
	bool CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) override;
	void Unload(IAsset& asset) override;
};
