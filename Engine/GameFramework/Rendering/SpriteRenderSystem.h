#pragma once

#include "GameFramework/System/GameSystem.h"

class IRenderScene;
class IRenderer;
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

protected:
	void OnUpdate(CScene& scene) override;

private:
	IRenderScene* m_renderScene = nullptr;
	IAssetManager* m_assetManager = nullptr;
	IRHIDevice* m_rhiDevice = nullptr;
	IRenderer* m_renderer = nullptr;
};
