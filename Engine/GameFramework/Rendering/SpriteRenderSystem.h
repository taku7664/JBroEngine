#pragma once

#include "GameFramework/System/GameSystem.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

class IRenderScene;
class IRenderMaterial;
class IRenderer;
class IRenderResourceCache;
class IAssetManager;
class IRHIDevice;
class CForward2DRenderer;

class CSpriteRenderSystem final : public CGameSystem
{
public:
	explicit CSpriteRenderSystem(IRenderScene* renderScene = nullptr);

	void SetRenderScene(IRenderScene* renderScene);
	void SetDependencies(IAssetManager* assetManager, IRHIDevice* rhiDevice, IRenderer* renderer,
		IRenderResourceCache* renderResourceCache, float pixelsPerUnit);
	IRenderScene* GetRenderScene() const;
	bool ShouldUpdateInEditMode() const override { return true; }

	// 캔버스 변경/언로드 시 호출 — 생성된 머티리얼 캐시를 비웁니다.
	void ClearMaterialCache();

protected:
	void OnUpdate(CGameCanvas& canvas) override;

private:
	IRenderScene*         m_renderScene         = nullptr;
	IAssetManager*        m_assetManager        = nullptr;
	IRHIDevice*           m_rhiDevice           = nullptr;
	CForward2DRenderer*   m_renderer            = nullptr;
	IRenderResourceCache* m_renderResourceCache = nullptr;

	// 프로젝트 Default PPU 폴백(자산 PPU 가 0 일 때). 호스트가 주입.
	float m_pixelsPerUnit = 100.0f;

	// 컴포넌트별 런타임 생성 머티리얼 소유권 캐시(키 = SpriteRenderer2D 주소).
	// SpriteRenderer2D 컴포넌트는 여기서 발급한 SafePtr 만 보관합니다.
	//
	// LastSeenFrame 은 "이번 프레임에 제출됐다"는 표시입니다. 예전에는 프레임마다
	// unordered_set 을 새로 만들어 모았는데, 그러면 살아있는 스프라이트 하나당 노드 하나를
	// 할당했다가 프레임 끝에 전부 버리게 됩니다 — 캐시가 이미 그 키를 들고 있는데도.
	// Text/Shape 렌더 시스템은 이미 스탬프 방식입니다(Utillity/Types/FrameLiveness.h).
	struct CachedMaterial
	{
		OwnerPtr<IRenderMaterial> Material;
		std::uint64_t             LastSeenFrame = 0;
	};
	std::unordered_map<const void*, CachedMaterial> m_materialCache;

	// OnUpdate 진입에서 증가. 스탬프는 시스템마다 자기 것을 씁니다(시스템 간 비교 없음).
	std::uint64_t m_frameStamp = 0;
};
