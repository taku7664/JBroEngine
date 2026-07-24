#include "pch.h"
#include "ImEditor.h"

#include "Core/EngineCore.h"
#include "Core/Platform/IRenderSurface.h"
#include "Core/Debug/DebugDraw2D.h"
#include "Core/Debug/DebugRenderer2D.h"
#include "Core/Debug/OutlineRenderer2D.h"
#include "Core/Renderer/Forward2DRenderer.h"
#include "Core/Renderer/IRenderer.h"
#include "Core/Renderer/IRenderScene.h"
#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/IRHIGpuTimer.h"
#include "Core/RHI/IRHITexture.h"
#include "Editor/Project/ProjectManager.h"
#include "GameFramework/Canvas/Canvas.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
	// 셰이더 리소스 겸용 렌더타겟 생성(없을 때만). 캔버스뷰/게임뷰/레이어 썸네일 공용.
	bool EnsureRenderTexture(IRHIDevice& device, OwnerPtr<IRHITexture>& texture,
	                         std::uint32_t width, std::uint32_t height)
	{
		if (false == static_cast<bool>(texture))
		{
			RHITexture2DDesc desc;
			desc.Width     = width;
			desc.Height    = height;
			desc.Format    = ERHITextureFormat::RGBA8;
			desc.BindFlags = static_cast<RHITextureBindFlags>(ERHITextureBindFlag::ShaderResource) |
			                 static_cast<RHITextureBindFlags>(ERHITextureBindFlag::RenderTarget);
			texture = device.CreateTexture2D(desc, nullptr);
		}
		return static_cast<bool>(texture);
	}
}

CImEditor::CImEditor()
	: m_imguiContext(nullptr)
	, m_isWin32BackendInitialized(false)
	, m_isDX11BackendInitialized(false)
{
}

CImEditor::~CImEditor()
{
    // 정상 경로는 OnPreFinalize에서 먼저 비워진다. 초기화 중 실패처럼 모듈
    // Finalize가 생략된 경로에서도 창별 OnDestroy와 ImGui 자원을 놓는 안전망이다.
    FinalizeImWindows();
    FinalizeImGui();
}

void CImEditor::BeginFrame()
{
    if (nullptr == m_imguiContext)
    {
        return;
    }

    if (m_isWin32BackendInitialized)
    {
        ImGui_ImplWin32_NewFrame();
    }

    if (m_isDX11BackendInitialized)
    {
        ImGui_ImplDX11_NewFrame();
    }

    ImGui::NewFrame();
}

