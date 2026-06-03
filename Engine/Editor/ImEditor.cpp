#include "pch.h"
#include "ImEditor.h"

#include "Core/Debug/DebugDraw2D.h"
#include "Core/Debug/DebugRenderer2D.h"
#include "Core/Debug/OutlineRenderer2D.h"
#include "Core/Renderer/Forward2DRenderer.h"
#include "Core/Renderer/IRenderer.h"
#include "Core/Renderer/IRenderScene.h"
#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/IRHITexture.h"
#include "Editor/Project/ProjectManager.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

CImEditor::CImEditor()
	: m_imguiContext(nullptr)
	, m_isWin32BackendInitialized(false)
	, m_isDX11BackendInitialized(false)
{
}

CImEditor::~CImEditor()
{
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

const EngineCore* CImEditor::GetEditorEngineCore() const
{
	return GetEngineCore();
}

void CImEditor::RequestSceneViewRenderTarget(std::uint32_t width, std::uint32_t height)
{
	m_sceneViewRequested = 0 != width && 0 != height;
	if (false == m_sceneViewRequested)
	{
		return;
	}

	if (m_sceneViewWidth != width || m_sceneViewHeight != height)
	{
		m_sceneViewRenderTarget.Reset();
		m_sceneViewWidth  = width;
		m_sceneViewHeight = height;
	}
}

void CImEditor::SetSceneViewCamera(float posX, float posY, float orthographicSize)
{
	m_sceneViewCamX    = posX;
	m_sceneViewCamY    = posY;
	m_sceneViewCamSize = orthographicSize > 0.0f ? orthographicSize : 5.0f;
}

SafePtr<IRHITexture> CImEditor::GetSceneViewRenderTarget() const
{
	return m_sceneViewRenderTarget.GetSafePtr();
}

void* CImEditor::GetSceneViewTextureID() const
{
	if (false == static_cast<bool>(m_sceneViewRenderTarget))
	{
		return nullptr;
	}
	return m_sceneViewRenderTarget->GetNativeHandle().ShaderResourceView;
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

void CImEditor::SetGameViewCameras(const std::vector<GameRenderCameraDesc>& cameras)
{
	m_gameViewCameras = cameras;
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

void CImEditor::OnPreInitialize()
{
}

void CImEditor::OnPostInitialize()
{
    InitializeImGui();
    m_projectManager = MakeOwnerPtr<CProjectManager>();
    if (m_projectManager)
    {
        if (const EngineCore* engineCore = GetEngineCore())
        {
            m_projectManager->Initialize(*engineCore);
        }
    }

    // Initialize GPU debug/outline renderers.
    if (const EngineCore* engineCore = GetEngineCore())
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
}

void CImEditor::OnPreFinalize()
{
    if (m_projectManager && m_projectManager->IsProjectLoaded())
    {
        if (Core::Localization.IsValid())
        {
            m_projectManager->SetEditorLocaleCode(Core::Localization->GetCurrentLocale());
        }
    }

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

// ── Scene view focus context ────────────────────────────────────────────────

void CImEditor::SetSceneViewFocusContext(std::vector<ObjectId> contextEntities)
{
    m_sceneViewFocusActive = !contextEntities.empty();
    m_sceneViewFocusEntities.clear();
    for (ObjectId e : contextEntities) m_sceneViewFocusEntities.insert(e);
}

void CImEditor::ClearSceneViewFocusContext()
{
    m_sceneViewFocusActive = false;
    m_sceneViewFocusEntities.clear();
}

// ── Scene view selection ────────────────────────────────────────────────────

void CImEditor::SetSceneViewSelection(std::vector<ObjectId> selectedEntities)
{
    m_sceneViewHasSelection = !selectedEntities.empty();
    m_sceneViewSelectedEntities.clear();
    for (ObjectId e : selectedEntities) m_sceneViewSelectedEntities.insert(e);
}

void CImEditor::ClearSceneViewSelection()
{
    m_sceneViewHasSelection = false;
    m_sceneViewSelectedEntities.clear();
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
	const EngineCore* engineCore = GetEngineCore();
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
		if (false == static_cast<bool>(rt))
		{
			RHITexture2DDesc desc;
			desc.Width     = w;
			desc.Height    = h;
			desc.Format    = ERHITextureFormat::RGBA8;
			desc.BindFlags = static_cast<RHITextureBindFlags>(ERHITextureBindFlag::ShaderResource) |
			                 static_cast<RHITextureBindFlags>(ERHITextureBindFlag::RenderTarget);
			rt = engineCore->RHIDevice->CreateTexture2D(desc, nullptr);
		}
		return static_cast<bool>(rt);
	};

	// ── Scene view (editor camera) ────────────────────────────────────────────────
	//
	// RT 파이프라인 순서:
	//   ① 그리드 (DebugDraw, Entity==INVALID)
	//   ② 스프라이트 전체
	//   ③ [포커스 모드] 흰 반투명 오버레이 + 포커스 스프라이트 + 포커스 콜라이더
	//      [루트 모드] 모든 콜라이더 (Entity!=INVALID)
	//   ④ [선택 아웃라인] Alpha Dilation 셰이더
	if (m_sceneViewRequested && EnsureRT(m_sceneViewRenderTarget, m_sceneViewWidth, m_sceneViewHeight))
	{
		const int viewW = static_cast<int>(m_sceneViewWidth);
		const int viewH = static_cast<int>(m_sceneViewHeight);
		const float camX    = m_sceneViewCamX;
		const float camY    = m_sceneViewCamY;
		const float camSize = m_sceneViewCamSize;

		engineCore->Renderer->SetRenderTargetSize(RenderSurfaceSize{ viewW, viewH });
		engineCore->Renderer->SetViewCamera(camX, camY, camSize);

		// ── Step 0: 선택 마스크 패스 (아웃라인용, BeginRenderPass 밖) ─────────────
		if (m_sceneViewHasSelection && m_outlineRenderer && !m_sceneViewSelectedEntities.empty())
		{
			if (CForward2DRenderer* fwd = dynamic_cast<CForward2DRenderer*>(engineCore->Renderer.TryGet()))
			{
				m_outlineRenderer->RenderMask(
					commandContext, *fwd, *engineCore->RenderScene,
					m_sceneViewSelectedEntities,
					camX, camY, camSize, viewW, viewH);
				// 카메라 설정 복원 (RenderMask 내부에서 변경됨)
				engineCore->Renderer->SetRenderTargetSize(RenderSurfaceSize{ viewW, viewH });
				engineCore->Renderer->SetViewCamera(camX, camY, camSize);
			}
		}

		// ── Step 1~4: 메인 씬 패스 ───────────────────────────────────────────────
		RenderPassDesc rpDesc;
		rpDesc.ColorAttachment.Target     = m_sceneViewRenderTarget.GetSafePtr();
		rpDesc.ColorAttachment.LoadOp     = ERHILoadOp::Clear;
		rpDesc.ColorAttachment.StoreOp    = ERHIStoreOp::Store;
		rpDesc.ColorAttachment.ClearColor = Color{ 0.08f, 0.09f, 0.11f, 0.0f };
		commandContext->BeginRenderPass(rpDesc);

		// ① 그리드 (전역 DebugDraw, Entity==INVALID)
		if (m_debugRenderer && Core::DebugDraw2D.IsValid())
		{
			m_debugRenderer->RenderGlobal(
				commandContext, *Core::DebugDraw2D,
				camX, camY, camSize, viewW, viewH);
		}

		// ② 스프라이트 전체
		engineCore->Renderer->Render(*engineCore->RenderScene);

		if (m_sceneViewFocusActive && !m_sceneViewFocusEntities.empty())
		{
			// ③ 포커스 모드: 흰 오버레이 → 포커스 스프라이트 → 포커스 콜라이더
			engineCore->Renderer->FillViewportColor(1.0f, 1.0f, 1.0f, 0.7f);

			if (CForward2DRenderer* fwd = dynamic_cast<CForward2DRenderer*>(engineCore->Renderer.TryGet()))
			{
				fwd->RenderFiltered(*engineCore->RenderScene, m_sceneViewFocusEntities);
			}

			if (m_debugRenderer && Core::DebugDraw2D.IsValid())
			{
				m_debugRenderer->RenderEntities(
					commandContext, *Core::DebugDraw2D,
					camX, camY, camSize, viewW, viewH,
					&m_sceneViewFocusEntities);
			}
		}
		else
		{
			// ③ 루트 모드: 모든 콜라이더 (Entity!=INVALID)
			if (m_debugRenderer && Core::DebugDraw2D.IsValid())
			{
				m_debugRenderer->RenderEntities(
					commandContext, *Core::DebugDraw2D,
					camX, camY, camSize, viewW, viewH,
					nullptr);
			}
		}

		// ④ 선택 아웃라인 합성 (Alpha Dilation)
		if (m_sceneViewHasSelection && m_outlineRenderer && !m_sceneViewSelectedEntities.empty())
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
		RenderGameCameraStack(
			*commandContext,
			*engineCore->Renderer,
			*engineCore->RenderScene,
			m_gameViewCameras,
			RenderSurfaceSize{ static_cast<int>(m_gameViewWidth), static_cast<int>(m_gameViewHeight) },
			m_gameViewRenderTarget.GetSafePtr());
	}
}

void CImEditor::OnRender()
{
    EndFrame();
}

bool CImEditor::InitializeImGui()
{
    const EngineCore* engineCore = GetEngineCore();
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
    (void)mainFont;

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

void CImEditor::FinalizeImGui()
{
    if (const EngineCore* engineCore = GetEngineCore())
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
