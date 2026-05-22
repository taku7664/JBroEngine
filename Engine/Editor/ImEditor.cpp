#include "pch.h"
#include "ImEditor.h"

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
		m_sceneViewWidth = width;
		m_sceneViewHeight = height;
	}
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
}

void CImEditor::OnPreFinalize()
{
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
	if (false == m_sceneViewRequested)
	{
		return;
	}

	const EngineContext* engineContext = GetEngineContext();
	if (nullptr == engineContext || false == engineContext->RHIDevice.IsValid() || false == engineContext->Renderer.IsValid() || false == engineContext->RenderScene.IsValid())
	{
		return;
	}

	if (false == static_cast<bool>(m_sceneViewRenderTarget))
	{
		RHITexture2DDesc textureDesc;
		textureDesc.Width = m_sceneViewWidth;
		textureDesc.Height = m_sceneViewHeight;
		textureDesc.Format = ERHITextureFormat::RGBA8;
		textureDesc.BindFlags = static_cast<RHITextureBindFlags>(ERHITextureBindFlag::ShaderResource) |
			static_cast<RHITextureBindFlags>(ERHITextureBindFlag::RenderTarget);
		m_sceneViewRenderTarget = engineContext->RHIDevice->CreateTexture2D(textureDesc, nullptr);
	}

	if (false == static_cast<bool>(m_sceneViewRenderTarget))
	{
		return;
	}

	SafePtr<IRHICommandContext> commandContext = engineContext->RHIDevice->GetImmediateCommandContext();
	if (false == commandContext.IsValid())
	{
		return;
	}

	RenderPassDesc renderPassDesc;
	renderPassDesc.ColorAttachment.Target = m_sceneViewRenderTarget.GetSafePtr();
	renderPassDesc.ColorAttachment.LoadOp = ERHILoadOp::Clear;
	renderPassDesc.ColorAttachment.StoreOp = ERHIStoreOp::Store;
	renderPassDesc.ColorAttachment.ClearColor = Color{ 0.08f, 0.09f, 0.11f, 1.0f };
	commandContext->BeginRenderPass(renderPassDesc);
	engineContext->Renderer->Render(*engineContext->RenderScene);
	commandContext->EndRenderPass();
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
