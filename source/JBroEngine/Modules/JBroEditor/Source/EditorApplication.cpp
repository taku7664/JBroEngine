#include <JBro/Core/Log.h>
#include <JBro/Core/Version.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/EditorShortcuts.h>
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
#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Editor/Command/SetAssetMetaCommand.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Runtime/GameObject.h>

#include "EditorThumbnails.h"
#include "NewProjectPopup.h"
#include "Tool/SpriteViewerWindow.h"

#include "Panel/AssetBrowserPanel.h"
#include "Panel/CanvasViewPanel.h"

#include "Panel/GameViewPanel.h"
#include "Panel/HierarchyPanel.h"
#include "Panel/InspectorPanel.h"
#include "Panel/LogPanel.h"
#include "Panel/ProfilerPanel.h"
#include "Panel/ProjectSettingsPanel.h"
#include "Panel/ShortcutPanel.h"
#include "Panel/StatsPanel.h"

#include <imgui.h>
// **기본 도킹 자리를 잡으려면 내부 헤더가 필요하다.** `DockBuilder*` 는 공개
// `imgui.h` 에 없다 - ImGui 가 아직 확정하지 않은 API 라서다. 에디터를 만드는
// 쪽은 대개 이것을 쓰고, 우리도 첫 프레임에 한 번 부르는 데만 쓴다.
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <cmath>
#include <new>

namespace JBro
{
    namespace
    {
        // 도크 노드가 스스로 그리는 단추를 끈다(ProjectRule §11.3). 왼쪽 위의 창 메뉴와
        // 오른쪽 위의 닫기 단추 둘 다이고, 둘 다 `imgui_internal.h` 의 플래그다 -
        // 공개 헤더에 없을 뿐 저장되는 노드 상태의 일부라 ImGui 가 계속 들고 있는 값이다.
        constexpr ImGuiDockNodeFlags EditorDockNodeFlags =
            ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton;

        // **도크가 두 겹이므로 칸도 둘로 나눈다**(D-134). 기존 엔진의
        // `m_imWndClass.ClassId` + `DockingAllowUnclassed = false` 와 같은 수다.
        //
        // 나누지 않으면 도구 창을 끌다가 **바깥 뿌리에 붙일 수 있다** - 그러면 그 창만
        // 메인 도크 밖으로 빠져나가 메뉴 막대와 나란히 서고, 다시 넣을 방법이 화면에 없다.
        const ImGuiWindowClass& RootDockClass()
        {
            static ImGuiWindowClass value = []
            {
                ImGuiWindowClass made;
                made.ClassId = ImHashStr("JBroRootDock");
                made.DockingAllowUnclassed = false;
                return made;
            }();
            return value;
        }

        // 메인 도크 창의 ImGui 이름이다. `###` 뒤가 식별자라 보이는 이름이 바뀌어도
        // 도킹 자리를 잃지 않는다(패널과 같은 규칙, D-80).
        // 보이는 이름은 앞에 붙는다 - 뿌리에 파일 창이 붙으면 탭으로 이 이름이 보인다(D-155).
        constexpr const char* MainDockLabel = "###MainDock";
    }

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

            // **글자를 먼저 읽는다.** 창 제목부터 이미 번역 대상이다. 파일은 플랫폼이 열므로(D-112) 플랫폼 뒤다.
            // 실패해도 그냥 간다 - 코드에 있는 영어 원문으로 떨어질 뿐이다.
            m_localizationDirectory = config.localizationDirectory != nullptr
                ? config.localizationDirectory
                : "";
            m_locale = config.locale != nullptr ? config.locale : "";
            m_fallbackLocale = config.fallbackLocale != nullptr ? config.fallbackLocale : "";
            LocalizationTable::Get().Load(
                *m_platform, m_localizationDirectory.c_str(), m_locale.c_str(),
                m_fallbackLocale.c_str());

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
            // 에디터는 메타가 없는 에셋 파일에 메타를 만든다(D-111). 게임 실행은 만들지 않는다.
            engineConfig.createMissingAssetMeta = true;
            engineConfig.watchAssetDirectory = true;
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

    namespace
    {
        // 에셋 폴더 기준 상대경로를 실제 경로로. 둘 다 비면 빈 글자다.
        String JoinPath(const String& root, const char* relative)
        {
            String result = root;
            if (relative == nullptr || relative[0] == '\0')
            {
                return result;
            }
            if (false == result.empty() && result.back() != '/' && result.back() != '\\')
            {
                result.append("/", 1);
            }
            result.append(relative, std::strlen(relative));
            return result;
        }

        // `art/enemy.png` 의 폴더는 `art` 다. 슬래시가 없으면 빈 글자(뿌리)다.
        String FolderOf(const char* relative)
        {
            const String path(relative != nullptr ? relative : "");
            const std::size_t slash = path.find_last_of("/\\");
            return slash == String::npos ? String() : String(path.substr(0, slash).c_str());
        }

        String LeafOfPath(const char* relative)
        {
            const String path(relative != nullptr ? relative : "");
            const std::size_t slash = path.find_last_of("/\\");
            return slash == String::npos ? path : String(path.substr(slash + 1).c_str());
        }
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
        if (false == LoadProjectFile(*m_platform, projectFilePath, probe, error))
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

        // ── 세션을 되살린다(D-146) ──────────────────────────────────────────
        const ProjectFile& file = GetProjectFile();
        if (false == file.editorLocale.empty() && file.editorLocale != m_locale)
        {
            // 실패해도 그냥 간다. 지금 언어로 계속 쓸 수 있고, 열리지 않는 것보다 낫다.
            SetEditorLocale(file.editorLocale.c_str());
        }
        RestoreEditorLayout();
        m_sessionCameraX = file.canvasViewCameraX;
        m_sessionCameraY = file.canvasViewCameraY;
        m_sessionCameraSize = file.canvasViewCameraSize;
        if (false == file.lastOpenedCanvasPath.empty() && false == GetAssetRoot().empty())
        {
            // **보던 캔버스를 연다.** 없거나 깨졌으면 로그만 남기고 빈 캔버스로 간다 -
            // 지워진 파일 하나 때문에 프로젝트가 열리지 않으면 고칠 방법도 없다.
            const String canvasPath = JoinPath(GetAssetRoot(), file.lastOpenedCanvasPath.c_str());
            CanvasFileError canvasError;
            if (LoadCanvas(canvasPath.c_str(), canvasError))
            {
                Log::Write(LogLevel::Info, "editor", "opened the canvas from last time: %s",
                    file.lastOpenedCanvasPath.c_str());
            }
            else
            {
                Log::Write(LogLevel::Warning, "editor",
                    "the canvas from last time could not be opened: %s", canvasError.message.c_str());
            }
        }
        return true;
    }

    void EditorApplication::GetSessionCamera(float& centerX, float& centerY, float& size) const
    {
        centerX = m_sessionCameraX;
        centerY = m_sessionCameraY;
        size = m_sessionCameraSize;
    }

    void EditorApplication::GetCanvasViewCamera(float& centerX, float& centerY, float& size)
    {
        centerX = 0.0f;
        centerY = 0.0f;
        size = 0.0f;
        if (CanvasViewPanel* view = static_cast<CanvasViewPanel*>(FindPanel("CanvasView")))
        {
            centerX = view->GetCameraX();
            centerY = view->GetCameraY();
            size = view->GetCameraSize();
        }
    }

    bool EditorApplication::GetAssetSourceSize(
        AssetId asset, std::uint32_t& width, std::uint32_t& height) const
    {
        return m_thumbnails.Get() != nullptr && m_thumbnails->GetSourceSize(asset, width, height);
    }

    bool EditorApplication::OpenSpriteViewer(AssetId asset)
    {
        return m_spriteViewer.Get() != nullptr && m_spriteViewer->Open(asset);
    }

    std::size_t EditorApplication::GetSpriteViewerTabCount() const
    {
        return m_spriteViewer.Get() != nullptr ? m_spriteViewer->GetTabCount() : 0;
    }

    bool EditorApplication::GetSpriteViewerFrame(std::uint32_t& frame) const
    {
        return m_spriteViewer.Get() != nullptr && m_spriteViewer->GetActiveFrame(frame);
    }

