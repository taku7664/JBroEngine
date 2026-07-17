#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Core/Module/Module.h"
#include "Core/Platform/PlatformTypes.h"   // SurfaceEvent / SurfaceEventToken
#include "Core/Renderer/RendererTypes.h"
#include "GameFramework/Rendering/GameCamera.h"
#include "GameFramework/Canvas/CanvasTypes.h"

// ImEditor 의 멤버에서 사용하는 ImWindow 패밀리 — self-contained 보장.
#include "Editor/ImWindow/IImWindow.h"
#include "Editor/ImWindow/ImWindow.h"
#include "Editor/ImWindow/ImDockWindow.h"
#include "Editor/ImWindow/ImPopupWindow.h"
#include "Editor/ImWindow/ImWindowContext.h"   // PopupHandle

class CProjectManager;
class CDebugRenderer2D;
class CGameCanvas;
class COutlineRenderer2D;
class IRHITexture;
struct ScriptCore;

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
	void SetCanvasViewCamera(float posX, float posY, float orthographicSize);
	SafePtr<IRHITexture> GetSceneViewRenderTarget() const;
	void* GetSceneViewTextureID() const;
	std::uint32_t GetSceneViewWidth()  const { return m_sceneViewWidth;  }
	std::uint32_t GetSceneViewHeight() const { return m_sceneViewHeight; }

	// 레이어 썸네일 — 캔버스(기본 뷰포트) 카메라로 레이어를 하나씩 그린 축소판. 편집 카메라와
	// 무관하다(캔버스뷰 팬/줌·창 개폐에 영향받지 않는다). 하이어라키가 매 프레임 원하는 높이를
	// 요청하고(0 = 사용 안 함 → RT 해제·작업 생략), 폭은 프로젝트 해상도 종횡비로 정해진다.
	void RequestLayerThumbnails(std::uint32_t height);
	// layerIndex = CGameLayer::GetIndex(). 없거나 아직 안 그려졌으면 nullptr.
	void* GetLayerThumbnailTextureID(std::uint16_t layerIndex) const;
	// 캔버스(기본 뷰포트) 카메라로 레이어별 썸네일을 그린다. OnPrepareRender 가 호출.
	void RenderLayerThumbnails();
	std::uint32_t GetLayerThumbnailWidth()  const { return m_layerThumbnailWidth;  }
	std::uint32_t GetLayerThumbnailHeight() const { return m_layerThumbnailHeight; }

	// 포커스 오버레이: 흰 반투명 박스 + 포커스 스프라이트/콜라이더 재렌더 (RT 파이프라인).
	// SceneViewTool이 매 프레임 호출.
	void SetSceneViewFocusContext(std::vector<const void*> contextObjects);
	void ClearSceneViewFocusContext();

	// 선택 아웃라인: 셰이더 기반 Alpha Dilation (RT 파이프라인).
	// SceneViewTool이 매 프레임 호출.
	void SetSceneViewSelection(std::vector<const void*> selectedObjects);
	void ClearSceneViewSelection();

	// 에디터 캔버스뷰에서만 렌더 제외할 오브젝트 키(주소) 집합. 매 프레임 SceneViewTool 이 갱신.
	void SetSceneViewHidden(std::vector<const void*> hiddenObjects);

	// Game view (multi-camera)
	void RequestGameViewRenderTarget(std::uint32_t width, std::uint32_t height);
	// 게임뷰가 그릴 캔버스 — 카메라/라이트 스냅샷은 PrepareRender(시뮬 이후·렌더 직전)가
	// 이 캔버스에서 직접 수집한다. UI 빌드 시점 수집은 1프레임 지연 카메라를 만든다.
	void SetGameViewScene(SafePtr<CGameCanvas> scene);
	void* GetGameViewTextureID() const;
	std::uint32_t GetGameViewWidth()  const { return m_gameViewWidth;  }
	std::uint32_t GetGameViewHeight() const { return m_gameViewHeight; }
	bool TryGetCameraCullingStats(const void* cameraOwnerObject, RenderCullingStats& outStats) const;

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
	void FinalizeImWindows();
	void FinalizeImGui();

	bool DestroyImWindowEx(ImGuiID id);

	// 윈도우 이벤트(메인 surface) 단일 구독자. ImEditor 가 받아 에디터 하위로 분배한다.
	// 현재: 포커스 복귀 시 라이브 컴파일 재빌드 1회.
	void OnSurfaceEvent(const SurfaceEvent& surfaceEvent);

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

	// 메인 surface 윈도우 이벤트 구독 토큰(OnPostInitialize 구독, OnPreFinalize 해지).
	SurfaceEventToken m_surfaceEventToken = 0;

	// Scene view (editor camera)
	OwnerPtr<IRHITexture> m_sceneViewRenderTarget;
	std::uint32_t m_sceneViewWidth  = 0;
	std::uint32_t m_sceneViewHeight = 0;
	float m_sceneViewCamX    = 0.0f;
	float m_sceneViewCamY    = 0.0f;
	float m_sceneViewCamSize = 5.0f;
	bool m_sceneViewRequested = false;
	// 편집 뷰가 합성할 활성 캔버스의 레이어 스냅샷(OnPrepareRender 가 매 프레임 갱신).
	std::vector<GameRenderLayerDesc> m_sceneViewLayers;

	// 레이어 썸네일 — 인덱스 = 레이어 인덱스. 크기가 바뀌면 전부 재생성한다.
	std::vector<OwnerPtr<IRHITexture>> m_layerThumbnails;
	std::uint32_t m_layerThumbnailWidth  = 0;
	std::uint32_t m_layerThumbnailHeight = 0;
	std::uint32_t m_layerThumbnailRequestedHeight = 0;

	// Game view (multi-camera)
	OwnerPtr<IRHITexture>      m_gameViewRenderTarget;
	std::uint32_t              m_gameViewWidth    = 0;
	std::uint32_t              m_gameViewHeight   = 0;
	bool                       m_gameViewRequested = false;
	SafePtr<CGameCanvas>        m_gameViewScene;
	std::vector<GameRenderViewportDesc> m_gameViewViewports;
	std::vector<GameRenderLightDesc> m_gameViewLights;
	std::vector<GameRenderLayerDesc> m_gameViewLayers;
	std::unordered_map<const void*, RenderCullingStats> m_gameViewCameraCullingStats;

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

	// 에디터 캔버스뷰 숨김(EditorHidden) 키 집합 — 매 프레임 SceneViewTool 이 채움.
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
