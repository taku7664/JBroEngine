#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/GameFramework/Object/GameObject.h"    // CGameObject (선택 = SafePtr)
#include "Engine/GameFramework/Scene/GameLayer.h"      // CGameLayer (레이어 선택 = SafePtr)
#include "Editor/Command/EditorCommandManager.h"
#include "Editor/Shortcut/EditorShortcutManager.h"
#include "Utillity/File/FilePath.h"
#include "Utillity/Pointer/SafePtr.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

class CImEditor;
class CRootDockWindow;
class CMainDockWindow;
class CLayerTool;
class CCanvasViewTool;
class CGameViewTool;
class CInspectorTool;
class CAssetBrowserTool;
class CLogTool;
class CShortcutReferenceTool;
class CEditorStatisticsTool;
class CBuildSettingsWindow;
class CProjectSettingsWindow;
class CSpriteImporterWindow;
class CAudioImporterWindow;
class CProjectManager;
class CGameScene;

struct Editor
{
public:
	inline static SafePtr<CImEditor>				ImEditor = nullptr;
	inline static SafePtr<CRootDockWindow>			RootDockWindow = nullptr;
	inline static SafePtr<CMainDockWindow>			MainDockWindow = nullptr;
	inline static SafePtr<CLayerTool>				Layers = nullptr;
	inline static SafePtr<CCanvasViewTool>			CanvasView = nullptr;
	inline static SafePtr<CGameViewTool>			GameView = nullptr;
	inline static SafePtr<CInspectorTool>			Inspector = nullptr;
	inline static SafePtr<CAssetBrowserTool>		AssetBrowser = nullptr;
	inline static SafePtr<CLogTool>					LogTool = nullptr;
	inline static SafePtr<CShortcutReferenceTool>	ShortcutReference = nullptr;
	inline static SafePtr<CEditorStatisticsTool>	EditorStatistics = nullptr;
	inline static SafePtr<CBuildSettingsWindow>		BuildSettings = nullptr;
	inline static SafePtr<CProjectSettingsWindow>	ProjectSettings = nullptr;
	inline static SafePtr<CSpriteImporterWindow>	SpriteImporter = nullptr;
	inline static SafePtr<CAudioImporterWindow>		AudioImporter  = nullptr;
	inline static CEditorCommandManager				CommandManager;
	inline static CEditorShortcutManager			ShortcutManager;

	// ── 단일 선택: 이전 다중 선택 초기화 후 오브젝트 하나만 선택 ──────────────
	static void SelectEntity(CGameObject* object)
	{
		m_selectedObjects.clear();
		m_primarySelected = object ? object->SafeFromThis() : SafePtr<CGameObject>();
		if (object)
			m_selectedObjects.push_back(m_primarySelected);
		ClearAssetSelection();
		ClearScriptSelection();
		ClearLayerSelection();
		ClearCanvasSelection();
	}

	// ── 다중 선택: objects 목록 전체 선택 (첫 항목이 primary) ────────────────
	static void SelectEntities(std::vector<CGameObject*> objects)
	{
		m_selectedObjects.clear();
		for (CGameObject* o : objects)
			if (o) m_selectedObjects.push_back(o->SafeFromThis());
		m_primarySelected = m_selectedObjects.empty()
			? SafePtr<CGameObject>() : m_selectedObjects.front();
		ClearAssetSelection();
		ClearScriptSelection();
		ClearLayerSelection();
		ClearCanvasSelection();
	}

	// ── 개별 추가 (Ctrl+Click용): 이미 선택돼 있으면 무시 ────────────────────
	static void AddToSelection(CGameObject* object)
	{
		if (nullptr == object) return;
		if (!IsSelected(object))
		{
			m_selectedObjects.push_back(object->SafeFromThis());
			if (!m_primarySelected.IsValid())
				m_primarySelected = object->SafeFromThis();
		}
		ClearAssetSelection();
		ClearScriptSelection();
		ClearLayerSelection();
		ClearCanvasSelection();
	}

	// ── 개별 제거 ─────────────────────────────────────────────────────────────
	static void RemoveFromSelection(const CGameObject* object)
	{
		auto it = std::find_if(m_selectedObjects.begin(), m_selectedObjects.end(),
			[object](const SafePtr<CGameObject>& s) { return s.TryGet() == object; });
		if (it != m_selectedObjects.end())
			m_selectedObjects.erase(it);
		if (m_primarySelected.TryGet() == object)
			m_primarySelected = m_selectedObjects.empty()
				? SafePtr<CGameObject>() : m_selectedObjects.front();
	}

	// ── 선택 여부 확인 ────────────────────────────────────────────────────────
	static bool IsSelected(const CGameObject* object)
	{
		return std::find_if(m_selectedObjects.begin(), m_selectedObjects.end(),
			[object](const SafePtr<CGameObject>& s) { return s.TryGet() == object; })
			!= m_selectedObjects.end();
	}

	// ── 전체 선택 목록 (outline 렌더링, 다중 처리 등). 살아있는 것만 반환 ─────
	static std::vector<CGameObject*> GetSelectedEntities()
	{
		std::vector<CGameObject*> out;
		out.reserve(m_selectedObjects.size());
		for (const SafePtr<CGameObject>& s : m_selectedObjects)
			if (CGameObject* o = s.TryGet()) out.push_back(o);
		return out;
	}

	// ── Primary 선택 오브젝트 (Inspector 표시). 파괴됐으면 nullptr ────────────
	static CGameObject* GetSelectedEntity()
	{
		return m_primarySelected.TryGet();
	}

