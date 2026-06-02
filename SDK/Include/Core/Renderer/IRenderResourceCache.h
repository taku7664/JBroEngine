#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Utillity/Pointer/SafePtr.h"

class IRHITexture;
class CSpriteAsset;

// ── IRenderResourceCache ─────────────────────────────────────────────────────
// 자산에서 분리한 GPU 리소스 캐시.
// 자산은 raw 픽셀/desc 만 보유하고, GPU 객체(IRHITexture 등) 의 소유는
// 이 캐시가 가진다.
//
// 라이프타임 모델
//   - Acquire* 호출 시점에 lazy 생성. 이미 있으면 그대로 반환.
//   - AssetManager 가 자산을 unload 할 때 Release*(guid) 로 GPU 객체 폐기.
//   - 자산 reload 의 in-place pixel 교체 후에는 Invalidate*(guid) 로 다음
//     Acquire 가 새 픽셀로 재생성하도록 유도.
//
// AssetRef 의 use-count 보호 덕에 "쓰는 동안 unload" 는 발생하지 않으므로
// 캐시는 별도 ref-count 를 두지 않는다. (자산 lifetime 에 위탁)
class IRenderResourceCache : public EnableSafeFromThis<IRenderResourceCache>
{
public:
	virtual ~IRenderResourceCache() = default;

	// 스프라이트 자산의 GPU 텍스처를 가져온다. 없으면 sprite 의 픽셀로 생성.
	// sprite 가 nullptr 이거나 픽셀이 비어있으면 nullptr 반환.
	virtual SafePtr<IRHITexture> AcquireSpriteTexture(const AssetGuid& guid, CSpriteAsset& sprite) = 0;

	// 캐시된 GPU 텍스처가 있으면 반환, 없으면 nullptr (생성하지 않음).
	virtual SafePtr<IRHITexture> FindSpriteTexture(const AssetGuid& guid) const = 0;

	// 자산 unload 시 호출 — 보유 중인 GPU 텍스처 폐기.
	virtual void ReleaseSpriteTexture(const AssetGuid& guid) = 0;

	// reload 의 in-place pixel 교체 후 호출. 다음 Acquire 가 새 픽셀로 재생성.
	virtual void InvalidateSpriteTexture(const AssetGuid& guid) = 0;

	// 전체 폐기 (셧다운/디바이스 lost).
	virtual void Clear() = 0;
};
