#pragma once

#include "Engine/Framework.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
#include "Engine/Editor/ImEditor.h"   // OwnerPtr<CImEditor> 멤버
#endif

#if !JBRO_EDITOR
#include "Engine/Core/Game/GameModuleLoader.h"
#include "Engine/GameFramework/Rendering/GameCamera.h"   // 렌더 스냅샷 스크래치 멤버 타입

#include <string>
#include <vector>
#endif

struct BuildManifest;

class CGameApplication : public CApplication
{
public:
	void OnPreInitialize() override;
	void OnPostInitialize() override;
	void OnPreTick() override;
	void OnPostTick() override;
	void OnPreFinalize() override;
	void OnPostFinalize() override;

private:
#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
	OwnerPtr<CImEditor> m_editor;
#endif
#if !JBRO_EDITOR
	bool InitializeRuntimeGame();
	bool MountRuntimeAssets(const BuildManifest& manifest);
	bool LoadRuntimeScriptModule(const BuildManifest& manifest);
	bool LoadRuntimeStartupCanvas(const BuildManifest& manifest);
	void ConfigureRuntimeViewCamera();
	void ShutdownRuntimeGame();

	OwnerPtr<CGameModuleLoader> m_gameModuleLoader;
	std::wstring m_runtimeApplicationName;
	float m_runtimeRenderWidth = 1.0f;
	float m_runtimeRenderHeight = 1.0f;
	bool m_runtimeGameInitialized = false;

	// 매 프레임 렌더 스냅샷 수집 버퍼 — 엔진과 swap 으로 주고받아 양쪽 용량을 유지한다
	// (지역 변수로 두면 프레임마다 힙 할당·해제가 붙는다).
	std::vector<Render2DViewportDesc> m_runtimeRenderViewports;
	std::vector<Render2DLightDesc> m_runtimeRenderLights;
	std::vector<Render2DLayerDesc> m_runtimeRenderLayers;
#endif
};
