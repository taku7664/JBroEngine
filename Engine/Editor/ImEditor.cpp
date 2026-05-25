#include "pch.h"
#include "ImEditor.h"

#include "Core/Core.h"
#include "Core/Debug/DebugDraw2D.h"
#include "Core/Debug/DebugRenderer2D.h"
#include "Core/EngineContext.h"
#include "Core/Renderer/IRenderer.h"
#include "Core/Renderer/IRenderScene.h"
#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/IRHITexture.h"
#include "Editor/Project/ProjectManager.h"
#include "ThirdParty/imgui/imgui_impl_win32.h"

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
    if (false == m_imPopupWindowQueue.empty())
    {
        CImPopupWindow& popup = m_imPopupWindowQueue.front();
        if (false == popup.Render())
        {
            m_imPopupWindowQueue.pop();
        }
    }

    while (false == m_delayEventQueue.empty())
    {
        if (auto& event = m_delayEventQueue.front())
        {
            event();
        }
        m_delayEventQueue.pop();
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

const EngineContext* CImEditor::GetEditorEngineContext() const
{
	return GetEngineContext();
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

void CImEditor::SetGameViewCameras(const std::vector<GameCameraDesc>& cameras)
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

void CImEditor::OpenPopup(const ImPopupDesc& desc)
{
    m_imPopupWindowQueue.push(desc);
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
        if (const EngineContext* engineContext = GetEngineContext())
        {
            m_projectManager->Initialize(*engineContext);
        }
    }

    // Initialize the GPU debug renderer.
    if (const EngineContext* engineContext = GetEngineContext())
    {
        if (engineContext->RHIDevice.IsValid())
        {
            m_debugRenderer = MakeOwnerPtr<CDebugRenderer2D>();
            if (m_debugRenderer)
            {
                if (false == m_debugRenderer->Initialize(engineContext->RHIDevice))
                {
                    m_debugRenderer.Reset();
                }
            }
        }
    }
}

void CImEditor::OnPreFinalize()
{
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
	const EngineContext* engineContext = GetEngineContext();
	if (nullptr == engineContext ||
	    false == engineContext->RHIDevice.IsValid() ||
	    false == engineContext->Renderer.IsValid() ||
	    false == engineContext->RenderScene.IsValid())
	{
		return;
	}

	SafePtr<IRHICommandContext> commandContext = engineContext->RHIDevice->GetImmediateCommandContext();
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
			rt = engineContext->RHIDevice->CreateTexture2D(desc, nullptr);
		}
		return static_cast<bool>(rt);
	};

	// ── Scene view (editor camera) ────────────────────────────────────────────────
	// Clear colour alpha = 0 so the RT background is transparent.  Sprite pixels
	// are written opaque.  SceneViewTool draws a solid grey rect and then composites
	// this texture with AddImage — the grid (submitted to IDebugDraw2D by SceneViewTool
	// earlier in the frame) is rendered into the RT here, after the sprite pass,
	// so it appears on top of empty areas but is correctly occluded by sprites.
	if (m_sceneViewRequested && EnsureRT(m_sceneViewRenderTarget, m_sceneViewWidth, m_sceneViewHeight))
	{
		RenderPassDesc rpDesc;
		rpDesc.ColorAttachment.Target     = m_sceneViewRenderTarget.GetSafePtr();
		rpDesc.ColorAttachment.LoadOp     = ERHILoadOp::Clear;
		rpDesc.ColorAttachment.StoreOp    = ERHIStoreOp::Store;
		rpDesc.ColorAttachment.ClearColor = Color{ 0.08f, 0.09f, 0.11f, 0.0f };
		commandContext->BeginRenderPass(rpDesc);

		engineContext->Renderer->SetRenderTargetSize(
			RenderSurfaceSize{ static_cast<int>(m_sceneViewWidth), static_cast<int>(m_sceneViewHeight) });
		engineContext->Renderer->SetViewCamera(m_sceneViewCamX, m_sceneViewCamY, m_sceneViewCamSize);
		engineContext->Renderer->Render(*engineContext->RenderScene);

		// Render debug primitives (grid, colliders, camera frusta, etc.) into the same RT.
		if (m_debugRenderer && Core::DebugDraw2D.IsValid())
		{
			m_debugRenderer->Render(
				commandContext,
				*Core::DebugDraw2D,
				m_sceneViewCamX, m_sceneViewCamY, m_sceneViewCamSize,
				static_cast<int>(m_sceneViewWidth),
				static_cast<int>(m_sceneViewHeight));
		}

		commandContext->EndRenderPass();
	}

	// ── Game view (multi-camera) ───────────────────────────────────────────────────
	// Cameras are sorted by Priority (ascending) by GameViewTool.
	//
	// Rendering strategy:
	//   1. Clear the entire RT to transparent (0,0,0,0) once.
	//   2. For each camera: begin a Load pass → set sub-viewport → FillViewportColor
	//      (clears that sub-area with the camera's own ClearColor including alpha) →
	//      SetViewCameraEx (stretch + rotation) → Render scene.
	//
	// FillViewportColor draws a full-NDC quad that directly overwrites all RGBA channels,
	// so alpha is correctly written per camera.  Alpha=0 areas remain transparent in
	// the final ImGui game-view composite.
	if (m_gameViewRequested && EnsureRT(m_gameViewRenderTarget, m_gameViewWidth, m_gameViewHeight))
	{
		const float rtW = static_cast<float>(m_gameViewWidth);
		const float rtH = static_cast<float>(m_gameViewHeight);

		// Step 1: Clear entire RT to fully transparent.
		{
			RenderPassDesc rpDesc;
			rpDesc.ColorAttachment.Target     = m_gameViewRenderTarget.GetSafePtr();
			rpDesc.ColorAttachment.LoadOp     = ERHILoadOp::Clear;
			rpDesc.ColorAttachment.StoreOp    = ERHIStoreOp::Store;
			rpDesc.ColorAttachment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
			commandContext->BeginRenderPass(rpDesc);
			commandContext->EndRenderPass();
		}

		// Step 2: Render each camera into its sub-viewport.
		for (const GameCameraDesc& cam : m_gameViewCameras)
		{
			const float vpX = cam.ViewportX * rtW;
			const float vpY = cam.ViewportY * rtH;
			const float vpW = std::max(cam.ViewportW * rtW, 1.0f);
			const float vpH = std::max(cam.ViewportH * rtH, 1.0f);

			RenderPassDesc rpDesc;
			rpDesc.ColorAttachment.Target  = m_gameViewRenderTarget.GetSafePtr();
			rpDesc.ColorAttachment.LoadOp  = ERHILoadOp::Load;
			rpDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;

			commandContext->BeginRenderPass(rpDesc);
			// Set sub-viewport for this camera.
			commandContext->SetViewport(vpX, vpY, vpW, vpH);

			engineContext->Renderer->SetRenderTargetSize(
				RenderSurfaceSize{ static_cast<int>(vpW), static_cast<int>(vpH) });

			// Clear this camera's viewport area with its own ClearColor (alpha included).
			// FillViewportColor draws a full-NDC quad → direct RGBA overwrite, no blending.
			// alpha ≤ 0 (1/255)이면 스킵 → 이전 카메라가 그린 내용을 보존(멀티카메라 합성).
			if (cam.ClearColor[3] > (1.0f / 255.0f))
			{
				engineContext->Renderer->FillViewportColor(
					cam.ClearColor[0], cam.ClearColor[1],
					cam.ClearColor[2], cam.ClearColor[3]);
			}

			// SetViewCameraEx: explicit halfW/halfH + rotation → stretch rendering.
			//   scaleX → halfW (가로), scaleY → halfH (세로), cos/sinR → 회전.
			engineContext->Renderer->SetViewCameraEx(
				cam.PosX, cam.PosY,
				cam.OrthoSizeX, cam.OrthoSize,
				cam.CosR, cam.SinR);
			engineContext->Renderer->Render(*engineContext->RenderScene);

			commandContext->EndRenderPass();
		}
	}

	// Reset camera to default so the main swapchain render is unaffected.
	engineContext->Renderer->SetViewCamera(0.0f, 0.0f, 1.0f);
}

void CImEditor::OnRender()
{
    EndFrame();
}

bool CImEditor::InitializeImGui()
{
    const EngineContext* engineContext = GetEngineContext();
    if (nullptr == engineContext)
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
    if (engineContext->MainRenderSurface)
    {
        const NativeSurfaceHandle nativeSurfaceHandle = engineContext->MainRenderSurface->GetNativeSurfaceHandle();
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

    if (engineContext->MainRenderSurface)
    {
        engineContext->MainRenderSurface->SetNativeMessageHandler(
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
    if (engineContext->RHIDevice)
    {
        const RHINativeDeviceDesc nativeDeviceDesc = engineContext->RHIDevice->GetNativeDeviceDesc();
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
    if (const EngineContext* engineContext = GetEngineContext())
    {
        if (engineContext->MainRenderSurface)
        {
            engineContext->MainRenderSurface->SetNativeMessageHandler(nullptr);
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
