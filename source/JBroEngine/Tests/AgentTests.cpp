#include <JBro/Editor/Agent/Provider.h>
#include <JBro/Editor/Agent/Tools.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorPanelRegistry.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/AgentChat.h>
#include <JBro/Editor/Widget/DragDrop.h>
#include <JBro/Editor/Widget/AssetDrag.h>
#include <JBro/Editor/Widget/ComponentDrag.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/ItemLocator.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "../Modules/JBroEditor/Source/Panel/CodeEditorPanel.h"
#include "../Modules/JBroEditor/Source/Panel/AgentPanel.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <stdexcept>
#include <cstring>
#include <thread>

namespace
{
    using namespace JBro;
    using Json = nlohmann::json;
    void Check(Bool value, const char* text)
    {
        if (!value)
        {
            throw std::runtime_error(text);
        }
    }
    struct Channel final : IProcessChannel
    {
        String input, output;
        Bool running = true;
        Bool Write(std::string_view bytes) override { output += bytes; return true; }
        Bool Read(String& into, Bool standardError = false) override
        {
            if (!standardError)
            {
                into = input;
                input.clear();
            }
            return true;
        }
        Bool IsRunning() const override { return running; }
        void Feed(const Json& message) { input += message.dump() + "\n"; }
        Json Last() const
        {
            const auto end = output.size() - 1;
            const auto start = output.rfind('\n', end - 1);
            return Json::parse(output.substr(start == String::npos ? 0 : start + 1, end - (start == String::npos ? 0 : start + 1)));
        }
    };
    struct Counter
    {
        UInt32 calls = 0;
        Agent::ToolResult Call(const String&, const String&)
        {
            ++calls;
            return {true, "Applied once."};
        }
    };
    void Reply(Agent::CodexProvider& provider, Channel& channel, const Json& result)
    {
        channel.Feed({{"id", channel.Last()["id"]}, {"result", result}});
        provider.Poll(0.01f);
    }
    void Ready(Agent::CodexProvider& provider, Channel& channel, Bool resume = false, const Json& turns = Json::array(),
        const Json& inventory = Json::array(), Bool expectReady = true, const Json& models = Json::array())
    {
        Reply(provider, channel, Json::object());
        Check(channel.Last()["method"] == "config/read", "effective project config is inspected through the official API");
        Reply(provider, channel, {{"config", {{"mcp_servers", {{"inherited", {{"command", "unused"}, {"enabled", true}}}}},
            {"plugins", {{"fixture@local", {{"enabled", true}}}}}}}});
        Check(channel.Last()["method"] == "account/read", "initialize must precede account access");
        Reply(provider, channel, {{"account", {{"type", "chatgpt"}}}});
        Check(channel.Last()["method"] == "model/list", "signed-in account queries its model catalog");
        Reply(provider, channel, {{"data", models.empty()
            ? Json::array({{{"model", "test-model"}, {"displayName", "Test"}, {"isDefault", true}}}) : models}});
        Check(channel.Last()["method"] == (resume ? "thread/resume" : "thread/start") && channel.Last()["params"]["model"] == "test-model", "thread explicitly uses the account catalog default, not an incompatible CLI default");
        Check(channel.Last()["params"]["sandbox"] == "read-only", "built-in filesystem stays read-only");
        const auto config = channel.Last()["params"]["config"];
        Check(config["mcp_servers"]["inherited"]["enabled"] == false && config["plugins"]["fixture@local"]["enabled"] == false
            && config["features.apps"] == false, "inherited MCP/plugin entries and app tools are explicitly disabled");
        if (!resume)
        {
            Check(channel.Last()["params"]["dynamicTools"].is_array(), "thread receives dynamic tool definitions");
        }
        Reply(provider, channel, {{"thread", {{"id", "thread-test"}, {"turns", turns}}}});
        Check(provider.GetConnection() == Agent::Connection::Starting && channel.Last()["method"] == "mcpServerStatus/list",
            "thread acknowledgment alone cannot allow user turns before inventory verification");
        const Json records = inventory.empty() ? Json::array({{{"name", "inherited"}, {"runtimeStatus", "disabled"},
            {"tools", Json::object()}, {"resources", Json::array()}, {"resourceTemplates", Json::array()}}}) : inventory;
        Reply(provider, channel, {{"data", records}});
        Check(provider.GetConnection() == (expectReady ? Agent::Connection::Ready : Agent::Connection::Failed),
            "external tools deny readiness; an empty verified inventory permits it");
    }
    void Protocol()
    {
        Agent::CodexProvider provider;
        auto owned = MakeOwnerPtr<Channel>();
        auto* channel = owned.Get();
        Agent::Settings settings;
        settings.project = "C:/test";
        settings.toolDefinitions = "[]";
        Counter counter;
        Check(provider.Attach(std::move(owned), settings, Agent::CodexProvider::ToolHandler::Bind<&Counter::Call>(&counter)), "test channel attaches");
        Ready(provider, *channel);
        Check(provider.Submit("How do I add a collider?", "selected"), "a turn starts");
        Check(!provider.Submit("second", "selected"), "parallel turns cannot race");
        Reply(provider, *channel, {{"turn", {{"id", "turn-test"}}}});
        const String unicode = "\xed\x95\x9c\xea\xb8\x80";
        const String line = Json({{"method", "item/agentMessage/delta"}, {"params", {{"threadId", "thread-test"}, {"turnId", "turn-test"}, {"delta", unicode}}}}).dump() + "\n";
        const auto split = line.find(unicode) + 1;
        channel->input = line.substr(0, split);
        provider.Poll(0.01f);
        channel->input = line.substr(split);
        provider.Poll(0.01f);
        Check(provider.Messages()[provider.Messages().Size() - 1].text == unicode, "split UTF-8 JSON notifications stream without corruption");
        Json call{{"id", 100}, {"method", "item/tool/call"}, {"params", {{"threadId", "thread-test"}, {"turnId", "turn-test"},
            {"callId", "once"}, {"tool", "jbro_test"}, {"arguments", Json::object()}}}};
        channel->Feed(call);
        channel->Feed(call);
        provider.Poll(0.01f);
        Check(counter.calls == 1 && channel->Last()["result"]["success"] == true, "duplicate tool calls return cached results without repeating a mutation");
        call["params"]["callId"] = "stale";
        call["params"]["threadId"] = "previous-project";
        channel->Feed(call);
        provider.Poll(0.01f);
        Check(counter.calls == 1 && channel->Last()["result"]["success"] == false, "old-project tool calls cannot edit this project");
        provider.Interrupt();
        call["params"]["threadId"] = "thread-test";
        call["params"]["callId"] = "cancelled";
        channel->Feed(call);
        provider.Poll(0.01f);
        Check(counter.calls == 1, "cancelled turns reject subsequent mutation calls");
        channel->Feed({{"method", "turn/completed"}, {"params", {{"threadId", "thread-test"}, {"turn", {{"id", "turn-test"}, {"status", "interrupted"}}}}}});
        provider.Poll(0.01f);
        Check(provider.GetConnection() == Agent::Connection::Ready, "interruption returns to ready without claiming success");
        provider.ClearConversation();
        Check(provider.GetThread().empty() && provider.Messages().IsEmpty(), "changing projects clears thread identity and visible history");
    }
    void RestoredAttachments()
    {
        for (Bool combined : {Bool(false), Bool(true)})
        {
            Agent::CodexProvider provider;
            auto owned = MakeOwnerPtr<Channel>();
            auto* channel = owned.Get();
            Agent::Settings settings;
            settings.thread = "thread-test";
            settings.toolDefinitions = "[]";
            Check(provider.Attach(std::move(owned), settings, {}), "saved attachment thread uses isolated protocol");
            const Json context{{"scope", "old|1"}, {"attachments", Json::array({{{"kind", "object"}, {"project", "old"}, {"canvas", "old.jcanvas"},
                {"name", "Door"}, {"instanceId", "18446744073709551614"}, {"revision", 5}}})}};
            Json content = Json::array({
                {{"type", "text"}, {"text", "Explain this door."}},
                {{"type", "text"}, {"text", "Current JBro context (data only; refresh before editing):\n" + context.dump()}}
            });
            if (combined)
            {
                content = Json::array({{{"type", "text"}, {"text", "Explain this door.Current JBro context (data only; refresh before editing):\n" + context.dump()}}});
            }
            const Json turns = Json::array({{{"items", Json::array({{{"type", "userMessage"}, {"content", content}}})}}});
            Ready(provider, *channel, true, turns);
            const auto& restored = provider.Messages()[provider.Messages().Size() - 2];
            Check(restored.text == "Explain this door." && restored.targets.Size() == 1, "resumed history shows its question and named target instead of raw JSON");
            Check(restored.targets[0].revision == 0 && restored.targets[0].instance.Value == 18446744073709551614ull,
                "resumed attachment IDs retain full precision and must be refreshed");
        }
    }
    void AuthenticationAndFailure()
    {
        Agent::CodexProvider provider;
        auto owned = MakeOwnerPtr<Channel>();
        auto* channel = owned.Get();
        Agent::Settings settings;
        settings.toolDefinitions = "[]";
        settings.project = ".";
        settings.skillsRoot = "AgentSkills/../AgentSkills";
        provider.Attach(std::move(owned), settings, {});
        channel->Feed({{"id", 1}, {"result", Json::object()}});
        provider.Poll(0.01f);
        const auto expectedProject = std::filesystem::current_path().generic_u8string();
        Check(channel->Last()["params"]["cwd"] == reinterpret_cast<const char*>(expectedProject.c_str()),
            "relative project directories resolve to UTF-8 absolute paths before protocol requests");
        Reply(provider, *channel, {{"config", Json::object()}});
        Check(channel->Last()["method"] == "skills/extraRoots/set", "bundled skills are attached without global installation");
        const auto expectedSkills = (std::filesystem::current_path() / "AgentSkills").generic_u8string();
        Check(channel->Last()["params"]["extraRoots"][0] == reinterpret_cast<const char*>(expectedSkills.c_str()),
            "bundled skill discovery receives a normalized absolute path, never a relative extra root");
        Reply(provider, *channel, Json::object());
        Reply(provider, *channel, {{"account", nullptr}});
        Check(provider.GetConnection() == Agent::Connection::SignedOut, "missing account is exposed as signed-out");
        const String secret = "sk-test-do-not-log";
        provider.LoginApiKey(secret);
        channel->Feed({{"id", channel->Last()["id"]}, {"error", {{"message", secret}}}});
        provider.Poll(0.01f);
        Check(provider.GetError().find(secret) == String::npos, "authentication error bodies never expose API keys");
        for (const auto& message : provider.Messages())
        {
            Check(message.text.find(secret) == String::npos, "API keys never enter history");
        }
        channel->input = "broken-json\n";
        provider.Poll(0.01f);
        Check(provider.GetConnection() == Agent::Connection::Failed, "invalid protocol fails without executing tools");
        auto another = MakeOwnerPtr<Channel>();
        provider.Attach(std::move(another), settings, {});
        provider.Poll(46.0f);
        Check(provider.GetConnection() == Agent::Connection::Failed, "incompatible or unresponsive startup times out");
    }
    void IsolationPaginationAndRestoration()
    {
        Agent::CodexProvider blocked;
        auto owned = MakeOwnerPtr<Channel>();
        auto* channel = owned.Get();
        Agent::Settings settings;
        settings.toolDefinitions = "[]";
        blocked.Attach(std::move(owned), settings, {});
        Ready(blocked, *channel, false, Json::array(), Json::array({{{"tools", {{"external_write", Json::object()}}}}}), false);
        Check(!blocked.Submit("change something", "{}"), "external MCP tools must block model turns");
        Agent::CodexProvider resources;
        owned = MakeOwnerPtr<Channel>();
        channel = owned.Get();
        resources.Attach(std::move(owned), settings, {});
        Ready(resources, *channel, false, Json::array(), Json::array({{{"tools", Json::object()}, {"runtimeStatus", "connected"},
            {"resources", Json::array({{{"uri", "external://write"}}})}, {"resourceTemplates", Json::array()}}}), false);
        Agent::CodexProvider enforced;
        owned = MakeOwnerPtr<Channel>();
        channel = owned.Get();
        enforced.Attach(std::move(owned), settings, {});
        Reply(enforced, *channel, Json::object());
        Reply(enforced, *channel, {{"config", {{"features", {{"hooks", true}}}}}});
        Check(enforced.GetConnection() == Agent::Connection::Failed, "a feature that remains enabled denies connection before a thread is created");

        Agent::CodexProvider paged;
        owned = MakeOwnerPtr<Channel>();
        channel = owned.Get();
        settings.model = "later";
        settings.thread = "saved";
        paged.Attach(std::move(owned), settings, {});
        Reply(paged, *channel, Json::object());
        Reply(paged, *channel, {{"config", Json::object()}});
        Reply(paged, *channel, {{"account", {{"type", "chatgpt"}}}});
        Reply(paged, *channel, {{"data", Json::array({{{"model", "first"}, {"isDefault", true}}})}, {"nextCursor", "page2"}});
        Check(channel->Last()["method"] == "model/list" && channel->Last()["params"]["cursor"] == "page2", "model pagination finishes before starting a thread");
        Reply(paged, *channel, {{"data", Json::array({{{"model", "later"}}})}, {"nextCursor", nullptr}});
        Check(channel->Last()["params"]["model"] == "later", "saved model on a later page remains selected");
        const Json turns = Json::array({{{"status", "failed"}, {"error", {{"message", "fixture failure"}}},
            {"items", Json::array({{{"type", "agentMessage"}, {"text", "Partial response"}}})}},
            {{"status", "inProgress"}, {"items", Json::array()}}});
        Reply(paged, *channel, {{"thread", {{"id", "saved"}, {"turns", turns}}}});
        Reply(paged, *channel, {{"data", Json::array()}, {"nextCursor", "inventory2"}});
        Check(paged.GetConnection() == Agent::Connection::Starting, "all MCP inventory pages are verified");
        Reply(paged, *channel, {{"data", Json::array()}});
        UInt32 errors = 0;
        for (const auto& message : paged.Messages())
        {
            if (message.role == Agent::MessageRole::Error)
            {
                ++errors;
            }
        }
        Check(errors == 3, "failed and unfinished restored turns remain explicitly incomplete");
        Check(paged.GetConnection() == Agent::Connection::Ready, "restored failures allow a new request, never automatic replay");
        channel->running = false;
        paged.Poll(0.01f);
        Check(paged.GetConnection() == Agent::Connection::Failed, "process exit invalidates readiness");

        Agent::CodexProvider cycle;
        owned = MakeOwnerPtr<Channel>();
        channel = owned.Get();
        cycle.Attach(std::move(owned), settings, {});
        Reply(cycle, *channel, Json::object());
        Reply(cycle, *channel, {{"config", Json::object()}});
        Reply(cycle, *channel, {{"account", {{"type", "chatgpt"}}}});
        const Json repeat{{"data", Json::array()}, {"nextCursor", "same"}};
        Reply(cycle, *channel, repeat);
        Reply(cycle, *channel, repeat);
        Check(cycle.GetConnection() == Agent::Connection::Failed, "repeated cursors cannot loop indefinitely");
    }
    struct Files
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() / ("JBroAgent-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Files()
        {
            std::filesystem::create_directories(root / "Contents/Scripts");
            std::filesystem::create_directories(root / "Assets");
            Write("Game.jproject", "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nRootPath: .\nAssetDirectory: Assets\nScriptSourceDirectory: Contents\n");
            Write("Contents/Scripts/Probe.cpp", "// original\n");
            Write("Assets/Tile.jmat", "Version: 1\n");
        }
        ~Files() { std::filesystem::remove_all(root); }
        String Path(const char* relative) const
        {
            const auto path = (root / relative).generic_u8string();
            return String(reinterpret_cast<const char*>(path.data()), path.size());
        }
        void Write(const char* relative, const String& text)
        {
            std::ofstream file(root / relative, std::ios::binary);
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
    };
    void JunctionBoundary(EditorApplication& editor, Files& files, Agent::Tools& tools, const String& scope)
    {
        std::filesystem::create_directories(files.root / "Outside");
        files.Write("Outside/Outside.cpp", "// outside\n");
        const auto link = files.root / "Contents/Scripts/Linked";
        struct RemoveLink
        {
            std::filesystem::path path;
            ~RemoveLink()
            {
                std::error_code error;
                std::filesystem::remove(path, error);
            }
        } cleanup{link};
        auto& platform = editor.GetPlatform();
        Check(platform.IsPathWithinDirectory(files.Path("FutureScripts").c_str(), files.Path("FutureScripts/Deep/New.h").c_str()),
            "a project can create its first script before the script directory exists");
        Check(!platform.IsPathWithinDirectory(files.Path("FutureScripts").c_str(), files.Path("FutureScripts/../Outside/Outside.cpp").c_str()),
            "a missing script directory does not permit parent traversal");
        const String command = "cmd.exe /d /c mklink /J \"" + files.Path("Contents/Scripts/Linked") + "\" \"" + files.Path("Outside") + "\"";
        auto process = platform.StartProcess(command.c_str(), files.Path("").c_str(), files.Path("junction.log").c_str());
        Int32 exit = -1;
        ProcessStatus status = ProcessStatus::Running;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (status == ProcessStatus::Running && std::chrono::steady_clock::now() < deadline)
        {
            status = platform.PollProcess(process, exit);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        platform.CloseProcess(process);
        Check(status == ProcessStatus::Exited && exit == 0, "isolated Windows junction fixture is created");
        Check(!tools.Execute("jbro_script_read", Json({{"scope", scope}, {"path", "Linked/Outside.cpp"}}).dump()).success,
            "script reads cannot follow a junction outside the script root");
        Check(!tools.Execute("jbro_script_write", Json({{"scope", scope}, {"path", "Linked/Outside.cpp"}, {"token", "any"}, {"text", "overwrite"}}).dump()).success,
            "script writes cannot follow a junction outside the script root");
        Check(!tools.Execute("jbro_script_create", Json({{"scope", scope}, {"folder", "Linked"}, {"name", "Escaped"}}).dump()).success,
            "script creation cannot follow a junction outside the script root");
        const auto listed = tools.Execute("jbro_script_list", "{}");
        Check(listed.success && listed.text.find("Outside") == String::npos, "script listing omits external junction targets");
        std::ifstream outside(files.root / "Outside/Outside.cpp", std::ios::binary);
        const std::string actual((std::istreambuf_iterator<char>(outside)), std::istreambuf_iterator<char>());
        Check(actual == "// outside\n" && !std::filesystem::exists(files.root / "Outside/Escaped.h"), "outside script content remains untouched");
    }
    void SaveAttachmentImage(EditorApplication& editor, const char* suffix);
    void GuideResume(EditorApplication& editor, Agent::Tools& tools, const String& scope, const String& object)
    {
        auto* original = editor.GetSelectedObject();
        const String yaml = "Id: resume\nTitle:\n  Key: test.resume\n  String: Resume\n  Loc: en-US\nSteps:\n"
            "  - Do: field.edit\n    Object: " + object + "\n    Component: Transform2D\n    Field: rotation\n    Value: 15\n"
            "    Title:\n      Key: test.resume.title\n      String: Rotate original\n      Loc: en-US\n"
            "    Body:\n      Key: test.resume.body\n      String: Set the original rotation\n      Loc: en-US\n";
        Check(tools.Execute("jbro_guide_start", Json({{"scope", scope}, {"yaml", yaml}}).dump()).success, "resume guide starts");
        Check(editor.IsAgentGuideReady() && !editor.GetGuide().IsRunning() && !editor.GetGuideFocus().IsActive()
            && editor.GetSelectedObject() == original, "AI prepares a guide without entering a step, changing selection or requesting panel focus");
        const auto prepared = Json::parse(tools.Execute("jbro_guide_status", "{}").text);
        Check(prepared["prepared"] == true && prepared["running"] == false, "guide status separates preparation from execution");
        Check(!tools.Execute("jbro_guide_start", Json({{"scope", scope}, {"yaml", "Id: invalid\nSteps: []\n"}}).dump()).success
            && editor.IsAgentGuideReady(), "invalid replacement YAML preserves the prepared guide");
        auto* agentPanel = editor.FindPanel("Agent");
        auto* inspectorPanel = editor.FindPanel("Inspector");
        Check(agentPanel != nullptr && inspectorPanel != nullptr, "AI and inspector panels exist");
        const auto tick = [&](UInt32 count) {
            for (UInt32 index = 0; index < count; ++index)
            {
                Check(editor.Tick(0.016f), "manual navigation renders through the editor frame path");
            }
        };
        const auto clickTarget = [&]() {
            auto& focus = editor.GetGuideFocus();
            Check(focus.HasHole() && focus.IsHoleSettled(), "manual navigation has a settled clickable target");
            const auto rect = focus.GetHoleRect();
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(((rect.min.x + rect.max.x) * 0.5f).Get(), ((rect.min.y + rect.max.y) * 0.5f).Get());
            tick(2);
            io.AddMouseButtonEvent(0, true);
            tick(1);
            io.AddMouseButtonEvent(0, false);
            tick(80);
        };
        auto* agentWindow = ImGui::FindWindowByName("###Agent");
        auto* inspectorWindow = ImGui::FindWindowByName("###Inspector");
        Check(agentWindow != nullptr && inspectorWindow != nullptr && inspectorWindow->DockId != 0,
            "manual-navigation fixture has an inspector dock");
        ImGui::SetWindowDock(agentWindow, inspectorWindow->DockId, ImGuiCond_Always);
        agentPanel->RequestFocus();
        tick(8);
        Check(agentPanel->IsVisible() && !inspectorPanel->IsVisible(), "AI starts in front of the inspector in the shared dock");
        editor.ResumeAgentGuide();
        Check(!editor.IsAgentGuideReady() && editor.GetGuide().IsRunning(), "only the explicit user start enters the prepared guide");
        const UInt64 navigationRevision = editor.GetCommands().GetRevision();
        tick(160);
        auto& focus = editor.GetGuideFocus();
        Check(agentPanel->IsVisible() && !inspectorPanel->IsVisible() && focus.GetLevel() == 0 && !focus.IsBroken(),
            "starting and dwelling never switches the inspector tab for the user");
        Check(focus.GetHoleRect().max.y - focus.GetHoleRect().min.y < 60.0f,
            "the guide highlights the actual inspector tab rather than the hidden window body");
        Check(editor.GetCommands().GetRevision() == navigationRevision, "waiting for navigation performs no scene edits");
        SaveAttachmentImage(editor, "manual_inspector_tab");
        clickTarget();
        Check(inspectorPanel->IsVisible() && focus.GetLevel() > 0, "only clicking the highlighted tab reveals the inspector and advances navigation");
        SaveAttachmentImage(editor, "manual_inspector_field");
        editor.StopAgentGuide();
        Check(!editor.GetGuide().UsesManualPanels() && !editor.GetGuide().IsOpeningPanel(),
            "stopping an active AI guide clears its panel navigation policy");
        inspectorPanel->SetOpen(false);
        agentPanel->RequestFocus();
        tick(8);
        Check(tools.Execute("jbro_guide_start", Json({{"scope", scope}, {"yaml", yaml}}).dump()).success, "a closed-panel guide can be prepared");
        editor.ResumeAgentGuide();
        tick(160);
        Check(!inspectorPanel->IsOpen() && editor.GetGuide().IsOpeningPanel()
            && focus.GetPath().targets[0] == GuideFocusTargets::Menu("menu.window"),
            "closed panels remain closed and are reached through the real Window menu");
        SaveAttachmentImage(editor, "manual_closed_panel");
        clickTarget();
        Check(focus.GetLevel() == 1, "the user opens Window");
        SaveAttachmentImage(editor, "manual_window_menu");
        clickTarget();
        Check(focus.GetLevel() == 2, "the user opens the Editor submenu");
        SaveAttachmentImage(editor, "manual_editor_menu");
        clickTarget();
        SaveAttachmentImage(editor, "manual_closed_panel_clicked");
        Check(inspectorPanel->IsOpen(), "the highlighted Inspector menu toggle opens the panel");
        Check(!editor.GetGuide().IsOpeningPanel(), "opening Inspector finishes the menu navigation");
        Check(focus.GetPath().targets[0] == GuideFocusTargets::Panel("Inspector"),
            "opening Inspector returns to the original field step");
        editor.SetAgentGuidePaused(true);
        const auto created = tools.Execute("jbro_object_create", Json({{"scope", scope}, {"parent", ""}}).dump());
        Check(created.success, "another object can be selected while chatting");
        const String other = Json::parse(created.text)["instanceId"].get<std::string>();
        const auto detail = tools.Execute("jbro_object_read", Json({{"scope", scope}, {"object", other}}).dump());
        const auto transform = Json::parse(detail.text)["components"][0];
        for (const auto& field : transform["fields"])
        {
            if (field["name"] == "rotation")
            {
                Check(tools.Execute("jbro_field_set", Json({{"scope", scope}, {"object", other}, {"component", transform["instanceId"]},
                    {"field", "rotation"}, {"expected", field["valueYaml"]}, {"value", "15"}}).dump()).success, "other object holds the target value");
            }
        }
        editor.SetAgentGuidePaused(false);
        Check(editor.GetSelectedObject() == original && editor.GetGuide().WhyNextBlocked(editor) != nullptr,
            "resume restores the original object; another object's value cannot complete its step");
        String selectionYaml = yaml;
        selectionYaml.replace(selectionYaml.find("Object: ") + 8, object.size(), "Selection");
        Check(tools.Execute("jbro_guide_start", Json({{"scope", scope}, {"yaml", selectionYaml}}).dump()).success, "selection-bound guide starts");
        editor.ResumeAgentGuide();
        Object::GameObject* alternative = nullptr;
        editor.GetCanvas()->ForEachObject([&](Object::GameObject& candidate) {
            if (&candidate != original)
            {
                alternative = &candidate;
            }
        });
        Check(alternative != nullptr && alternative != original, "alternative selection exists");
        editor.SetAgentGuidePaused(true);
        editor.SetSelectedObject(alternative);
        editor.SetAgentGuidePaused(false);
        Check(editor.GetSelectedObject() == original && editor.GetGuide().WhyNextBlocked(editor) != nullptr,
            "Selection is captured for the paused step, not rebound to a later selection");
        editor.SetAgentGuidePaused(true);
        String replacement = yaml;
        replacement.replace(replacement.find("Object: ") + 8, object.size(), other);
        Check(tools.Execute("jbro_guide_start", Json({{"scope", scope}, {"yaml", replacement}}).dump()).success,
            "a replacement guide can start during chat");
        Check(editor.IsAgentGuideReady() && !editor.GetGuideFocus().IsActive() && editor.GetSelectedObject() == original,
            "preparing a guide for another object leaves the user's current selection alone");
        editor.ResumeAgentGuide();
        Check(!editor.IsAgentGuidePaused() && editor.GetSelectedObject() == alternative, "new guide does not inherit the paused guide's selection");
        Check(tools.Execute("jbro_guide_start", Json({{"scope", scope}, {"yaml", yaml}}).dump()).success, "original target guide restarts");
        editor.ResumeAgentGuide();
        editor.SetAgentGuidePaused(true);
        Check(tools.Execute("jbro_object_delete", Json({{"scope", scope}, {"object", object}}).dump()).success, "guide target can disappear while chatting");
        editor.SetAgentGuidePaused(false);
        Check(!editor.GetGuide().IsRunning(), "a deleted target stops the guide instead of substituting another object");
        editor.GetCanvas()->ForEachObject([&](Object::GameObject& remaining) { editor.SetSelectedObject(&remaining); });
        auto* remainingSelection = editor.GetSelectedObject();
        Check(tools.Execute("jbro_guide_start", Json({{"scope", scope}, {"yaml", yaml}}).dump()).success,
            "a guide can be prepared before its target is checked at user start");
        editor.ResumeAgentGuide();
        Check(!editor.GetGuide().IsRunning() && !editor.IsAgentGuideReady() && editor.GetSelectedObject() == remainingSelection,
            "starting revalidates a deleted target without substituting the current selection");
        Check(tools.Execute("jbro_guide_start", Json({{"scope", scope}, {"yaml", replacement}}).dump()).success,
            "another guide can be prepared for cancellation");
        Check(tools.Execute("jbro_guide_stop", Json({{"scope", scope}}).dump()).success
            && !editor.IsAgentGuideReady() && editor.GetAgentGuide() == nullptr
            && !editor.GetGuide().UsesManualPanels(), "stop cancels preparation and clears the manual navigation policy");
    }
    struct AttachmentPicker
    {
        String path;
        UInt32 calls = 0;
        Bool accepted = true;
        static Bool Answer(const FileDialogDesc& desc, String& result, void* user)
        {
            auto& picker = *static_cast<AttachmentPicker*>(user);
            ++picker.calls;
            Check(!desc.save && !desc.pickFolder, "attachment opens a file selection dialog");
            Check(!ImGui::GetCurrentContext()->WithinFrameScope, "file picking never blocks an open editor UI frame");
            result = picker.path;
            return picker.accepted;
        }
    };
    void SaveAttachmentImage(EditorApplication& editor, const char* suffix)
    {
        char* directory = nullptr;
        std::size_t size = 0;
        if (_dupenv_s(&directory, &size, "JBRO_EDITOR_SHOT") != 0 || directory == nullptr)
        {
            return;
        }
        const std::filesystem::path path = std::filesystem::path(directory) / (String("agent_attachments_") + suffix + ".ppm");
        std::free(directory);
        const auto extent = editor.GetRenderer()->GetSurfaceExtent();
        Array<std::byte> pixels;
        pixels.Resize(static_cast<std::size_t>(extent.width.Value) * extent.height.Value * 4);
        TextureReadback image;
        Check(editor.GetRenderer()->ReadBackBuffer(pixels.Data(), pixels.Size(), image), "editor screenshot uses existing renderer readback");
        std::ofstream file(path, std::ios::binary);
        file << "P6\n" << extent.width.Value << ' ' << extent.height.Value << "\n255\n";
        const Bool bgra = image.format == TextureFormat::BGRA8Unorm || image.format == TextureFormat::BGRA8UnormSrgb;
        for (UInt32 y = 0; y < image.extent.height; ++y)
        {
            for (UInt32 x = 0; x < image.extent.width; ++x)
            {
                const auto* pixel = reinterpret_cast<const char*>(pixels.Data()) + static_cast<std::size_t>(y.Value) * image.rowPitch.Value + x.Value * 4;
                const char rgb[] = {pixel[bgra ? 2 : 0], pixel[1], pixel[bgra ? 0 : 2]};
                file.write(rgb, 3);
            }
        }
        Check(file.good(), "attachment screenshot is saved");
    }
    void AttachmentInput(EditorApplication& editor, AgentPanel& panel, Agent::Tools& tools, AttachmentPicker& picker)
    {
        auto* object = editor.GetSelectedObject();
        Check(object != nullptr, "attachment input has a selected object");
        const auto objectId = editor.GetObjectIds().Track(object);
        const auto component = object->GetComponents()[0].reference->GetInstanceId();
        const auto* record = editor.GetAssetRegistry().FindByPath("Tile.jmat");
        Check(record != nullptr, "attachment asset is registered by the normal project scan");
        const AssetId assetId = record->id;
        Agent::Target componentTarget, assetTarget;
        Check(tools.MakeComponentTarget(objectId, component, componentTarget), "component attachments capture exact instance identity");
        Check(tools.MakeAssetTarget(assetId, assetTarget), "asset attachments capture AssetId");
        Check(!tools.MakeComponentTarget(objectId, 0, componentTarget), "an unrelated component ID cannot resolve by slot or name");
        Check(tools.MakeComponentTarget(objectId, component, componentTarget), "valid component target is recoverable after rejection");
        editor.ClearSelection();
        Array<Agent::Target> explicitTargets;
        explicitTargets.Add(componentTarget);
        explicitTargets.Add(assetTarget);
        const auto changedSelection = Json::parse(tools.Context(explicitTargets.View()));
        Check(changedSelection["selection"].empty() && changedSelection["attachments"].size() == 2
            && tools.IsTargetValid(componentTarget), "explicit attachments do not follow later selection changes");
        const auto refreshed = Json::parse(tools.Execute("jbro_context", "{}").text);
        Check(refreshed["attachments"].size() == 2 && refreshed["attachments"][1]["metadata"]["type"] == "Material",
            "context refresh retains explicit subjects and live metadata even with a resumed old toolset");
        editor.SetSelectedObject(object);
        const String scope = Json::parse(tools.Context())["scope"].get<std::string>();
        char assetText[Uuid::TextCapacity]{};
        assetId.ToText(assetText, sizeof(assetText));
        const auto metadata = tools.Execute("jbro_asset_read", Json({{"scope", scope}, {"asset", assetText}}).dump());
        Check(metadata.success && Json::parse(metadata.text)["path"] == "Tile.jmat", "asset tool resolves live metadata by ID");

        auto owned = MakeOwnerPtr<Channel>();
        auto* channel = owned.Get();
        auto& provider = panel.GetProvider();
        Agent::Settings settings;
        settings.toolDefinitions = Agent::Tools::Definitions();
        Check(provider.Attach(std::move(owned), settings, {}), "attachment UI uses an isolated protocol channel, no model session");
        Ready(provider, *channel, false, Json::array(), Json::array(), true, Json::array({
            {{"model", "test-model"}, {"displayName", "Test model A"}, {"isDefault", true}},
            {{"model", "test-b"}, {"displayName", "Test model B (long name)"}},
            {{"model", "test-c"}, {"displayName", "Test model C"}},
            {{"model", "test-d"}, {"displayName", "Test model D"}}}));
        const auto commandsBefore = editor.GetCommands().GetRevision();
        ImRect sources[3];
        Bool retargetSource = false;
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1000.0f, 800.0f);
        const auto frame = [&]() {
            io.DisplaySize = ImVec2(1000.0f, 800.0f);
            io.DeltaTime = 0.016f;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(260.0f, 400.0f));
            ImGui::Begin("attachment sources", nullptr, ImGuiWindowFlags_NoSavedSettings);
            for (Int32 i = 0; i < 3; ++i)
            {
                Widget::Button(i == 0 ? "object source" : i == 1 ? "component source" : "asset source");
                sources[i.Value] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                if (Widget::BeginDragSource())
                {
                    if (i == 0)
                    {
                        Widget::SetDragValue(Widget::DragKind::HierarchyObject, objectId);
                    }
                    else if (i == 1)
                    {
                        Widget::ComponentDragPayload payload;
                        payload.object = objectId;
                        payload.instance = retargetSource ? component + 1 : component;
                        Widget::SetComponentDragPayload(payload);
                    }
                    else
                    {
                        Widget::SetAssetDragPayload(assetId, {}, "Tile.jmat");
                    }
                    Widget::Text("source");
                    Widget::EndDragSource();
                }
            }
            ImGui::End();
            ImGui::SetNextWindowPos(ImVec2(280.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(520.0f, 780.0f));
            ImGui::Begin("attachment target", nullptr, ImGuiWindowFlags_NoSavedSettings);
            auto* context = ImGui::GetCurrentContext();
            const Int32 colorsBefore = context->ColorStack.Size;
            const Int32 varsBefore = context->StyleVarStack.Size;
            const Int32 fontsBefore = context->FontStack.Size;
            const ImVec4 textBefore = ImGui::GetStyle().Colors[ImGuiCol_Text];
            const ImVec2 paddingBefore = ImGui::GetStyle().WindowPadding;
            const Float fontBefore = ImGui::GetFontSize();
            panel.OnDraw();
            Check(context->ColorStack.Size == colorsBefore && context->StyleVarStack.Size == varsBefore
                && context->FontStack.Size == fontsBefore, "AI controls restore every global style and font stack");
            Check(std::memcmp(&textBefore, &ImGui::GetStyle().Colors[ImGuiCol_Text], sizeof(textBefore)) == 0
                && std::memcmp(&paddingBefore, &ImGui::GetStyle().WindowPadding, sizeof(paddingBefore)) == 0
                && fontBefore == ImGui::GetFontSize(), "AI button colors, padding, and font size never escape the widget");
            ImGui::End();
            ImGui::Render();
        };
        frame();
        frame();
        const auto locate = [&](const char* label) {
            auto* window = ImGui::FindWindowByName("attachment target");
            for (auto* candidate : ImGui::GetCurrentContext()->Windows)
            {
                if (std::strcmp(label, "##agent.settings_toggle") != 0
                    && std::strcmp(label, "##agent.new") != 0
                    && candidate->ParentWindow == window && std::strstr(candidate->Name, "##agent.composer") != nullptr)
                {
                    window = candidate;
                    break;
                }
            }
            Widget::ItemLocator::Watch(window->GetID(label));
            frame();
            Widget::ItemLocator::ItemRect rect;
            Check(Widget::ItemLocator::Take(rect), "attachment composer control is visible");
            return ImRect(ImVec2(rect.minX.Value, rect.minY.Value), ImVec2(rect.maxX.Value, rect.maxY.Value));
        };
        const auto drag = [&](Int32 source) {
            const ImVec2 start = sources[source.Value].GetCenter();
            io.AddMousePosEvent(start.x, start.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            for (Int32 step = 1; step <= 5; ++step)
            {
                io.AddMousePosEvent(start.x + step.Value * 4.0f, start.y);
                frame();
            }
            retargetSource = source == 1;
            const ImRect drop = locate("##agent.input");
            io.AddMousePosEvent(drop.GetCenter().x, drop.GetCenter().y);
            frame();
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            frame();
            retargetSource = false;
        };
        drag(0);
        drag(1);
        drag(2);
        drag(0);
        const auto renderPanel = [&](const char* suffix) {
            auto* main = ImGui::FindWindowByName("###MainDock");
            if (main != nullptr)
            {
                Widget::ItemLocator::Watch(ImHashStr("#TAB", 4, main->ID));
                Check(editor.Tick(0.016f), "root tabs draw before selecting Main");
                Widget::ItemLocator::ItemRect tab;
                if (Widget::ItemLocator::Take(tab))
                {
                    io.AddMousePosEvent((tab.minX.Value + tab.maxX.Value) * 0.5f, (tab.minY.Value + tab.maxY.Value) * 0.5f);
                    Check(editor.Tick(0.016f), "mouse reaches Main tab");
                    io.AddMouseButtonEvent(0, true);
                    Check(editor.Tick(0.016f), "Main tab is pressed");
                    io.AddMouseButtonEvent(0, false);
                    Check(editor.Tick(0.016f), "Main tab is released");
                }
            }
            panel.RequestFocus();
            for (UInt32 i = 0; i < 8; ++i)
            {
                Check(editor.Tick(0.016f), "engine renders its real attachment panel");
            }
            SaveAttachmentImage(editor, suffix);
            frame();
            frame();
        };
        renderPanel("pending");
        // Compare the actual editor surface at the same panel sizes as the approved draft.
        auto* agentWindow = ImGui::FindWindowByName("###Agent");
        Check(agentWindow != nullptr, "the engine owns the AI window");
        const ImGuiID originalDock = agentWindow->DockId;
        const auto captureWidth = [&](Float width, const char* suffix) {
            ImGui::SetWindowDock(agentWindow, 0, ImGuiCond_Always);
            ImGui::SetWindowPos(agentWindow, ImVec2(20.0f, 20.0f), ImGuiCond_Always);
            ImGui::SetWindowSize(agentWindow, ImVec2(width.Value, 720.0f), ImGuiCond_Always);
            for (UInt32 i = 0; i < 8; ++i)
            {
                Check(editor.Tick(0.016f), "the actual AI panel settles after a width change");
            }
            SaveAttachmentImage(editor, suffix);
        };
        captureWidth(245.0f, "width245");
        captureWidth(360.0f, "width360");
        captureWidth(440.0f, "width440");
        ImGui::SetWindowDock(agentWindow, originalDock, ImGuiCond_Always);
        frame();
        frame();
        const auto click = [&](const ImRect& rect) {
            io.AddMousePosEvent(rect.GetCenter().x, rect.GetCenter().y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        const auto pendingReference = [&]() {
            ImGuiWindow* child = nullptr;
            for (auto* window : ImGui::GetCurrentContext()->Windows)
            {
                if (window->RootWindow == ImGui::FindWindowByName("attachment target")
                    && std::strstr(window->Name, "##agent.attachments") != nullptr)
                {
                    child = window;
                }
            }
            Check(child != nullptr, "the composer attachment list exists");
            const Int32 first = 0;
            Widget::ItemLocator::Watch(ImHashStr("##agent.remove", 0,
                ImHashStr("##reference", 0, ImHashData(&first.Value, sizeof(first.Value), ImHashStr("##attachment.rows", 0, child->ID)))));
            frame();
            Widget::ItemLocator::ItemRect remove;
            Check(Widget::ItemLocator::Take(remove), "an attachment remains visible and removable in the composer");
            return ImRect(ImVec2(remove.minX.Value, remove.minY.Value), ImVec2(remove.maxX.Value, remove.maxY.Value));
        };
        const Float priorFontScale = ImGui::GetStyle().FontScaleMain;
        ImGui::GetStyle().FontScaleMain = 1.5f;
        const auto scaledSend = locate("##agent.send");
        const Float expectedButton = 28.0f * 1.5f * ImGui::GetStyle().FontScaleDpi;
        Check(scaledSend.GetWidth() > expectedButton - 1.0f && scaledSend.GetWidth() < expectedButton + 1.0f,
            "chat controls apply the requested scale once instead of compounding font sizes");
        for (auto* candidate : ImGui::GetCurrentContext()->Windows)
        {
            if (candidate->ParentWindow == ImGui::FindWindowByName("attachment target")
                && std::strstr(candidate->Name, "##agent.composer") != nullptr)
            {
                const Float expectedFont = ImGui::GetRoundedFontSize(13.0f * 1.5f * ImGui::GetStyle().FontScaleDpi);
                const String fontDiagnostic = "chat body must apply scaling once: expected " + String(std::to_string(expectedFont.Value))
                    + ", actual " + String(std::to_string(candidate->FontRefSize));
                Check(candidate->FontRefSize > expectedFont - 0.1f && candidate->FontRefSize < expectedFont + 0.1f,
                    fontDiagnostic.c_str());
            }
        }
        ImGui::GetStyle().FontScaleMain = priorFontScale.Value;
        frame();
        frame();
        click(locate("##agent.settings_toggle"));
        captureWidth(360.0f, "settings");
        click(locate("##agent.settings_toggle"));
        ImGui::SetWindowDock(agentWindow, originalDock, ImGuiCond_Always);
        frame();
        click(locate("##agent.input"));
        io.AddInputCharactersUTF8("오브젝트 포커스 모드는 어떻게 들어가? 처음 사용하는 사람도 따라 할 수 있도록 선택 방법과 진입 순서를 설명해 주세요. LongUnbrokenIdentifier0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ가 포함돼도 문장이 입력창 폭에 맞춰 보여야 합니다.");
        frame();
        captureWidth(245.0f, "wrapped");
        ImGuiWindow* composerWindow = nullptr;
        for (auto* candidate : ImGui::GetCurrentContext()->Windows)
        {
            if (candidate->ParentWindow == agentWindow && std::strstr(candidate->Name, "##agent.composer") != nullptr)
            {
                composerWindow = candidate;
            }
        }
        Check(composerWindow != nullptr, "composer is present for hover geometry checks");
        Widget::ItemLocator::Watch(composerWindow->GetID("##agent.send"));
        Check(editor.Tick(0.016f), "send button geometry is measured in the actual editor");
        Widget::ItemLocator::ItemRect sendRect;
        Check(Widget::ItemLocator::Take(sendRect), "the send button reports its actual rectangle");
        const Float bottomGap = composerWindow->InnerRect.Max.y - sendRect.maxY;
        const Float paddingScale = ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi;
        Check(bottomGap >= 4.0f * paddingScale && bottomGap <= 8.0f * paddingScale, "composer bottom padding is six pixels rather than an unused reserved block");
        const Float priorHoverDelay = ImGui::GetStyle().HoverDelayNormal;
        const Float priorStationaryDelay = ImGui::GetStyle().HoverStationaryDelay;
        ImGui::GetStyle().HoverDelayNormal = 0.0f;
        ImGui::GetStyle().HoverStationaryDelay = 0.0f;
        io.AddMousePosEvent((sendRect.minX.Value + sendRect.maxX.Value) * 0.5f, (sendRect.minY.Value + sendRect.maxY.Value) * 0.5f);
        Check(editor.Tick(0.016f) && editor.Tick(0.016f), "hover tooltip settles with the same editor frame path");
        ImGuiWindow* tooltip = nullptr;
        for (auto* candidate : ImGui::GetCurrentContext()->Windows)
        {
            if (candidate->Active && (candidate->Flags & ImGuiWindowFlags_Tooltip) != 0)
            {
                tooltip = candidate;
            }
        }
        Check(tooltip != nullptr, "send hover actually opens its tooltip");
        const Float expectedTooltipFont = ImGui::GetRoundedFontSize(13.0f * ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi);
        Check(tooltip->FontRefSize == expectedTooltipFont && tooltip->WindowPadding.x == ImGui::GetStyle().WindowPadding.x,
            "tooltip uses the body font and normal window padding instead of icon or composer settings");
        Bool readableText = false;
        for (const auto& vertex : tooltip->DrawList->VtxBuffer)
        {
            readableText = readableText || vertex.col == ImGui::GetColorU32(ImGuiCol_Text);
        }
        Check(readableText, "tooltip text uses the normal foreground instead of the dark send-icon color");
        Check(editor.Tick(0.016f) && editor.Tick(0.016f), "hover pixels reach the rendered back buffer");
        SaveAttachmentImage(editor, "send_hover");
        ImGui::GetStyle().HoverDelayNormal = priorHoverDelay.Value;
        ImGui::GetStyle().HoverStationaryDelay = priorStationaryDelay.Value;
        io.AddMousePosEvent(-100.0f, -100.0f);
        Widget::ItemLocator::Watch(composerWindow->GetID("##agent.model"));
        Check(editor.Tick(0.016f), "model selector reports its actual rectangle");
        Widget::ItemLocator::ItemRect modelRect;
        Check(Widget::ItemLocator::Take(modelRect), "model selector remains reachable beside attachment and send");
        Check(modelRect.maxX <= sendRect.minX, "model selector never covers the send button");
        io.AddMousePosEvent((modelRect.minX.Value + modelRect.maxX.Value) * 0.5f, (modelRect.minY.Value + modelRect.maxY.Value) * 0.5f);
        Check(editor.Tick(0.016f), "model hover settles");
        io.AddMouseButtonEvent(0, true);
        Check(editor.Tick(0.016f), "model popup opens through normal input");
        io.AddMouseButtonEvent(0, false);
        for (UInt32 i = 0; i < 5; ++i)
        {
            Check(editor.Tick(0.016f), "model popup geometry and pixels settle");
        }
        ImGuiWindow* modelPopup = nullptr;
        for (auto* candidate : ImGui::GetCurrentContext()->Windows)
        {
            if (candidate->Active && std::strncmp(candidate->Name, "##Combo_", 8) == 0)
            {
                modelPopup = candidate;
            }
        }
        Check(modelPopup != nullptr && modelPopup->FontRefSize == expectedTooltipFont,
            "model search and list use the normal body font");
        Check(modelPopup->Size.x >= modelRect.maxX - modelRect.minX
            && modelPopup->WindowPadding.y == ImGui::GetStyle().WindowPadding.y,
            "model popup is at least as wide as its trigger and uses normal popup padding");
        SaveAttachmentImage(editor, "model_popup");
        io.AddInputCharactersUTF8("testl");
        for (UInt32 i = 0; i < 5; ++i)
        {
            Check(editor.Tick(0.016f), "empty model search and pixels settle");
        }
        Check(modelPopup->ContentSize.x <= modelPopup->WorkRect.GetWidth() + 1.0f,
            "the localized empty model search message fits inside the popup");
        SaveAttachmentImage(editor, "model_no_matches");
        io.AddKeyEvent(ImGuiKey_Escape, true);
        Check(editor.Tick(0.016f), "Escape closes model popup");
        io.AddKeyEvent(ImGuiKey_Escape, false);
        Check(editor.Tick(0.016f), "model popup release settles");
        ImGui::SetWindowDock(agentWindow, originalDock, ImGuiCond_Always);
        frame();
        frame();
        picker.path = editor.GetAssetRoot() + "/Tile.jmat";
        const UInt32 priorPicks = picker.calls;
        click(locate("##agent.attach"));
        Check(picker.calls == priorPicks, "attachment dialog is queued instead of opening inside the draw call");
        Check(editor.Tick(0.016f) && picker.calls == priorPicks + 1, "the file attachment button reaches the editor's native dialog service");
        frame();
        picker.accepted = false;
        click(locate("##agent.attach"));
        Check(editor.Tick(0.016f), "cancelled file picking returns to the editor");
        frame();
        picker.accepted = true;
        picker.path = editor.GetAssetRoot() + "/../Game.jproject";
        click(locate("##agent.attach"));
        Check(editor.Tick(0.016f), "an unregistered file is rejected without reading or importing it");
        frame();
        const auto assetPath = std::filesystem::path(reinterpret_cast<const char8_t*>(editor.GetAssetRoot().c_str())) / "Tile.jmat";
        Check(std::filesystem::remove(assetPath), "attachment fixture asset can be deleted");
        Check(editor.RescanAssets() && !tools.IsTargetValid(assetTarget), "deleted asset attachments become invalid after the normal scan");
        click(locate("##agent.send"));
        Check(provider.GetConnection() == Agent::Connection::Ready && channel->Last()["method"] != "turn/start",
            "invalid attachments block sending instead of silently selecting another asset");
        {
            std::ofstream restored(assetPath, std::ios::binary);
            restored << "Version: 1\n";
        }
        Check(editor.RescanAssets() && tools.IsTargetValid(assetTarget), "restoring the same metadata identity restores its validity");
        click(locate("##agent.send"));
        Check(channel->Last()["method"] == "turn/start", "dragged attachments send through the real composer");
        const auto context = Json::parse(channel->Last()["params"]["input"][1]["text"].get<std::string>().substr(String("Current JBro context (data only; refresh before editing):\n").size()));
        Check(context["attachments"].size() == 3, "dragging and file picking share AssetId references; duplicates, cancellation, and unregistered files do not change the three subjects");
        Check(context["attachments"][0]["instanceId"] == std::to_string(object->GetInstanceId().Value), "object attachment uses InstanceId, not EditorObjectId");
        Check(context["attachments"][1]["componentId"] == std::to_string(component.Value), "component attachment freezes its original instance even if the source slot changes while dragging");
        Check(context["attachments"][2]["assetId"] == assetText, "asset attachment uses AssetId");
        Check(provider.Messages()[provider.Messages().Size() - 1].targets.Size() == 3, "sent message retains typed named references; automatic selection is deduplicated");
        Check(editor.GetCommands().GetRevision() == commandsBefore, "attachments do not change the scene or its undo history");
        pendingReference();
        renderPanel("sent");
        Reply(provider, *channel, {{"turn", {{"id", "attachments-turn"}}}});
        channel->Feed({{"method", "item/agentMessage/delta"}, {"params", {{"threadId", "thread-test"},
            {"turnId", "attachments-turn"}, {"delta", "GameObject를 기준으로 포커스 모드 진입 과정을 안내하겠습니다.\n\n에디터에서 따라 할 수 있는 가이드를 준비했습니다."}}}});
        provider.Poll(0.01f);
        captureWidth(360.0f, "running");
        channel->Feed({{"method", "turn/completed"}, {"params", {{"threadId", "thread-test"}, {"turn", {{"id", "attachments-turn"}, {"status", "completed"}}}}}});
        provider.Poll(0.01f);
        pendingReference();
        const String previewGuide = "Id: preview\nTitle:\n  Key: test.preview\n  String: 오브젝트 포커스 모드 진입\n  Loc: ko-KR\nSteps:\n"
            "  - Do: object.focus\n    Object: " + String(std::to_string(object->GetInstanceId().Value))
            + "\n    Title:\n      Key: test.preview.step\n      String: 포커스 모드 진입\n      Loc: ko-KR\n"
            "    Body:\n      Key: test.preview.body\n      String: 캔버스에서 오브젝트를 두 번 클릭합니다.\n      Loc: ko-KR\n";
        const auto previewStarted = tools.Execute("jbro_guide_start", Json({{"scope", scope}, {"yaml", previewGuide}}).dump());
        Check(previewStarted.success, previewStarted.text.c_str());
        Check(editor.IsAgentGuideReady() && Json::parse(previewStarted.text)["started"] == false,
            "AI guide preparation is visible without running it");
        captureWidth(360.0f, "guide_ready");
        editor.ResumeAgentGuide();
        editor.SetAgentGuidePaused(true);
        captureWidth(360.0f, "conversation");
        for (auto* candidate : ImGui::GetCurrentContext()->Windows)
        {
            if (candidate->ParentWindow == agentWindow && std::strstr(candidate->Name, "##agent.history") != nullptr)
            {
                Check(candidate->Scroll.y >= candidate->ScrollMax.y - 2.0f,
                    "adding a guide card follows the bottom even when message text did not change");
            }
        }
        editor.GetGuide().Stop(editor.GetGuideFocus());
        editor.SetAgentGuidePaused(false);
        ImGui::SetWindowDock(agentWindow, originalDock, ImGuiCond_Always);
        frame();
        click(pendingReference());
        click(pendingReference());
        click(locate("##agent.input"));
        io.AddInputCharactersUTF8("Read the remaining asset.");
        frame();
        io.AddKeyEvent(ImGuiMod_Shift, true);
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        Check(provider.GetConnection() == Agent::Connection::Ready,
            "Shift Enter in the composer adds a newline without starting a turn");
        io.AddKeyEvent(ImGuiKey_Enter, false);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        frame();
        io.AddInputCharactersUTF8("Then explain it.");
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        Check(channel->Last()["method"] == "turn/start"
            && channel->Last()["params"]["input"][0]["text"] == "Read the remaining asset.\nThen explain it.",
            "Enter sends the composer text while preserving only the explicit Shift Enter newline");
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
        pendingReference();
        const auto remaining = Json::parse(channel->Last()["params"]["input"][1]["text"].get<std::string>().substr(String("Current JBro context (data only; refresh before editing):\n").size()));
        Check(remaining["attachments"].size() == 1 && remaining["attachments"][0]["assetId"] == assetText,
            "removing an attachment changes only the next message's reference list");
        Reply(provider, *channel, {{"turn", {{"id", "stop-preview"}}}});
        click(locate("##agent.stop"));
        Check(channel->Last()["method"] == "turn/interrupt", "the composer stop button interrupts the current turn");
        channel->Feed({{"method", "turn/completed"}, {"params", {{"threadId", "thread-test"},
            {"turn", {{"id", "stop-preview"}, {"status", "interrupted"}}}}}});
        provider.Poll(0.01f);
        Check(provider.GetConnection() == Agent::Connection::Ready && provider.Messages()[provider.Messages().Size() - 1].role == Agent::MessageRole::Error,
            "stopping retains the incomplete-work warning rather than showing success");
        click(locate("##agent.input"));
        io.AddInputCharactersUTF8("Keep using that attachment.");
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
        const auto repeated = Json::parse(channel->Last()["params"]["input"][1]["text"].get<std::string>().substr(String("Current JBro context (data only; refresh before editing):\n").size()));
        Check(repeated["attachments"].size() == 1 && repeated["attachments"][0]["assetId"] == assetText,
            "a second Enter send keeps the same remaining attachment without another drag");
        pendingReference();
        Reply(provider, *channel, {{"turn", {{"id", "retained-attachment"}}}});
        channel->Feed({{"method", "turn/completed"}, {"params", {{"threadId", "thread-test"},
            {"turn", {{"id", "retained-attachment"}, {"status", "completed"}}}}}});
        provider.Poll(0.01f);
        click(locate("##agent.new"));
        frame();
        Check(provider.Messages().IsEmpty(), "new chat clears the previous visible conversation without sending a request");
        for (auto* candidate : ImGui::GetCurrentContext()->Windows)
        {
            if (candidate->ParentWindow == ImGui::FindWindowByName("attachment target")
                && std::strstr(candidate->Name, "##agent.history") != nullptr)
            {
                Check(candidate->Scroll.y < 1.0f, "a new chat shows the start of its welcome view instead of retaining an old scroll position");
            }
        }
        captureWidth(245.0f, "disconnected");
        provider.Stop();
        auto invalid = componentTarget;
        ++invalid.revision;
        Check(!tools.IsTargetValid(invalid), "stale canvas generations cannot keep an attachment valid");
        Check(tools.IsTargetValid(componentTarget) && tools.IsTargetValid(assetTarget), "live attachment targets remain valid");
    }
    void EngineTools()
    {
        Files files;
        EditorApplication editor;
        EditorApplicationConfig config;
        AttachmentPicker picker;
        config.fileDialog = &AttachmentPicker::Answer;
        config.fileDialogUser = &picker;
        config.windowVisible = false;
        config.windowHeight = 820;
        Check(editor.Initialize(config), "agent integration editor initializes");
        ProjectFileError error;
        Check(editor.OpenProjectFile(files.Path("Game.jproject").c_str(), error), "agent test project opens");
        Check(editor.EnableEditorUi({64, 48}), "agent test UI initializes");
        Agent::Tools tools(editor);
        const auto context = Json::parse(tools.Context());
        const String scope = context["scope"].get<std::string>();
        const auto execute = [&](const char* name, Json args) { return tools.Execute(name, args.dump()); };
        Check(!execute("jbro_object_create", {{"parent", ""}}).success, "missing scope never permits mutations");
        auto made = execute("jbro_object_create", {{"scope", scope}, {"parent", ""}});
        Check(made.success && editor.GetCanvas()->GetObjectCount() == 1, "AI creates through editor commands");
        String object = Json::parse(made.text)["instanceId"].get<std::string>();
        Agent::Target attached;
        Check(tools.MakeObjectTarget(editor.GetObjectIds().Track(editor.GetSelectedObject()), attached), "editor IDs convert into object attachment identities");
        Check(tools.IsTargetValid(attached), "new object attachments are live");
        Check(editor.GetCommands().Undo() && editor.GetCanvas()->GetObjectCount() == 0, "AI object creation can be undone");
        Check(!tools.IsTargetValid(attached), "deleted object attachments are invalid");
        Check(editor.GetCommands().Redo() && editor.GetCanvas()->GetObjectCount() == 1, "AI object creation can be redone");
        Check(!tools.IsTargetValid(attached), "recreated objects never replace an old attachment");
        Check(!execute("jbro_object_read", {{"scope", scope}, {"object", object}}).success, "old InstanceId cannot silently resolve to a recreated object");
        editor.GetCanvas()->ForEachObject([&](Object::GameObject& restored) {
            object = std::to_string(restored.GetInstanceId().Value);
            editor.SetSelectedObject(&restored);
        });
        const auto inspected = execute("jbro_object_read", {{"scope", scope}, {"object", object}});
        Check(inspected.success, "fresh InstanceId resolves the restored object");
        const auto original = Json::parse(inspected.text);
        const String component = original["components"][0]["instanceId"].get<std::string>();
        String expected;
        for (const auto& field : original["components"][0]["fields"])
        {
            if (field["name"] == "rotation")
            {
                expected = field["valueYaml"].get<std::string>();
            }
        }
        Check(!expected.empty(), "live reflection exposes an editable field");
        const Json edit{{"scope", scope}, {"object", object}, {"component", component}, {"field", "rotation"}, {"expected", expected}, {"value", "15"}};
        Check(execute("jbro_field_set", edit).success, "AI property edit uses reflection and commands");
        Check(!execute("jbro_field_set", edit).success, "stale expected field value cannot overwrite a newer edit");
        Check(editor.GetCommands().Undo(), "AI field edit participates in normal undo");
        Check(execute("jbro_component_add", {{"scope", scope}, {"object", object}, {"type", "Collider2D"}}).success, "AI attaches a live component type");
        const auto read = execute("jbro_script_read", {{"scope", scope}, {"path", "Probe.cpp"}});
        Check(read.success, "source read returns a document and write token");
        const String token = Json::parse(read.text)["token"].get<std::string>();
        Check(execute("jbro_script_write", {{"scope", scope}, {"path", "Probe.cpp"}, {"token", token}, {"text", "// AI edit\n"}}).success, "clean source edits save through the code document");
        Check(!execute("jbro_script_write", {{"scope", scope}, {"path", "Probe.cpp"}, {"token", token}, {"text", "// repeated\n"}}).success, "source write tokens cannot be replayed");
        Array<EditorPanel*> documents;
        editor.FindPanels(CodeEditorPanel::TypeName, documents);
        auto* document = static_cast<CodeEditorPanel*>(documents[0]);
        document->Undo();
        document->OnUpdate(0.01f);
        Check(document->GetBufferText() == "// original\n", "AI source replacement preserves document undo");
        const auto dirty = execute("jbro_script_read", {{"scope", scope}, {"path", "Probe.cpp"}});
        Check(!execute("jbro_script_write", {{"scope", scope}, {"path", "Probe.cpp"}, {"token", Json::parse(dirty.text)["token"]}, {"text", "// discard user\n"}}).success,
            "unsaved document edits are not overwritten");
        Check(!execute("jbro_script_read", {{"scope", scope}, {"path", "../../outside.cpp"}}).success, "script paths cannot escape the script root");
        Check(document->Save(), "dirty fixture is explicitly saved by the test user");
        const auto versioned = execute("jbro_script_read", {{"scope", scope}, {"path", "Probe.cpp"}});
        document->GetBuffer().SetText("// transient user change\n");
        document->GetBuffer().SetText("// original\n");
        Check(!execute("jbro_script_write", {{"scope", scope}, {"path", "Probe.cpp"}, {"token", Json::parse(versioned.text)["token"]}, {"text", "// stale version\n"}}).success,
            "returning to equal text does not revive a token from an older document revision");
        document->OnUpdate(0.01f);
        JunctionBoundary(editor, files, tools, scope);
        files.Write("Contents/Scripts/NotSource.txt", "not a script\n");
        String nulPath = "NotSource.txt";
        nulPath.push_back('\0');
        nulPath += ".cpp";
        Check(!execute("jbro_script_read", {{"scope", scope}, {"path", nulPath}}).success, "NUL cannot bypass the script extension check at a C-string boundary");
        String nulReference = "jbro-guide/SKILL.md";
        nulReference.push_back('\0');
        nulReference += ".h";
        Check(!execute("jbro_script_reference", {{"source", "skill"}, {"path", nulReference}}).success, "reference paths reject NUL aliases");
        Check(!execute("jbro_guide_start", {{"scope", scope}, {"yaml", "Id: invalid\nSteps: []\n"}}).success, "invalid generated guides are rejected");
        auto* panel = static_cast<AgentPanel*>(editor.CreatePanel(AgentPanel::TypeName));
        Check(panel != nullptr, "AI panel is created by the panel registry");
        for (UInt32 i = 0; i < 3; ++i)
        {
            Check(editor.Tick(0.016f), "real editor draws the AI panel using common widgets");
        }
        AttachmentInput(editor, *panel, tools, picker);
        GuideResume(editor, tools, scope, object);
        editor.CloseProject();
        Check(!execute("jbro_object_delete", {{"scope", scope}, {"object", object}}).success, "closed-project references are invalid");
        files.Write("NewGame.jproject", "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nRootPath: .\nAssetDirectory: Assets\nScriptSourceDirectory: NewSource\n");
        Check(editor.OpenProjectFile(files.Path("NewGame.jproject").c_str(), error), "empty script project opens");
        Check(!std::filesystem::exists(files.root / "NewSource/Scripts"), "first script fixture has no source directory");
        const String newScope = Json::parse(tools.Context())["scope"].get<std::string>();
        Check(execute("jbro_script_create", {{"scope", newScope}, {"folder", ""}, {"name", "FirstScript"}}).success,
            "the engine tool creates a first C++ script in a previously absent source directory");
        Check(std::filesystem::exists(files.root / "NewSource/Scripts/FirstScript.h")
            && std::filesystem::exists(files.root / "NewSource/Scripts/FirstScript.cpp"), "first script creation emits the actual C++ source pair");
        editor.CloseProject();
        editor.Shutdown();
    }
}
JBro::Int32 RunAgentTests()
{
    Protocol();
    RestoredAttachments();
    AuthenticationAndFailure();
    IsolationPaginationAndRestoration();
    EngineTools();
    return 0;
}
