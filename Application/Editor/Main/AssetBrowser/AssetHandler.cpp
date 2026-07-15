#include "pch.h"
#include "AssetHandler.h"

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Editor/Main/AssetBrowser/AssetBrowserUtils.h"
#include "Editor/Path/EditorPathUtils.h"
#include "Editor/Main/Inspector/EffectEditorWindow.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Core/RuntimeConfig.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/GameFramework/Audio/AudioSystem.h"
#include "Engine/GameFramework/Rendering/TextRenderSystem.h"
#include "Engine/GameFramework/Scene/SceneRuntimeAccess.h"
#include "Utillity/File/FileUtillities.h"
#include "Utillity/String/StringUtillity.h"

bool CDefaultAssetOpenHandler::CanOpen(const AssetBrowserEntry& entry) const
{
	return true;
}

void CDefaultAssetOpenHandler::Open(CAssetBrowserTool& browser, const AssetBrowserEntry& entry)
{
	if (entry.IsDirectory)
	{
		browser.SetFocusFolderPath(entry.AbsolutePath);
		return;
	}

	File::OpenFile(entry.AbsolutePath);
}

bool CSceneAssetOpenHandler::CanOpen(const AssetBrowserEntry& entry) const
{
	return false == entry.IsDirectory
		&& (EAssetType::Scene == entry.Type || entry.ExtensionUtf8 == ".jcanvas");
}

void CSceneAssetOpenHandler::Open(CAssetBrowserTool&, const AssetBrowserEntry& entry)
{
	if (false == Engine.SceneManager.IsValid())
	{
		CSystemLog::Error(Utillity::U8(u8"씬 로드에 실패하였습니다."));
		File::OpenFile(entry.AbsolutePath);
		return;
	}

	const std::string sceneName = entry.RelativePath.empty()
		? entry.DisplayNameUtf8
		: EditorPathUtils::ToUtf8(entry.RelativePath);
	CGameScene* scene = Engine.SceneManager->CreateScene(sceneName.c_str());
	if (nullptr == scene)
	{
		CSystemLog::Error(Utillity::U8(u8"씬 로드에 실패하였습니다."));
		return;
	}

	CSceneSerializer serializer;
	if (ESceneSerializeResult::Success == serializer.LoadFromFile(*scene, entry.AbsolutePath))
	{
		Engine.SceneManager->AcquireReferencedAssets(*scene);
		if (const EngineCore* context = Editor::ImEditor ? Editor::ImEditor->GetEditorEngineCore() : nullptr)
		{
			CSpriteAnimationSystem* animationSystem = CSceneRuntimeAccess::FindSystem<CSpriteAnimationSystem>(*scene);
			if (nullptr == animationSystem)
			{
				animationSystem = CSceneRuntimeAccess::AddSystem<CSpriteAnimationSystem>(*scene, context->AssetManager);
			}
			if (nullptr != animationSystem)
			{
				animationSystem->SetAssetManager(context->AssetManager);
			}

			CSpriteRenderSystem* spriteSystem = CSceneRuntimeAccess::FindSystem<CSpriteRenderSystem>(*scene);
			if (nullptr == spriteSystem)
			{
				spriteSystem = CSceneRuntimeAccess::AddSystem<CSpriteRenderSystem>(*scene, context->RenderScene.TryGet());
			}
			if (nullptr != spriteSystem)
			{
				spriteSystem->SetRenderScene(context->RenderScene.TryGet());
				spriteSystem->SetDependencies(context->AssetManager.TryGet(), context->RHIDevice.TryGet(), context->Renderer.TryGet(),
					context->RenderResourceCache.TryGet(), Runtime.PixelsPerUnit);
			}

			CShapeRenderSystem* shapeSystem = CSceneRuntimeAccess::FindSystem<CShapeRenderSystem>(*scene);
			if (nullptr == shapeSystem)
			{
				shapeSystem = CSceneRuntimeAccess::AddSystem<CShapeRenderSystem>(*scene, context->RenderScene.TryGet());
			}
			if (nullptr != shapeSystem)
			{
				shapeSystem->SetRenderScene(context->RenderScene.TryGet());
				shapeSystem->SetDependencies(context->RHIDevice.TryGet(), context->Renderer.TryGet());
			}

			CTextRenderSystem* textSystem = CSceneRuntimeAccess::FindSystem<CTextRenderSystem>(*scene);
			if (nullptr == textSystem)
			{
				textSystem = CSceneRuntimeAccess::AddSystem<CTextRenderSystem>(*scene, context->RenderScene.TryGet());
			}
			if (nullptr != textSystem)
			{
				textSystem->SetRenderScene(context->RenderScene.TryGet());
				textSystem->SetDependencies(context->AssetManager.TryGet(), context->RHIDevice.TryGet(), context->Renderer.TryGet(),
					Runtime.PixelsPerUnit, Runtime.DefaultFontFamilyGuid, Runtime.FallbackFontFamilies);
			}

			CAudioSystem* audioSystem = CSceneRuntimeAccess::FindSystem<CAudioSystem>(*scene);
			if (nullptr == audioSystem)
			{
				audioSystem = CSceneRuntimeAccess::AddSystem<CAudioSystem>(*scene, context->Audio, context->AssetManager);
			}
			if (nullptr != audioSystem)
			{
				audioSystem->SetDevice(context->Audio);
				audioSystem->SetAssetManager(context->AssetManager);
			}
		}
		Engine.SceneManager->SetActiveScene(sceneName.c_str());
		Editor::SetActiveScenePath(entry.AbsolutePath);
		Editor::CommandManager.SetActiveDocument(sceneName.c_str());
		Editor::CommandManager.MarkSaved(sceneName.c_str());
		CSystemLog::Info("Scene loaded.");
	}
	else
	{
		CSystemLog::Error(Utillity::U8(u8"씬 로드에 실패하였습니다."));
	}
}

// ── ScriptAssetOpenHandler ───────────────────────────────────────────────────
bool CScriptAssetOpenHandler::CanOpen(const AssetBrowserEntry& entry) const
{
	if (entry.IsDirectory)
	{
		return false;
	}
	if (EAssetType::Script == entry.Type)
	{
		return true;
	}
	// 확장자 fallback — 디스크 탐색만 했고 아직 Type 이 세팅 안 된 경우 대응
	return entry.ExtensionUtf8 == ".cpp" || entry.ExtensionUtf8 == ".h" || entry.ExtensionUtf8 == ".hpp";
}

void CScriptAssetOpenHandler::Open(CAssetBrowserTool&, const AssetBrowserEntry& entry)
{
	SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
	if (false == projectManager.IsValid())
	{
		return;
	}
	projectManager->OpenScriptInIde(File::NULL_PATH);
}

// ── EffectAssetOpenHandler ───────────────────────────────────────────────────
bool CEffectAssetOpenHandler::CanOpen(const AssetBrowserEntry& entry) const
{
	return false == entry.IsDirectory
		&& (EAssetType::AudioEffect == entry.Type || entry.ExtensionUtf8 == ".jfx");
}

void CEffectAssetOpenHandler::Open(CAssetBrowserTool&, const AssetBrowserEntry& entry)
{
	EffectEditorWindow::Open(entry.Guid, entry.DisplayNameUtf8);
}
