#include "pch.h"
#include "Application.h" 
#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
#include "Editor/Editor.h"
#include "Editor/Helper/ImGuiHelper.h"
#endif

#if !JBRO_EDITOR
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/FileAsset.h"
#include "Engine/Core/Build/BuildManifest.h"
#include "Engine/Core/Engine.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/Core/Platform/IRenderSurface.h"
#include "Engine/Core/Renderer/IRenderer.h"
#include "Engine/GameFramework/Audio/AudioSystem.h"
#include "Engine/GameFramework/Rendering/GameCamera.h"
#include "Engine/GameFramework/Rendering/SpriteRenderSystem.h"
#include "Engine/GameFramework/Scene/Scene.h"
#include "Engine/GameFramework/Scene/SceneManager.h"
#include "Engine/GameFramework/Scene/SceneSerializer.h"

#include <algorithm>
#endif

void CGameApplication::OnPreInitialize()
{
#if !JBRO_EDITOR
	File::Path manifestPath;
	if (CBuildManifestLoader::FindDefaultManifest(manifestPath))
	{
		BuildManifest manifest;
		if (CBuildManifestLoader::LoadFromFile(manifestPath, manifest))
		{
			if (CEngine* engine = GetEngine())
			{
				PlatformDesc platformDesc;
				platformDesc.WindowWidth = manifest.ResolutionWidth > 0 ? manifest.ResolutionWidth : platformDesc.WindowWidth;
				platformDesc.WindowHeight = manifest.ResolutionHeight > 0 ? manifest.ResolutionHeight : platformDesc.WindowHeight;
				engine->SetPlatformDesc(platformDesc);
				m_runtimeRenderWidth = static_cast<float>(platformDesc.WindowWidth);
				m_runtimeRenderHeight = static_cast<float>(platformDesc.WindowHeight);
			}
		}
	}
#endif
}

void CGameApplication::OnPostInitialize()
{
#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
	CEngine* engine = GetEngine();
	if (engine)
	{
		m_editor = MakeOwnerPtr<CImEditor>();
		Editor::ImEditor = m_editor.GetSafePtr();
		if (m_editor)
		{
			engine->InitializeModule(*m_editor, "ImEditor");
			Editor::RootDockWindow = m_editor->CreateImWindow<CRootDockWindow>("RootDockWindow");
			ImGuiHelper::SetDarkThemeColor();
			ImGuiHelper::SetDefaultThemeStyle();
		}
	}
#else
	InitializeRuntimeGame();
#endif
}

void CGameApplication::OnPreTick()
{
#if !JBRO_EDITOR
	if (m_gameModuleLoader)
	{
		m_gameModuleLoader->Tick();
	}
	ConfigureRuntimeViewCamera();
#endif
}

void CGameApplication::OnPostTick()
{
}

void CGameApplication::OnPreFinalize()
{
#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
	CEngine* engine = GetEngine();
	if (engine && m_editor)
	{
		engine->FinalizeModule(*m_editor);
		m_editor.Reset();
	}
#else
	ShutdownRuntimeGame();
#endif
}

void CGameApplication::OnPostFinalize()
{
}

#if !JBRO_EDITOR
bool CGameApplication::InitializeRuntimeGame()
{
	if (m_runtimeGameInitialized)
	{
		return true;
	}

	File::Path manifestPath;
	if (false == CBuildManifestLoader::FindDefaultManifest(manifestPath))
	{
		CSystemLog::Warning("Runtime build manifest was not found. Game startup is skipped.");
		return false;
	}

	BuildManifest manifest;
	std::string error;
	if (false == CBuildManifestLoader::LoadFromFile(manifestPath, manifest, &error))
	{
		CSystemLog::Error(std::string("Runtime build manifest load failed: ") + error);
		return false;
	}
	m_runtimeRenderWidth = static_cast<float>(manifest.ResolutionWidth > 0 ? manifest.ResolutionWidth : 1);
	m_runtimeRenderHeight = static_cast<float>(manifest.ResolutionHeight > 0 ? manifest.ResolutionHeight : 1);

	if (false == MountRuntimeAssets(manifest))
	{
		return false;
	}

	if (false == LoadRuntimeScriptModule(manifest))
	{
		return false;
	}

	if (false == LoadRuntimeStartupScene(manifest))
	{
		return false;
	}

	m_runtimeGameInitialized = true;
	CSystemLog::Info(std::string("Runtime game initialized from manifest: ") + manifest.ManifestPath.generic_string());
	return true;
}

