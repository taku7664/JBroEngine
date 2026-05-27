#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "GameFramework/ECS/EntityTypes.h"

class CProjectManager;
class CDebugRenderer2D;
class COutlineRenderer2D;
class IRHITexture;

// ── GameCameraDesc ─────────────────────────────────────────────────────────────
// Describes one active game camera submitted by GameViewTool each frame.
// Cameras are sorted by Priority (ascending) before being passed here;
// the first camera clears the RT, subsequent cameras load it.
struct GameCameraDesc
{
	float        PosX        = 0.0f;
	float        PosY        = 0.0f;
	// Orthographic half-extents in world units.
	// OrthoSize  = half-height (Y axis).
	// OrthoSizeX = half-width  (X axis).  Used for stretch rendering via SetViewCameraEx.
	// When both are provided, the renderer bypasses viewport-aspect derivation so
	// scaleX / scaleY independently control how much of the world is visible.
	float        OrthoSize   = 5.0f;
	float        OrthoSizeX  = 5.0f;
	// Camera rotation extracted from world transform (cos / sin of rotation angle).
	float        CosR        = 1.0f;
	float        SinR        = 0.0f;
	// Normalized viewport rect within the game-view RT [0, 1].
	float        ViewportX   = 0.0f;
	float        ViewportY   = 0.0f;
	float        ViewportW   = 1.0f;
	float        ViewportH   = 1.0f;
	float        ClearColor[4] = { 0.08f, 0.09f, 0.11f, 1.0f };
	std::int32_t Priority    = 0;
	bool         IsMainCamera = false;
};

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
	SafePtr<IImWindow>  FindImWindow(ImGuiID id);
	template<typename T>
	SafePtr<T>			FindImWindow(ImGuiID id);

	void OpenPopup(const ImPopupDesc& desc);
	const EngineContext* GetEditorEngineContext() const;
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
	void SetSceneViewFocusContext(std::vector<EntityId> contextEntities);
	void ClearSceneViewFocusContext();

	// 선택 아웃라인: 셰이더 기반 Alpha Dilation (RT 파이프라인).
	// SceneViewTool이 매 프레임 호출.
	void SetSceneViewSelection(std::vector<EntityId> selectedEntities);
	void ClearSceneViewSelection();

	// Game view (multi-camera)
	void RequestGameViewRenderTarget(std::uint32_t width, std::uint32_t height);
	// Submit all active game cameras for this frame (sorted by Priority, ascending).
	void SetGameViewCameras(const std::vector<GameCameraDesc>& cameras);
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
	std::queue<CImPopupWindow>	m_imPopupWindowQueue;

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
	std::vector<GameCameraDesc> m_gameViewCameras;

	// GPU renderer for IDebugDraw2D primitives — renders into scene RT.
	OwnerPtr<CDebugRenderer2D>  m_debugRenderer;
	// GPU Alpha-Dilation 아웃라인 렌더러.
	OwnerPtr<COutlineRenderer2D> m_outlineRenderer;

	// 포커스 오버레이 상태 (SceneViewTool → ImEditor)
	bool                         m_sceneViewFocusActive = false;
	std::unordered_set<EntityId> m_sceneViewFocusEntities;

	// 선택 아웃라인 상태
	bool                         m_sceneViewHasSelection = false;
	std::unordered_set<EntityId> m_sceneViewSelectedEntities;
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
