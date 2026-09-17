#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/MessagePopup.h>

#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>

#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/VulkanRHI/VulkanRHI.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Framework3DSystem/Framework3D.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Host/EngineInstance.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Runtime/GameObject.h>

#include "Panel/GameViewPanel.h"
#include "Panel/HierarchyPanel.h"
#include "Panel/InspectorPanel.h"
#include "Panel/StatsPanel.h"

#include <imgui.h>
// **기본 도킹 자리를 잡으려면 내부 헤더가 필요하다.** `DockBuilder*` 는 공개
// `imgui.h` 에 없다 - ImGui 가 아직 확정하지 않은 API 라서다. 에디터를 만드는
// 쪽은 대개 이것을 쓰고, 우리도 첫 프레임에 한 번 부르는 데만 쓴다.
#include <imgui_internal.h>

#include <cstdio>
#include <cstring>

#include <cmath>
#include <new>

namespace JBro
{
    EditorApplication::EditorApplication() = default;

    EditorApplication::~EditorApplication()
    {
        Shutdown();
    }

    bool EditorApplication::Initialize(const EditorApplicationConfig& config)
    {
        if (m_initialized || config.windowWidth == 0 || config.windowHeight == 0
            || false == std::isfinite(config.fixedDeltaTime) || config.fixedDeltaTime <= 0.0f
            || config.maxFixedStepsPerFrame == 0
            || (config.graphicsApi != GraphicsApi::D3D12 && config.graphicsApi != GraphicsApi::D3D11
                && config.graphicsApi != GraphicsApi::Vulkan))
        {
            return false;
        }

        // **글자를 먼저 읽는다.** 창 제목부터 이미 번역 대상이다.
        // 실패해도 그냥 간다 - 코드에 있는 영어 원문으로 떨어질 뿐이다.
        LocalizationTable::Get().Load(
            config.localizationDirectory, config.locale, config.fallbackLocale);

        try
        {
            m_fileDialog = config.fileDialog;
            m_fileDialogUser = config.fileDialogUser;
            EditorTheme::SetIconFontPath(config.iconFontPath);
            m_platform = MakeOwnerPtr<WindowsPlatform>();
            if (false == m_platform->Initialize(config.memory))
            {
                ReleaseProcessResources();
                return false;
            }

            // 세 백엔드 중 하나다(D-107·D-108). 기본은 D3D12 다.
            if (config.graphicsApi == GraphicsApi::D3D11)
            {
                m_rhiModule = MakeOwnerPtr<D3D11RHIModule>();
            }
            else if (config.graphicsApi == GraphicsApi::Vulkan)
            {
                m_rhiModule = MakeOwnerPtr<VulkanRHIModule>();
            }
            else
            {
                m_rhiModule = MakeOwnerPtr<D3D12RHIModule>();
            }
            if (false == m_rhiModule->Initialize(config.memory))
            {
                ReleaseProcessResources();
                return false;
            }

            EngineConfig engineConfig;
            engineConfig.graphicsApi = config.graphicsApi;
            engineConfig.fixedDeltaTime = config.fixedDeltaTime;
            engineConfig.maxFixedStepsPerFrame = config.maxFixedStepsPerFrame;
            engineConfig.enableValidation = config.enableValidation;
            engineConfig.window.title = {"JBro Editor", 11};
            engineConfig.window.width = config.windowWidth;
            engineConfig.window.height = config.windowHeight;
            engineConfig.window.visible = config.windowVisible;
            engineConfig.memory = config.memory;

            m_engine = MakeOwnerPtr<EngineInstance>();
            if (false == m_engine->Initialize(engineConfig, *m_platform, *m_rhiModule))
            {
                ReleaseProcessResources();
                return false;
            }
        }
        catch (const std::bad_alloc&)
        {
            ReleaseProcessResources();
            return false;
        }
        catch (...)
        {
            ReleaseProcessResources();
            throw;
        }

        m_graphicsApi = config.graphicsApi;
        m_lastFrameStatus = FrameStatus::Ready;
        m_initialized = true;
        return true;
    }

    bool EditorApplication::OpenProject(const ProjectDescriptor& project)
    {
        if (false == m_initialized || m_framework || project.graphicsApi != m_graphicsApi)
        {
            return false;
        }
        if (false == CreateSelectedFramework(project.framework))
        {
            return false;
        }
        bool opened = false;
        try
        {
            opened = m_engine->OpenProject(*m_framework);
        }
        catch (...)
        {
            DestroySelectedFramework();
            throw;
        }
        if (false == opened)
        {
            DestroySelectedFramework();
            return false;
        }
        m_frameworkKind = project.framework;
        return true;
    }

    bool EditorApplication::OpenProjectFile(const char* projectFilePath, ProjectFileError& error)
    {
        error = ProjectFileError{};
        if (false == m_initialized || m_framework)
        {
            error.message = "the editor is not ready for another project";
            return false;
        }
        // 어느 프레임워크를 만들지 파일이 정하므로(D-99) 먼저 읽는다. 엔진이 뒤에서 한 번
        // 더 읽지만 프로젝트를 여는 순간에 한 번 더 읽는 것뿐이고, 프레임마다 도는 길이 아니다.
        ProjectFile probe;
        if (false == LoadProjectFile(projectFilePath, probe, error))
        {
            return false;
        }
        const FrameworkKind framework = probe.framework;
        if (false == CreateSelectedFramework(framework))
        {
            error.message = "the framework for this project could not be created";
            return false;
        }

        bool opened = false;
        try
        {
            opened = m_engine->OpenProjectFile(*m_framework, projectFilePath, error);
        }
        catch (...)
        {
            DestroySelectedFramework();
            throw;
        }
        if (false == opened)
        {
            DestroySelectedFramework();
            return false;
        }
        m_frameworkKind = framework;
        m_projectFilePath = projectFilePath != nullptr ? projectFilePath : "";
        return true;
    }

