#include "pch.h"
#include "RenderResourceCache.h"

#include "Core/Asset/SpriteAsset.h"
#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/IRHITexture.h"

CRenderResourceCache::CRenderResourceCache(SafePtr<IRHIDevice> device)
	: m_device(std::move(device))
{
}

CRenderResourceCache::~CRenderResourceCache() = default;

SafePtr<IRHITexture> CRenderResourceCache::AcquireSpriteTexture(const AssetGuid& guid, CSpriteAsset& sprite)
{
	auto it = m_spriteTextures.find(guid);
	if (it != m_spriteTextures.end() && it->second)
	{
		return it->second.GetSafePtr();
	}

	if (false == m_device.IsValid())
	{
		return nullptr;
	}
	const auto& pixels = sprite.GetPixels();
	const std::uint32_t width  = sprite.GetWidth();
	const std::uint32_t height = sprite.GetHeight();
	if (pixels.empty() || 0 == width || 0 == height)
	{
		return nullptr;
	}

	RHITexture2DDesc desc;
	desc.Width  = width;
	desc.Height = height;
	desc.Format = ERHITextureFormat::RGBA8;
	OwnerPtr<IRHITexture> texture = m_device->CreateTexture2D(desc, pixels.data());
	if (false == static_cast<bool>(texture))
	{
		return nullptr;
	}
	SafePtr<IRHITexture> result = texture.GetSafePtr();
	m_spriteTextures[guid] = std::move(texture);
	return result;
}

SafePtr<IRHITexture> CRenderResourceCache::FindSpriteTexture(const AssetGuid& guid) const
{
	auto it = m_spriteTextures.find(guid);
	if (it == m_spriteTextures.end() || false == static_cast<bool>(it->second))
	{
		return nullptr;
	}
	return it->second.GetSafePtr();
}

void CRenderResourceCache::ReleaseSpriteTexture(const AssetGuid& guid)
{
	m_spriteTextures.erase(guid);
}

void CRenderResourceCache::InvalidateSpriteTexture(const AssetGuid& guid)
{
	m_spriteTextures.erase(guid);
}

void CRenderResourceCache::Clear()
{
	m_spriteTextures.clear();
}
