#pragma once

#include "Core/Renderer/IRenderResourceCache.h"
#include "Utillity/Pointer/SafePtr.h"

#include <unordered_map>

class IRHIDevice;
class IRHITexture;

// CRenderResourceCache — IRenderResourceCache 의 단일 구현.
// AssetGuid -> OwnerPtr<IRHITexture> 매핑. RHI device 는 외부 주입.
class CRenderResourceCache final : public IRenderResourceCache
{
public:
	explicit CRenderResourceCache(SafePtr<IRHIDevice> device);
	~CRenderResourceCache() override;

	SafePtr<IRHITexture> AcquireSpriteTexture(const AssetGuid& guid, CSpriteAsset& sprite) override;
	SafePtr<IRHITexture> FindSpriteTexture(const AssetGuid& guid) const override;
	void ReleaseSpriteTexture(const AssetGuid& guid) override;
	void InvalidateSpriteTexture(const AssetGuid& guid) override;
	void Clear() override;

private:
	SafePtr<IRHIDevice>                                  m_device;
	std::unordered_map<AssetGuid, OwnerPtr<IRHITexture>> m_spriteTextures;
};