    bool EditorApplication::IsScriptModuleLoaded() const
    {
        return m_engine->IsScriptModuleLoaded();
    }

    const String& EditorApplication::GetScriptModuleError() const
    {
        return m_engine->GetScriptModuleError();
    }

    const ProjectFile& EditorApplication::GetProjectFile() const
    {
        return m_engine->GetProjectFile();
    }

    String EditorApplication::ResolveProjectPath(const char* relativePath) const
    {
        String relative(relativePath != nullptr ? relativePath : "");
        if (relative.empty())
        {
            return relative;
        }
        // 이미 절대경로면 그대로 둔다. `ResolveScriptModulePath` 와 같은 판정이다.
        const bool absolute = relative.size() > 1
            && (relative[0] == '/' || relative[0] == '\\' || relative[1] == ':');
        if (absolute || m_projectFilePath.empty())
        {
            return relative;
        }
        String directory(m_projectFilePath);
        const std::size_t slash = directory.find_last_of("/\\");
        if (slash == String::npos)
        {
            return relative;
        }
        directory.resize(slash + 1);
        directory.append(relative.c_str(), relative.size());
        return directory;
    }

    Canvas* EditorApplication::GetCanvas()
    {
        if (m_framework.Get() == nullptr)
        {
            return nullptr;
        }
        // 만든 쪽이 무엇을 만들었는지 안다. dynamic_cast 를 쓰지 않는 이유가 그것이다.
        switch (m_frameworkKind)
        {
        case FrameworkKind::Framework2D:
            return static_cast<Framework2D*>(m_framework.Get())->GetCanvas();
        case FrameworkKind::Framework3D:
            return static_cast<Framework3D*>(m_framework.Get())->GetCanvas();
        }
        return nullptr;
    }

    bool EditorApplication::LoadCanvas(const char* path, CanvasFileError& error)
    {
        error = CanvasFileError{};
        Canvas* canvas = GetCanvas();
        if (canvas == nullptr)
        {
            error.message = "no project is open";
            return false;
        }
        if (false == LoadCanvasFile(*canvas, path, error))
        {
            return false;
        }
        m_canvasPath = path;
        return true;
    }

    bool EditorApplication::SaveCanvas(const char* path, CanvasFileError& error)
    {
        error = CanvasFileError{};
        Canvas* canvas = GetCanvas();
        if (canvas == nullptr)
        {
            error.message = "no project is open";
            return false;
        }
        if (false == SaveCanvasFile(*canvas, path, error))
        {
            return false;
        }
        m_canvasPath = path;
        m_commands.MarkSaved();
        return true;
    }

    void EditorApplication::RequestSaveCanvas()
    {
        m_saveRequested = true;
    }

    bool EditorApplication::CopySelection()
    {
        const Array<GameObject*> roots = GetTopLevelSelectedObjects();
        if (roots.IsEmpty())
        {
            return false;
        }
        Array<ObjectTreeSnapshot> copied;
        for (std::size_t index = 0; index < roots.Size(); ++index)
        {
            ObjectTreeSnapshot tree;
            if (roots[index] == nullptr || false == tree.Capture(m_objectIds, *roots[index]))
            {
                // 하나라도 뜨지 못하면 클립보드를 건드리지 않는다. 반쪽을 붙이게 두지 않는다.
                return false;
            }
            copied.Add(std::move(tree));
        }
        m_clipboard = std::move(copied);
        return true;
    }

    bool EditorApplication::PasteClipboard()
    {
        Canvas* canvas = GetCanvas();
        if (canvas == nullptr || m_clipboard.IsEmpty())
        {
            return false;
        }
        // 주된 선택의 형제로 붙인다. 고른 것이 없거나 뿌리면 캔버스 뿌리다.
        EditorObjectId parentId = InvalidEditorObjectId;
        if (GameObject* selected = GetSelectedObject())
        {
            if (GameObject* parent = selected->GetParent())
            {
                parentId = m_objectIds.Track(parent);
            }
        }
        auto command = MakeOwnerPtr<PasteObjectsCommand>(*canvas, m_objectIds, m_clipboard, parentId);
        PasteObjectsCommand* raw = command.Get();
        if (false == m_commands.Execute(std::move(command)))
        {
            return false;
        }
        const Array<EditorObjectId> pasted = raw->GetPastedRootIds();
        Array<GameObject*> objects;
        for (std::size_t index = 0; index < pasted.Size(); ++index)
        {
            if (GameObject* object = m_objectIds.Resolve(pasted[index]))
            {
                objects.Add(object);
            }
        }
        SelectObjects({objects.Data(), static_cast<std::uint32_t>(objects.Size())});
        return true;
    }

