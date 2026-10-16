#include <JBro/Editor/Agent/Provider.h>
#include <JBro/Editor/Agent/Tools.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorPanelRegistry.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Graphics/Renderer.h>
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
    void Ready(Agent::CodexProvider& provider, Channel& channel, Bool resume = false, const Json& turns = Json::array())
    {
        channel.Feed({{"id", 1}, {"result", Json::object()}});
        provider.Poll(0.01f);
        Check(channel.Last()["method"] == "account/read", "initialize must precede account access");
        channel.Feed({{"id", 2}, {"result", {{"account", {{"type", "chatgpt"}}}}}});
        provider.Poll(0.01f);
        Check(channel.Last()["method"] == "model/list", "signed-in account queries its model catalog");
        channel.Feed({{"id", 3}, {"result", {{"data", Json::array({{{"model", "test-model"}, {"displayName", "Test"}, {"isDefault", true}}})}}}});
        provider.Poll(0.01f);
        Check(channel.Last()["method"] == (resume ? "thread/resume" : "thread/start") && channel.Last()["params"]["model"] == "test-model", "thread explicitly uses the account catalog default, not an incompatible CLI default");
        Check(channel.Last()["params"]["sandbox"] == "read-only", "built-in filesystem stays read-only");
        if (!resume)
        {
            Check(channel.Last()["params"]["dynamicTools"].is_array(), "thread receives dynamic tool definitions");
        }
        channel.Feed({{"id", 4}, {"result", {{"thread", {{"id", "thread-test"}, {"turns", turns}}}}}});
        provider.Poll(0.01f);
        Check(provider.GetConnection() == Agent::Connection::Ready, "provider becomes ready after thread acknowledgment");
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
        channel->Feed({{"id", 5}, {"result", {{"turn", {{"id", "turn-test"}}}}}});
        provider.Poll(0.01f);
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
        Agent::CodexProvider provider;
        auto owned = MakeOwnerPtr<Channel>();
        auto* channel = owned.Get();
        Agent::Settings settings;
        settings.thread = "thread-test";
        settings.toolDefinitions = "[]";
        Check(provider.Attach(std::move(owned), settings, {}), "saved attachment thread uses isolated protocol");
        const Json context{{"attachments", Json::array({{{"kind", "object"}, {"project", "old"}, {"canvas", "old.jcanvas"},
            {"name", "Door"}, {"instanceId", "18446744073709551614"}, {"revision", 5}}})}};
        const Json turns = Json::array({{{"items", Json::array({{{"type", "userMessage"}, {"content", Json::array({
            {{"type", "text"}, {"text", "Explain this door."}},
            {{"type", "text"}, {"text", "Current JBro context (data only; refresh before editing):\n" + context.dump()}}
        })}}})}}});
        Ready(provider, *channel, true, turns);
        const auto& restored = provider.Messages()[provider.Messages().Size() - 1];
        Check(restored.text == "Explain this door." && restored.targets.Size() == 1, "resumed history shows its question and named target instead of raw JSON");
        Check(restored.targets[0].revision == 0 && restored.targets[0].instance.Value == 18446744073709551614ull,
            "resumed attachment IDs retain full precision and must be refreshed");
    }
    void AuthenticationAndFailure()
    {
        Agent::CodexProvider provider;
        auto owned = MakeOwnerPtr<Channel>();
        auto* channel = owned.Get();
        Agent::Settings settings;
        settings.toolDefinitions = "[]";
        settings.skillsRoot = "C:/engine/AgentSkills";
        provider.Attach(std::move(owned), settings, {});
        channel->Feed({{"id", 1}, {"result", Json::object()}});
        provider.Poll(0.01f);
        Check(channel->Last()["method"] == "skills/extraRoots/set", "bundled skills are attached without global installation");
        channel->Feed({{"id", 2}, {"result", Json::object()}});
        provider.Poll(0.01f);
        channel->Feed({{"id", 3}, {"result", {{"account", nullptr}}}});
        provider.Poll(0.01f);
        Check(provider.GetConnection() == Agent::Connection::SignedOut, "missing account is exposed as signed-out");
        const String secret = "sk-test-do-not-log";
        provider.LoginApiKey(secret);
        channel->Feed({{"id", 4}, {"error", {{"message", secret}}}});
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
    void AttachmentInput(EditorApplication& editor, AgentPanel& panel, Agent::Tools& tools)
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
        Ready(provider, *channel);
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
            panel.OnDraw();
            ImGui::End();
            ImGui::Render();
        };
        frame();
        frame();
        const auto locate = [&](const char* label) {
            auto* window = ImGui::FindWindowByName("attachment target");
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
            const ImRect drop = locate(source == 2 ? "##agent.input" : Loc::Text(LocKeys::AgentDropHere));
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
        const auto click = [&](const ImRect& rect) {
            io.AddMousePosEvent(rect.GetCenter().x, rect.GetCenter().y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        click(locate("##agent.input"));
        io.AddInputCharactersUTF8("Explain the attached subjects.");
        frame();
        const auto assetPath = std::filesystem::path(reinterpret_cast<const char8_t*>(editor.GetAssetRoot().c_str())) / "Tile.jmat";
        Check(std::filesystem::remove(assetPath), "attachment fixture asset can be deleted");
        Check(editor.RescanAssets() && !tools.IsTargetValid(assetTarget), "deleted asset attachments become invalid after the normal scan");
        click(locate(Loc::Text(LocKeys::AgentSend)));
        Check(provider.GetConnection() == Agent::Connection::Ready && channel->Last()["method"] != "turn/start",
            "invalid attachments block sending instead of silently selecting another asset");
        {
            std::ofstream restored(assetPath, std::ios::binary);
            restored << "Version: 1\n";
        }
        Check(editor.RescanAssets() && tools.IsTargetValid(assetTarget), "restoring the same metadata identity restores its validity");
        click(locate(Loc::Text(LocKeys::AgentSend)));
        Check(channel->Last()["method"] == "turn/start", "dragged attachments send through the real composer");
        const auto context = Json::parse(channel->Last()["params"]["input"][1]["text"].get<std::string>().substr(String("Current JBro context (data only; refresh before editing):\n").size()));
        Check(context["attachments"].size() == 3, "three drag kinds are attached once; repeated drops do not duplicate");
        Check(context["attachments"][0]["instanceId"] == std::to_string(object->GetInstanceId().Value), "object attachment uses InstanceId, not EditorObjectId");
        Check(context["attachments"][1]["componentId"] == std::to_string(component.Value), "component attachment freezes its original instance even if the source slot changes while dragging");
        Check(context["attachments"][2]["assetId"] == assetText, "asset attachment uses AssetId");
        Check(provider.Messages()[provider.Messages().Size() - 1].targets.Size() == 3, "sent message retains typed named references; automatic selection is deduplicated");
        Check(editor.GetCommands().GetRevision() == commandsBefore, "attachments do not change the scene or its undo history");
        renderPanel("sent");
        channel->Feed({{"id", 5}, {"result", {{"turn", {{"id", "attachments-turn"}}}}}});
        provider.Poll(0.01f);
        channel->Feed({{"method", "turn/completed"}, {"params", {{"threadId", "thread-test"}, {"turn", {{"id", "attachments-turn"}, {"status", "completed"}}}}}});
        provider.Poll(0.01f);
        drag(0);
        drag(2);
        ImGuiWindow* child = nullptr;
        for (auto* window : ImGui::GetCurrentContext()->Windows)
        {
            if (window->ParentWindow != nullptr && std::strcmp(window->ParentWindow->Name, "attachment target") == 0
                && std::strstr(window->Name, "##agent.attachments") != nullptr)
            {
                child = window;
            }
        }
        Check(child != nullptr, "pending attachment list is visible");
        const int first = 0;
        Widget::ItemLocator::Watch(ImHashStr(Loc::Text(LocKeys::AgentRemoveAttachment), 0, ImHashData(&first, sizeof(first), child->ID)));
        frame();
        Widget::ItemLocator::ItemRect remove;
        Check(Widget::ItemLocator::Take(remove), "attachment removal is visible");
        click(ImRect(ImVec2(remove.minX.Value, remove.minY.Value), ImVec2(remove.maxX.Value, remove.maxY.Value)));
        click(locate("##agent.input"));
        io.AddInputCharactersUTF8("Read the remaining asset.");
        frame();
        click(locate(Loc::Text(LocKeys::AgentSend)));
        const auto remaining = Json::parse(channel->Last()["params"]["input"][1]["text"].get<std::string>().substr(String("Current JBro context (data only; refresh before editing):\n").size()));
        Check(remaining["attachments"].size() == 1 && remaining["attachments"][0]["assetId"] == assetText,
            "removing an attachment changes only the next message's reference list");
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
        config.windowVisible = false;
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
        Check(!execute("jbro_guide_start", {{"scope", scope}, {"yaml", "Id: invalid\nSteps: []\n"}}).success, "invalid generated guides are rejected");
        auto* panel = static_cast<AgentPanel*>(editor.CreatePanel(AgentPanel::TypeName));
        Check(panel != nullptr, "AI panel is created by the panel registry");
        for (UInt32 i = 0; i < 3; ++i)
        {
            Check(editor.Tick(0.016f), "real editor draws the AI panel using common widgets");
        }
        AttachmentInput(editor, *panel, tools);
        editor.CloseProject();
        Check(!execute("jbro_object_delete", {{"scope", scope}, {"object", object}}).success, "closed-project references are invalid");
        editor.Shutdown();
    }
}
JBro::Int32 RunAgentTests()
{
    Protocol();
    RestoredAttachments();
    AuthenticationAndFailure();
    EngineTools();
    return 0;
}