void CImEditor::EndFrame()
{
    if (nullptr == m_imguiContext)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::Render();

    if (m_isDX11BackendInitialized)
    {
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    if (m_isWin32BackendInitialized && m_isDX11BackendInitialized && (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void CImEditor::Update()
{
    for (CImWindow* wnd : m_imWindowVector)
    {
        if (wnd && 0 == wnd->GetOwnerID())
        {
            wnd->Update();
            if (false == wnd->IsAlive())
            {
                DestroyImWindow(wnd->GetID());
            }
        }
    }
    // 팝업 큐 처리. ImGui::OpenPopup / BeginPopupModal 의 stack 동작 때문에
    // 한 프레임에 하나의 모달만 정상적으로 활성화된다 — FIFO 로 처리.
    // 1) 외부 ClosePopup 으로 dead 표시된 항목은 Render 이전에 정리.
    while (false == m_popups.empty() && (!m_popups.front() || false == m_popups.front()->IsAlive()))
    {
        m_popups.pop_front();
    }
    // 2) front 만 Render. Render 가 false 를 반환하면 그 자리에서 pop_front
    //    하여 다음 프레임에 다음 항목이 활성화된다.
    if (false == m_popups.empty())
    {
        if (false == m_popups.front()->Render())
        {
            m_popups.pop_front();
        }
    }
    // 3) 대기 항목 중에도 외부에서 close 된 게 있을 수 있으므로 한 번 더 청소.
    m_popups.erase(
        std::remove_if(m_popups.begin(), m_popups.end(),
            [](const OwnerPtr<CImPopupWindow>& p) { return !p || false == p->IsAlive(); }),
        m_popups.end());

    while (false == m_delayEventQueue.empty())
    {
        if (auto& event = m_delayEventQueue.front())
        {
            event();
        }
        m_delayEventQueue.pop();
    }
}

void CImEditor::QueueDeferred(std::function<void()> fn)
{
    if (fn)
    {
        m_delayEventQueue.push(std::move(fn));
    }
}

void CImEditor::DestroyImWindow(ImGuiID id)
{
    m_delayEventQueue.push([this, id]() {
        DestroyImWindowEx(id);
        });
}

bool CImEditor::DestroyImWindowEx(ImGuiID id)
{
    auto it = m_imWindowTable.find(id);
    if (it != m_imWindowTable.end())
    {
        CImWindow* pWnd = it->second.Get();
        if (pWnd)
        {
            pWnd->Finalize();
            ImGuiID destID = pWnd->GetID();
            ImGuiID parentID = pWnd->GetOwnerID();
            if (CImDockWindow* parent = dynamic_cast<CImDockWindow*>(FindImWindow(parentID).TryGet()))
            {
                parent->RemoveChildImWindow(destID);
            }
            m_imWindowVector.erase(std::remove_if(
                m_imWindowVector.begin(),
                m_imWindowVector.end(),
                [id](CImWindow* wnd) {
                    return wnd->GetID() == id;
                }),
                m_imWindowVector.end()
            );
        }
        m_imWindowTable.erase(it);
        return true;
    }
    return false;
}

SafePtr<IImWindow> CImEditor::FindImWindow(ImGuiID id)
{
    auto it = m_imWindowTable.find(id);
    if (it != m_imWindowTable.end())
    {
        return it->second.GetSafePtr();
    }
    return nullptr;
}

SafePtr<CProjectManager> CImEditor::GetProjectManager() const
{
	return m_projectManager.GetSafePtr();
}

bool CImEditor::TryGetCameraCullingStats(const void* cameraOwnerObject, RenderCullingStats& outStats) const
{
	if (nullptr == cameraOwnerObject)
	{
		return false;
	}

	const auto it = m_gameViewCameraCullingStats.find(cameraOwnerObject);
	if (it == m_gameViewCameraCullingStats.end())
	{
		return false;
	}

	outStats = it->second;
	return true;
}

void CImEditor::RequestCanvasViewRenderTarget(std::uint32_t width, std::uint32_t height)
{
	m_canvasViewRequested = 0 != width && 0 != height;
	if (false == m_canvasViewRequested)
	{
		return;
	}

	if (m_canvasViewWidth != width || m_canvasViewHeight != height)
	{
		m_canvasViewRenderTarget.Reset();
		m_canvasViewWidth  = width;
		m_canvasViewHeight = height;
	}
}

void CImEditor::SetCanvasViewCamera(float posX, float posY, float orthographicSize)
{
	m_canvasViewCamX    = posX;
	m_canvasViewCamY    = posY;
	m_canvasViewCamSize = orthographicSize > 0.0f ? orthographicSize : 5.0f;
}

SafePtr<IRHITexture> CImEditor::GetCanvasViewRenderTarget() const
{
	return m_canvasViewRenderTarget.GetSafePtr();
}

void* CImEditor::GetCanvasViewTextureID() const
{
	if (false == static_cast<bool>(m_canvasViewRenderTarget))
	{
		return nullptr;
	}
	return m_canvasViewRenderTarget->GetNativeHandle().ShaderResourceView;
}

void CImEditor::RequestLayerThumbnails(std::uint32_t height)
{
	m_layerThumbnailRequestedHeight = height;
	if (0 == height)
	{
		// 아무도 안 보면 VRAM 을 물고 있을 이유가 없다(레이어 수 × RT).
		m_layerThumbnails.clear();
		m_layerThumbnailWidth  = 0;
		m_layerThumbnailHeight = 0;
	}
}

void* CImEditor::GetLayerThumbnailTextureID(std::uint16_t layerIndex) const
{
	if (layerIndex >= m_layerThumbnails.size())
	{
		return nullptr;
	}
	const OwnerPtr<IRHITexture>& thumbnail = m_layerThumbnails[layerIndex];
	if (false == static_cast<bool>(thumbnail))
	{
		return nullptr;
	}
	return thumbnail->GetNativeHandle().ShaderResourceView;
}

void CImEditor::RenderLayerThumbnails()
{
	if (0 == m_layerThumbnailRequestedHeight)
	{
		return;
	}

	const EngineCore* engineCore = GetEditorEngineCore();
	if (nullptr == engineCore || false == engineCore->RHIDevice.IsValid()
		|| false == engineCore->Renderer.IsValid() || false == engineCore->RenderScene.IsValid()
		|| false == Engine.CanvasManager.IsValid())
	{
		return;
	}

	CGameCanvas* activeCanvas = Engine.CanvasManager->GetActiveCanvas().TryGet();
	if (nullptr == activeCanvas || m_canvasViewLayers.empty())
	{
		return;
	}

	SafePtr<IRHICommandContext> commandContext = engineCore->RHIDevice->GetImmediateCommandContext();
	if (false == commandContext.IsValid())
	{
		return;
	}

	// 크기 = 요청 높이 × 캔버스(프로젝트 해상도) 종횡비. 카메라도 같은 종횡비로 세우므로
	// 썸네일은 "게임 화면을 그대로 축소한 그림" 이 된다.
	float canvasWidth  = 16.0f;
	float canvasHeight = 9.0f;
	if (SafePtr<CProjectManager> projectManager = GetProjectManager();
	    projectManager.IsValid() && projectManager->IsProjectLoaded())
	{
		canvasWidth  = static_cast<float>(projectManager->GetResolutionWidth());
		canvasHeight = static_cast<float>(projectManager->GetResolutionHeight());
	}

	const std::uint32_t thumbnailHeight = m_layerThumbnailRequestedHeight;
	const std::uint32_t thumbnailWidth  = std::max(1u, static_cast<std::uint32_t>(
		static_cast<float>(thumbnailHeight) * canvasWidth / std::max(1.0f, canvasHeight)));

	if (m_layerThumbnailWidth != thumbnailWidth || m_layerThumbnailHeight != thumbnailHeight
		|| m_layerThumbnails.size() != m_canvasViewLayers.size())
	{
		m_layerThumbnails.clear();
		m_layerThumbnails.resize(m_canvasViewLayers.size());
		m_layerThumbnailWidth  = thumbnailWidth;
		m_layerThumbnailHeight = thumbnailHeight;
	}

	for (OwnerPtr<IRHITexture>& thumbnail : m_layerThumbnails)
	{
		EnsureRenderTexture(*engineCore->RHIDevice, thumbnail, thumbnailWidth, thumbnailHeight);
	}

	// 썸네일 = "게임 화면에서 이 레이어만 켠 그림". 그래서 게임뷰와 같은 경로를 태운다 —
	// 뷰포트 렉트(스플릿)·뷰포트별 레이어 필터·패럴랙스가 전부 그 안에 있다. 카메라 하나를
	// 뽑아 쓰면 스플릿에서 나머지 뷰포트의 내용이 통째로 빠진다.
	const std::vector<GameRenderViewportDesc> viewports = CollectGameRenderViewports(
		*activeCanvas, static_cast<float>(thumbnailWidth), static_cast<float>(thumbnailHeight));

	// 바탕은 불투명으로 — 스프라이트가 premultiplied 로 얹혀 결과 알파가 1 로 유지된다.
	// (투명 배경이면 ImGui 의 straight-alpha 블렌드에서 색이 어두워진다.)
	const float thumbnailBackground[4] = { 0.08f, 0.09f, 0.11f, 1.0f };
	const RenderSurfaceSize thumbnailSize{
		static_cast<int>(thumbnailWidth), static_cast<int>(thumbnailHeight) };

	for (const GameRenderLayerDesc& layer : m_canvasViewLayers)
	{
		if (layer.Index >= m_layerThumbnails.size()
			|| false == static_cast<bool>(m_layerThumbnails[layer.Index]))
		{
			continue;
		}

		// 이 레이어 하나만 담은 목록으로 호출 → 뷰포트 순회는 그대로, 그리는 레이어만 하나.
		// 포토샵 썸네일처럼 "원본 내용" 을 보여주려고 표시 속성은 중립값으로 덮는다.
		GameRenderLayerDesc thumbnailLayer = layer;
		thumbnailLayer.Visible         = true;                      // 꺼둔 레이어도 내용은 보여준다
		thumbnailLayer.Opacity         = 1.0f;                      // 페이드 상태 무시
		thumbnailLayer.BlendMode       = ERHIBlendMode::LayerNormal; // 아래 레이어가 없으니 무의미
		thumbnailLayer.NeedsOwnTexture = false;                     // 합성할 게 없다 → RT 왕복 생략
		const std::vector<GameRenderLayerDesc> singleLayer{ thumbnailLayer };

		RenderGameViewports(
			*commandContext,
			*engineCore->Renderer,
			*engineCore->RenderScene,
			viewports,
			thumbnailSize,
			m_layerThumbnails[layer.Index].GetSafePtr(),
			nullptr,
			/*lights*/ {},   // 썸네일은 라이팅 미적용 — 레이어 내용 식별이 목적이다
			singleLayer,
			thumbnailBackground);
	}
}

void CImEditor::RequestGameViewRenderTarget(std::uint32_t width, std::uint32_t height)
{
	m_gameViewRequested = 0 != width && 0 != height;
	if (false == m_gameViewRequested)
	{
		// No camera this frame — destroy the RT so GetGameViewTextureID() returns null
		// and GameViewTool shows the "No Camera" overlay instead of a stale image.
		m_gameViewRenderTarget.Reset();
		m_gameViewWidth  = 0;
		m_gameViewHeight = 0;
		return;
	}

	if (m_gameViewWidth != width || m_gameViewHeight != height)
	{
		m_gameViewRenderTarget.Reset();
		m_gameViewWidth  = width;
		m_gameViewHeight = height;
	}
}

void CImEditor::SetGameViewCanvas(SafePtr<CGameCanvas> canvas)
{
	m_gameViewCanvas = canvas;
}

void* CImEditor::GetGameViewTextureID() const
{
	if (false == static_cast<bool>(m_gameViewRenderTarget))
	{
		return nullptr;
	}
	return m_gameViewRenderTarget->GetNativeHandle().ShaderResourceView;
}

PopupHandle CImEditor::OpenPopup(const ImPopupDesc& desc)
{
    // Id 가 비어있지 않으면 같은 Id 의 활성 팝업이 있는지 확인 후 재활용.
    if (false == desc.Id.empty())
    {
        for (const OwnerPtr<CImPopupWindow>& p : m_popups)
        {
            if (p && p->IsAlive() && p->GetId() == desc.Id)
            {
                return p->GetHandle();
            }
        }
    }

    const PopupHandle handle = m_nextPopupHandle++;
    if (INVALID_POPUP_HANDLE == m_nextPopupHandle) ++m_nextPopupHandle; // 0 회피(64bit 사실상 발생 X)

    OwnerPtr<CImPopupWindow> popup = MakeOwnerPtr<CImPopupWindow>(handle, desc);
    if (!popup)
    {
        return INVALID_POPUP_HANDLE;
    }
    m_popups.push_back(std::move(popup));
    return handle;
}

void CImEditor::ClosePopup(PopupHandle handle)
{
    if (INVALID_POPUP_HANDLE == handle) return;
    for (const OwnerPtr<CImPopupWindow>& p : m_popups)
    {
        if (p && p->GetHandle() == handle)
        {
            p->Close();
            return;
        }
    }
}

bool CImEditor::IsPopupOpen(PopupHandle handle) const
{
    if (INVALID_POPUP_HANDLE == handle) return false;
    for (const OwnerPtr<CImPopupWindow>& p : m_popups)
    {
        if (p && p->GetHandle() == handle) return p->IsAlive();
    }
    return false;
}

bool CImEditor::IsPopupOpenById(std::string_view id) const
{
    if (id.empty()) return false;
    for (const OwnerPtr<CImPopupWindow>& p : m_popups)
    {
        if (p && p->IsAlive() && p->GetId() == id) return true;
    }
    return false;
}

const EngineCore* CImEditor::GetEditorEngineCore() const
{
    return (&Engine);
}

void CImEditor::OnPreInitialize()
{
}

void CImEditor::OnPostInitialize()
{
    InitializeImGui();
    m_projectManager = MakeOwnerPtr<CProjectManager>();
    if (m_projectManager)
    {
        m_projectManager->Initialize();
    }

    // Initialize GPU debug/outline renderers.
    if (const EngineCore* engineCore = (&Engine))
    {
        if (engineCore->RHIDevice.IsValid())
        {
            m_debugRenderer = MakeOwnerPtr<CDebugRenderer2D>();
            if (m_debugRenderer)
            {
                if (false == m_debugRenderer->Initialize(engineCore->RHIDevice))
                    m_debugRenderer.Reset();
            }

            m_outlineRenderer = MakeOwnerPtr<COutlineRenderer2D>();
            if (m_outlineRenderer)
            {
                if (false == m_outlineRenderer->Initialize(engineCore->RHIDevice))
                    m_outlineRenderer.Reset();
            }
        }
    }

    // 메인 surface 윈도우 이벤트 구독(단일 채널). ImEditor 가 받아 하위로 분배한다.
    if (Engine.MainRenderSurface)
    {
        m_surfaceEventToken = Engine.MainRenderSurface->Subscribe(
            [this](const SurfaceEvent& surfaceEvent) { OnSurfaceEvent(surfaceEvent); });
    }
}

void CImEditor::OnSurfaceEvent(const SurfaceEvent& surfaceEvent)
{
    // 라이브 컴파일 재빌드는 *포커스 복귀(FocusGained)에만* 한다. FocusLost/Resized 는 무시.
    switch (surfaceEvent.Type)
    {
    case ESurfaceEventType::FocusGained:
        // 외부 에디터로 헤더/소스 수정 후 창에 돌아오는 흐름 → *변경이 있을 때만* 재빌드(비동기).
        if (m_projectManager)
        {
            m_projectManager->RebuildScriptModuleOnFocus();
        }
        break;
    case ESurfaceEventType::FocusLost:
    case ESurfaceEventType::Resized:
        break;
    }
}

void CImEditor::OnPreFinalize()
{
    if (0 != m_surfaceEventToken && Engine.MainRenderSurface)
    {
        Engine.MainRenderSurface->Unsubscribe(m_surfaceEventToken);
        m_surfaceEventToken = 0;
    }

    if (m_projectManager && m_projectManager->IsProjectLoaded())
    {
        if (Engine.Localization.IsValid())
        {
            m_projectManager->SetEditorLocaleCode(Engine.Localization->GetCurrentLocale());
        }
    }

    // 창의 OnDestroy는 ProjectManager, Engine 서비스와 ImGui context를 사용할 수 있다.
    // 따라서 모든 창을 먼저 종료한 뒤 서비스와 ImGui backend를 내린다.
    FinalizeImWindows();

    if (m_outlineRenderer)
    {
        m_outlineRenderer->Finalize();
        m_outlineRenderer.Reset();
    }
    if (m_debugRenderer)
    {
        m_debugRenderer->Finalize();
        m_debugRenderer.Reset();
    }
    if (m_projectManager)
    {
        m_projectManager->Finalize();
        m_projectManager.Reset();
    }
    FinalizeImGui();
}

void CImEditor::OnPostFinalize()
{
}

// ── Canvas view focus context ────────────────────────────────────────────────

void CImEditor::SetCanvasViewFocusContext(std::vector<const void*> contextObjects)
{
    m_canvasViewFocusActive = !contextObjects.empty();
    m_canvasViewFocusEntities.clear();
    for (const void* o : contextObjects) m_canvasViewFocusEntities.insert(o);
}

void CImEditor::ClearCanvasViewFocusContext()
{
    m_canvasViewFocusActive = false;
    m_canvasViewFocusEntities.clear();
}

// ── Canvas view selection ────────────────────────────────────────────────────

void CImEditor::SetCanvasViewSelection(std::vector<const void*> selectedObjects)
{
    m_canvasViewHasSelection = !selectedObjects.empty();
    m_canvasViewSelectedEntities.clear();
    for (const void* o : selectedObjects) m_canvasViewSelectedEntities.insert(o);
}

void CImEditor::SetCanvasViewHidden(std::vector<const void*> hiddenObjects)
{
    m_canvasViewHidden.clear();
    for (const void* o : hiddenObjects) m_canvasViewHidden.insert(o);
}

void CImEditor::ClearCanvasViewSelection()
{
    m_canvasViewHasSelection = false;
    m_canvasViewSelectedEntities.clear();
}

void CImEditor::OnBeginFrame()
{
    BeginFrame();
}

void CImEditor::OnUpdate()
{
    if (m_projectManager)
    {
        m_projectManager->Tick();
    }
    Update();
}

void CImEditor::OnPrepareRender()
{
	const EngineCore* engineCore = (&Engine);
	if (nullptr == engineCore ||
	    false == engineCore->RHIDevice.IsValid() ||
	    false == engineCore->Renderer.IsValid() ||
	    false == engineCore->RenderScene.IsValid())
	{
		return;
	}

	SafePtr<IRHICommandContext> commandContext = engineCore->RHIDevice->GetImmediateCommandContext();
	if (false == commandContext.IsValid())
	{
		return;
	}

	auto EnsureRT = [&](OwnerPtr<IRHITexture>& rt, std::uint32_t w, std::uint32_t h) -> bool
	{
		return EnsureRenderTexture(*engineCore->RHIDevice, rt, w, h);
	};

	// 캔버스뷰 레이어 스냅샷 — 편집 뷰가 그리는 대상은 활성 캔버스가다(게임뷰는 자기 캔버스를 따로 지정).
	// 전 레이어 RT 강제: 에디터에선 레이어별 결과가 그대로 보여야 하고 성능 여유가 있다.
	m_canvasViewLayers.clear();
	if (Engine.CanvasManager.IsValid())
	{
		if (const CGameCanvas* activeCanvas = Engine.CanvasManager->GetActiveCanvas().TryGet())
		{
			m_canvasViewLayers = CollectGameRenderLayers(*activeCanvas, /*forceOwnTextureAll*/ true);
		}
	}

	// ── Canvas view (editor camera) ────────────────────────────────────────────────
	//
	// RT 파이프라인 순서:
	//   ① 그리드 (DebugDraw, Entity==INVALID)
	//   ② 스프라이트 전체
	//   ③ [포커스 모드] 흰 반투명 오버레이 + 포커스 스프라이트 + 포커스 콜라이더
	//      [루트 모드] 모든 콜라이더 (Entity!=INVALID)
	//   ④ [선택 아웃라인] Alpha Dilation 셰이더
	if (m_canvasViewRequested && EnsureRT(m_canvasViewRenderTarget, m_canvasViewWidth, m_canvasViewHeight))
	{
		const int viewW = static_cast<int>(m_canvasViewWidth);
		const int viewH = static_cast<int>(m_canvasViewHeight);
		const float camX    = m_canvasViewCamX;
		const float camY    = m_canvasViewCamY;
		const float camSize = m_canvasViewCamSize;

		engineCore->Renderer->SetRenderTargetSize(RenderSurfaceSize{ viewW, viewH });
		engineCore->Renderer->SetViewCamera(camX, camY, camSize);

		// ── Step 0: 선택 마스크 패스 (아웃라인용, BeginRenderPass 밖) ─────────────
		if (m_canvasViewHasSelection && m_outlineRenderer && !m_canvasViewSelectedEntities.empty())
		{
			if (CForward2DRenderer* fwd = engineCore->Renderer->AsForward2DRenderer())
			{
				m_outlineRenderer->RenderMask(
					commandContext, *fwd, *engineCore->RenderScene,
					m_canvasViewSelectedEntities,
					camX, camY, camSize, viewW, viewH);
				// 카메라 설정 복원 (RenderMask 내부에서 변경됨)
				engineCore->Renderer->SetRenderTargetSize(RenderSurfaceSize{ viewW, viewH });
				engineCore->Renderer->SetViewCamera(camX, camY, camSize);
			}
		}

		// ── Step 1~4: 메인 캔버스 패스 ───────────────────────────────────────────────
		RenderPassDesc rpDesc;
		rpDesc.ColorAttachment.Target     = m_canvasViewRenderTarget.GetSafePtr();
		rpDesc.ColorAttachment.LoadOp     = ERHILoadOp::Clear;
		rpDesc.ColorAttachment.StoreOp    = ERHIStoreOp::Store;
		rpDesc.ColorAttachment.ClearColor = Color{ 0.08f, 0.09f, 0.11f, 0.0f };
		commandContext->BeginRenderPass(rpDesc);

		// ① 그리드 (전역 DebugDraw, Entity==INVALID)
		if (m_debugRenderer && Engine.DebugDraw2D.IsValid())
		{
			m_debugRenderer->RenderGlobal(
				commandContext, *Engine.DebugDraw2D,
				camX, camY, camSize, viewW, viewH);
		}

		// ② 스프라이트 — 캔버스 레이어를 순서대로 합성한다(편집 뷰도 WYSIWYG).
		//    EditorHidden 오브젝트는 캔버스뷰에서만 제외. 레이어 스냅샷이 없으면 평면 렌더로 폴백.
		//    편집 카메라는 자유 카메라라 패럴랙스를 적용하지 않는다 — 레이어를 같은 뷰로 보여
		//    배치 작업이 되게 하고, 패럴랙스 확인은 게임뷰에서 한다.
		{
			CForward2DRenderer* fwd = engineCore->Renderer->AsForward2DRenderer();
			const std::unordered_set<RenderObjectId>* hidden =
				m_canvasViewHidden.empty() ? nullptr : &m_canvasViewHidden;

			if (nullptr == fwd || m_canvasViewLayers.empty())
			{
				if (fwd && hidden)
				{
					fwd->RenderExcluding(*engineCore->RenderScene, *hidden);
				}
				else
				{
					engineCore->Renderer->Render(*engineCore->RenderScene);
				}
			}
			else
			{
				commandContext->EndRenderPass();

				RenderPassDesc resumeDesc;
				resumeDesc.ColorAttachment.Target  = m_canvasViewRenderTarget.GetSafePtr();
				resumeDesc.ColorAttachment.LoadOp  = ERHILoadOp::Load;
				resumeDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;

				engineCore->RenderScene->Sort();
				for (const GameRenderLayerDesc& layer : m_canvasViewLayers)
				{
					// 비가시/빈 레이어는 스크래치 RT 자체를 빌리지 않는다.
					if (false == layer.Visible
						|| 0 == engineCore->RenderScene->GetLayerRange(layer.Index).Count)
					{
						continue;
					}

					// 에디터는 전 레이어 RT 강제(CollectGameRenderLayers 인자) — 레이어별 결과를
					// 그대로 썸네일·디버깅에 쓸 수 있게. 화면 결과는 런타임 lazy 경로와 동일.
					RWTextureDesc scratchDesc;
					scratchDesc.Width  = static_cast<std::uint32_t>(std::max(1, viewW));
					scratchDesc.Height = static_cast<std::uint32_t>(std::max(1, viewH));
					scratchDesc.Format = ERHITextureFormat::RGBA8;
					SafePtr<IRHITexture> scratch = fwd->GetRenderWeavePool().Acquire(scratchDesc);
					if (false == scratch.IsValid())
					{
						continue;
					}

					RenderPassDesc layerClear;
					layerClear.ColorAttachment.Target     = scratch;
					layerClear.ColorAttachment.LoadOp     = ERHILoadOp::Clear;
					layerClear.ColorAttachment.StoreOp    = ERHIStoreOp::Store;
					layerClear.ColorAttachment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
					commandContext->BeginRenderPass(layerClear);
					commandContext->EndRenderPass();

					RenderPassDesc layerPass;
					layerPass.ColorAttachment.Target  = scratch;
					layerPass.ColorAttachment.LoadOp  = ERHILoadOp::Load;
					layerPass.ColorAttachment.StoreOp = ERHIStoreOp::Store;
					commandContext->BeginRenderPass(layerPass);
					commandContext->SetViewport(0.0f, 0.0f, static_cast<float>(viewW), static_cast<float>(viewH));
					engineCore->Renderer->SetRenderTargetSize(RenderSurfaceSize{ viewW, viewH });
					engineCore->Renderer->SetViewCamera(camX, camY, camSize);
					fwd->RenderLayer(*engineCore->RenderScene, layer.Index, hidden);
					commandContext->EndRenderPass();

					commandContext->BeginRenderPass(resumeDesc);
					commandContext->SetViewport(0.0f, 0.0f, static_cast<float>(viewW), static_cast<float>(viewH));
					engineCore->Renderer->SetRenderTargetSize(RenderSurfaceSize{ viewW, viewH });
					fwd->CompositeLayer(*commandContext, scratch, layer.BlendMode, layer.Opacity);
					commandContext->EndRenderPass();
				}

				// 이후 단계(③ 포커스/콜라이더, ④ 아웃라인)가 이어서 그리도록 패스를 재개한다.
				commandContext->BeginRenderPass(resumeDesc);
				engineCore->Renderer->SetRenderTargetSize(RenderSurfaceSize{ viewW, viewH });
				engineCore->Renderer->SetViewCamera(camX, camY, camSize);
			}
		}

		if (m_canvasViewFocusActive && !m_canvasViewFocusEntities.empty())
		{
			// ③ 포커스 모드: 흰 오버레이 → 포커스 스프라이트 → 포커스 콜라이더
			engineCore->Renderer->FillViewportColor(1.0f, 1.0f, 1.0f, 0.7f);

			if (CForward2DRenderer* fwd = engineCore->Renderer->AsForward2DRenderer())
			{
				fwd->RenderFiltered(*engineCore->RenderScene, m_canvasViewFocusEntities);
			}

			if (m_debugRenderer && Engine.DebugDraw2D.IsValid())
			{
				m_debugRenderer->RenderEntities(
					commandContext, *Engine.DebugDraw2D,
					camX, camY, camSize, viewW, viewH,
					&m_canvasViewFocusEntities);
			}
		}
		else
		{
			// ③ 루트 모드: 모든 콜라이더 (Entity!=INVALID)
			if (m_debugRenderer && Engine.DebugDraw2D.IsValid())
			{
				m_debugRenderer->RenderEntities(
					commandContext, *Engine.DebugDraw2D,
					camX, camY, camSize, viewW, viewH,
					nullptr);
			}
		}

		// ④ 선택 아웃라인 합성 (Alpha Dilation)
		if (m_canvasViewHasSelection && m_outlineRenderer && !m_canvasViewSelectedEntities.empty())
		{
			m_outlineRenderer->RenderOutline(
				commandContext,
				1.0f, 1.0f, 0.0f, 1.0f, // 노란색 아웃라인
				2.0f,                     // 2픽셀 두께
				viewW, viewH);
		}

		commandContext->EndRenderPass();
	}

	// ── Game view (multi-camera) ───────────────────────────────────────────────────
	if (m_gameViewRequested && EnsureRT(m_gameViewRenderTarget, m_gameViewWidth, m_gameViewHeight))
	{
		// 카메라/라이트 스냅샷을 여기(시뮬 이후·렌더 직전)서 수집한다 — GameViewTool
		// 의 UI 빌드 시점(캔버스 업데이트 전) 수집은 1프레임 지연 카메라를 만든다.
		if (CGameCanvas* gameViewCanvas = m_gameViewCanvas.TryGet())
		{
			// 에디터에서 "플레이어가 보는 표면" = 게임뷰 RT. 스크립트의 ScreenToWorld 가 역투영
			// 기준으로 쓴다(레이어 썸네일도 같은 수집기를 타므로 기록은 여기서만 한다).
			gameViewCanvas->SetLastRenderSize(
				static_cast<float>(m_gameViewWidth),
				static_cast<float>(m_gameViewHeight));
			m_gameViewViewports = CollectGameRenderViewports(
				*gameViewCanvas,
				static_cast<float>(m_gameViewWidth),
				static_cast<float>(m_gameViewHeight));
			m_gameViewLights = CollectGameRenderLights(*gameViewCanvas);
			// 에디터는 전 레이어를 RT 경유로 강제한다 — 레이어별 결과를 그대로 볼 수 있어야
			// 썸네일·디버깅이 되기 때문. 화면 결과는 lazy 경로와 동일하고 비용만 다르다.
			m_gameViewLayers = CollectGameRenderLayers(*gameViewCanvas, /*forceOwnTextureAll*/ true);
		}

		std::vector<GameRenderCameraStats> cameraStats;
		const CGameCanvas* gameViewCanvas = m_gameViewCanvas.TryGet();
		// 예외 5: 레이어별 GPU 시간은 게임뷰 렌더에서만 잰다(썸네일·캔버스뷰는 실제 게임과 다르다).
		// 타이머는 프로파일링이 꺼져 있으면 스스로 no-op 이라, 항상 넘겨도 비용이 없다.
		IRHIGpuTimer* gpuTimer = engineCore->RHIDevice.IsValid() ? engineCore->RHIDevice->GetGpuTimer() : nullptr;
		// 게임뷰 전체 GPU 시간(레이어 + 라이팅/컴포짓 총합, 키=nullptr). 게임뷰가 안 그려지면
		// 이 구간이 아예 안 열려 결과가 0 이 된다 → 게임뷰를 끄면 총합이 뚝 떨어지는 게 보인다.
		const std::uint32_t gpuTotalScope = gpuTimer ? gpuTimer->BeginScope(nullptr) : INVALID_GPU_SCOPE;
		RenderGameViewports(
			*commandContext,
			*engineCore->Renderer,
			*engineCore->RenderScene,
			m_gameViewViewports,
			RenderSurfaceSize{ static_cast<int>(m_gameViewWidth), static_cast<int>(m_gameViewHeight) },
			m_gameViewRenderTarget.GetSafePtr(),
			&cameraStats,
			m_gameViewLights,
			m_gameViewLayers,
			gameViewCanvas ? gameViewCanvas->GetBackgroundColor() : nullptr,
			gpuTimer);
		if (gpuTimer)
		{
			gpuTimer->EndScope(gpuTotalScope);
		}
		m_gameViewCameraCullingStats.clear();
		for (const GameRenderCameraStats& stats : cameraStats)
		{
			if (stats.OwnerObject)
			{
				m_gameViewCameraCullingStats[stats.OwnerObject] = stats.Culling;
			}
		}
	}
	else
	{
		m_gameViewCameraCullingStats.clear();
	}

	// 게임뷰 렌더 요청은 매 프레임 opt-in 이다 — 여기서 내려 두고, 게임뷰 패널이 실제로 그려질
	// 때만(GameViewTool::OnRenderStay, 이 함수보다 뒤의 ImGui 렌더 단계) 다음 프레임용으로 다시
	// 켠다. 그래서 게임뷰 탭이 닫히거나 배경 탭으로 가려지면 요청이 안 들어와 렌더가 멈춘다.
	// (RT 는 파기하지 않아 다시 보일 때 재할당 없이 매끄럽게 이어진다.)
	m_gameViewRequested = false;

	// 캔버스뷰/게임뷰와 독립 — 둘 다 닫혀 있어도 하이어라키가 요청하면 그린다.
	// 렌더러 뷰 상태를 바꾸므로 다른 뷰의 렌더가 끝난 뒤 마지막에 돈다.
	RenderLayerThumbnails();
}

void CImEditor::OnRender()
{
    EndFrame();
}

bool CImEditor::InitializeImGui()
{
    const EngineCore* engineCore = (&Engine);
    if (nullptr == engineCore)
    {
        return false;
    }

    IMGUI_CHECKVERSION();
    m_imguiContext = ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    io.IniFilename = nullptr;

    ImGuiStyle& style = ImGui::GetStyle();
    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
    style.WindowMinSize = ImVec2(60.0f, 30.0f);

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 3;
    fontConfig.OversampleV = 3;
    fontConfig.PixelSnapH = true;

    static const ImWchar customRanges[] = {
        0x0020, 0x00FF,
        0x1100, 0x11FF,
        0x3130, 0x318F,
        0xAC00, 0xD7AF,
        0x2160, 0x2188,
        0,
    };
    ImFont* mainFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", 15.0f, &fontConfig, customRanges);

	// FontAwesome 아이콘 폰트 추가 (글꼴 파일은 프로젝트 루트에 위치한다고 가정)
    File::Path fontPath = Engine.FileSystem->GetOriginPath() / "Font Awesome 7 Free-Solid-900.otf";
    if (true == std::filesystem::exists(fontPath.generic_string()))
    {
        static const ImWchar icons_ranges[] = { 0xf000, 0xf8ff, 0 }; // FontAwesome 유니코드 범위
        ImFontConfig  config;

        config.MergeMode = true;
        config.PixelSnapH = true;

        ImFontAtlas* atlas = io.Fonts;
        ImFont* iconFont = atlas->AddFontFromFileTTF(fontPath.string().c_str(), 15.0f, &config, icons_ranges);
    }

    ImGui_ImplWin32_EnableDpiAwareness();

    HWND hwnd = nullptr;
    if (engineCore->MainRenderSurface)
    {
        const NativeSurfaceHandle nativeSurfaceHandle = engineCore->MainRenderSurface->GetNativeSurfaceHandle();
        if (ERenderSurfaceType::Win32Hwnd == nativeSurfaceHandle.SurfaceType)
        {
            hwnd = static_cast<HWND>(nativeSurfaceHandle.Handle);
        }
    }

    if (nullptr == hwnd)
    {
        FinalizeImGui();
        return false;
    }

    m_isWin32BackendInitialized = ImGui_ImplWin32_Init(hwnd);
    if (false == m_isWin32BackendInitialized)
    {
        FinalizeImGui();
        return false;
    }

    if (engineCore->MainRenderSurface)
    {
        engineCore->MainRenderSurface->SetNativeMessageHandler(
            [](const NativeSurfaceMessage& message, std::intptr_t& result) {
                result = ImGui_ImplWin32_WndProcHandler(
                    static_cast<HWND>(message.SurfaceHandle),
                    static_cast<UINT>(message.Message),
                    static_cast<WPARAM>(message.WParam),
                    static_cast<LPARAM>(message.LParam));
                return 0 != result;
            });
    }

    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dDeviceContext = nullptr;
    if (engineCore->RHIDevice)
    {
        const RHINativeDeviceDesc nativeDeviceDesc = engineCore->RHIDevice->GetNativeDeviceDesc();
        d3dDevice = static_cast<ID3D11Device*>(nativeDeviceDesc.Device);
        d3dDeviceContext = static_cast<ID3D11DeviceContext*>(nativeDeviceDesc.DeviceContext);
    }

    if (nullptr == d3dDevice || nullptr == d3dDeviceContext)
    {
        FinalizeImGui();
        return false;
    }

    m_isDX11BackendInitialized = ImGui_ImplDX11_Init(d3dDevice, d3dDeviceContext);
    if (false == m_isDX11BackendInitialized)
    {
        FinalizeImGui();
        return false;
    }

    auto& colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.105f, 0.11f, 1.0f);

    colors[ImGuiCol_Header] = ImVec4(0.1f, 0.205f, 0.21f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.305f, 0.31f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);

    colors[ImGuiCol_Button] = ImVec4(0.2f, 0.205f, 0.21f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.305f, 0.31f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.2f, 0.205f, 0.21f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.3f, 0.305f, 0.31f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);

    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.38f, 0.3805f, 0.381f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.28f, 0.2805f, 0.281f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.2f, 0.205f, 0.21f, 1.0f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);

    colors[ImGuiCol_DragDropTarget] = ImVec4(0.2f, 0.6f, 0.4f, 1.0f);

    return true;
}

void CImEditor::FinalizeImWindows()
{
    // 생성의 역순으로 종료해 dock child가 보통 parent보다 먼저 정리되도록 한다.
    for (auto it = m_imWindowVector.rbegin(); it != m_imWindowVector.rend(); ++it)
    {
        if (CImWindow* window = *it)
        {
            window->Finalize();
        }
    }

    m_imWindowVector.clear();
    m_imWindowTable.clear();
    m_popups.clear();
    while (false == m_delayEventQueue.empty())
    {
        m_delayEventQueue.pop();
    }
}

void CImEditor::FinalizeImGui()
{
    if (const EngineCore* engineCore = (&Engine))
    {
        if (engineCore->MainRenderSurface)
        {
            engineCore->MainRenderSurface->SetNativeMessageHandler(nullptr);
        }
    }

    if (m_isDX11BackendInitialized)
    {
        ImGui_ImplDX11_Shutdown();
        m_isDX11BackendInitialized = false;
    }

    if (m_isWin32BackendInitialized)
    {
        ImGui_ImplWin32_Shutdown();
        m_isWin32BackendInitialized = false;
    }

    if (m_imguiContext)
    {
        ImGui::DestroyContext(m_imguiContext);
        m_imguiContext = nullptr;
    }
}
