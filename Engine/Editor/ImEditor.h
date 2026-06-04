#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Core/Module/Module.h"
#include "GameFramework/Rendering/GameCamera.h"
#include "GameFramework/Scene/SceneTypes.h"

// ImEditor 의 멤버에서 사용하는 ImWindow 패밀리 — self-contained 보장.
#include "Editor/ImWindow/IImWindow.h"
#include "Editor/ImWindow/ImWindow.h"
#include "Editor/ImWindow/ImDockWindow.h"
#include "Editor/ImWindow/ImPopupWindow.h"
#include "Editor/ImWindow/ImWindowContext.h"   // PopupHandle

class CProjectManager;
class CDebugRenderer2D;
class COutlineRenderer2D;
class IRHITexture;
struct EngineCore;

class CImEditor : public CModule
{
public:
	CImEditor();
	virtual ~CImEditor();

public:
	void BeginFrame();
	void EndFrame();
	void Update();

public:
	template<typename T>
	SafePtr<T>			CreateImWindow(const char* key, ImGuiID parentId = 0);
	void				DestroyImWindow(ImGuiID id);

	// 콜백을 다음 Update 끝(윈도우 순회 이후)으로 지연 실행한다.
	// 윈도우 draw/순회 도중에 CreateImWindow 를 호출하면 m_imWindowVector 가
	// 재할당되어 순회 중인 반복자가 무효화되므로(크래시), 생성류는 이걸로 미룬다.
	void				QueueDeferred(std::function<void()> fn);
	SafePtr<IImWindow>  FindImWindow(ImGuiID id);
	template<typename T>
	SafePtr<T>			FindImWindow(ImGuiID id);

	// 새 모달 팝업을 등록한다.
	//   - desc.Id 가 비어있지 않고 이미 같은 Id 의 팝업이 살아있으면
	//     기존 핸들을 그대로 반환한다 (중복 방지).
	//   - 그 외에는 새 인스턴스를 만들고 신규 핸들을 반환한다.
	//   - 실패 시 INVALID_POPUP_HANDLE 반환.
	PopupHandle OpenPopup(const ImPopupDesc& desc);
	// 핸들로 명시 종료. 살아있지 않으면 no-op.
	void        ClosePopup(PopupHandle handle);
	bool        IsPopupOpen(PopupHandle handle) const;
	// Id 가 비어있지 않은 팝업 중 동일 Id 가 활성인지 검사.
	bool        IsPopupOpenById(std::string_view id) const;
	const EngineCore* GetEditorEngineCore() const;
	SafePtr<CProjectManager> GetProjectManager() const;

	// Scene view (editor camera)
	void RequestSceneViewRenderTarget(std::uint32_t width, std::uint32_t height);
	void SetSceneViewCamera(float posX, float posY, float orthographicSize);
	SafePtr<IRHITexture> GetSceneViewRenderTarget() const;
	void* GetSceneViewTextureID() const;
	std::uint32_t GetSceneViewWidth()  const { return m_sceneViewWidth;  }
	std::uint32_t GetSceneViewHeight() const { return m_sceneViewHeight; }

	// 포커스 오버레이: 흰 반투명 박스 + 포커스 스프라이트/콜라이더 재렌더 (RT 파이프라인).
	// SceneViewTool이 매 프레임 호출.
	void SetSceneViewFocusContext(std::vector<const void*> contextObjects);
	void ClearSceneViewFocusContext();

	// 선택 아웃라인: 셰이더 기반 Alpha Dilation (RT 파이프라인).
	// SceneViewTool이 매 프레임 호출.
	void SetSceneViewSelection(std::vector<const void*> selectedObjects);
	void ClearSceneViewSelection();

	// 에디터 씬뷰에서만 렌더 제외할 오브젝트 키(주소) 집합. 매 프레임 SceneViewTool 이 갱신.
	void SetSceneViewHidden(std::vector<const void*> hiddenObjects);

	// Game view (multi-camera)
	void RequestGameViewRenderTarget(std::uint32_t width, std::uint32_t height);
	// Submit all active game cameras for this frame (sorted by Priority, ascending).
	void SetGameViewCameras(const std::vector<GameRenderCameraDesc>& cameras);
	void* GetGameViewTextureID() const;
	std::uint32_t GetGameViewWidth()  const { return m_gameViewWidth;  }
	std::uint32_t GetGameViewHeight() const { return m_gameViewHeight; }

private:
	void OnPreInitialize() override;
	void OnPostInitialize() override;
	void OnPreFinalize() override;
	void OnPostFinalize() override;
	void OnBeginFrame() override;
	void OnUpdate() override;
	void OnPrepareRender() override;
	void OnRender() override;

private:
	bool InitializeImGui();
	void FinalizeImGui();

