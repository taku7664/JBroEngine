#include "pch.h"
#include "Application.h" 
#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
#include "Editor/Editor.h"
#include "Editor/Theme/EditorTheme.h"
#endif

#if !JBRO_EDITOR
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/FileAsset.h"
#include "Engine/Core/Build/BuildManifest.h"
#include "Engine/Core/Engine.h"
#include "Engine/Core/RuntimeConfig.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/Core/Input/InputSystem.h"
#include "Engine/Core/Platform/IRenderSurface.h"
#include "Engine/Core/Renderer/IRenderer.h"
#include "Engine/Utillity/String/StringUtillity.h"
#include "Engine/GameFramework/Audio/AudioSystem.h"
#include "Engine/GameFramework/Rendering/GameCamera.h"
#include "Engine/GameFramework/Rendering/SpriteRenderSystem.h"
#include "Engine/GameFramework/Rendering/TextRenderSystem.h"
#include "Engine/GameFramework/Canvas/Canvas.h"
#include "Engine/GameFramework/Canvas/CanvasManager.h"
#include "Engine/GameFramework/Canvas/CanvasRuntimeAccess.h"
#include "Engine/GameFramework/Canvas/CanvasSerializer.h"

#include <algorithm>
#endif

// 정적 스크립트 모듈은 Web/Android 런타임 모두 .so/.wasm 안에 함께 링크된다(프로젝트 GameModule.cpp 제공).
#if !JBRO_EDITOR && (JBRO_PLATFORM_WEB || JBRO_PLATFORM_ANDROID)
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
				// desired orientation(빌드설정) → 회전 보정 권위. 빈 문자열/미인식은 Auto.
				if (manifest.Orientation == "Landscape")
				{
					platformDesc.DesiredOrientation = EScreenOrientation::Landscape;
				}
				else if (manifest.Orientation == "Portrait")
				{
					platformDesc.DesiredOrientation = EScreenOrientation::Portrait;
				}
				else
				{
					platformDesc.DesiredOrientation = EScreenOrientation::Auto;
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
			EditorTheme::ApplyDarkColors();
			EditorTheme::ApplyDefaultLayout();
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
	// 카메라/라이트 수집은 여기서 하지 않는다 — 시뮬 이전 수집은 1프레임 지연 카메라를
	// 만든다. InitializeRuntimeGame 이 등록한 pre-render 콜백(렌더 직전)에서 수집한다.
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
	::Runtime.DefaultFontFamilyGuid = AssetGuid(manifest.DefaultFontFamilyGuid);
	::Runtime.FallbackFontFamilies.clear();
	for (const std::string& guid : manifest.FallbackFontFamilyGuids)
	{
		AssetGuid value(guid);
		if (false == value.IsNull()) ::Runtime.FallbackFontFamilies.push_back(std::move(value));
	}
	if (Engine.InputSystem)
	{
		Engine.InputSystem->SetInputMap(manifest.InputActions);
	}

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

	// 카메라/라이트 스냅샷은 렌더 직전에 수집한다(시뮬 이후 상태 반영 — OnPreTick
	// 수집은 물리/스크립트보다 1프레임 뒤진 카메라를 만든다).
	if (CEngine* engine = GetEngine())
	{
		engine->SetPreRenderCallback([this] { ConfigureRuntimeViewCamera(); });
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
#if JBRO_PLATFORM_WEB || JBRO_PLATFORM_ANDROID
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

	// 런타임 캔버스에 씬 파일 내용을 싣고 리소스(에셋)까지 확보한다.
	//  · guid 가 유효하면 패키지 에셋(LoadAsset → text)에서, 아니면 경로(ResolveAssetPath)에서 로드.
	//  · Sprite/Audio 시스템을 부착하고 ScriptCore 디바이스를 주입한다.
	// 캔버스는 런타임에 하나뿐이라 "실패 시 만든 씬을 지운다"가 성립하지 않는다 —
	// 실패하면 캔버스는 로드 이전 상태(대개 빈 상태)로 남고 호출자가 부팅을 접는다.
	bool LoadRuntimeCanvas(CCanvasManager& canvasManager,
	                       IAssetManager& assetManager,
	                       const ScriptCore* context,
	                       const std::string& canvasName,
	                       const AssetGuid& sceneGuid,
	                       const std::string& scenePathText)
	{
		CGameCanvas* scene = &canvasManager.GetOrCreateCanvas();

		CCanvasSerializer serializer;
		ECanvasSerializeResult loadResult = ECanvasSerializeResult::IoError;
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
				CSystemLog::Error(std::string("Runtime scene path fallback is not allowed in release package: ") + scenePathText);
				return false;
			}

			File::Path scenePath;
			if (false == assetManager.ResolveAssetPath(File::Path(scenePathText), scenePath))
			{
				CSystemLog::Error(std::string("Runtime scene path resolve failed: ") + scenePathText);
				return false;
			}
			loadResult = serializer.LoadFromFile(*scene, scenePath);
		}

		if (ECanvasSerializeResult::Success != loadResult)
		{
			CSystemLog::Error(std::string("Runtime scene load failed: ") + canvasName);
			return false;
		}

		// 내용이 실린 뒤에야 이름·리소스를 확정한다 — 실패 경로가 옛 이름을 덮어쓰지 않게.
		canvasManager.SetCanvasName(canvasName.c_str());
		canvasManager.RefreshReferencedAssets();

		// 캔버스에 렌더/오디오 시스템을 부착하고 ScriptCore 디바이스를 주입한다.
		// 시스템은 ClearObjects 로 지워지지 않으므로(캔버스 수명에 붙는다) Find→Add 로 멱등이다.
		if (context)
		{
			// 렌더 시스템은 호스트 전용 — 전역 `Engine`(EngineCore)에서 직접 가져온다.
			// (RenderScene/RHIDevice/Renderer 는 스크립트에 노출하지 않으므로 ScriptCore 에 없다.)
			// 애니메이션 시스템은 SpriteRenderSystem 보다 먼저 등록해 같은 프레임에 FrameIndex 반영.
			CSpriteAnimationSystem* animationSystem = CCanvasRuntimeAccess::FindSystem<CSpriteAnimationSystem>(*scene);
			if (nullptr == animationSystem)
			{
				animationSystem = CCanvasRuntimeAccess::AddSystem<CSpriteAnimationSystem>(*scene, Engine.AssetManager);
			}
			if (nullptr != animationSystem)
			{
				animationSystem->SetAssetManager(Engine.AssetManager);
			}

			CSpriteRenderSystem* spriteSystem = CCanvasRuntimeAccess::FindSystem<CSpriteRenderSystem>(*scene);
			if (nullptr == spriteSystem)
			{
				spriteSystem = CCanvasRuntimeAccess::AddSystem<CSpriteRenderSystem>(*scene, Engine.RenderScene.TryGet());
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

			CShapeRenderSystem* shapeSystem = CCanvasRuntimeAccess::FindSystem<CShapeRenderSystem>(*scene);
			if (nullptr == shapeSystem)
			{
				shapeSystem = CCanvasRuntimeAccess::AddSystem<CShapeRenderSystem>(*scene, Engine.RenderScene.TryGet());
			}
			if (nullptr != shapeSystem)
			{
				shapeSystem->SetRenderScene(Engine.RenderScene.TryGet());
				shapeSystem->SetDependencies(Engine.RHIDevice.TryGet(), Engine.Renderer.TryGet());
			}

			CTextRenderSystem* textSystem = CCanvasRuntimeAccess::FindSystem<CTextRenderSystem>(*scene);
			if (nullptr == textSystem)
			{
				textSystem = CCanvasRuntimeAccess::AddSystem<CTextRenderSystem>(*scene, Engine.RenderScene.TryGet());
			}
			if (nullptr != textSystem)
			{
				textSystem->SetRenderScene(Engine.RenderScene.TryGet());
				textSystem->SetDependencies(Engine.AssetManager.TryGet(), Engine.RHIDevice.TryGet(), Engine.Renderer.TryGet(),
					Runtime.PixelsPerUnit, Runtime.DefaultFontFamilyGuid, Runtime.FallbackFontFamilies);
			}

			CAudioSystem* audioSystem = CCanvasRuntimeAccess::FindSystem<CAudioSystem>(*scene);
			if (nullptr == audioSystem)
			{
				audioSystem = CCanvasRuntimeAccess::AddSystem<CAudioSystem>(*scene, context->Audio, context->AssetManager);
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

	SafePtr<CCanvasManager> canvasManager = Engine.CanvasManager;
	SafePtr<IAssetManager> assetManager = Engine.AssetManager;
	if (false == canvasManager.IsValid() || false == assetManager.IsValid())
	{
		CSystemLog::Error("Runtime startup scene load failed: CanvasManager or AssetManager is not available.");
		return false;
	}

	CEngine* engine = GetEngine();
	const ScriptCore* context = engine ? &engine->GetScriptCore() : nullptr;

	const std::string startupName = false == manifest.StartupScene.empty()
		? manifest.StartupScene
		: startupSceneGuid.generic_string();

	// startup 캔버스만 로드한다 — 런타임 캔버스는 하나고, 전환은 그 하나에 diff 를 적용하는
	// 것이라 나머지 빌드 씬을 미리 인스턴스화해 둘 자리가 없다(있으면 그게 곧 다중 씬이다).
	// 다른 캔버스는 전환 시점에 파일에서 읽는다. startup 은 guid 로 로드해 경로 의존을 피한다.
	if (false == LoadRuntimeCanvas(*canvasManager, *assetManager, context,
	                               startupName, startupSceneGuid,
	                               manifest.StartupScene))
	{
		return false;
	}

	canvasManager->PlaySimulation();
	CSystemLog::Info(std::string("Runtime startup scene loaded: ") + startupName);
	return true;
}

void CGameApplication::ConfigureRuntimeViewCamera()
{
	SafePtr<CCanvasManager> canvasManager = Engine.CanvasManager;
	if (false == canvasManager.IsValid())
	{
		return;
	}

	SafePtr<CGameCanvas> scene = canvasManager->GetActiveCanvas();
	if (false == scene.IsValid())
	{
		return;
	}

	// 카메라 종횡비/뷰포트는 매니페스트 고정 해상도가 아니라 실제 렌더 타깃(표시 방향) 크기를
	// 따라야 한다. 안 그러면 surface 종횡비가 매니페스트와 다른 기기(풀스크린 모바일 등)에서
	// 비균등 스케일로 화면이 찌그러진다(매니페스트와 창 크기가 우연히 같은 데스크톱만 정상).
	float renderWidth = std::max(1.0f, m_runtimeRenderWidth);
	float renderHeight = std::max(1.0f, m_runtimeRenderHeight);
	if (CEngine* engine = GetEngine())
	{
		const RenderSurfaceSize renderSize = engine->GetRenderTargetSize();
		if (renderSize.Width > 0 && renderSize.Height > 0)
		{
			renderWidth = static_cast<float>(renderSize.Width);
			renderHeight = static_cast<float>(renderSize.Height);
		}
	}
	std::vector<GameRenderViewportDesc> viewports = CollectGameRenderViewports(*scene, renderWidth, renderHeight);
	std::vector<GameRenderLightDesc> lights = CollectGameRenderLights(*scene);
	// 런타임은 lazy RT 승격 — 블렌드/Opacity/Static/강제가 걸린 레이어만 자기 RT 를 쓴다.
	std::vector<GameRenderLayerDesc> layers = CollectGameRenderLayers(*scene, /*forceOwnTextureAll*/ false);

	if (CEngine* engine = GetEngine())
	{
		engine->SetGameRenderViewports(std::move(viewports));
		engine->SetGameRenderLights(std::move(lights));
		engine->SetGameRenderLayers(std::move(layers));
		engine->SetGameRenderBackgroundColor(scene->GetBackgroundColor());
	}
}

void CGameApplication::ShutdownRuntimeGame()
{
	if (Engine.CanvasManager)
	{
		Engine.CanvasManager->DestroyScriptInstances();
	}
	if (m_gameModuleLoader)
	{
		m_gameModuleLoader->Unload();
		m_gameModuleLoader.Reset();
	}
	if (CEngine* engine = GetEngine())
	{
		engine->SetPreRenderCallback(nullptr);
	}
	m_runtimeGameInitialized = false;
}
#endif
