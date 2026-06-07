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
#include "Engine/Core/RuntimeConfig.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/Core/Platform/IRenderSurface.h"
#include "Engine/Core/Renderer/IRenderer.h"
#include "Engine/Utillity/String/StringUtillity.h"
#include "Engine/GameFramework/Audio/AudioSystem.h"
#include "Engine/GameFramework/Rendering/GameCamera.h"
#include "Engine/GameFramework/Rendering/SpriteRenderSystem.h"
#include "Engine/GameFramework/Scene/Scene.h"
#include "Engine/GameFramework/Scene/SceneManager.h"
#include "Engine/GameFramework/Scene/SceneSerializer.h"

#include <algorithm>
#endif

#if !JBRO_EDITOR && JBRO_PLATFORM_WEB
extern "C" IGameModule* CreateGameModule(const GameModuleHostApi* hostApi) __attribute__((weak));
extern "C" void DestroyGameModule(IGameModule* module, const GameModuleHostApi* hostApi) __attribute__((weak));
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
				if (false == manifest.ProductName.empty())
				{
					m_runtimeApplicationName = Utillity::U8ToWString(manifest.ProductName);
					platformDesc.ApplicationName = m_runtimeApplicationName.c_str();
				}
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
	::Runtime.PixelsPerUnit = manifest.PixelsPerUnit >= 1.0f ? manifest.PixelsPerUnit : 100.0f;

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
	SafePtr<IAssetManager> assetManager = Engine.AssetManager;
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
#if JBRO_PLATFORM_WEB
		if (nullptr == CreateGameModule || nullptr == DestroyGameModule)
		{
			CSystemLog::Warning("Runtime static script module was not linked. Continuing without project scripts.");
			return true;
		}

		m_gameModuleLoader = MakeOwnerPtr<CGameModuleLoader>();
		if (!m_gameModuleLoader)
		{
			CSystemLog::Error("Runtime static script module loader allocation failed.");
			return false;
		}

		CEngine* engine = GetEngine();
		GameModuleContext context;
		context.HostScriptCore = engine ? &engine->GetScriptCore() : nullptr;
		if (false == m_gameModuleLoader->LoadStaticModule(&CreateGameModule, &DestroyGameModule, context, "StaticGameScript"))
		{
			CSystemLog::Error("Runtime static script module initialization failed.");
			m_gameModuleLoader.Reset();
			return false;
		}

		CSystemLog::Info("Runtime static script module initialized.");
#endif
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
	context.HostScriptCore = engine ? &engine->GetScriptCore() : nullptr;
	if (false == m_gameModuleLoader->LoadDynamicLibrary(modulePath, context))
	{
		CSystemLog::Error(std::string("Runtime script module load failed: ") + modulePath.generic_string());
		m_gameModuleLoader.Reset();
		return false;
	}

	CSystemLog::Info(std::string("Runtime script module loaded: ") + modulePath.generic_string());
	return true;
}

namespace
{
	constexpr bool AllowRuntimeScenePathFallback()
	{
#if defined(JBRO_EDITOR) || !defined(NDEBUG)
		return true;
#else
		return false;
#endif
	}