    void EditorApplication::PerformSaveRequest()
    {
        if (false == m_saveRequested)
        {
            return;
        }
        m_saveRequested = false;
        if (GetCanvas() == nullptr)
        {
            return;
        }
        String path = m_canvasPath;
        if (path.empty())
        {
            // 경로를 모른다. 대화상자로 받는다 - **막히는 호출이라 프레임 밖이어야 한다.**
            FileDialogDesc desc;
            desc.title = Loc::TextOr(LocKeys::DialogSaveCanvasTitle, "Save Canvas");
            desc.filterName = Loc::TextOr(LocKeys::DialogCanvasFilter, "JBro canvas file");
            desc.filterPattern = "*.jcanvas";
            desc.defaultFileName = "Canvas.jcanvas";
            String directory = m_projectFilePath;
            const std::size_t slash = directory.find_last_of("/\\");
            if (slash != String::npos)
            {
                directory.resize(slash);
            }
            desc.initialDirectory = directory.empty() ? nullptr : directory.c_str();
            desc.save = true;
            const bool chosen = m_fileDialog != nullptr
                ? m_fileDialog(desc, path, m_fileDialogUser)
                : m_platform->ShowFileDialog(m_engine->GetMainWindow(), desc, path);
            if (false == chosen || path.empty())
            {
                return;
            }
        }
        CanvasFileError error;
        if (false == SaveCanvas(path.c_str(), error))
        {
            // 실패는 로그가 아니라 사용자에게 간다. 같은 Id 라 연달아 실패해도 하나만 뜬다.
            String message = path;
            message.append("\n", 1);
            message.append(error.message.c_str(), error.message.size());
            OpenPopup(MakeOwnerPtr<MessagePopup>(
                Loc::TextOr(LocKeys::PopupSaveFailed, "The canvas could not be saved"),
                message.c_str(), "save_failed"));
        }
    }

    bool EditorApplication::EnableEditorUi(const Extent2D& gameViewExtent)
    {
        // 크기가 0 인 것은 아래 `CreateTexture` 도 거절한다. 그래도 여기서 막는 것은
        // 계약을 이 함수에서 읽을 수 있게 하려는 것이다 - RHI 가 마침 거절해 주는
        // 것에 기대면, RHI 가 관대해지는 날 조용히 통과한다.
        if (false == m_initialized || m_uiEnabled
            || gameViewExtent.width == 0 || gameViewExtent.height == 0)
        {
            return false;
        }

        Renderer* renderer = m_engine->GetRenderer();
        if (renderer == nullptr)
        {
            return false;
        }
        IRHIDevice* device = renderer->GetDevice();
        if (device == nullptr)
        {
            return false;
        }

        // 게임이 그려 넣고 UI 가 읽는 텍스처다. 둘 다 되어야 한다.
        TextureDesc desc;
        desc.extent = gameViewExtent;
        desc.format = renderer->GetBackBufferFormat();
        desc.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
        m_gameView = device->CreateTexture(desc);
        if (false == m_gameView.IsValid())
        {
            return false;
        }

        // **포맷은 렌더러에게 묻는다.** D3D12 는 파이프라인이 선언한 렌더 타깃 포맷이
        // 실제 타깃과 달라도(BGRA 대 RGBA) 조용히 넘어간다 - 검증 레이어도, GPU 기반
        // 검증도 한 마디 하지 않았고 그림도 똑같이 나온다. 그래서 이 줄을 상수로
        // 바꿔도 뮤테이션이 죽지 않는다. 그래도 묻는다: 규격이 맞추라고 하고,
        // 크기가 다른 포맷이면 그때는 실제로 깨진다.
        if (false == m_ui.Initialize(*device, renderer->GetBackBufferFormat(), m_graphicsApi))
        {
            device->DestroyTexture(m_gameView);
            m_gameView = {};
            return false;
        }

        FrameTarget target;
        target.texture = m_gameView;
        target.extent = gameViewExtent;
        if (false == m_engine->SetGameViewTarget(target)
            || false == renderer->SetFrameOverlay(&DrawEditorOverlay, this))
        {
            ReleaseEditorUi();
            return false;
        }

        m_gameViewExtent = gameViewExtent;
        m_uiEnabled = true;

        // 기본 패널이다. 더 얹는 것은 이 위에 `AddPanel` 로 붙인다.
        // 첫 번째가 가운데를 갖는다 - 게임 화면이 거기여야 한다.
        try
        {
            if (false == AddPanel(MakeOwnerPtr<GameViewPanel>())
                || false == AddPanel(MakeOwnerPtr<HierarchyPanel>())
                || false == AddPanel(MakeOwnerPtr<InspectorPanel>())
                || false == AddPanel(MakeOwnerPtr<StatsPanel>()))
            {
                ReleaseEditorUi();
                return false;
            }
        }
        catch (const std::bad_alloc&)
        {
            ReleaseEditorUi();
            return false;
        }
        return true;
    }

    void EditorApplication::DisableEditorUi()
    {
        if (false == m_uiEnabled)
        {
            return;
        }
        ReleaseEditorUi();
    }

    bool EditorApplication::IsEditorUiEnabled() const
    {
        return m_uiEnabled;
    }

    namespace
    {
        // ImGui 에 넘길 창 이름이다: `보이는이름###안정된이름`.
        //
        // **`ImHashStr` 은 `###` 을 만나면 해시를 처음부터 다시 센다.** 그래서
        // 앞쪽(번역된 이름)이 무엇으로 바뀌든 창의 정체는 뒤쪽 하나로 정해진다 -
        // 언어를 바꿔도 도킹 자리와 크기가 그대로 남는 것이 이 때문이다.
        // 기존 엔진 `CImWindow::GetImGuiLabel` 과 같은 수다.
        String PanelWindowLabel(const EditorPanel& panel)
        {
            String label = panel.GetDisplayTitle() != nullptr
                ? panel.GetDisplayTitle() : "";
            label += "###";
            label += panel.GetTitle() != nullptr ? panel.GetTitle() : "";
            return label;
        }
    }