bool CGameApplication::MountRuntimeAssets(const BuildManifest& manifest)
{
	SafePtr<IAssetManager> assetManager = Core::AssetManager;
	if (false == assetManager.IsValid())
	{
		CSystemLog::Error("Runtime asset mount failed: AssetManager is not available.");
		return false;
	}

	bool mountedAny = false;
	for (const BuildAssetMount& mount : manifest.AssetMounts)
	{
		const File::Path resolvedPath = CBuildManifestLoader::ResolvePackagePath(manifest, mount.Path);
		if (resolvedPath.empty())
		{
			if (mount.Required)
			{
				CSystemLog::Error("Runtime asset mount failed: asset mount path is empty.");
				return false;
			}
			continue;
		}

		std::error_code errorCode;
		if (mount.Required && false == std::filesystem::exists(resolvedPath, errorCode))
		{
			CSystemLog::Error(std::string("Runtime asset mount path was not found: ") + resolvedPath.generic_string());
			return false;
		}

		switch (mount.Type)
		{
		case EBuildAssetMountType::Loose:
			if (false == assetManager->SetAssetRootPath(resolvedPath))
			{
				CSystemLog::Error(std::string("Runtime loose asset mount failed: ") + resolvedPath.generic_string());
				return false;
			}
			mountedAny = true;
			break;
		case EBuildAssetMountType::Pack:
			if (false == assetManager->LoadPackedAssetManifest(resolvedPath))
			{
				CSystemLog::Error(std::string("Runtime packed asset mount failed: ") + resolvedPath.generic_string());
				return false;
			}
			mountedAny = true;
			break;
		default:
			if (mount.Required)
			{
				CSystemLog::Error("Runtime asset mount failed: unsupported asset mount type.");
				return false;
			}
			break;
		}
	}

	if (false == mountedAny)
	{
		CSystemLog::Error("Runtime asset mount failed: no asset mounts were applied.");
		return false;
	}

	return true;
}

bool CGameApplication::LoadRuntimeScriptModule(const BuildManifest& manifest)
{
	if (manifest.ScriptMode.empty() || manifest.ScriptMode == "Static")
	{
		return true;
	}

	if (manifest.ScriptMode != "DynamicLibrary")
	{
		CSystemLog::Error(std::string("Unsupported runtime script mode: ") + manifest.ScriptMode);
		return false;
	}

	if (manifest.ScriptModule.empty())
	{
		CSystemLog::Error("Runtime script module is empty for DynamicLibrary mode.");
		return false;
	}

	const File::Path modulePath = CBuildManifestLoader::ResolvePackagePath(manifest, File::Path(manifest.ScriptModule));
	std::error_code errorCode;
	if (false == std::filesystem::exists(modulePath, errorCode))
	{
		CSystemLog::Error(std::string("Runtime script module was not found: ") + modulePath.generic_string());
		return false;
	}

	m_gameModuleLoader = MakeOwnerPtr<CGameModuleLoader>();
	if (!m_gameModuleLoader)
	{
		CSystemLog::Error("Runtime script module loader allocation failed.");
		return false;
	}

	CEngine* engine = GetEngine();
	GameModuleContext context;
	context.HostEngine = engine ? &engine->GetEngineCore() : nullptr;
	if (false == m_gameModuleLoader->LoadDynamicLibrary(modulePath, context))
	{
		CSystemLog::Error(std::string("Runtime script module load failed: ") + modulePath.generic_string());
		m_gameModuleLoader.Reset();
		return false;
	}

	CSystemLog::Info(std::string("Runtime script module loaded: ") + modulePath.generic_string());
	return true;
}

