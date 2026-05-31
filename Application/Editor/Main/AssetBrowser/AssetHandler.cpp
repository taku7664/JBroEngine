#include "pch.h"
#include "AssetHandler.h"

#include "Editor/Editor.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/GameFramework/Audio/AudioSystem.h"
#include "File/FileUtillities.h"
#include "StringUtillity.h"

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
		&& (EAssetType::Scene == entry.Type || entry.ExtensionUtf8 == ".jscene");
}

void CSceneAssetOpenHandler::Open(CAssetBrowserTool&, const AssetBrowserEntry& entry)
{
	if (false == Core::SceneManager.IsValid())
	{
		CSystemLog::Error(Utillity::U8(u8"씬 로드에 실패하였습니다."));
		File::OpenFile(entry.AbsolutePath);
		return;
	}

	const std::string sceneName = entry.RelativePath.empty() ? entry.DisplayNameUtf8 : ToUtf8(entry.RelativePath);
	CScene* scene = Core::SceneManager->CreateScene(sceneName.c_str());
	if (nullptr == scene)
	{
		CSystemLog::Error(Utillity::U8(u8"씬 로드에 실패하였습니다."));
		return;
	}

	CSceneSerializer serializer;
	if (ESceneSerializeResult::Success == serializer.LoadFromFile(*scene, entry.AbsolutePath))
	{
		Core::SceneManager->PreloadReferencedAssets(*scene);
		if (const EngineCore* context = Editor::ImEditor ? Editor::ImEditor->GetEditorEngineCore() : nullptr)
		{
			CSpriteRenderSystem* spriteSystem = scene->FindSystem<CSpriteRenderSystem>();
			if (nullptr == spriteSystem)
			{
				spriteSystem = scene->AddSystem<CSpriteRenderSystem>(context->RenderScene.TryGet());
			}
			if (nullptr != spriteSystem)
			{
				spriteSystem->SetRenderScene(context->RenderScene.TryGet());
				spriteSystem->SetDependencies(context->AssetManager.TryGet(), context->RHIDevice.TryGet(), context->Renderer.TryGet());
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
		Core::SceneManager->SetActiveScene(sceneName.c_str());
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
	SafePtr<CProjectManager> projectManager = GetProjectManager();
	if (false == projectManager.IsValid())
	{
		return;
	}
	projectManager->OpenScriptInIde(File::NULL_PATH);
}
