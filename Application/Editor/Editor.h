#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/GameFramework/ECS/EntityTypes.h"
#include "Editor/Command/EditorCommandManager.h"
#include "File/FilePath.h"
#include "Utillity/SafePtr.h"

class CImEditor;
class CRootDockWindow;
class CMainDockWindow;
class CHierarchyTool;
class CSceneViewTool;
class CGameViewTool;
class CInspectorTool;
class CAssetBrowserTool;
class CLogTool;
class CProjectSettingsWindow;

struct Editor
{
public:
	inline static SafePtr<CImEditor>				ImEditor = nullptr;
	inline static SafePtr<CRootDockWindow>			RootDockWindow = nullptr;
	inline static SafePtr<CMainDockWindow>			MainDockWindow = nullptr;
	inline static SafePtr<CHierarchyTool>			Hierarchy = nullptr;
	inline static SafePtr<CSceneViewTool>			SceneView = nullptr;
	inline static SafePtr<CGameViewTool>			GameView = nullptr;
	inline static SafePtr<CInspectorTool>			Inspector = nullptr;
	inline static SafePtr<CAssetBrowserTool>		AssetBrowser = nullptr;
	inline static SafePtr<CLogTool>					LogTool = nullptr;
	inline static SafePtr<CProjectSettingsWindow>	ProjectSettings = nullptr;
	inline static CEditorCommandManager				CommandManager;

	static void SelectEntity(EntityId entity)
	{
		m_selectedEntity = entity;
		ClearAssetSelection();
	}

	static void ClearSelection()
	{
		m_selectedEntity = INVALID_ENTITY_ID;
	}

	static EntityId GetSelectedEntity()
	{
		return m_selectedEntity;
	}

	static void SelectAsset(const File::Guid& guid, const File::Path& path)
	{
		m_selectedAssetGuid = guid;
		m_selectedAssetPath = path;
	}

	static void ClearAssetSelection()
	{
		m_selectedAssetGuid = File::NULL_GUID;
		m_selectedAssetPath = File::NULL_PATH;
	}

	static const File::Guid& GetSelectedAssetGuid()
	{
		return m_selectedAssetGuid;
	}

	static const File::Path& GetSelectedAssetPath()
	{
		return m_selectedAssetPath;
	}

	static void SetActiveScenePath(const File::Path& path)
	{
		m_activeScenePath = path;
	}

	static const File::Path& GetActiveScenePath()
	{
		return m_activeScenePath;
	}

private:
	inline static EntityId	m_selectedEntity = INVALID_ENTITY_ID;
	inline static File::Path m_activeScenePath = File::NULL_PATH;
	inline static File::Guid m_selectedAssetGuid = File::NULL_GUID;
	inline static File::Path m_selectedAssetPath = File::NULL_PATH;
};

#endif