    bool EditorApplication::AddPanel(OwnerPtr<EditorPanel> panel)
    {
        if (false == m_initialized || panel.Get() == nullptr)
        {
            return false;
        }
        const char* title = panel->GetTitle();
        if (title == nullptr || *title == '\0' || FindPanel(title) != nullptr)
        {
            return false;
        }
        if (false == panel->OnCreate(*this))
        {
            return false;
        }
        try
        {
            m_panels.Add(std::move(panel));
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        return true;
    }

    EditorPanel* EditorApplication::FindPanel(const char* title)
    {
        if (title == nullptr)
        {
            return nullptr;
        }
        for (std::size_t index = 0; index < m_panels.Size(); ++index)
        {
            EditorPanel* panel = m_panels[index].Get();
            if (panel != nullptr && std::strcmp(panel->GetTitle(), title) == 0)
            {
                return panel;
            }
        }
        return nullptr;
    }

    std::size_t EditorApplication::GetPanelCount() const
    {
        return m_panels.Size();
    }

    EditorCommandManager& EditorApplication::GetCommands()
    {
        return m_commands;
    }

    EditorObjectRegistry& EditorApplication::GetObjectIds()
    {
        return m_objectIds;
    }

    void EditorApplication::SetSelectedObject(GameObject* object)
    {
        m_selection.Clear();
        if (object != nullptr)
        {
            m_selection.Add(object->SafeFromThis());
        }
        m_selected = object != nullptr ? object->SafeFromThis() : SafePtr<GameObject>();
    }

    GameObject* EditorApplication::GetSelectedObject() const
    {
        return m_selected.TryGet();
    }

    void EditorApplication::SelectObjects(JArrayView<GameObject*> objects)
    {
        m_selection.Clear();
        for (std::size_t index = 0; index < objects.size; ++index)
        {
            if (objects.data[index] != nullptr)
            {
                m_selection.Add(objects.data[index]->SafeFromThis());
            }
        }
        // 주된 것은 목록의 머리다. 기존 엔진이 그렇게 하고, 한 번에 여럿을
        // 고르는 쪽(사각 선택, 붙여넣기)이 순서를 정해 넘긴다.
        m_selected = m_selection.IsEmpty() ? SafePtr<GameObject>() : m_selection[0];
    }

    void EditorApplication::AddToSelection(GameObject* object)
    {
        if (object == nullptr || IsSelected(object))
        {
            return;
        }
        m_selection.Add(object->SafeFromThis());
        if (m_selected.TryGet() == nullptr)
        {
            // 주된 것이 없거나 죽었다. 방금 더한 것이 그 자리를 받는다.
            m_selected = object->SafeFromThis();
        }
    }

    void EditorApplication::RemoveFromSelection(const GameObject* object)
    {
        if (object == nullptr)
        {
            return;
        }
        for (std::size_t index = 0; index < m_selection.Size(); ++index)
        {
            if (m_selection[index].TryGet() != object)
            {
                continue;
            }
            // 순서를 지키며 한 칸씩 당긴다. 목록의 머리가 주된 것을 정하므로
            // 마지막 것을 끌어다 덮으면 남은 선택의 주인이 바뀐다.
            for (std::size_t later = index + 1; later < m_selection.Size(); ++later)
            {
                m_selection[later - 1] = m_selection[later];
            }
            m_selection.Resize(m_selection.Size() - 1);
            break;
        }
        if (m_selected.TryGet() == object)
        {
            m_selected = m_selection.IsEmpty() ? SafePtr<GameObject>() : m_selection[0];
        }
    }

    bool EditorApplication::IsSelected(const GameObject* object) const
    {
        if (object == nullptr)
        {
            return false;
        }
        for (std::size_t index = 0; index < m_selection.Size(); ++index)
        {
            if (m_selection[index].TryGet() == object)
            {
                return true;
            }
        }
        return false;
    }

    void EditorApplication::ClearSelection()
    {
        m_selection.Clear();
        m_selected = {};
    }

    std::size_t EditorApplication::GetSelectionCount() const
    {
        std::size_t living = 0;
        for (std::size_t index = 0; index < m_selection.Size(); ++index)
        {
            if (m_selection[index].TryGet() != nullptr)
            {
                ++living;
            }
        }
        return living;
    }

    Array<GameObject*> EditorApplication::GetSelectedObjects() const
    {
        Array<GameObject*> living;
        for (std::size_t index = 0; index < m_selection.Size(); ++index)
        {
            if (GameObject* object = m_selection[index].TryGet())
            {
                living.Add(object);
            }
        }
        return living;
    }

    Array<GameObject*> EditorApplication::GetTopLevelSelectedObjects() const
    {
        const Array<GameObject*> living = GetSelectedObjects();
        Array<GameObject*> roots;
        for (std::size_t index = 0; index < living.Size(); ++index)
        {
            bool ancestorSelected = false;
            for (const GameObject* walk = living[index]->GetParent();
                walk != nullptr && false == ancestorSelected;
                walk = walk->GetParent())
            {
                ancestorSelected = IsSelected(walk);
            }
            if (false == ancestorSelected)
            {
                roots.Add(living[index]);
            }
        }
        return roots;
    }

    bool EditorApplication::UiWantsMouse() const
    {
        return m_uiEnabled && m_ui.WantsMouse();
    }

    bool EditorApplication::UiWantsKeyboard() const
    {
        return m_uiEnabled && m_ui.WantsKeyboard();
    }

    TextureHandle EditorApplication::GetGameViewTexture() const
    {
        return m_gameView;
    }

    void EditorApplication::RequestGameView()
    {
        m_gameViewRequested = true;
    }

    Extent2D EditorApplication::GetGameViewExtent() const
    {
        return m_gameViewExtent;
    }

    void EditorApplication::AbandonEditorUi()
    {
        DestroyPanels();
        // 엔진이 렌더 실패로 스스로 정리하면서 디바이스까지 지운 뒤다. 우리가 만든
        // 텍스처도 그때 함께 사라졌으므로 지우려 들지 않는다 - 죽은 디바이스로
        // DestroyTexture 를 부르면 그 자리에서 터진다.
        m_ui.AbandonDevice();
        m_gameView = {};
        m_gameViewExtent = {};
        ClearSelection();
        m_commands.Clear();
        m_objectIds.Clear();
        m_uiEnabled = false;
    }

    void EditorApplication::ReleaseEditorUi()
    {
        DestroyPanels();
        // 팝업은 UI 와 함께 사라진다. 뜨지 않은 채 기다리던 것은 훅을 받지 않는다.
        m_popups.Clear();
        // 게임을 백버퍼로 되돌리고 오버레이를 뗀다. 둘 중 하나만 하면 다음 프레임에
        // 사라진 UI 를 그리려 들거나 게임 화면이 버려진 텍스처로 간다.
        if (m_engine)
        {
            m_engine->SetGameViewTarget({});
            if (Renderer* renderer = m_engine->GetRenderer())
            {
                renderer->SetFrameOverlay(nullptr, nullptr);
                if (m_gameView.IsValid())
                {
                    if (IRHIDevice* device = renderer->GetDevice())
                    {
                        device->DestroyTexture(m_gameView);
                    }
                }
            }
        }
        m_ui.Shutdown();
        m_gameView = {};
        m_gameViewExtent = {};
        ClearSelection();
        m_commands.Clear();
        m_objectIds.Clear();
        m_uiEnabled = false;
    }

    void EditorApplication::DestroyPanels()
    {
        // 들인 순서의 반대로 내보낸다. 나중에 붙은 것이 앞의 것에 기대고
        // 있을 수 있다.
        for (std::size_t index = m_panels.Size(); index > 0; --index)
        {
            if (EditorPanel* panel = m_panels[index - 1].Get())
            {
                panel->OnDestroy();
            }
        }
        m_panels.Clear();
    }

    void EditorApplication::DrawMenuBar()
    {
        if (false == ImGui::BeginMenuBar())
        {
            return;
        }

        // 메뉴는 보이는 이름으로 Id 를 받는다. 언어를 바꾸면 Id 가 달라지지만
        // 메뉴는 창과 달리 도킹 자리 같은 것을 남기지 않으므로 잃는 것이 없다.
        if (ImGui::BeginMenu(Loc::TextOr(LocKeys::MenuFile, "File")))
        {
            // 프로젝트가 없으면 저장할 캔버스도 없다. 경로는 처리 시점에 정한다 -
            // 아는 경로가 없으면 대화상자다.
            const bool canSave = GetCanvas() != nullptr;
            if (false == canSave)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::MenuSaveCanvas, "Save Canvas"), "Ctrl+S"))
            {
                RequestSaveCanvas();
            }
            if (false == canSave)
            {
                ImGui::EndDisabled();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::MenuExit, "Exit")))
            {
                m_exitRequested = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(Loc::TextOr(LocKeys::MenuEdit, "Edit")))
        {
            // **할 수 없는 것은 회색으로 보인다.** 눌리는데 아무 일도 안 하면
            // 고장인지 할 게 없는 건지 알 수 없다.
            const bool canUndo = m_commands.CanUndo();
            if (false == canUndo)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::MenuUndo, "Undo"), "Ctrl+Z"))
            {
                m_commands.Undo();
            }
            if (false == canUndo)
            {
                ImGui::EndDisabled();
            }

            const bool canRedo = m_commands.CanRedo();
            if (false == canRedo)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::MenuRedo, "Redo"), "Ctrl+Y"))
            {
                m_commands.Redo();
            }
            if (false == canRedo)
            {
                ImGui::EndDisabled();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(Loc::TextOr(LocKeys::MenuWindow, "Window")))
        {
            // 패널이 무엇인지 모른 채로 만든다. 레지스트리에 있는 것이 곧
            // 이 목록이라, 패널을 더해도 여기는 그대로다.
            for (std::size_t index = 0; index < m_panels.Size(); ++index)
            {
                EditorPanel* panel = m_panels[index].Get();
                if (panel == nullptr)
                {
                    continue;
                }
                bool open = panel->IsOpen();
                if (ImGui::MenuItem(panel->GetDisplayTitle(), nullptr, &open))
                {
                    panel->SetOpen(open);
                }
            }
            ImGui::EndMenu();
        }

        // 저장하지 않은 편집이 있으면 오른쪽 끝에 말해 준다. 판번호로 재므로
        // 고쳤다 되돌려 원래대로 온 상태는 여기 나오지 않는다.
        if (m_commands.IsDirty())
        {
            const char* mark = Loc::TextOr(LocKeys::MenuUnsaved, "unsaved");
            const float width = ImGui::CalcTextSize(mark).x;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - width
                - ImGui::GetStyle().ItemSpacing.x);
            ImGui::TextDisabled("%s", mark);
        }
        ImGui::EndMenuBar();
    }

    bool EditorApplication::BuildEditorUi(float deltaTime)
    {
        Renderer* renderer = m_engine->GetRenderer();
        if (renderer == nullptr)
        {
            return false;
        }
        const Extent2D display = renderer->GetSurfaceExtent();
        if (display.width == 0 || display.height == 0)
        {
            // 창이 최소화됐다. 그릴 화면이 없으니 UI 도 만들지 않는다.
            return true;
        }

        // **여기서 한 번 더 펌프를 돈다.** 엔진의 Tick 도 펌프를 돌지만 그것은 UI 를
        // 다 만든 뒤라, 거기서 받은 입력은 다음 프레임에나 반영된다.
        m_platform->PumpEvents();
        if (false == m_ui.PushInput(m_platform->GetInputEvents()))
        {
            return false;
        }

        if (false == m_ui.BeginFrame(display, deltaTime))
        {
            return false;
        }

        // 기존 엔진과 같은 배치다: Ctrl+Z 되돌리기, Ctrl+Y 또는
        // Ctrl+Shift+Z 다시하기. **텍스트 필드에 타자를 치는 중이면 건너뛴다** -
        // 이름을 고치다 Ctrl+Z 를 누르면 글자를 되돌려야지 씬을 되돌리면 안 된다.
        if (false == ImGui::GetIO().WantTextInput)
        {
            const bool control = ImGui::GetIO().KeyCtrl;
            const bool shift = ImGui::GetIO().KeyShift;
            if (control && ImGui::IsKeyPressed(ImGuiKey_Z, false))
            {
                if (shift)
                {
                    m_commands.Redo();
                }
                else
                {
                    m_commands.Undo();
                }
            }
            else if (control && ImGui::IsKeyPressed(ImGuiKey_Y, false))
            {
                m_commands.Redo();
            }
        }

        // **창 전체를 덮는 도크 공간.** 패널들은 이 안에 붙는다 - 자리를 ImGui
        // 기본값에 맡기면 작은 창에서 화면 밖으로 밀린다.
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(
            static_cast<float>(display.width), static_cast<float>(display.height)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("##EditorRoot", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_NoNavFocus
                | ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar(3);
        DrawMenuBar();
        const ImGuiID dockSpace = ImGui::GetID("EditorDockSpace");
        ImGui::DockSpace(dockSpace);
        if (false == m_dockLayoutBuilt)
        {
            // 첫 프레임에 한 번만 자리를 잡는다. 그 뒤로는 사용자가 옮긴 자리다.
            //
            // **전부 한 노드에 붙이면 탭으로 겹친다** - 위에 있는 하나만 보이고
            // 나머지는 가려진다. 그래서 방향마다 칸을 떼어 두고, 패널이 말한
            // 자리에 붙인다.
            ImGui::DockBuilderRemoveNode(dockSpace);
            ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockSpace, ImVec2(
                static_cast<float>(display.width),
                static_cast<float>(display.height)));

            ImGuiID center = dockSpace;
            ImGuiID nodes[4] = {};
            nodes[static_cast<int>(EditorDock::Left)] = ImGui::DockBuilderSplitNode(
                center, ImGuiDir_Left, 0.18f, nullptr, &center);
            nodes[static_cast<int>(EditorDock::Right)] = ImGui::DockBuilderSplitNode(
                center, ImGuiDir_Right, 0.24f, nullptr, &center);
            nodes[static_cast<int>(EditorDock::Bottom)] = ImGui::DockBuilderSplitNode(
                center, ImGuiDir_Down, 0.26f, nullptr, &center);
            nodes[static_cast<int>(EditorDock::Center)] = center;

            for (std::size_t index = 0; index < m_panels.Size(); ++index)
            {
                if (const EditorPanel* panel = m_panels[index].Get())
                {
                    const int slot = static_cast<int>(panel->GetPreferredDock());
                    const String label = PanelWindowLabel(*panel);
                    ImGui::DockBuilderDockWindow(label.c_str(), nodes[slot]);
                }
            }
            ImGui::DockBuilderFinish(dockSpace);
            m_dockLayoutBuilt = true;
        }
        ImGui::End();

        for (std::size_t index = 0; index < m_panels.Size(); ++index)
        {
            EditorPanel* panel = m_panels[index].Get();
            if (panel == nullptr)
            {
                continue;
            }
            // **닫혀 있어도 갱신은 돈다.** 보이지 않는다고 멈춰야 하는 일과
            // 계속 돌아야 하는 일은 다르고, 그 판단은 패널의 몫이다.
            panel->OnUpdate(deltaTime);
            if (false == panel->IsOpen())
            {
                continue;
            }
            bool open = true;
            const ImGuiWindowFlags flags = panel->HasMenuBar()
                ? ImGuiWindowFlags_MenuBar
                : ImGuiWindowFlags_None;
            const String label = PanelWindowLabel(*panel);
            // 닫기 단추를 원하지 않는 패널에는 불리언을 넘기지 않는다. ImGui 는
            // 그것으로 단추를 그릴지 정한다.
            bool* closable = panel->HasCloseButton() ? &open : nullptr;
            if (ImGui::Begin(label.c_str(), closable, flags))
            {
                if (panel->HasMenuBar() && ImGui::BeginMenuBar())
                {
                    panel->OnMenuBar();
                    ImGui::EndMenuBar();
                }
                panel->OnDraw();
            }
            ImGui::End();
            panel->SetOpen(open);
        }

        DrawPopups();

        // Ctrl+S. 메뉴 항목의 표시와 같은 손짓이다. 글자 칸이 입력을 먹고 있어도 저장은 된다.
        if (GetCanvas() != nullptr
            && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
        {
            RequestSaveCanvas();
        }
        // Ctrl+C / Ctrl+V. 글자 칸이 입력을 먹고 있을 때는 그 칸의 복사·붙여넣기다.
        if (GetCanvas() != nullptr && false == ImGui::GetIO().WantTextInput)
        {
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, ImGuiInputFlags_RouteGlobal))
            {
                CopySelection();
            }
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V, ImGuiInputFlags_RouteGlobal))
            {
                PasteClipboard();
            }
        }

        // **텍스처와 버퍼는 여기서 올라간다. RHI 프레임 밖이어야 한다** -
        // 아래 엔진 Tick 이 프레임을 열고 나면 만들 수도 쓸 수도 없다.
        return m_ui.EndFrame();
    }

    void EditorApplication::DrawPopups()
    {
        // 1) 밖에서 `ClosePopup` 으로 닫힌 것은 그리기 전에 뺀다 - 뜨지 않은 채 닫힌 것은
        //    `OnEnter` 도 `OnExit` 도 받지 않는다.
        for (std::size_t index = 0; index < m_popups.Size();)
        {
            EditorPopup* popup = m_popups[index].Get();
            if (popup == nullptr || (false == popup->IsAlive() && false == popup->m_shown))
            {
                m_popups.RemoveAt(index);
                continue;
            }
            ++index;
        }
        if (m_popups.IsEmpty())
        {
            return;
        }

        // 2) 맨 앞만 그린다. ImGui 의 모달은 스택이라 한 프레임에 하나만 정상적으로 열린다.
        EditorPopup& popup = *m_popups[0];
        // `###` 뒤만 해싱되므로 제목은 바뀌어도 같은 창이다(D-80 과 같은 수).
        char label[192] = {};
        std::snprintf(label, sizeof(label), "%s###popup_%llu",
            popup.GetTitle() != nullptr ? popup.GetTitle() : "",
            static_cast<unsigned long long>(popup.GetHandle()));
        if (popup.IsAlive())
        {
            if (false == popup.m_shown)
            {
                ImGui::OpenPopup(label);
            }
            const float width = popup.GetInitialWidth();
            const float height = popup.GetInitialHeight();
            const bool autoSize = width <= 0.0f || height <= 0.0f;
            if (false == autoSize && false == popup.m_shown)
            {
                ImGui::SetNextWindowSize(ImVec2(width, height));
            }
            // p_open 이 nullptr 이면 ImGui 가 제목줄의 X 를 그리지 않는다.
            bool* open = popup.HasCloseButton() ? &popup.m_open : nullptr;
            const ImGuiWindowFlags flags = autoSize
                ? ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
                : ImGuiWindowFlags_NoSavedSettings;
            if (ImGui::BeginPopupModal(label, open, flags))
            {
                if (false == popup.m_shown)
                {
                    popup.OnEnter(*this);
                }
                popup.OnDraw(*this);
                // `OnDraw` 안에서 `Close` 를 불렀어도 ImGui 쪽을 따로 닫지 않는다. 다음 프레임에
                // `BeginPopupModal` 이 불리지 않으면 ImGui 가 스스로 닫는다 - 닫는 줄을 두었을
                // 때와 결과가 같아 뮤테이션에서 살아남았고, 잴 수 없는 줄은 지운다(§12).
                ImGui::EndPopup();
            }
            popup.m_shown = true;
        }
        // 3) 닫혔으면 나가는 훅을 부르고 뺀다. 다음 프레임에 다음 것이 뜬다.
        if (false == popup.IsAlive())
        {
            popup.OnExit(*this);
            m_popups.RemoveAt(0);
        }
    }

    PopupHandle EditorApplication::OpenPopup(OwnerPtr<EditorPopup> popup)
    {
        if (popup.Get() == nullptr || false == m_uiEnabled)
        {
            return InvalidPopupHandle;
        }
        const char* id = popup->GetId();
        if (id != nullptr && *id != '\0')
        {
            for (std::size_t index = 0; index < m_popups.Size(); ++index)
            {
                const EditorPopup* waiting = m_popups[index].Get();
                const char* waitingId = waiting != nullptr ? waiting->GetId() : nullptr;
                if (waiting != nullptr && waiting->IsAlive() && waitingId != nullptr
                    && std::strcmp(waitingId, id) == 0)
                {
                    return waiting->GetHandle();
                }
            }
        }
        const PopupHandle handle = m_nextPopupHandle++;
        popup->m_handle = handle;
        m_popups.Add(std::move(popup));
        return handle;
    }

    void EditorApplication::ClosePopup(PopupHandle handle)
    {
        if (handle == InvalidPopupHandle)
        {
            return;
        }
        for (std::size_t index = 0; index < m_popups.Size(); ++index)
        {
            EditorPopup* popup = m_popups[index].Get();
            if (popup != nullptr && popup->GetHandle() == handle)
            {
                popup->Close();
                return;
            }
        }
    }

    bool EditorApplication::IsPopupOpen(PopupHandle handle) const
    {
        if (handle == InvalidPopupHandle)
        {
            return false;
        }
        for (std::size_t index = 0; index < m_popups.Size(); ++index)
        {
            const EditorPopup* popup = m_popups[index].Get();
            if (popup != nullptr && popup->GetHandle() == handle)
            {
                return popup->IsAlive();
            }
        }
        return false;
    }

    bool EditorApplication::IsPopupOpenById(const char* id) const
    {
        if (id == nullptr || *id == '\0')
        {
            return false;
        }
        for (std::size_t index = 0; index < m_popups.Size(); ++index)
        {
            const EditorPopup* popup = m_popups[index].Get();
            const char* candidate = popup != nullptr ? popup->GetId() : nullptr;
            if (popup != nullptr && popup->IsAlive() && candidate != nullptr
                && std::strcmp(candidate, id) == 0)
            {
                return true;
            }
        }
        return false;
    }

    bool EditorApplication::DrawEditorOverlay(
        IRHICommandContext& commands,
        TextureHandle backBuffer,
        std::uint32_t frameSlot,
        void* user)
    {
        auto* self = static_cast<EditorApplication*>(user);
        Renderer* renderer = self->m_engine->GetRenderer();
        if (renderer == nullptr)
        {
            return false;
        }
        const Extent2D display = renderer->GetSurfaceExtent();

        ColorAttachmentDesc attachment;
        attachment.texture = backBuffer;
        attachment.loadOperation = LoadOperation::Clear;
        attachment.clearColor = {0.09f, 0.09f, 0.11f, 1.0f};
        RenderPassDesc pass;
        pass.colorAttachments = {&attachment, 1};
        if (false == commands.BeginRenderPass(pass))
        {
            return false;
        }

        Viewport viewport;
        viewport.width = static_cast<float>(display.width);
        viewport.height = static_cast<float>(display.height);
        commands.SetViewport(viewport);

        const bool drawn = self->m_ui.Draw(commands, frameSlot);
        commands.EndRenderPass();
        return drawn;
    }

    bool EditorApplication::Tick(float deltaTime)
    {
        if (false == m_initialized || m_engine.Get() == nullptr)
        {
            return false;
        }
        // UI 를 먼저 만든다. 텍스처와 정점 버퍼가 RHI 프레임 **밖에서** 올라가야
        // 하는데, 엔진 Tick 이 그 프레임을 연다.
        if (m_uiEnabled && false == BuildEditorUi(deltaTime))
        {
            return false;
        }
        // **게임 뷰 렌더는 매 프레임 opt-in 이다**(D-63). UI 를 먼저 만들었으므로 이 프레임에
        // 게임 뷰 패널이 그려졌는지 이미 안다. 패널이 닫히거나 다른 탭에 가려진 프레임에는
        // 뷰를 기록하지 않고, 텍스처는 파기하지 않아 다시 보일 때 마지막 그림에서 이어진다.
        if (m_uiEnabled && m_gameView.IsValid())
        {
            FrameTarget target;
            target.texture = m_gameView;
            target.extent = m_gameViewExtent;
            target.recordViews = m_gameViewRequested;
            m_engine->SetGameViewTarget(target);
        }
        m_gameViewRequested = false;
        // 저장은 UI 프레임이 닫힌 뒤, 엔진 프레임이 열리기 전이다. 대화상자가 막혀 있는 동안
        // 어느 프레임도 열려 있지 않다.
        PerformSaveRequest();
        if (m_exitRequested)
        {
            // 메뉴에서 끝내기를 골랐다. UI 를 먼저 놓고 내려간다 -
            // 엔진이 디바이스를 지우기 전이어야 한다.
            ReleaseProcessResources();
            return false;
        }
        const bool running = m_engine->Tick(deltaTime);
        m_lastFrameStatus = m_engine->GetLastFrameStatus();
        if (m_framework && m_engine->GetFramework() == nullptr)
        {
            DestroySelectedFramework();
        }
        if (false == running)
        {
            // **엔진은 실패하면 그 자리에서 디바이스까지 놓는다.** UI 가 들고 있던
            // 파이프라인과 텍스처는 그때 함께 사라졌다 - 지우려 들면 터진다.
            if (m_uiEnabled)
            {
                AbandonEditorUi();
            }
            ReleaseProcessResources();
            return false;
        }
        return true;
    }

    void EditorApplication::CloseProject()
    {
        if (false == m_initialized || m_framework.Get() == nullptr)
        {
            return;
        }
        // 캔버스 경로는 프로젝트의 것이다. 다음 프로젝트의 저장이 옛 파일에 가면 안 된다.
        m_canvasPath.clear();
        m_saveRequested = false;
        // 클립보드의 번호는 이 프로젝트의 것이다. 다음 프로젝트에서 그 번호를 믿지 않도록 비운다.
        m_clipboard.Clear();
        m_engine->CloseProject();
        m_lastFrameStatus = m_engine->GetLastFrameStatus();
        if (m_engine->GetFramework() == nullptr)
        {
            DestroySelectedFramework();
        }
    }

    void EditorApplication::Shutdown()
    {
        ReleaseProcessResources();
    }

    Renderer* EditorApplication::GetRenderer()
    {
        return m_engine ? m_engine->GetRenderer() : nullptr;
    }

    const Renderer* EditorApplication::GetRenderer() const
    {
        return m_engine ? m_engine->GetRenderer() : nullptr;
    }

    bool EditorApplication::IsInitialized() const
    {
        return m_initialized;
    }

    bool EditorApplication::HasOpenProject() const
    {
        return static_cast<bool>(m_framework);
    }

    FrameStatus EditorApplication::GetLastFrameStatus() const
    {
        return m_lastFrameStatus;
    }

    bool EditorApplication::CreateSelectedFramework(FrameworkKind framework)
    {
        try
        {
            switch (framework)
            {
            case FrameworkKind::Framework2D:
                m_framework = MakeOwnerPtr<Framework2D>();
                return true;
            case FrameworkKind::Framework3D:
                m_framework = MakeOwnerPtr<Framework3D>();
                return true;
            }
        }
        catch (const std::bad_alloc&)
        {
            m_framework.Reset();
            return false;
        }
        return false;
    }

    void EditorApplication::DestroySelectedFramework()
    {
        m_framework.Reset();
        // 상대경로 기준도 함께 지운다. 프로젝트가 없는데 남아 있으면
        // 다음 프로젝트의 경로가 옛 폴더를 기준으로 풀린다.
        m_projectFilePath.clear();
    }

    void EditorApplication::ReleaseProcessResources()
    {
        // UI 가 잡은 GPU 리소스를 먼저 놓는다. 엔진이 디바이스를 지우고 나면
        // 그것들을 놓아 줄 길이 없다.
        ReleaseEditorUi();
        if (m_engine)
        {
            m_engine->Shutdown();
            m_lastFrameStatus = m_engine->GetLastFrameStatus();
        }
        DestroySelectedFramework();
        m_engine.Reset();
        if (m_rhiModule)
        {
            m_rhiModule->Shutdown();
            m_rhiModule.Reset();
        }
        if (m_platform)
        {
            m_platform->Shutdown();
            m_platform.Reset();
        }
        m_initialized = false;
    }
}