    TextureHandle EditorApplication::GetAssetThumbnail(AssetId asset, std::uint32_t maxSide)
    {
        if (m_thumbnails.Get() == nullptr || asset.IsNull())
        {
            return TextureHandle{};
        }
        // 스프라이트를 물으면 그 짝 텍스처의 그림을 준다. 그림을 가진 것은 텍스처 쪽이고,
        // 목록에서 사람이 가리키는 것은 대개 스프라이트다.
        const AssetRegistry& registry = GetAssetRegistry();
        if (const AssetRecord* record = registry.Find(asset))
        {
            if (record->type == AssetType::Sprite && false == record->owner.IsNull())
            {
                return m_thumbnails->Get(record->owner, maxSide);
            }
            if (record->type != AssetType::Texture)
            {
                return TextureHandle{};
            }
        }
        return m_thumbnails->Get(asset, maxSide);
    }

    const Array<EditorSpriteContours::Segment>* EditorApplication::GetSpriteContour(
        AssetHandle texture, const SpriteFrame& frame)
    {
        if (m_contours.Get() == nullptr)
        {
            return nullptr;
        }
        return m_contours->Get(texture, frame);
    }

    String EditorApplication::GetLayoutFilePath() const
    {
        if (m_projectFilePath.empty())
        {
            return String();
        }
        String path = m_projectFilePath;
        path.append(".layout.ini", 11);
        return path;
    }

    void EditorApplication::RestoreEditorLayout()
    {
        // ImGui 가 있어야 읽을 수 있다. UI 를 켜기 전이면 켜는 쪽이 다시 부른다.
        if (m_layoutRestored || false == m_uiEnabled)
        {
            return;
        }
        const String path = GetLayoutFilePath();
        if (path.empty() || false == m_platform->FileExists(path.c_str()))
        {
            return;
        }
        ImGui::LoadIniSettingsFromDisk(path.c_str());
        // **적힌 배치가 이긴다.** 기본 배치를 만드는 쪽(D-134)은 노드를 지우고 다시
        // 만들므로, 둘 다 돌면 사람이 옮겨 둔 자리가 매 실행 지워진다.
        m_layoutRestored = true;
        m_rootLayoutBuilt = true;
        m_dockLayoutBuilt = true;
        Log::Write(LogLevel::Info, "editor", "the window layout was read from %s", path.c_str());
    }

    bool EditorApplication::SetEditorLocale(const char* locale)
    {
        if (locale == nullptr || locale[0] == '\0' || m_platform.Get() == nullptr)
        {
            return false;
        }
        if (false == LocalizationTable::Get().Load(*m_platform, m_localizationDirectory.c_str(),
                locale, m_fallbackLocale.c_str()))
        {
            // **읽지 못했으면 지금 언어를 지킨다.** 표가 반쯤 바뀐 채로 두면 화면의 절반이
            // 키로 나온다. `Load` 가 실패해도 이전 표는 그대로다.
            Log::Write(LogLevel::Warning, "editor", "the language could not be read: %s", locale);
            return false;
        }
        m_locale = locale;
        Log::Write(LogLevel::Info, "editor", "the editor language is now %s", locale);
        return true;
    }

    Array<String> EditorApplication::GetAvailableLocales() const
    {
        Array<String> locales;
        if (m_platform.Get() == nullptr || m_localizationDirectory.empty())
        {
            return locales;
        }
        // 폴더의 `<로케일>.yaml` 이 곧 목록이다. 따로 적어 두면 파일을 더해도 목록에 없다.
        m_platform->EnumerateDirectory(m_localizationDirectory.c_str(),
            [](const char* relativePath, bool isDirectory, void* user) -> bool
            {
                if (isDirectory || relativePath == nullptr)
                {
                    return true;
                }
                const String path(relativePath);
                if (path.size() < 6 || path.find('/') != String::npos)
                {
                    return true;
                }
                const String suffix(path.substr(path.size() - 5).c_str());
                if (suffix != ".yaml")
                {
                    return true;
                }
                static_cast<Array<String>*>(user)->Add(String(path.substr(0, path.size() - 5).c_str()));
                return true;
            },
            &locales);
        // 차례가 있어야 목록이 프레임마다 흔들리지 않는다.
        std::sort(locales.begin(), locales.end(),
            [](const String& left, const String& right) { return left < right; });
        return locales;
    }

    bool EditorApplication::SaveEditorSession()
    {
        if (m_projectFilePath.empty())
        {
            // 적을 파일이 없는 것은 실패가 아니다(파일 없이 연 프로젝트).
            return true;
        }
        ProjectFile settings = GetProjectFile();
        settings.editorLocale = m_locale;

        // 보던 캔버스는 **에셋 폴더 기준 상대경로**로 적는다. 절대경로를 적으면 프로젝트를
        // 옮기거나 다른 기계에서 열 때 가리키는 곳이 없다.
        const String& assetRoot = GetAssetRoot();
        if (false == m_canvasPath.empty() && false == assetRoot.empty()
            && m_canvasPath.size() > assetRoot.size()
            && m_canvasPath.compare(0, assetRoot.size(), assetRoot) == 0)
        {
            std::size_t start = assetRoot.size();
            while (start < m_canvasPath.size()
                && (m_canvasPath[start] == '/' || m_canvasPath[start] == '\\'))
            {
                ++start;
            }
            // **슬래시로 적는다.** 파일은 다른 기계에서도 읽히고, 역슬래시는 거기서
            // 경로의 구분자가 아니라 글자다.
            String relative(m_canvasPath.substr(start).c_str());
            for (std::size_t index = 0; index < relative.size(); ++index)
            {
                if (relative[index] == '\\')
                {
                    relative[index] = '/';
                }
            }
            settings.lastOpenedCanvasPath = relative;
        }

        float cameraX = 0.0f;
        float cameraY = 0.0f;
        float cameraSize = 0.0f;
        GetCanvasViewCamera(cameraX, cameraY, cameraSize);
        if (cameraSize > 0.0f)
        {
            settings.canvasViewCameraX = cameraX;
            settings.canvasViewCameraY = cameraY;
            settings.canvasViewCameraSize = cameraSize;
        }

        // 배치는 ImGui 의 형식 그대로 옆 파일에 적는다. `.jproject` 안에 넣으면 여러 줄짜리
        // 덩어리가 YAML 한가운데 앉고, 그 파일을 손으로 고치기 어려워진다.
        if (m_uiEnabled)
        {
            const String layoutPath = GetLayoutFilePath();
            if (false == layoutPath.empty())
            {
                ImGui::SaveIniSettingsToDisk(layoutPath.c_str());
            }
        }

        ProjectFileError error;
        if (false == SaveProjectSettings(settings, error))
        {
            Log::Write(LogLevel::Warning, "editor",
                "the editor session could not be written: %s", error.message.c_str());
            return false;
        }
        return true;
    }

    const String& EditorApplication::GetAssetRoot() const
    {
        static const String empty;
        return m_engine.Get() != nullptr ? m_engine->GetAssetRoot() : empty;
    }

    bool EditorApplication::RescanAssets()
    {
        if (m_engine.Get() == nullptr || false == m_engine->RescanAssets())
        {
            return false;
        }
        if (m_framework.Get() != nullptr)
        {
            m_framework->BindCanvasAssets();
        }
        return true;
    }

    bool EditorApplication::CreateAssetFolder(const char* relativeFolder, const char* name)
    {
        if (name == nullptr || name[0] == '\0' || GetAssetRoot().empty())
        {
            return false;
        }
        String relative(relativeFolder != nullptr ? relativeFolder : "");
        if (false == relative.empty())
        {
            relative.append("/", 1);
        }
        relative.append(name, std::strlen(name));
        const String path = JoinPath(GetAssetRoot(), relative.c_str());
        if (false == m_platform->CreateDirectoryAt(path.c_str()))
        {
            return false;
        }
        // 빈 폴더는 레지스트리에 없다. 그래도 다시 스캔해 두면 다음 파일이 바로 보인다.
        m_engine->RescanAssets();
        return true;
    }

