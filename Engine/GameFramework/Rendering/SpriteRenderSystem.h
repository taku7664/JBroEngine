#pragma once

#include "GameFramework/System/GameSystem.h"
#include "Utillity/Pointer/SafePtr.h"

#include <unordered_map>

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
	void SetDependencies(IAssetManager* assetManager, IRHIDevice* rhiDevice, IRenderer* renderer);
	IRenderScene* GetRenderScene() const;
	bool ShouldUpdateInEditMode() const override { return true; }

	// 씬 변경/언로드 시 호출 — 생성된 머티리얼 캐시를 비웁니다.
	void ClearMaterialCache();

protected:
	void OnUpdate(CScene& scene) override;

private:
	IRenderScene*  m_renderScene  = nullptr;
	IAssetManager* m_assetManager = nullptr;
	IRHIDevice*    m_rhiDevice    = nullptr;
	IRenderer*     m_renderer     = nullptr;

	// 컴포넌트별 런타임 생성 머티리얼 소유권 캐시(키 = SpriteRenderer2D 주소).
	// SpriteRenderer2D 컴포넌트는 여기서 발급한 SafePtr 만 보관합니다.
	std::unordered_map<const void*, OwnerPtr<IRenderMaterial>> m_materialCache;
};
