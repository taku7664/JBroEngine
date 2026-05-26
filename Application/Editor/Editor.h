#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/GameFramework/ECS/EntityTypes.h"
#include "Editor/Command/EditorCommandManager.h"
#include "File/FilePath.h"
#include "Utillity/SafePtr.h"

#include <algorithm>
#include <vector>

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

	// ── 단일 선택: 이전 다중 선택 초기화 후 entity 하나만 선택 ──────────────
	static void SelectEntity(EntityId entity)
	{
		m_selectedEntities.clear();
		m_primarySelectedEntity = entity;
		if (entity != INVALID_ENTITY_ID)
			m_selectedEntities.push_back(entity);
		ClearAssetSelection();
	}

	// ── 다중 선택: entities 목록 전체 선택 (첫 항목이 primary) ───────────────
	static void SelectEntities(std::vector<EntityId> entities)
	{
		m_selectedEntities = std::move(entities);
		m_primarySelectedEntity = m_selectedEntities.empty()
			? INVALID_ENTITY_ID : m_selectedEntities.front();
		ClearAssetSelection();
	}

	// ── 개별 추가 (Ctrl+Click용): 이미 선택돼 있으면 무시 ────────────────────
	static void AddToSelection(EntityId entity)
	{
		if (entity == INVALID_ENTITY_ID) return;
		if (!IsSelected(entity))
		{
			m_selectedEntities.push_back(entity);
			if (m_primarySelectedEntity == INVALID_ENTITY_ID)
				m_primarySelectedEntity = entity;
		}
		ClearAssetSelection();
	}

	// ── 개별 제거 ─────────────────────────────────────────────────────────────
	static void RemoveFromSelection(EntityId entity)
	{
		auto it = std::find(m_selectedEntities.begin(), m_selectedEntities.end(), entity);
		if (it != m_selectedEntities.end())
			m_selectedEntities.erase(it);
		if (m_primarySelectedEntity == entity)
			m_primarySelectedEntity = m_selectedEntities.empty()
				? INVALID_ENTITY_ID : m_selectedEntities.front();
	}

	// ── 선택 여부 확인 ────────────────────────────────────────────────────────
	static bool IsSelected(EntityId entity)
	{
		return std::find(m_selectedEntities.begin(),
		                 m_selectedEntities.end(), entity) != m_selectedEntities.end();
	}

	// ── 전체 선택 목록 (outline 렌더링, 다중 처리 등) ─────────────────────────
	static const std::vector<EntityId>& GetSelectedEntities()
	{
		return m_selectedEntities;
	}

	// ── Primary 선택 엔티티 (Inspector 표시, 하위 호환성 유지) ────────────────
	static EntityId GetSelectedEntity()
	{
		return m_primarySelectedEntity;
	}

	static void ClearSelection()
	{
		m_selectedEntities.clear();
		m_primarySelectedEntity = INVALID_ENTITY_ID;
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
	inline static EntityId              m_primarySelectedEntity = INVALID_ENTITY_ID;
	inline static std::vector<EntityId> m_selectedEntities;
	inline static File::Path            m_activeScenePath   = File::NULL_PATH;
	inline static File::Guid            m_selectedAssetGuid = File::NULL_GUID;
	inline static File::Path            m_selectedAssetPath = File::NULL_PATH;
};

#endif