    bool EditorApplication::RenameAsset(const char* relativePath, const char* newName)
    {
        if (relativePath == nullptr || newName == nullptr || newName[0] == '\0'
            || GetAssetRoot().empty())
        {
            return false;
        }
        String target = FolderOf(relativePath);
        if (false == target.empty())
        {
            target.append("/", 1);
        }
        target.append(newName, std::strlen(newName));
        if (target == relativePath)
        {
            return true;
        }
        const String from = JoinPath(GetAssetRoot(), relativePath);
        const String to = JoinPath(GetAssetRoot(), target.c_str());
        // **덮어쓰지 않는다.** 같은 이름이 이미 있으면 그 에셋을 잃는다.
        if (m_platform->FileExists(to.c_str()) || m_platform->DirectoryExists(to.c_str()))
        {
            return false;
        }
        if (false == m_platform->MoveFileTo(from.c_str(), to.c_str()))
        {
            return false;
        }
        // **`.jmeta` 도 함께 간다.** 두고 오면 아이디가 사라져 참조가 전부 풀린다.
        String fromMeta = from;
        fromMeta.append(".jmeta", 6);
        if (m_platform->FileExists(fromMeta.c_str()))
        {
            String toMeta = to;
            toMeta.append(".jmeta", 6);
            m_platform->MoveFileTo(fromMeta.c_str(), toMeta.c_str());
        }
        m_engine->RescanAssets();
        if (m_framework.Get() != nullptr)
        {
            m_framework->BindCanvasAssets();
        }
        return true;
    }

    bool EditorApplication::MoveAsset(const char* relativePath, const char* targetFolder)
    {
        if (relativePath == nullptr || GetAssetRoot().empty())
        {
            return false;
        }
        const String leaf = LeafOfPath(relativePath);
        String target(targetFolder != nullptr ? targetFolder : "");
        if (false == target.empty())
        {
            target.append("/", 1);
        }
        target.append(leaf.c_str(), leaf.size());
        if (target == relativePath)
        {
            return true;
        }
        const String from = JoinPath(GetAssetRoot(), relativePath);
        const String to = JoinPath(GetAssetRoot(), target.c_str());
        if (m_platform->FileExists(to.c_str()) || m_platform->DirectoryExists(to.c_str()))
        {
            return false;
        }
        if (false == m_platform->MoveFileTo(from.c_str(), to.c_str()))
        {
            return false;
        }
        String fromMeta = from;
        fromMeta.append(".jmeta", 6);
        if (m_platform->FileExists(fromMeta.c_str()))
        {
            String toMeta = to;
            toMeta.append(".jmeta", 6);
            m_platform->MoveFileTo(fromMeta.c_str(), toMeta.c_str());
        }
        m_engine->RescanAssets();
        if (m_framework.Get() != nullptr)
        {
            m_framework->BindCanvasAssets();
        }
        return true;
    }

    bool EditorApplication::DeleteAsset(const char* relativePath)
    {
        if (relativePath == nullptr || GetAssetRoot().empty())
        {
            return false;
        }
        const String path = JoinPath(GetAssetRoot(), relativePath);
        bool removed = false;
        if (m_platform->DirectoryExists(path.c_str()))
        {
            removed = m_platform->DeleteDirectoryAt(path.c_str());
        }
        else
        {
            removed = m_platform->DeleteFileAt(path.c_str());
            String meta = path;
            meta.append(".jmeta", 6);
            if (removed && m_platform->FileExists(meta.c_str()))
            {
                m_platform->DeleteFileAt(meta.c_str());
            }
        }
        if (false == removed)
        {
            return false;
        }
        // 고른 것이 방금 사라졌을 수 있다. 인스펙터가 죽은 것을 읽지 않게 비운다.
        SetSelectedAsset(AssetId{});
        m_engine->RescanAssets();
        if (m_framework.Get() != nullptr)
        {
            m_framework->BindCanvasAssets();
        }
        return true;
    }

    bool EditorApplication::RevealAsset(const char* relativePath)
    {
        if (GetAssetRoot().empty())
        {
            return false;
        }
        const String path = JoinPath(GetAssetRoot(), relativePath);
        return m_platform->RevealInFileBrowser(path.c_str());
    }

    bool EditorApplication::SaveProjectSettings(const ProjectFile& settings, ProjectFileError& error)
    {
        error = ProjectFileError{};
        if (m_projectFilePath.empty())
        {
            error.message = "this project has no file to write";
            return false;
        }
        if (false == SaveProjectFile(*m_platform, m_projectFilePath.c_str(), settings, error))
        {
            return false;
        }
        // 파일이 정본이다. 방금 쓴 것을 **도로 읽어** 엔진이 든 값과 맞춘다 -
        // 쓰기가 일부만 반영했다면 그것도 여기서 드러난다.
        ProjectFile reloaded;
        ProjectFileError reloadError;
        if (false == LoadProjectFile(*m_platform, m_projectFilePath.c_str(), reloaded, reloadError))
        {
            error = reloadError;
            return false;
        }
        m_engine->SetProjectFile(reloaded);
        return true;
    }

    const AssetRegistry& EditorApplication::GetAssetRegistry() const
    {
        static const AssetRegistry empty;
        return m_engine.Get() != nullptr ? m_engine->GetAssetRegistry() : empty;
    }

    AssetSystem* EditorApplication::GetAssetSystem()
    {
        return m_engine.Get() != nullptr ? m_engine->GetAssetSystem() : nullptr;
    }

    bool EditorApplication::IsWatchingAssets() const
    {
        return m_engine.Get() != nullptr && m_engine->IsWatchingAssets();
    }

    void EditorApplication::SetSelectedAsset(AssetId id)
    {
        if (false == id.IsNull())
        {
            m_selection.Clear();
            m_selected = {};
        }
        m_selectedAsset = id;
        ReloadSelectedAssetMeta();
    }

    AssetId EditorApplication::GetSelectedAsset() const
    {
        return m_selectedAsset;
    }

    const AssetMetaFile* EditorApplication::GetSelectedAssetMeta() const
    {
        return m_selectedAssetMetaLoaded ? m_selectedAssetMeta.Get() : nullptr;
    }

    bool EditorApplication::DescribeSelectedAssetMeta(AssetMetaTarget& target) const
    {
        if (m_selectedAsset.IsNull() || m_engine.Get() == nullptr || m_platform.Get() == nullptr)
        {
            return false;
        }
        const AssetRecord* record = m_engine->GetAssetRegistry().Find(m_selectedAsset);
        AssetSystem* assets = m_engine->GetAssetSystem();
        if (record == nullptr || assets == nullptr)
        {
            return false;
        }
        target.platform = m_platform.Get();
        target.assets = assets;
        target.metaPath = assets->GetMetaPath(*record);
        target.id = record->id;
        target.spriteId = m_selectedAssetMetaLoaded ? m_selectedAssetMeta->spriteId : AssetId{};
        return true;
    }

