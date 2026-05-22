#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/GameFramework/ECS/EntityTypes.h"
#include "Utillity/SafePtr.h"

class CImEditor;
class CRootDockWindow;
class CMainDockWindow;

struct Editor
{
public:
	inline static SafePtr<CImEditor>			ImEditor = nullptr;
	inline static SafePtr<CRootDockWindow>		RootDockWindow = nullptr;
	inline static SafePtr<CMainDockWindow>		MainDockWindow = nullptr;
	inline static SafePtr<CHierarchyTool>		Hierarchy = nullptr;
	inline static SafePtr<CSceneViewTool>		SceneView = nullptr;
	inline static SafePtr<CInspectorTool>		Inspector = nullptr;
	inline static SafePtr<CAssetBrowserTool>	AssetBrowser = nullptr;

private:
	inline static EntityId	m_selectedEntity = INVALID_ENTITY_ID;

	static void SelectEntity(EntityId entity)
	{
		m_selectedEntity = entity;
	}

	static void ClearSelection()
	{
		m_selectedEntity = INVALID_ENTITY_ID;
	}

	static EntityId GetSelectedEntity()
	{
		return m_selectedEntity;
	}
};

#endif