	// 런타임 씬 1개의 노드(구조)만 로드한다 — 리소스(에셋)는 로드하지 않는다.
	// 리소스는 그 씬이 active 가 될 때(SetActiveScene) 비로소 로드된다.
	//  · guid 가 유효하면 패키지 에셋(LoadAsset → text)에서, 아니면 경로(ResolveAssetPath)에서 로드.
	//  · Sprite/Audio 시스템을 부착하고 ScriptCore 디바이스를 주입한다(씬마다 필요).
	// 반환: 로드+부착 성공 여부. 실패 시 생성한 씬을 정리한다.
	bool LoadRuntimeSceneNodes(CSceneManager& sceneManager,
	                           IAssetManager& assetManager,
	                           const ScriptCore* context,
	                           const std::string& sceneName,
	                           const AssetGuid& sceneGuid,
	                           const std::string& scenePathText)
	{
		CScene* scene = sceneManager.CreateScene(sceneName.c_str());
		if (nullptr == scene)
		{
			CSystemLog::Error(std::string("Runtime scene creation failed: ") + sceneName);
			return false;
		}

		CSceneSerializer serializer;
		ESceneSerializeResult loadResult = ESceneSerializeResult::IoError;
		if (false == sceneGuid.IsNull())
		{
			AssetRef<IAsset> sceneAsset = assetManager.LoadAsset(sceneGuid);
			const CFileAsset* fileAsset = sceneAsset.IsValid() ? dynamic_cast<const CFileAsset*>(sceneAsset.Get()) : nullptr;
			if (nullptr != fileAsset)
			{
				std::string_view sceneText = fileAsset->GetText();
				loadResult = serializer.DeserializeFromText(*scene, std::string(sceneText).c_str());
			}
		}
		else
		{
			if constexpr (false == AllowRuntimeScenePathFallback())
			{
				sceneManager.DestroyScene(sceneName.c_str());
				CSystemLog::Error(std::string("Runtime scene path fallback is not allowed in release package: ") + scenePathText);
				return false;
			}

			File::Path scenePath;
			if (false == assetManager.ResolveAssetPath(File::Path(scenePathText), scenePath))
			{
				sceneManager.DestroyScene(sceneName.c_str());
				CSystemLog::Error(std::string("Runtime scene path resolve failed: ") + scenePathText);
				return false;
			}
			loadResult = serializer.LoadFromFile(*scene, scenePath);
		}

		if (ESceneSerializeResult::Success != loadResult)
		{
			sceneManager.DestroyScene(sceneName.c_str());
			CSystemLog::Error(std::string("Runtime scene load failed: ") + sceneName);
			return false;
		}

		// 씬마다 렌더/오디오 시스템을 부착하고 ScriptCore 디바이스를 주입한다.
		if (context)
		{
			// 렌더 시스템은 호스트 전용 — 전역 `Engine`(EngineCore)에서 직접 가져온다.
			// (RenderScene/RHIDevice/Renderer 는 스크립트에 노출하지 않으므로 ScriptCore 에 없다.)
			CSpriteRenderSystem* spriteSystem = scene->FindSystem<CSpriteRenderSystem>();
			if (nullptr == spriteSystem)
			{
				spriteSystem = scene->AddSystem<CSpriteRenderSystem>(Engine.RenderScene.TryGet());
			}
			if (nullptr != spriteSystem)
			{
				spriteSystem->SetRenderScene(Engine.RenderScene.TryGet());
				spriteSystem->SetDependencies(
					Engine.AssetManager.TryGet(),
					Engine.RHIDevice.TryGet(),
					Engine.Renderer.TryGet(),
					Engine.RenderResourceCache.TryGet(),
					Runtime.PixelsPerUnit);
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

		return true;
	}
}

bool CGameApplication::LoadRuntimeStartupScene(const BuildManifest& manifest)
{
	const AssetGuid startupSceneGuid(manifest.StartupSceneGuid);
	if (manifest.StartupScene.empty() && startupSceneGuid.IsNull())
	{
		CSystemLog::Error("Runtime startup scene is empty.");
		return false;
	}

	SafePtr<CSceneManager> sceneManager = Engine.SceneManager;
	SafePtr<IAssetManager> assetManager = Engine.AssetManager;
	if (false == sceneManager.IsValid() || false == assetManager.IsValid())
	{
		CSystemLog::Error("Runtime startup scene load failed: SceneManager or AssetManager is not available.");
		return false;
	}

	CEngine* engine = GetEngine();
	const ScriptCore* context = engine ? &engine->GetScriptCore() : nullptr;

	const std::string startupName = false == manifest.StartupScene.empty()
		? manifest.StartupScene
		: startupSceneGuid.generic_string();

	// ── 1) 모든 빌드 씬의 노드(구조)를 선로드한다(리소스 제외) ──────────────────
	// 빌드 패키지는 BuildScenes 를 모두 포함한다. 프로그램 시작 시 전 씬의 노드를 메모리에
	// 올려 두고, 리소스는 그 씬이 active 가 될 때만 로드한다(SetActiveScene).
	// startup 씬은 guid 로 로드(경로 의존 회피), 나머지는 경로 문자열로 로드한다.
	if (false == LoadRuntimeSceneNodes(*sceneManager, *assetManager, context,
	                                   startupName, startupSceneGuid,
	                                   manifest.StartupScene))
	{
		return false;
	}

	for (const std::string& scenePath : manifest.BuildScenes)
	{
		if (scenePath.empty() || scenePath == manifest.StartupScene || scenePath == startupName)
		{
			continue; // startup 은 위에서 이미 로드. 중복 스킵.
		}
		// BuildScenes 항목은 프로젝트 상대 경로 문자열 — 이를 씬 이름으로도 쓴다(에디터와 동일 키).
		if (false == LoadRuntimeSceneNodes(*sceneManager, *assetManager, context,
		                                   scenePath, AssetGuid(), scenePath))
		{
			// 한 씬 로드 실패는 치명적이지 않게 — 로그만 남기고 나머지를 계속 로드한다.
			CSystemLog::Warning(std::string("Runtime build scene skipped (load failed): ") + scenePath);
		}
	}

	// ── 2) startup 씬을 active 로 전환 → 그 시점에 startup 리소스만 로드된다 ───────
	if (false == sceneManager->SetActiveScene(startupName.c_str()))
	{
		CSystemLog::Error(std::string("Runtime startup scene activation failed: ") + startupName);
		return false;
	}

	sceneManager->PlaySimulation();
	CSystemLog::Info(std::string("Runtime startup scene loaded: ") + startupName);
	return true;
}

void CGameApplication::ConfigureRuntimeViewCamera()
{
	SafePtr<CSceneManager> sceneManager = Engine.SceneManager;
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
	if (Engine.SceneManager)
	{
		Engine.SceneManager->DestroyScriptInstances();
	}
	if (m_gameModuleLoader)
	{
		m_gameModuleLoader->Unload();
		m_gameModuleLoader.Reset();
	}
	m_runtimeGameInitialized = false;
}
#endif