    void EditorApplication::ReloadSelectedAssetMeta()
    {
        m_selectedAssetMetaLoaded = false;
        AssetMetaTarget target;
        if (false == DescribeSelectedAssetMeta(target))
        {
            return;
        }
        if (m_selectedAssetMeta.Get() == nullptr)
        {
            m_selectedAssetMeta = MakeOwnerPtr<AssetMetaFile>();
        }
        AssetMetaError error;
        m_selectedAssetMetaLoaded =
            LoadAssetMetaFile(*target.platform, target.metaPath.c_str(), *m_selectedAssetMeta, error);
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
        // 파일은 플랫폼이 연다(D-112). 캔버스 모듈은 글자만 안다.
        Array<std::byte> text;
        if (path == nullptr || path[0] == '\0')
        {
            error.message = "no path was given";
            return false;
        }
        if (false == m_platform->ReadWholeFile(path, text))
        {
            error.message = "cannot open the file";
            return false;
        }
        if (false == ReadCanvasText(*canvas, reinterpret_cast<const char*>(text.Data()), text.Size(), error))
        {
            return false;
        }
        m_canvasPath = path;
        // 해석 패스는 프레임워크의 것이다(D-115). 게임 호스트도 같은 것을 부른다.
        m_framework->BindCanvasAssets();
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
        // **돌고 있는 동안은 쓰지 않는다**(D-153, 기존 `EditorSimulationGuard`). 재생 중의 캔버스는
        // 게임이 움직여 놓은 상태라, 그것이 파일이 되면 정지로 되돌린 뒤에도 파일에 남는다.
        // 단축키만 막아 두면 메뉴·프로젝트 저장 같은 다른 길로 새므로, 쓰는 길 하나에서 막는다.
        if (IsSimulationPlaying())
        {
            error.message = Loc::TextOr(LocKeys::PopupSaveBlockedWhilePlaying,
                "stop the simulation before saving");
            return false;
        }
        String text;
        if (false == WriteCanvasText(*canvas, text, error))
        {
            return false;
        }
        if (path == nullptr || path[0] == '\0')
        {
            error.message = "no path was given";
            return false;
        }
        JArrayView<std::byte> bytes;
        bytes.data = reinterpret_cast<const std::byte*>(text.data());
        bytes.size = static_cast<std::uint32_t>(text.size());
        if (false == m_platform->WriteWholeFile(path, bytes))
        {
            error.message = "cannot open the file for writing";
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

    void EditorApplication::RequestOpenProject()
    {
        m_openProjectRequested = true;
    }

    void EditorApplication::PerformOpenProjectRequest()
    {
        if (false == m_openProjectRequested)
        {
            return;
        }
        m_openProjectRequested = false;

        // **막히는 호출이라 프레임 밖이어야 한다**(D-93). 대화상자가 떠 있는 동안
        // 어느 프레임도 열려 있지 않다.
        FileDialogDesc desc;
        desc.title = Loc::TextOr(LocKeys::DialogOpenProjectTitle, "Open Project");
        desc.filterName = Loc::TextOr(LocKeys::DialogProjectFilter, "JBro project file");
        desc.filterPattern = "*.jproject";
        desc.save = false;
        String path;
        const bool chosen = m_fileDialog != nullptr
            ? m_fileDialog(desc, path, m_fileDialogUser)
            : m_platform->ShowFileDialog(m_engine->GetMainWindow(), desc, path);
        if (false == chosen || path.empty())
        {
            return;
        }
        SwitchToProject(path.c_str());
    }

    bool EditorApplication::SwitchToProject(const char* projectFilePath)
    {
        // **지금 연 것을 먼저 닫는다.** 고른 것·번호·되돌리기는 이 프로젝트의 것이라
        // 다음 프로젝트에서 그 번호를 믿으면 엉뚱한 오브젝트를 가리킨다.
        ClearSelection();
        SetSelectedObject(nullptr);
        SetSelectedAsset(AssetId{});
        m_commands.Clear();
        m_objectIds.Clear();
        StopSimulation();
        CloseProject();

        ProjectFileError error;
        if (false == OpenProjectFile(projectFilePath, error))
        {
            String message = projectFilePath;
            message.append("\n", 1);
            message.append(error.message.c_str(), error.message.size());
            OpenPopup(MakeOwnerPtr<MessagePopup>(
                Loc::TextOr(LocKeys::PopupOpenProjectFailed, "The project could not be opened"),
                message.c_str(), "open_project_failed"));
            return false;
        }
        // 새 프로젝트도 멈춘 채로 시작한다(D-131).
        m_engine->SetSimulationEnabled(false);
        Log::Write(LogLevel::Info, "project", "opened %s", projectFilePath);
        return true;
    }

    void EditorApplication::RequestNewProject()
    {
        m_newProjectRequested = true;
    }

    void EditorApplication::PerformNewProjectRequest()
    {
        if (false == m_pendingProjectPath.empty())
        {
            // 팝업이 만든 프로젝트로 넘어간다. 팝업은 프레임 안이라 거기서는 닫을 수 없었다.
            const String path = m_pendingProjectPath;
            m_pendingProjectPath.clear();
            SwitchToProject(path.c_str());
        }
        if (false == m_newProjectRequested)
        {
            return;
        }
        m_newProjectRequested = false;
        // **막히는 대화상자라 프레임 밖이다**(D-93). 기존과 같이 폴더를 먼저 고르고 이름은 팝업이 받는다.
        FileDialogDesc desc;
        desc.title = Loc::TextOr(LocKeys::DialogNewProjectFolder, "Choose a folder for the project");
        desc.pickFolder = true;
        String folder;
        const bool chosen = m_fileDialog != nullptr
            ? m_fileDialog(desc, folder, m_fileDialogUser)
            : m_platform->ShowFileDialog(m_engine->GetMainWindow(), desc, folder);
        if (false == chosen || folder.empty())
        {
            return;
        }
        OpenPopup(MakeOwnerPtr<NewProjectPopup>(folder.c_str()));
    }

    bool EditorApplication::CreateProject(
        const char* parentFolder, const char* name, FrameworkKind framework, ProjectCreateFailure* failure)
    {
        String path;
        ProjectFileError error;
        if (false == CreateProjectFile(*m_platform, parentFolder, name, framework, EngineVersionText, path, error))
        {
            Log::Write(LogLevel::Warning, "project", "a new project could not be created: %s", error.message.c_str());
            if (failure != nullptr)
            {
                *failure = error.createFailure;
            }
            return false;
        }
        Log::Write(LogLevel::Info, "project", "created %s", path.c_str());
        m_pendingProjectPath = path;
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
        const bool alsoSession = m_saveProjectRequested;
        m_saveProjectRequested = false;
        if (false == SaveCanvas(path.c_str(), error))
        {
            // 실패는 로그가 아니라 사용자에게 간다. 같은 Id 라 연달아 실패해도 하나만 뜬다.
            String message = path;
            message.append("\n", 1);
            message.append(error.message.c_str(), error.message.size());
            OpenPopup(MakeOwnerPtr<MessagePopup>(
                Loc::TextOr(LocKeys::PopupSaveFailed, "The canvas could not be saved"),
                message.c_str(), "save_failed"));
            return;
        }
        if (alsoSession)
        {
            SaveEditorSession();
        }
    }

    bool EditorApplication::ImportAssetFile(
        const char* sourcePath, const char* relativeFolder, String* importedPath)
    {
        if (sourcePath == nullptr || sourcePath[0] == '\0' || GetAssetRoot().empty())
        {
            return false;
        }
        const String leaf = LeafOfPath(sourcePath);
        if (leaf.empty() || AssetTypeRules::DetectTypeFromPath(leaf.c_str()) == AssetType::Unknown)
        {
            Log::Write(LogLevel::Warning, "asset", "not an asset type the engine knows: %s", sourcePath);
            return false;
        }
        String relative = relativeFolder != nullptr ? String(relativeFolder) : String();
        if (false == relative.empty())
        {
            relative.append("/", 1);
        }
        relative.append(leaf.c_str(), leaf.size());
        const String target = JoinPath(GetAssetRoot(), relative.c_str());
        if (m_platform->FileExists(target.c_str()))
        {
            // **덮어쓰지 않는다.** 이미 있는 파일은 아이디를 들고 있고, 그 아이디를 가리키는
            // 컴포넌트가 있다 - 내용을 바꿔치면 그것들이 모르는 사이에 다른 그림을 그린다.
            Log::Write(LogLevel::Warning, "asset", "an asset with that name already exists: %s",
                relative.c_str());
            return false;
        }
        Array<std::byte> bytes;
        if (false == m_platform->ReadWholeFile(sourcePath, bytes))
        {
            Log::Write(LogLevel::Error, "asset", "the file could not be read: %s", sourcePath);
            return false;
        }
        const String folder = JoinPath(GetAssetRoot(), relativeFolder != nullptr ? relativeFolder : "");
        m_platform->CreateDirectoryAt(folder.c_str());
        if (false == m_platform->WriteWholeFile(target.c_str(),
                {bytes.Data(), static_cast<std::uint32_t>(bytes.Size())}))
        {
            Log::Write(LogLevel::Error, "asset", "the file could not be written: %s", target.c_str());
            return false;
        }
        // 등록은 스캔이 한다. `.jmeta` 도 그때 선다(에디터는 메타를 만드는 쪽으로 훑는다).
        RescanAssets();
        Log::Write(LogLevel::Info, "asset", "imported %s", relative.c_str());
        if (importedPath != nullptr)
        {
            *importedPath = relative;
        }
        return true;
    }

    void EditorApplication::RequestImportAsset(const char* relativeFolder)
    {
        m_importRequested = true;
        m_importFolder = relativeFolder != nullptr ? relativeFolder : "";
    }

    void EditorApplication::PerformImportRequest()
    {
        if (false == m_importRequested)
        {
            return;
        }
        m_importRequested = false;
        if (GetAssetRoot().empty())
        {
            return;
        }
        FileDialogDesc desc;
        desc.title = Loc::TextOr(LocKeys::DialogImportTitle, "Import");
        desc.filterName = Loc::TextOr(LocKeys::DialogImportImages, "Images");
        desc.filterPattern = "*.png;*.jpg;*.jpeg;*.bmp;*.tga";
        desc.save = false;
        String path;
        const bool chosen = m_fileDialog != nullptr
            ? m_fileDialog(desc, path, m_fileDialogUser)
            : m_platform->ShowFileDialog(m_engine->GetMainWindow(), desc, path);
        if (false == chosen || path.empty())
        {
            return;
        }
        String imported;
        if (false == ImportAssetFile(path.c_str(), m_importFolder.c_str(), &imported))
        {
            OpenPopup(MakeOwnerPtr<MessagePopup>(
                Loc::TextOr(LocKeys::PopupImportFailed, "The file could not be imported"),
                path.c_str(), "import_failed"));
            return;
        }
        // 그림이면 바로 뷰어로 연다 - 자르는 옵션을 가져온 자리에서 고칠 수 있게.
        if (const AssetRecord* record = GetAssetRegistry().FindByPath(imported.c_str()))
        {
            OpenSpriteViewer(record->id);
        }
    }

    void EditorApplication::RequestSaveProject()
    {
        m_saveProjectRequested = true;
        RequestSaveCanvas();
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
        // 그림을 만들려면 장치와 에셋이 있어야 한다. 프로젝트가 아직 없으면 에셋도 없고,
        // 그때는 물어도 빈 핸들이 나올 뿐이라 여기서 한 번 잇는다.
        if (AssetSystem* assets = GetAssetSystem())
        {
            m_thumbnails = MakeOwnerPtr<EditorThumbnails>();
            m_thumbnails->Initialize(*device, *assets);
            m_contours = MakeOwnerPtr<EditorSpriteContours>();
            m_contours->Initialize(*assets);
        }
        m_spriteViewer = MakeOwnerPtr<SpriteViewerWindow>();
        m_spriteViewer->Initialize(*this);
        // 프로젝트가 먼저 열렸으면 그때는 읽을 ImGui 가 없었다. 여기서 한 번 더 본다.
        RestoreEditorLayout();

        // **에디터는 멈춘 상태로 뜬다**(D-131). 재생을 누르기 전까지 스크립트와 물리는
        // 돌지 않는다 - 편집하는 동안 게임이 돌면 방금 놓은 값이 다음 프레임에 덮어써진다.
        m_engine->SetSimulationEnabled(false);
        m_simulationPlaying = false;
        m_simulationPaused = false;

        // 기본 패널이다. 더 얹는 것은 이 위에 `AddPanel` 로 붙인다.
        // **첫 번째가 가운데를 갖는다 - 편집 화면이 거기여야 한다.** 게임 뷰도 같은 칸에
        // 탭으로 들어가지만, 처음 보이는 것은 만드는 화면이다(D-130).
        try
        {
            if (false == AddPanel(MakeOwnerPtr<CanvasViewPanel>())
                || false == AddPanel(MakeOwnerPtr<GameViewPanel>())
                || false == AddPanel(MakeOwnerPtr<HierarchyPanel>())
                || false == AddPanel(MakeOwnerPtr<InspectorPanel>())
                || false == AddPanel(MakeOwnerPtr<AssetBrowserPanel>())
                || false == AddPanel(MakeOwnerPtr<StatsPanel>())
                || false == AddPanel(MakeOwnerPtr<LogPanel>())
                || false == AddPanel(MakeOwnerPtr<ProjectSettingsPanel>())
                || false == AddPanel(MakeOwnerPtr<ProfilerPanel>())
                || false == AddPanel(MakeOwnerPtr<ShortcutPanel>()))
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
            // 오브젝트를 고르면 에셋 선택은 빈다 - 인스펙터는 하나만 보인다(D-120).
            m_selectedAsset = {};
            m_selectedAssetMetaLoaded = false;
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
        if (objects.size > 0)
        {
            m_selectedAsset = {};
            m_selectedAssetMetaLoaded = false;
        }
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
        m_selectedAsset = {};
        m_selectedAssetMetaLoaded = false;
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

    void EditorApplication::ClearCanvasObjects()
    {
        Canvas* canvas = GetCanvas();
        if (canvas == nullptr)
        {
            return;
        }
        // **뿌리만 지운다.** 자식은 부모와 함께 사라지므로, 전부를 큐에 넣으면 이미
        // 사라진 것을 한 번 더 지우려 든다.
        Array<GameObject*> roots;
        canvas->GetRootObjects(roots);
        for (std::size_t index = 0; index < roots.Size(); ++index)
        {
            if (GameObject* object = roots[index])
            {
                canvas->DestroyObject(object);
            }
        }
        canvas->FlushPendingDestroy();
    }

    bool EditorApplication::StartSimulation()
    {
        if (m_simulationPlaying || m_engine.Get() == nullptr)
        {
            return false;
        }
        Canvas* canvas = GetCanvas();
        if (canvas == nullptr)
        {
            return false;
        }
        // **되살릴 값을 먼저 뜬다**(§11.5). 뜨지 못하면 재생하지 않는다 - 돌려놓을 수
        // 없는 재생은 편집 내용을 잃는 일이다.
        String snapshot;
        CanvasFileError error;
        if (false == WriteCanvasText(*canvas, snapshot, error))
        {
            return false;
        }
        m_simulationSnapshot = std::move(snapshot);
        m_simulationPlaying = true;
        m_simulationPaused = false;
        m_engine->SetSimulationEnabled(true);
        return true;
    }

    void EditorApplication::StopSimulation()
    {
        if (false == m_simulationPlaying)
        {
            return;
        }
        m_simulationPlaying = false;
        m_simulationPaused = false;
        if (m_engine.Get() != nullptr)
        {
            m_engine->SetSimulationEnabled(false);
        }
        Canvas* canvas = GetCanvas();
        if (canvas == nullptr)
        {
            m_simulationSnapshot.clear();
            return;
        }
        // **고른 것과 번호를 먼저 놓는다.** 아래에서 오브젝트가 통째로 새로 만들어지므로,
        // 지금 들고 있는 주소와 번호는 전부 다른 것을 가리키게 된다.
        ClearSelection();
        SetSelectedObject(nullptr);
        ClearCanvasObjects();
        CanvasFileError error;
        if (m_simulationSnapshot.size() != 0)
        {
            ReadCanvasText(*canvas, m_simulationSnapshot.c_str(), m_simulationSnapshot.size(), error);
        }
        m_simulationSnapshot.clear();
        m_objectIds.Clear();
        // 재생 중에 쌓인 편집은 되돌릴 대상이 사라졌다. 스택을 비우지 않으면 Ctrl+Z 가
        // 이제 없는 오브젝트를 가리킨다.
        m_commands.Clear();
        if (m_framework.Get() != nullptr)
        {
            m_framework->BindCanvasAssets();
        }
    }

    bool EditorApplication::IsSimulationPlaying() const
    {
        return m_simulationPlaying;
    }

    void EditorApplication::ToggleSimulation()
    {
        if (m_simulationPlaying)
        {
            StopSimulation();
        }
        else
        {
            StartSimulation();
        }
    }

    void EditorApplication::SetSimulationPaused(bool paused)
    {
        if (false == m_simulationPlaying)
        {
            return;
        }
        m_simulationPaused = paused;
        if (m_engine.Get() != nullptr)
        {
            m_engine->SetSimulationEnabled(false == paused);
        }
    }

    bool EditorApplication::IsSimulationPaused() const
    {
        return m_simulationPaused;
    }

    bool EditorApplication::EnsureCanvasViewTexture(const Extent2D& extent)
    {
        if (m_canvasView.IsValid()
            && m_canvasViewExtent.width == extent.width
            && m_canvasViewExtent.height == extent.height)
        {
            return true;
        }
        Renderer* renderer = m_engine ? m_engine->GetRenderer() : nullptr;
        IRHIDevice* device = renderer != nullptr ? renderer->GetDevice() : nullptr;
        if (device == nullptr)
        {
            return false;
        }
        TextureDesc desc;
        desc.extent = extent;
        desc.format = renderer->GetBackBufferFormat();
        desc.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
        const TextureHandle created = device->CreateTexture(desc);
        if (false == created.IsValid())
        {
            return false;
        }
        // **새것을 만든 뒤에 옛것을 놓는다.** 만들지 못했는데 먼저 놓으면 그 프레임에
        // 붙일 그림이 없어 캔버스 뷰가 깜빡인다. RHI 는 GPU 가 다 쓴 뒤에 실제로 지운다.
        if (m_canvasView.IsValid())
        {
            device->DestroyTexture(m_canvasView);
        }
        m_canvasView = created;
        m_canvasViewExtent = extent;
        return true;
    }

    void EditorApplication::ReleaseCanvasViewTexture()
    {
        if (m_canvasView.IsValid() && m_engine)
        {
            if (Renderer* renderer = m_engine->GetRenderer())
            {
                if (IRHIDevice* device = renderer->GetDevice())
                {
                    device->DestroyTexture(m_canvasView);
                }
            }
        }
        m_canvasView = {};
        m_canvasViewExtent = {};
        m_canvasViewRequested = false;
    }

    bool EditorApplication::RequestCanvasView(
        const Extent2D& extent, float centerX, float centerY, float orthographicSize)
    {
        if (false == m_uiEnabled || extent.width == 0 || extent.height == 0
            || false == std::isfinite(centerX) || false == std::isfinite(centerY)
            || false == std::isfinite(orthographicSize) || orthographicSize <= 0.0f)
        {
            return false;
        }
        // **요청한 크기를 올려 맞춘다.** 패널을 조금씩 끄는 동안 픽셀마다 텍스처를
        // 다시 만들면 그 프레임마다 GPU 자원을 버리게 된다. 64 의 배수면 몇 번만 만든다.
        constexpr std::uint32_t Step = 64;
        Extent2D rounded;
        rounded.width = ((extent.width + Step - 1) / Step) * Step;
        rounded.height = ((extent.height + Step - 1) / Step) * Step;
        if (false == EnsureCanvasViewTexture(rounded))
        {
            return false;
        }
        m_canvasViewRequest = {};
        m_canvasViewRequest.target = m_canvasView;
        m_canvasViewRequest.extent = m_canvasViewExtent;
        m_canvasViewRequest.centerX = centerX;
        m_canvasViewRequest.centerY = centerY;
        m_canvasViewRequest.orthographicSize = orthographicSize;
        m_canvasViewRequested = true;
        return true;
    }

    bool EditorApplication::RequestCanvasView3D(
        const Extent2D& extent,
        float centerX, float centerY, float centerZ,
        float distance, float yawDegrees, float pitchDegrees)
    {
        // 2D 의 것과 같은 길이다. 배율 대신 거리를 재고, 각이 둘 더 온다.
        if (false == RequestCanvasView(extent, centerX, centerY, 1.0f))
        {
            return false;
        }
        if (false == std::isfinite(centerZ) || false == std::isfinite(distance)
            || distance <= 0.0f
            || false == std::isfinite(yawDegrees) || false == std::isfinite(pitchDegrees))
        {
            m_canvasViewRequested = false;
            return false;
        }
        m_canvasViewRequest.centerZ = centerZ;
        m_canvasViewRequest.distance = distance;
        m_canvasViewRequest.yawDegrees = yawDegrees;
        m_canvasViewRequest.pitchDegrees = pitchDegrees;
        return true;
    }

    TextureHandle EditorApplication::GetCanvasViewTexture() const
    {
        return m_canvasView;
    }

    Extent2D EditorApplication::GetCanvasViewExtent() const
    {
        return m_canvasViewExtent;
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
        // 캔버스 뷰 텍스처도 디바이스와 함께 사라졌다. 지우려 들지 않고 잊는다.
        m_canvasView = {};
        m_canvasViewExtent = {};
        m_canvasViewRequested = false;
        ClearSelection();
        m_commands.Clear();
        m_objectIds.Clear();
        m_uiEnabled = false;
    }

    void EditorApplication::ReleaseEditorUi()
    {
        DestroyPanels();
        // **그림을 먼저 내린다.** 장치가 사라진 뒤에 내리면 이미 없는 텍스처를 파괴하려 든다.
        if (m_thumbnails.Get() != nullptr)
        {
            m_thumbnails->Shutdown();
            m_thumbnails.Reset();
        }
        if (m_spriteViewer.Get() != nullptr)
        {
            // 뷰어는 스프라이트를 잡고 있다. 에셋보다 먼저 놓는다.
            m_spriteViewer->Shutdown();
            m_spriteViewer.Reset();
        }
        if (m_contours.Get() != nullptr)
        {
            m_contours->Shutdown();
            m_contours.Reset();
        }
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
        ReleaseCanvasViewTexture();
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

    bool EditorApplication::DrawShortcutItem(EditorShortcut id, const char* label)
    {
        // **글자도 할 수 있는지도 단축키 표에서 온다**(D-132). 메뉴에 박아 두면
        // 키를 바꿨을 때 화면만 옛 글자로 남는다.
        const bool enabled = EditorShortcuts::CanExecute(*this, id);
        const EditorShortcutText keys = EditorShortcuts::Describe(id);
        if (false == enabled)
        {
            ImGui::BeginDisabled();
        }
        const bool chosen = ImGui::MenuItem(label, keys.value);
        if (false == enabled)
        {
            ImGui::EndDisabled();
        }
        if (chosen)
        {
            EditorShortcuts::Execute(*this, id);
        }
        return chosen;
    }

    void EditorApplication::DrawRootMenuBar()
    {
        if (false == ImGui::BeginMenuBar())
        {
            return;
        }

        // **프로젝트에 대한 것이 여기 있다**(D-134). 기존 엔진의 도크 뿌리와 같은 자리다.
        // 메뉴는 보이는 이름으로 Id 를 받는다. 언어를 바꾸면 Id 가 달라지지만
        // 메뉴는 창과 달리 도킹 자리 같은 것을 남기지 않으므로 잃는 것이 없다.
        if (ImGui::BeginMenu(Loc::TextOr(LocKeys::MenuFile, "File")))
        {
            // 기존과 같은 차례다: 새 프로젝트 · 구분선 · 열기 · 저장.
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::MenuNewProject, "New Project")))
            {
                RequestNewProject();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::MenuOpenProject, "Open Project")))
            {
                RequestOpenProject();
            }
            ImGui::Separator();
            DrawShortcutItem(EditorShortcut::SaveCanvas,
                Loc::TextOr(LocKeys::MenuSaveCanvas, "Save Canvas"));
            {
                // 파일로 연 프로젝트만 적을 자리가 있다.
                Widget::DisableScope disabled(m_projectFilePath.empty() || IsSimulationPlaying());
                if (ImGui::MenuItem(Loc::TextOr(LocKeys::MenuSaveProject, "Save Project")))
                {
                    RequestSaveProject();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::MenuExit, "Exit")))
            {
                m_exitRequested = true;
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

    void EditorApplication::DrawMainMenuBar()
    {
        if (false == ImGui::BeginMenuBar())
        {
            return;
        }

        // **지금 연 캔버스에 대한 것이 여기 있다**(D-134). 기존 엔진의 메인 도크와 같은
        // 차례다: 시뮬레이션, 편집, 창.
        if (ImGui::BeginMenu(Loc::TextOr(LocKeys::MenuSimulation, "Simulation")))
        {
            const bool playing = IsSimulationPlaying();
            DrawShortcutItem(EditorShortcut::TogglePlay, playing
                ? Loc::TextOr(LocKeys::MenuSimulationStop, "Stop")
                : Loc::TextOr(LocKeys::MenuSimulationPlay, "Play"));
            DrawShortcutItem(EditorShortcut::TogglePause,
                Loc::TextOr(LocKeys::MenuSimulationPause, "Pause"));
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(Loc::TextOr(LocKeys::MenuEdit, "Edit")))
        {
            // **할 수 없는 것은 회색으로 보인다.** 눌리는데 아무 일도 안 하면
            // 고장인지 할 게 없는 건지 알 수 없다. 그 판단은 단축키 표가 한다.
            DrawShortcutItem(EditorShortcut::Undo, Loc::TextOr(LocKeys::MenuUndo, "Undo"));
            DrawShortcutItem(EditorShortcut::Redo, Loc::TextOr(LocKeys::MenuRedo, "Redo"));
            ImGui::Separator();
            DrawShortcutItem(EditorShortcut::Copy, Loc::TextOr(LocKeys::HierarchyCopy, "Copy"));
            DrawShortcutItem(EditorShortcut::Paste, Loc::TextOr(LocKeys::HierarchyPaste, "Paste"));
            DrawShortcutItem(EditorShortcut::DeleteSelection,
                Loc::TextOr(LocKeys::HierarchyDelete, "Delete"));
            ImGui::EndMenu();
        }

        // **설정과 디버그는 따로 선다**(D-151). 기존 엔진의 차례와 같다. 같은 창이 창 메뉴의
        // 목록에도 있지만, 설정을 찾는 사람은 "설정" 을 먼저 연다 - 창 목록을 훑게 하면
        // 있는 기능도 없는 것처럼 보인다. 빌드 설정과 GPU 프로파일링은 그 기능이 없어 두지 않는다.
        if (ImGui::BeginMenu(Loc::TextOr(LocKeys::MenuSettings, "Settings")))
        {
            DrawPanelMenuItem("ProjectSettings",
                Loc::TextOr(LocKeys::MenuSettingsProject, "Project Settings"));
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(Loc::TextOr(LocKeys::MenuDebug, "Debug")))
        {
            DrawPanelMenuItem("Profiler", Loc::TextOr(LocKeys::MenuDebugCpuProfiler, "CPU Profiler"));
            DrawPanelMenuItem("Stats", Loc::TextOr(LocKeys::MenuDebugStats, "Statistics"));
            DrawPanelMenuItem("Log", Loc::TextOr(LocKeys::MenuDebugLog, "Log"));
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(Loc::TextOr(LocKeys::MenuWindow, "Window")))
        {
            // 기존 엔진처럼 한 겹 더 들어간다 - 도구 창 말고도 열 것이 늘어날 자리다.
            if (ImGui::BeginMenu(Loc::TextOr(LocKeys::MenuWindowEditor, "Editor")))
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
            // **파일을 여는 창들**이다(기존 `MenuWindowImporter`). 고른 에셋이 그림일 때만 열 수 있다 -
            // 무엇을 열지 모르는 뷰어는 빈 창이다.
            if (ImGui::BeginMenu(Loc::TextOr(LocKeys::MenuWindowImporter, "Importer")))
            {
                const AssetRecord* chosen = GetAssetRegistry().Find(GetSelectedAsset());
                const bool image = chosen != nullptr && AssetTypeRules::IsImageType(chosen->type);
                if (Widget::MenuItem(Loc::TextOr(LocKeys::MenuImportSprite, "Import Sprite"),
                        nullptr, false == GetAssetRoot().empty()))
                {
                    RequestImportAsset("");
                }
                if (Widget::MenuItem(Loc::TextOr(LocKeys::SpriteViewerTitle, "Sprite Viewer"),
                        nullptr, image))
                {
                    OpenSpriteViewer(GetSelectedAsset());
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    void EditorApplication::DrawPanelMenuItem(const char* panelTitle, const char* label)
    {
        EditorPanel* panel = FindPanel(panelTitle);
        // 없는 패널이면 항목을 잠근다. 눌러도 아무 일도 없는 항목은 없는 것보다 나쁘다(D-134).
        Widget::DisableScope disabled(panel == nullptr);
        bool open = panel != nullptr && panel->IsOpen();
        if (ImGui::MenuItem(label, nullptr, &open) && panel != nullptr)
        {
            panel->SetOpen(open);
            if (open)
            {
                // 다른 탭 뒤에 있으면 연 것이 보이지 않는다. 앞으로 꺼낸다.
                ImGui::SetWindowFocus(panelTitle);
            }
        }
    }

    void EditorApplication::DrawRootDock(const Extent2D& display)
    {
        // **창 전체를 덮는 도크 뿌리다**(D-134). 기존 엔진의 `CRootDockWindow` 자리이고,
        // 여기에는 **메인 도크 하나만** 붙는다 - 도구 창은 그 안쪽에 붙는다.
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
                | ImGuiWindowFlags_NoDocking
                | ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar(3);
        DrawRootMenuBar();

        const ImGuiID rootDock = ImGui::GetID("EditorRootDockSpace");
        m_rootDockId = rootDock;
        if (false == m_rootLayoutBuilt)
        {
            // 배치를 먼저 잡는다(안쪽 도크와 같은 이유다).
            ImGui::DockBuilderRemoveNode(rootDock);
            ImGui::DockBuilderAddNode(rootDock,
                ImGuiDockNodeFlags_DockSpace | EditorDockNodeFlags
                    | ImGuiDockNodeFlags_AutoHideTabBar);
            ImGui::DockBuilderSetNodeSize(rootDock, ImVec2(
                static_cast<float>(display.width),
                static_cast<float>(display.height)));
            ImGui::DockBuilderDockWindow(MainDockLabel, rootDock);
            ImGui::DockBuilderFinish(rootDock);
            m_rootLayoutBuilt = true;
        }
        // **노드의 닫기·창 메뉴 단추를 끈다**(ProjectRule §11.3). 탭마다 있는 X 와
        // 별개로 ImGui 는 도크 노드 오른쪽 끝에 **그 노드의 창을 통째로 닫는 X** 를
        // 그린다. 그것까지 달아 두면 탭 하나를 닫으려다 그 칸의 창을 전부 닫는다 -
        // 기존 엔진은 `CImDockWindow` 의 기본값으로 둘 다 꺼 두었다.
        //
        // `AutoHideTabBar` 는 여기 하나뿐인 메인 도크에 탭 줄을 만들지 않으려는 것이다 -
        // 늘 하나인 탭은 이름만 보여 주고 한 줄을 먹는다.
        ImGui::DockSpace(rootDock, ImVec2(0.0f, 0.0f),
            EditorDockNodeFlags | ImGuiDockNodeFlags_AutoHideTabBar, &RootDockClass());
        ImGui::End();
    }

    void EditorApplication::DrawMainDock(float deltaTime)
    {
        // **도구 창이 붙는 안쪽 도크다**(D-134). 기존 엔진의 `CMainDockWindow` 자리이고,
        // 자기 메뉴 막대(시뮬레이션·편집·창)를 가진다.
        //
        // 닫기 단추를 주지 않는다 - 닫으면 에디터에 남는 것이 없다. 기존도
        // `IMWINDOW_FLAG_NO_CLOSE_BUTTON` 을 여기에 세웠다.
        ImGui::SetNextWindowClass(&RootDockClass());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        String mainTitle = Loc::TextOr(LocKeys::DockMain, "Main");
        mainTitle.append(MainDockLabel, std::strlen(MainDockLabel));
        const bool open = ImGui::Begin(mainTitle.c_str(), nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar();
        if (open)
        {
            DrawMainMenuBar();

            const ImGuiID mainDock = ImGui::GetID("EditorDockSpace");
            if (false == m_dockLayoutBuilt)
            {
                // **배치는 도크 공간을 내기 전에 잡는다.** 내고 나서 잡으면 이번 프레임의
                // 노드는 이미 빈 채로 굳어, 패널들이 그 프레임에 떠 있는 창으로 서고
                // 다음 프레임부터는 그 자리를 자기 자리로 기억한다.
                //
                // **전부 한 노드에 붙이면 탭으로 겹친다** - 위에 있는 하나만 보이고
                // 나머지는 가려진다. 그래서 방향마다 칸을 떼어 두고, 패널이 말한
                // 자리에 붙인다.
                const ImVec2 size = ImGui::GetContentRegionAvail();
                ImGui::DockBuilderRemoveNode(mainDock);
                ImGui::DockBuilderAddNode(mainDock,
                    ImGuiDockNodeFlags_DockSpace | EditorDockNodeFlags);
                ImGui::DockBuilderSetNodeSize(mainDock, ImVec2(
                    size.x > 1.0f ? size.x : 1280.0f,
                    size.y > 1.0f ? size.y : 720.0f));

                ImGuiID center = mainDock;
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
                ImGui::DockBuilderFinish(mainDock);
                m_dockLayoutBuilt = true;
            }
            ImGui::DockSpace(mainDock, ImVec2(0.0f, 0.0f), EditorDockNodeFlags);
        }
        else
        {
            // **가려진 프레임에도 안쪽 도크를 살려 둔다**(D-155). 뿌리에 파일 창(스프라이트 뷰어)이
            // 붙어 그 탭이 앞에 오면 이 창은 그려지지 않는데, 그 프레임에 도크 공간을 내지 않으면
            // ImGui 는 노드가 사라졌다고 보고 붙어 있던 패널을 전부 떠 있는 창으로 흩어 놓는다.
            ImGui::DockSpace(ImGui::GetID("EditorDockSpace"), ImVec2(0.0f, 0.0f),
                EditorDockNodeFlags | ImGuiDockNodeFlags_KeepAliveOnly);
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
            bool panelOpen = true;
            const ImGuiWindowFlags flags = panel->HasMenuBar()
                ? ImGuiWindowFlags_MenuBar
                : ImGuiWindowFlags_None;
            const String label = PanelWindowLabel(*panel);
            // 닫기 단추를 원하지 않는 패널에는 불리언을 넘기지 않는다. ImGui 는
            // 그것으로 단추를 그릴지 정한다.
            bool* closable = panel->HasCloseButton() ? &panelOpen : nullptr;
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
            panel->SetOpen(panelOpen);
        }
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

        // **시계가 움직이지 않은 프레임은 실패가 아니다**(D-160). 호스트는 첫 프레임의 시간을 `now - previous` 로
        // 재는데, 시계가 그 사이 넘어가지 않으면 0 이다. UI 는 0 을 받지 않으므로(ImGui 가 단언에서 멈춘다) 거절했고,
        // 에디터가 켜지자마자 꺼졌다 - 서른 번에 세 번꼴이었다. 음수와 NaN 은 여전히 잘못이다.
        constexpr float StillFrameTime = 1.0e-6f;
        const float uiDeltaTime = deltaTime == 0.0f ? StillFrameTime : deltaTime;
        if (false == m_ui.BeginFrame(display, uiDeltaTime))
        {
            return false;
        }

        // **단축키는 한 표에서 온다**(D-132). 누르는 자리와 메뉴에 보이는 글자와
        // 할 수 있는지 재는 자리가 갈리지 않게, 셋 다 `EditorShortcuts` 가 안다.
        EditorShortcuts::ProcessInput(*this);

        DrawRootDock(display);
        DrawMainDock(deltaTime);
        // 뿌리에 붙는 파일 창들이다(D-155). 메인 도크 뒤에 그려야 처음 뜰 때 그 옆 탭으로 선다.
        if (m_spriteViewer.Get() != nullptr)
        {
            m_spriteViewer->Draw(m_rootDockId, RootDockClass());
        }

        DrawPopups();

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
            // **폭만 정하고 높이는 내용에 맞출 수 있다**(D-160, 기존 `ImPopupDesc::InitSize(400, 0)`). ImGui 는 크기의
            // 한 축이 0 이면 그 축을 내용에 맞춘다 - 기존도 이렇게 매 프레임 불렀다. 폭까지 내용에 맞추면 긴 경로를
            // 보이는 팝업이 경로 폭만큼 늘고, 짧으면 칸이 좁아진다.
            const bool fixedWidth = width > 0.0f && height <= 0.0f;
            const bool autoSize = false == fixedWidth && (width <= 0.0f || height <= 0.0f);
            if (fixedWidth)
            {
                ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
            }
            else if (false == autoSize && false == popup.m_shown)
            {
                ImGui::SetNextWindowSize(ImVec2(width, height));
            }
            // **화면 가운데에 뜬다.** 내용에 맞추는 높이는 첫 프레임에 알 수 없어, ImGui 가 화면 높이로 가운데를
            // 잡아 팝업이 맨 위에 붙었다(실제 에디터에서 그랬다). 크기가 서는 동안만 붙들고 그 뒤는 끌어 옮길 수 있다.
            constexpr std::uint8_t SettleFrames = 3;
            if (popup.m_framesShown < SettleFrames)
            {
                ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
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
            if (popup.m_framesShown < SettleFrames)
            {
                ++popup.m_framesShown;
            }
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
        // 에셋 폴더의 변경은 프레임 밖, UI 보다 먼저 적용한다(D-121). 재로드는 핸들을 지키므로 화면은 다음 그림부터
        // 새 자료를 보고, 다시 스캔했으면 못 풀렸던 아이디가 풀릴 수 있어 해석을 다시 돌린다.
        {
            const EngineInstance::AssetChangeSummary changes = m_engine->PollAssetChanges();
            if (changes.rescanned && m_framework.Get() != nullptr)
            {
                m_framework->BindCanvasAssets();
            }
            if (changes.rescanned || changes.reloaded != 0 || changes.renamed != 0 || changes.removed != 0)
            {
                ReloadSelectedAssetMeta();
            }
            if (changes.rescanFailed)
            {
                Log::Write(LogLevel::Warning, "asset",
                    "the asset folder could not be rescanned; the registry keeps its previous contents");
            }
        }
        // 그림 만드는 몫을 이 프레임 몫으로 되돌린다. UI 가 그리면서 부른다.
        if (m_thumbnails.Get() != nullptr)
        {
            m_thumbnails->BeginFrame();
        }
        if (m_contours.Get() != nullptr)
        {
            m_contours->BeginFrame();
        }
        // UI 를 먼저 만든다. 텍스처와 정점 버퍼가 RHI 프레임 **밖에서** 올라가야
        // 하는데, 엔진 Tick 이 그 프레임을 연다.
        if (m_uiEnabled && false == BuildEditorUi(deltaTime))
        {
            Log::Write(LogLevel::Error, "editor", "the editor UI could not build a frame");
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
        // 캔버스 뷰도 같은 규칙이다(D-130). 이 프레임에 패널이 붙였으면 한 번 더 그린다.
        if (m_uiEnabled && m_canvasViewRequested)
        {
            m_engine->RequestEditorView(m_canvasViewRequest);
        }
        m_canvasViewRequested = false;
        // 커맨드가 돌았으면 에셋 해석을 다시 한다(D-115·D-116). UI 가 닫힌 뒤라 이 프레임의
        // 편집이 전부 들어 있고, 엔진 프레임 전이라 다음 그림부터 새 핸들이 보인다.
        if (m_framework.Get() != nullptr && m_commands.GetRevision() != m_boundRevision)
        {
            m_boundRevision = m_commands.GetRevision();
            m_framework->BindCanvasAssets();
            // 메타를 고친 커맨드(와 그 되돌리기)가 돌았을 수 있다. 인스펙터가 디스크와 같은 것을 보이도록.
            ReloadSelectedAssetMeta();
        }
        // 저장은 UI 프레임이 닫힌 뒤, 엔진 프레임이 열리기 전이다. 대화상자가 막혀 있는 동안
        // 어느 프레임도 열려 있지 않다.
        PerformSaveRequest();
        PerformOpenProjectRequest();
        PerformNewProjectRequest();
        PerformImportRequest();
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
        // **닫기 전에 적는다.** 닫고 나면 무엇을 보고 있었는지 아는 것이 아무도 없다.
        SaveEditorSession();
        // 그림은 이 프로젝트의 것이다. 다음 프로젝트의 같은 아이디는 다른 파일이다.
        if (m_thumbnails.Get() != nullptr)
        {
            m_thumbnails->Clear();
        }
        if (m_contours.Get() != nullptr)
        {
            m_contours->Clear();
        }
        if (m_spriteViewer.Get() != nullptr)
        {
            // 탭이 잡은 스프라이트는 이 프로젝트의 것이다. 닫기 전에 놓는다.
            m_spriteViewer->Clear();
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
