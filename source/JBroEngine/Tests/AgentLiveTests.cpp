#include <JBro/Editor/Agent/Tools.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorActions.h>
#include <JBro/Editor/EditorPanelRegistry.h>
#include <JBro/Editor/EditorPaths.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/ItemLocator.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Runtime/GameObject.h>
#include "../Modules/JBroEditor/Source/Panel/AgentPanel.h"
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace
{
    using namespace JBro;
    void Check(Bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
    String Path(const std::filesystem::path& value)
    {
        const auto bytes = value.generic_u8string();
        return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    void Tick(EditorApplication& editor, UInt32 frames = 1)
    {
        for (UInt32 i = 0; i < frames; ++i)
        {
            Check(editor.Tick(0.016f), "live guide editor tick failed");
        }
    }
    HWND Window()
    {
        HWND found = nullptr;
        EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
            DWORD process = 0;
            GetWindowThreadProcessId(hwnd, &process);
            wchar_t name[64]{};
            GetClassNameW(hwnd, name, 64);
            if (process == GetCurrentProcessId() && std::wcscmp(name, L"JBroEngineWindow") == 0)
            {
                *reinterpret_cast<HWND*>(data) = hwnd;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&found));
        return found;
    }
    void Click(EditorApplication& editor, HWND hwnd, Int32 x, Int32 y, Bool right = false)
    {
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x.Value, y.Value));
        Tick(editor);
        PostMessageW(hwnd, right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, right ? MK_RBUTTON : MK_LBUTTON, MAKELPARAM(x.Value, y.Value));
        Tick(editor);
        PostMessageW(hwnd, right ? WM_RBUTTONUP : WM_LBUTTONUP, 0, MAKELPARAM(x.Value, y.Value));
        Tick(editor);
    }
    Bool ClickButton(EditorApplication& editor, HWND hwnd, ImGuiWindow* window, const char* label)
    {
        if (window == nullptr)
        {
            return false;
        }
        Widget::ItemLocator::Watch(window->GetID(label));
        Tick(editor, 2);
        Widget::ItemLocator::ItemRect rect;
        if (!Widget::ItemLocator::Take(rect))
        {
            return false;
        }
        Click(editor, hwnd, static_cast<Int32>((rect.minX + rect.maxX) * 0.5f), static_cast<Int32>((rect.minY + rect.maxY) * 0.5f));
        return true;
    }
    struct Bridge
    {
        Agent::Tools tools;
        String guide;
        UInt32 edits = 0;
        explicit Bridge(EditorApplication& editor) : tools(editor) {}
        Agent::ToolResult Call(const String& name, const String& arguments)
        {
            if (name == "jbro_object_create" || name == "jbro_object_delete" || name == "jbro_component_add"
                || name == "jbro_field_set" || name == "jbro_script_write" || name == "jbro_script_create")
            {
                ++edits;
            }
            auto result = tools.Execute(name, arguments);
            if (name == "jbro_guide_start" && result.success)
            {
                guide = nlohmann::json::parse(arguments).at("yaml").get<std::string>();
            }
            return result;
        }
    };
    void Follow(EditorApplication& editor, const String& yaml)
    {
        YamlDocument document;
        YamlError error;
        Check(document.Parse(yaml.c_str(), yaml.size(), error), "AI guide parses in the UI driver");
        const auto steps = document.Find(document.GetRoot(), "Steps");
        const HWND hwnd = Window();
        Check(hwnd != nullptr, "native editor window exists for guide interaction");
        for (UInt32 frame = 0; frame < 1400 && editor.GetGuide().IsRunning(); ++frame)
        {
            Tick(editor);
            auto& guide = editor.GetGuide();
            if (auto* modal = ImGui::GetTopMostPopupModal())
            {
                ClickButton(editor, hwnd, modal, Loc::TextOr(LocKeys::CommonCreate, "Create"));
                continue;
            }
            if (guide.IsConfirming())
            {
                Check(ClickButton(editor, hwnd, ImGui::FindWindowByName("##guide_focus_balloon"), Loc::TextOr(LocKeys::GuideDone, "OK")), "guide confirmation button is visible");
                continue;
            }
            const auto step = document.GetElement(steps, guide.GetStepIndex().Value);
            const String action = document.GetText(document.Find(step, "Do"));
            if (action == "object.select" && guide.ShowsNext() && guide.WhyNextBlocked(editor) == nullptr)
            {
                const char* label = guide.GetStepIndex() + 1 == guide.GetGuide()->steps.Size()
                    ? Loc::TextOr(LocKeys::GuideDone, "OK") : Loc::TextOr(LocKeys::GuideNext, "Next");
                ClickButton(editor, hwnd, ImGui::FindWindowByName("##guide_focus_balloon"), label);
                continue;
            }
            const auto& focus = editor.GetGuideFocus();
            if (!focus.IsHoleSettled() || focus.IsPaused() || focus.GetPath().IsEmpty())
            {
                continue;
            }
            Check(!focus.IsCurrentDisabled(), "AI must choose a usable action, not a disabled menu item");
            const auto target = focus.GetPath().targets[focus.GetLevel().Value];
            const auto& hole = focus.GetHoleRect();
            const Int32 x = static_cast<Int32>((hole.min.x + hole.max.x) * 0.5f);
            const Int32 y = static_cast<Int32>((hole.min.y + hole.max.y) * 0.5f);
            if (target == GuideFocusTargets::ScriptsRootMenu() || target.name == GuideFocusTargets::InspectorComponentMenu(0).name
                || target.name == GuideFocusTargets::HierarchyObjectMenu(0).name)
            {
                Click(editor, hwnd, x, y, true);
            }
            else if (focus.IsAtTarget())
            {
                if (action == "object.focus")
                {
                    const Int32 awayFromGizmo = static_cast<Int32>(hole.min.x + (hole.max.x - hole.min.x) * 0.25f);
                    Click(editor, hwnd, awayFromGizmo, y);
                    Click(editor, hwnd, awayFromGizmo, y);
                }
                else
                {
                    Click(editor, hwnd, x, y);
                }
            }
        }
        Check(!editor.GetGuide().IsRunning(), "AI-generated guide must reach its final confirmation through real UI actions");
    }
}
JBro::Int32 RunAgentLiveTests()
{
    using namespace JBro;
    if (GetEnvironmentVariableW(L"JBRO_AGENT_LIVE", nullptr, 0) == 0)
    {
        std::cout << "AgentLive skipped: opt in with JBRO_AGENT_LIVE=1 (uses the installed Codex account)." << std::endl;
        return 0;
    }
    const auto root = std::filesystem::temp_directory_path() / ("JBroAgentLive-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    wchar_t evidencePath[32768]{};
    const auto evidenceLength = GetEnvironmentVariableW(L"JBRO_AGENT_EVIDENCE", evidencePath, 32768);
    const auto evidence = evidenceLength > 0 && evidenceLength < 32768 ? std::filesystem::path(evidencePath) : root / "evidence";
    std::filesystem::create_directories(root / "Contents/Scripts");
    std::filesystem::create_directories(root / "Assets");
    std::filesystem::create_directories(evidence);
    {
        std::ofstream project(root / "Game.jproject");
        project << "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nRootPath: .\nAssetDirectory: Assets\nScriptSourceDirectory: Contents\n";
    }
    EditorApplication editor;
    EditorApplicationConfig config;
    config.windowVisible = false;
    config.windowWidth = 1440;
    config.windowHeight = 1000;
    Check(editor.Initialize(config), "live editor initializes");
    ProjectFileError error;
    Check(editor.OpenProjectFile(Path(root / "Game.jproject").c_str(), error), "live project opens");
    Check(editor.EnableEditorUi({64, 48}), "live UI initializes");
    auto* first = EditorActions::CreateObject(editor, nullptr);
    Check(EditorActions::AddComponent(editor, *first, MakeNameId("Component::SpriteRenderer2D")), "first sprite attaches");
    auto* second = EditorActions::CreateObject(editor, nullptr);
    Check(EditorActions::AddComponent(editor, *second, MakeNameId("Component::SpriteRenderer2D")), "second sprite attaches");
    auto* secondTransform = static_cast<Component::Transform2D*>(FindComponentAt(*second, MakeNameId("Component::Transform2D"), 0));
    Check(secondTransform != nullptr, "second object has a transform");
    secondTransform->position.x = 3.0f;
    auto* sprite = static_cast<Component::SpriteRenderer2D*>(FindComponentAt(*first, MakeNameId("Component::SpriteRenderer2D"), 0));
    const Int32 originalOrder = sprite->renderOrder;
    editor.SetSelectedObject(first);
    auto* panel = static_cast<AgentPanel*>(editor.CreatePanel(AgentPanel::TypeName));
    Check(panel != nullptr, "live agent panel exists");
    Tick(editor, 5);
    Bridge bridge(editor);
    auto& provider = panel->GetProvider();
    Agent::Settings settings;
    settings.executable = Agent::CodexProvider::Locate(editor.GetPlatform());
    settings.project = Path(root);
    settings.skillsRoot = EditorPaths::JoinPath(ScriptProject::FindEngineRoot(editor.GetPlatform(), editor.GetPlatform().GetExecutableFolder().c_str()).c_str(), "AgentSkills");
    settings.toolDefinitions = Agent::Tools::Definitions();
    settings.instructions = Agent::CodexProvider::Instructions();
    Check(provider.Start(editor.GetPlatform(), settings, Agent::CodexProvider::ToolHandler::Bind<&Bridge::Call>(&bridge)), "installed Codex starts");
    const auto wait = [&]() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
        while (provider.GetConnection() == Agent::Connection::Starting || provider.GetConnection() == Agent::Connection::Running)
        {
            Tick(editor);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            Check(std::chrono::steady_clock::now() < deadline, "live model turn timed out");
        }
        if (provider.GetConnection() != Agent::Connection::Ready)
        {
            std::cout << "live connection: " << provider.GetError() << std::endl;
        }
        Check(provider.GetConnection() == Agent::Connection::Ready, "live Codex must be signed in and ready");
    };
    wait();
    const String questions[] = {
        "\xec\x98\xa4\xeb\xb8\x8c\xec\xa0\x9d\xed\x8a\xb8 \xed\x8f\xac\xec\xbb\xa4\xec\x8a\xa4 \xeb\xaa\xa8\xeb\x93\x9c\xeb\xa5\xbc \xec\x96\xb4\xeb\x96\xbb\xea\xb2\x8c \xec\xa7\x84\xec\x9e\x85\xed\x95\xb4?",
        "\xec\x8a\xa4\xed\x94\x84\xeb\x9d\xbc\xec\x9d\xb4\xed\x8a\xb8 Z\xec\x98\xa4\xeb\x8d\x94 \xec\x95\x88\xeb\xa7\x8c\xec\xa7\x80\xea\xb3\xa0 \xec\x88\x9c\xec\x84\x9c \xec\xa1\xb0\xec\xa0\x88 \xec\x96\xb4\xeb\x96\xbb\xea\xb2\x8c \xed\x95\xb4?",
        "\xec\x8a\xa4\xed\x81\xac\xeb\xa6\xbd\xed\x8a\xb8 \xec\x96\xb4\xeb\x96\xbb\xea\xb2\x8c \xeb\xa7\x8c\xeb\x93\xa4\xec\x96\xb4?"
    };
    for (std::size_t i = 0; i < 3; ++i)
    {
        bridge.guide.clear();
        const auto beforeEdits = bridge.edits;
        std::cout << "[AgentLive] asking scenario " << i + 1 << std::endl;
        Check(provider.Submit(questions[i], bridge.tools.Context()), "exact acceptance question is sent");
        wait();
        std::ofstream transcript(evidence / ("scenario-" + std::to_string(i + 1) + ".txt"), std::ios::binary);
        for (const auto& message : provider.Messages())
        {
            transcript << static_cast<int>(message.role) << ": " << message.text << "\n";
        }
        std::ofstream guide(evidence / ("scenario-" + std::to_string(i + 1) + ".yaml"), std::ios::binary);
        guide << bridge.guide;
        guide.close();
        Check(bridge.edits == beforeEdits, "how-to questions must not directly modify the user's project");
        Check(!bridge.guide.empty(), "AI must create and start a contextual guide, not answer with text only");
        if (i == 0)
        {
            Check(bridge.guide.find("object.focus") != String::npos, "focus-mode guide must enter object focus, not merely frame the camera");
        }
        else if (i == 1)
        {
            Check(bridge.guide.find("object.bring_") != String::npos || bridge.guide.find("object.send_") != String::npos,
                "sprite order guide must use ordering commands instead of editing Z/renderOrder");
        }
        else
        {
            Check(bridge.guide.find("script.create") != String::npos, "script guide must point to the actual script creation workflow");
        }
        editor.SetAgentGuidePaused(true);
        panel->RequestFocus();
        Tick(editor, 8);
        editor.SetAgentGuidePaused(false);
        Follow(editor, bridge.guide);
        if (i == 0)
        {
            Check(editor.GetCanvasViewFocus() == first, "following focus guide enters the requested object");
        }
        if (i == 1)
        {
            Check(sprite->renderOrder == originalOrder, "sprite guide changes sequence without touching renderOrder");
        }
        std::cout << "[AgentLive] scenario " << i + 1 << " model-generated guide followed in native UI" << std::endl;
    }
    provider.Stop();
    editor.Shutdown();
    std::cout << "[AgentLive] evidence: " << Path(evidence) << std::endl;
    return 0;
}
