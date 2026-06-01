#pragma once

#include "Engine/Framework.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
#include "Engine/Editor/ImEditor.h"   // OwnerPtr<CImEditor> 멤버
#endif

#if !JBRO_EDITOR
#include "Engine/Core/Game/GameModuleLoader.h"
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
	bool LoadRuntimeStartupScene(const BuildManifest& manifest);
	void ConfigureRuntimeViewCamera();
	void ShutdownRuntimeGame();

	OwnerPtr<CGameModuleLoader> m_gameModuleLoader;
	float m_runtimeRenderWidth = 1.0f;
	float m_runtimeRenderHeight = 1.0f;
	bool m_runtimeGameInitialized = false;
#endif
};