	bool DestroyImWindowEx(ImGuiID id);

private:
	ImGuiContext* m_imguiContext;
	bool m_isWin32BackendInitialized = false;
	bool m_isDX11BackendInitialized = false;

	std::unordered_map<ImGuiID, OwnerPtr<CImWindow>> m_imWindowTable;
	std::vector<CImWindow*>		m_imWindowVector;
	// 팝업 큐. ImGui::OpenPopup 의 stack 동작 특성상 모달은 한 번에 하나만
	// 정상 처리되므로 FIFO 로 유지한다.
	//   - front()      : 현재 활성 팝업 (매 프레임 Render)
	//   - 그 뒤         : 대기 — 앞 팝업이 닫히는 즉시 활성화
	//   - !IsAlive()   : ClosePopup 으로 외부 종료된 항목 (Render 이전에 정리)
	std::deque<OwnerPtr<CImPopupWindow>> m_popups;
	PopupHandle                           m_nextPopupHandle = 1;

	std::queue<std::function<void()>> m_delayEventQueue;
	OwnerPtr<CProjectManager> m_projectManager;

	// Scene view (editor camera)
	OwnerPtr<IRHITexture> m_sceneViewRenderTarget;
	std::uint32_t m_sceneViewWidth  = 0;
	std::uint32_t m_sceneViewHeight = 0;
	float m_sceneViewCamX    = 0.0f;
	float m_sceneViewCamY    = 0.0f;
	float m_sceneViewCamSize = 5.0f;
	bool m_sceneViewRequested = false;

	// Game view (multi-camera)
	OwnerPtr<IRHITexture>      m_gameViewRenderTarget;
	std::uint32_t              m_gameViewWidth    = 0;
	std::uint32_t              m_gameViewHeight   = 0;
	bool                       m_gameViewRequested = false;
	std::vector<GameRenderCameraDesc> m_gameViewCameras;

	// GPU renderer for IDebugDraw2D primitives — renders into scene RT.
	OwnerPtr<CDebugRenderer2D>  m_debugRenderer;
	// GPU Alpha-Dilation 아웃라인 렌더러.
	OwnerPtr<COutlineRenderer2D> m_outlineRenderer;

	// 포커스 오버레이 상태 (SceneViewTool → ImEditor)
	// 키 = 오브젝트 주소(불투명). 렌더 필터 집합 비교 전용 — 역참조 안 함.
	bool                            m_sceneViewFocusActive = false;
	std::unordered_set<const void*> m_sceneViewFocusEntities;

	// 선택 아웃라인 상태
	bool                            m_sceneViewHasSelection = false;
	std::unordered_set<const void*> m_sceneViewSelectedEntities;

	// 에디터 씬뷰 숨김(EditorHidden) 키 집합 — 매 프레임 SceneViewTool 이 채움.
	std::unordered_set<const void*> m_sceneViewHidden;
};

template<typename T>
inline SafePtr<T> CImEditor::CreateImWindow(const char* key, ImGuiID parentId)
{
	static_assert(std::is_base_of_v<CImWindow, T>, "T must derive from CImWindow.");

	if (nullptr == key)
	{
		return nullptr;
	}

	ImGuiID hashedID = ImHashStr(key);
	if (false == m_imWindowTable.contains(hashedID))
	{
		OwnerPtr<T> newWindow = MakeOwnerPtr<T>(hashedID, parentId);
		SafePtr<T> result = newWindow.GetSafePtr();
		result->SetStableID(key);
		m_imWindowTable[hashedID] = std::move(newWindow);
		if (CImDockWindow* parent = dynamic_cast<CImDockWindow*>(FindImWindow(parentId).TryGet()))
		{
			parent->AddChildImWindow(result);
		}
		result->Initialize();
		m_imWindowVector.push_back(result.TryGet());
		return result;
	}
	return nullptr;
}

template<typename T>
inline SafePtr<T> CImEditor::FindImWindow(ImGuiID id)
{
	static_assert(std::is_base_of_v<IImWindow, T>, "T must derive from IImWindow.");
	return DynamicSafePtrCast<T>(FindImWindow(id));
}
