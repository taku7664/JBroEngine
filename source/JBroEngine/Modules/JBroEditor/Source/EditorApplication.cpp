#include <JBro/Editor/EditorApplication.h>

#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>

#include <JBro/D3D12RHI/D3D12RHI.h>
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
            || config.maxFixedStepsPerFrame == 0 || config.graphicsApi != GraphicsApi::D3D12)
        {
            return false;
        }

        // **글자를 먼저 읽는다.** 창 제목부터 이미 번역 대상이다.
        // 실패해도 그냥 간다 - 코드에 있는 영어 원문으로 떨어질 뿐이다.
        LocalizationTable::Get().Load(
            config.localizationDirectory, config.locale, config.fallbackLocale);

        try
        {
            m_platform = MakeOwnerPtr<WindowsPlatform>();
            if (false == m_platform->Initialize(config.memory))
            {
                ReleaseProcessResources();
                return false;
            }

            m_rhiModule = MakeOwnerPtr<D3D12RHIModule>();
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

    bool EditorApplication::OpenProjectFile(
        const char* projectFilePath,
        FrameworkKind framework,
        ProjectFileError& error)
    {
        error = ProjectFileError{};
        if (false == m_initialized || m_framework)
        {
            error.message = "the editor is not ready for another project";
            return false;
        }
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
        return LoadCanvasFile(*canvas, path, error);
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
        return SaveCanvasFile(*canvas, path, error);
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
        if (false == m_ui.Initialize(*device, renderer->GetBackBufferFormat()))
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
            // 저장은 경로를 받아야 하므로 아직 손잡이가 없다. 그래도 자리를
            // 비워 두지 않는 이유는, 비어 있으면 붙일 자리를 잊기 때문이다.
            ImGui::BeginDisabled();
            ImGui::MenuItem(Loc::TextOr(LocKeys::MenuSaveCanvas, "Save Canvas"), "Ctrl+S");
            ImGui::EndDisabled();
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

        // **텍스처와 버퍼는 여기서 올라간다. RHI 프레임 밖이어야 한다** -
        // 아래 엔진 Tick 이 프레임을 열고 나면 만들 수도 쓸 수도 없다.
        return m_ui.EndFrame();
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
