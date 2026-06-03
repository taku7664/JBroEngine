#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/GameFramework/Scene/SceneTypes.h"
#include "Engine/GameFramework/Scene/SceneManager.h"   // CSceneManager 사용
#include "Engine/GameFramework/Object/GameObject.h"    // CGameObject (선택 = SafePtr)
#include "Engine/Editor/ImEditor.h"   // CImEditor 사용
#include "Editor/Command/EditorCommandManager.h"
#include "Utillity/File/FilePath.h"
#include "Utillity/Pointer/SafePtr.h"

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
class CBuildSettingsWindow;
class CProjectSettingsWindow;
class CSpriteImporterWindow;
class CAudioImporterWindow;
class CProjectManager;
class CScene;

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
	inline static SafePtr<CBuildSettingsWindow>		BuildSettings = nullptr;
	inline static SafePtr<CProjectSettingsWindow>	ProjectSettings = nullptr;
	inline static SafePtr<CSpriteImporterWindow>	SpriteImporter = nullptr;
	inline static SafePtr<CAudioImporterWindow>		AudioImporter  = nullptr;
	inline static CEditorCommandManager				CommandManager;

	// ── 단일 선택: 이전 다중 선택 초기화 후 오브젝트 하나만 선택 ──────────────
	static void SelectEntity(CGameObject* object)
	{
		m_selectedObjects.clear();
		m_primarySelected = object ? object->SafeFromThis() : SafePtr<CGameObject>();
		if (object)
			m_selectedObjects.push_back(m_primarySelected);
		ClearAssetSelection();
		ClearScriptSelection();
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

	static void ClearSelection()
	{
		m_selectedObjects.clear();
		m_primarySelected = SafePtr<CGameObject>();
	}

	static void SelectAsset(const File::Guid& guid, const File::Path& path)
	{
		m_selectedAssetGuid = guid;
		m_selectedAssetPath = path;
		ClearScriptSelection();
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
	inline static File::Path            m_activeScenePath   = File::NULL_PATH;
	inline static File::Guid            m_selectedAssetGuid = File::NULL_GUID;
	inline static File::Path            m_selectedAssetPath = File::NULL_PATH;
	inline static File::Path            m_selectedScriptPath = File::NULL_PATH;
	inline static std::string           m_focusComponentName;
};

inline SafePtr<CProjectManager> GetProjectManager()
{
	return Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
}

inline CScene* GetActiveScene()
{
	if (false == Core::SceneManager.IsValid())
	{
		return nullptr;
	}

	SafePtr<CScene> activeScene = Core::SceneManager->GetActiveScene();
	return activeScene.TryGet();
}

inline std::string ToUtf8(const std::filesystem::path& path)
{
	const auto text = path.generic_u8string();
	return std::string(reinterpret_cast<const char*>(text.c_str()), text.size());
}

inline std::string ToLower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
		});
	return text;
}

inline std::time_t ToTimeT(std::filesystem::file_time_type fileTime)
{
	const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
		fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
	return std::chrono::system_clock::to_time_t(systemTime);
}

inline const char* GetTypeName(EAssetType type)
{
	switch (type)
	{
	case EAssetType::Sprite: return "Sprite";
	case EAssetType::Mesh: return "Mesh";
	case EAssetType::Material: return "Material";
	case EAssetType::Shader: return "Shader";
	case EAssetType::Scene: return "Scene";
	case EAssetType::Prefab: return "Prefab";
	case EAssetType::Script: return "Script";
	case EAssetType::Audio:  return "Audio";
	case EAssetType::Custom: return "Custom";
	default: return "Unknown";
	}
}

#endif