	// ── 선택 중 "최상위"만 (조상이 선택돼 있으면 제외) ───────────────────────
	// Transform 편집(인스펙터/기즈모 공용): 부모가 선택돼 있으면 자식은 부모 따라
	// 움직이므로 중복 적용 금지. 부모+자식 동시 선택 시 부모만 반환.
	static std::vector<CGameObject*> GetSelectedTopLevel()
	{
		std::vector<CGameObject*> live = GetSelectedEntities();
		std::unordered_set<const CGameObject*> selectedSet(live.begin(), live.end());

		std::vector<CGameObject*> roots;
		roots.reserve(live.size());
		for (CGameObject* object : live)
		{
			bool ancestorSelected = false;
			for (CGameObject* p = object->GetParent().TryGet(); p; p = p->GetParent().TryGet())
			{
				if (selectedSet.count(p) > 0)
				{
					ancestorSelected = true;
					break;
				}
			}
			if (false == ancestorSelected)
			{
				roots.push_back(object);
			}
		}
		return roots;
	}

	// 씬 선택(오브젝트 + 레이어 + 캔버스) 해제. 레이어/캔버스까지 지우는 이유: 호출부(씬뷰
	// 빈 곳 클릭, 에셋 선택, 활성 씬 없음)는 전부 "씬에서 고른 것을 없던 일로" 를 뜻한다.
	static void ClearSelection()
	{
		m_selectedObjects.clear();
		m_primarySelected = SafePtr<CGameObject>();
		ClearLayerSelection();
		ClearCanvasSelection();
	}

	// ── 캔버스 선택 ───────────────────────────────────────────────────────────
	// 캔버스는 런타임에 하나(활성 씬)뿐이라 대상 포인터가 필요 없다 — 켜짐/꺼짐만 든다.
	// 인스펙터가 이 플래그를 보고 캔버스 설정 패널(배경색·뷰포트)을 띄운다.
	// 에셋 브라우저에서 활성 `.jcanvas` 를 고르는 경로는 에셋 선택으로 들어와 인스펙터가
	// 같은 패널로 분기한다 — 이 플래그는 캔버스 뷰의 캔버스 노드 클릭 전용이다.
	static void SelectCanvas()
	{
		ClearSelection();
		ClearAssetSelection();
		ClearScriptSelection();
		m_canvasSelected = true;
	}

	static void ClearCanvasSelection()
	{
		m_canvasSelected = false;
	}

	static bool IsCanvasSelected()
	{
		return m_canvasSelected;
	}

	// ── 레이어 선택 ───────────────────────────────────────────────────────────
	// 오브젝트/에셋/스크립트 선택과 상호 배타 — 인스펙터가 레이어 속성 패널을 띄운다.
	// 레이어는 씬 소유(OwnerPtr)이므로 SafePtr 로 들고, 파괴되면 자동 무효화된다.
	static void SelectLayer(CGameLayer* layer)
	{
		ClearSelection();
		ClearAssetSelection();
		ClearScriptSelection();
		m_selectedLayer = layer ? layer->SafeFromThis() : SafePtr<CGameLayer>();
	}

	static void ClearLayerSelection()
	{
		m_selectedLayer = SafePtr<CGameLayer>();
	}

	// 파괴됐으면 nullptr.
	static CGameLayer* GetSelectedLayer()
	{
		return m_selectedLayer.TryGet();
	}

	static void SelectAsset(const File::Guid& guid, const File::Path& path)
	{
		m_selectedAssetGuid = guid;
		m_selectedAssetPath = path;
		ClearScriptSelection();
		ClearLayerSelection();
		ClearCanvasSelection();
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

	// ── 스크립트 파일(.h) 선택 ────────────────────────────────────────────────
	// 스크립트 루트의 .h 는 에셋 guid 가 없으므로 별도 선택 상태로 인스펙터가 스키마
	// 에디터를 띄운다. 엔티티/에셋 선택과 상호 배타.
	static void SelectScriptFile(const File::Path& path)
	{
		ClearSelection();         // 엔티티 선택 해제
		ClearAssetSelection();    // 에셋 선택 해제
		ClearLayerSelection();    // 레이어 선택 해제
		m_selectedScriptPath = path;
	}

	static void ClearScriptSelection()
	{
		m_selectedScriptPath = File::NULL_PATH;
	}

	static const File::Path& GetSelectedScriptPath()
	{
		return m_selectedScriptPath;
	}

	static void SetActiveScenePath(const File::Path& path)
	{
		m_activeScenePath = path;
	}

	static const File::Path& GetActiveScenePath()
	{
		return m_activeScenePath;
	}

	// ── 인스펙터 컴포넌트 포커스 힌트 ─────────────────────────────────────────
	// 하이어라키에서 컴포넌트를 클릭하면 그 타입 이름을 설정한다. 인스펙터는 다음
	// 렌더에서 해당 컴포넌트 탭을 선택하고 힌트를 비운다.
	static void SetFocusComponent(const char* typeName)
	{
		m_focusComponentName = (typeName != nullptr) ? typeName : "";
	}
	static const std::string& GetFocusComponent()
	{
		return m_focusComponentName;
	}
	static void ClearFocusComponent()
	{
		m_focusComponentName.clear();
	}

private:
	inline static SafePtr<CGameObject>              m_primarySelected;
	inline static std::vector<SafePtr<CGameObject>> m_selectedObjects;
	inline static SafePtr<CGameLayer>              m_selectedLayer;
	inline static bool                            m_canvasSelected = false;
	inline static File::Path            m_activeScenePath   = File::NULL_PATH;
	inline static File::Guid            m_selectedAssetGuid = File::NULL_GUID;
	inline static File::Path            m_selectedAssetPath = File::NULL_PATH;
	inline static File::Path            m_selectedScriptPath = File::NULL_PATH;
	inline static std::string           m_focusComponentName;
};

#endif