bool CGameApplication::LoadRuntimeStartupScene(const BuildManifest& manifest)
{
	const AssetGuid startupSceneGuid(manifest.StartupSceneGuid);
	if (manifest.StartupScene.empty() && startupSceneGuid.IsNull())
	{
		CSystemLog::Error("Runtime startup scene is empty.");
		return false;
	}

	SafePtr<CSceneManager> sceneManager = Core::SceneManager;
	SafePtr<IAssetManager> assetManager = Core::AssetManager;
	if (false == sceneManager.IsValid() || false == assetManager.IsValid())
	{
		CSystemLog::Error("Runtime startup scene load failed: SceneManager or AssetManager is not available.");
		return false;
	}

	const std::string sceneName = false == manifest.StartupScene.empty()
		? manifest.StartupScene
		: startupSceneGuid.generic_string();
	CScene* scene = sceneManager->CreateScene(sceneName.c_str());
	if (nullptr == scene)
	{
		CSystemLog::Error(std::string("Runtime startup scene creation failed: ") + sceneName);
		return false;
	}

	CSceneSerializer serializer;
	ESceneSerializeResult loadResult = ESceneSerializeResult::IoError;
	if (false == startupSceneGuid.IsNull())
	{
		SafePtr<IAsset> sceneAsset = assetManager->LoadAsset(startupSceneGuid);
		const CFileAsset* fileAsset = sceneAsset.IsValid() ? dynamic_cast<const CFileAsset*>(sceneAsset.TryGet()) : nullptr;
		if (nullptr != fileAsset)
		{
			std::string_view sceneText = fileAsset->GetText();
			loadResult = serializer.DeserializeFromText(*scene, std::string(sceneText).c_str());
		}
	}
	else
	{
		File::Path scenePath;
		if (false == assetManager->ResolveAssetPath(File::Path(manifest.StartupScene), scenePath))
		{
			sceneManager->DestroyScene(sceneName.c_str());
			CSystemLog::Error(std::string("Runtime startup scene path resolve failed: ") + manifest.StartupScene);
			return false;
		}
		loadResult = serializer.LoadFromFile(*scene, scenePath);
	}

	if (ESceneSerializeResult::Success != loadResult)
	{
		sceneManager->DestroyScene(sceneName.c_str());
		CSystemLog::Error(std::string("Runtime startup scene load failed: ") + sceneName);
		return false;
	}

	CEngine* engine = GetEngine();
	const EngineCore* context = engine ? &engine->GetEngineCore() : nullptr;
	if (context)
	{
		CSpriteRenderSystem* spriteSystem = scene->FindSystem<CSpriteRenderSystem>();
		if (nullptr == spriteSystem)
		{
			spriteSystem = scene->AddSystem<CSpriteRenderSystem>(context->RenderScene.TryGet());
		}
		if (nullptr != spriteSystem)
		{
			spriteSystem->SetRenderScene(context->RenderScene.TryGet());
			spriteSystem->SetDependencies(
				context->AssetManager.TryGet(),
				context->RHIDevice.TryGet(),
				context->Renderer.TryGet());
		}

		CAudioSystem* audioSystem = scene->FindSystem<CAudioSystem>();
		if (nullptr == audioSystem)
		{
			audioSystem = scene->AddSystem<CAudioSystem>(context->Audio, context->AssetManager);
		}
		if (nullptr != audioSystem)
		{
			audioSystem->SetDevice(context->Audio);
			audioSystem->SetAssetManager(context->AssetManager);
		}
	}

	sceneManager->PreloadReferencedAssets(*scene);
	if (false == sceneManager->SetActiveScene(sceneName.c_str()))
	{
		sceneManager->DestroyScene(sceneName.c_str());
		CSystemLog::Error(std::string("Runtime startup scene activation failed: ") + sceneName);
		return false;
	}

	sceneManager->PlaySimulation();
	CSystemLog::Info(std::string("Runtime startup scene loaded: ") + sceneName);
	return true;
}

void CGameApplication::ConfigureRuntimeViewCamera()
{
	SafePtr<CSceneManager> sceneManager = Core::SceneManager;
	if (false == sceneManager.IsValid())
	{
		return;
	}

	SafePtr<CScene> scene = sceneManager->GetActiveScene();
	if (false == scene.IsValid())
	{
		return;
	}

	const float renderWidth = std::max(1.0f, m_runtimeRenderWidth);
	const float renderHeight = std::max(1.0f, m_runtimeRenderHeight);
	std::vector<GameRenderCameraDesc> cameras = CollectGameRenderCameras(*scene, renderWidth, renderHeight);

	if (CEngine* engine = GetEngine())
	{
		engine->SetGameRenderCameras(std::move(cameras));
	}
}

void CGameApplication::ShutdownRuntimeGame()
{
	if (Core::SceneManager)
	{
		Core::SceneManager->DestroyScriptInstances();
	}
	if (m_gameModuleLoader)
	{
		m_gameModuleLoader->Unload();
		m_gameModuleLoader.Reset();
	}
	m_runtimeGameInitialized = false;
}
#endif
