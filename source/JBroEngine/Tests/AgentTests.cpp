#include <JBro/Editor/Agent/Provider.h>
#include <JBro/Editor/Agent/Tools.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorPanelRegistry.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Runtime/GameObject.h>
#include "../Modules/JBroEditor/Source/Panel/CodeEditorPanel.h"
#include "../Modules/JBroEditor/Source/Panel/AgentPanel.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <stdexcept>

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
    void Ready(Agent::CodexProvider& provider, Channel& channel)
    {
        channel.Feed({{"id", 1}, {"result", Json::object()}});
        provider.Poll(0.01f);
        Check(channel.Last()["method"] == "account/read", "initialize must precede account access");
        channel.Feed({{"id", 2}, {"result", {{"account", {{"type", "chatgpt"}}}}}});
        provider.Poll(0.01f);
        Check(channel.Last()["method"] == "model/list", "signed-in account queries its model catalog");
        channel.Feed({{"id", 3}, {"result", {{"data", Json::array({{{"model", "test-model"}, {"displayName", "Test"}, {"isDefault", true}}})}}}});
        provider.Poll(0.01f);
        Check(channel.Last()["method"] == "thread/start" && channel.Last()["params"]["model"] == "test-model", "thread explicitly uses the account catalog default, not an incompatible CLI default");
        Check(channel.Last()["params"]["sandbox"] == "read-only", "built-in filesystem stays read-only");
        Check(channel.Last()["params"]["dynamicTools"].is_array(), "thread receives dynamic tool definitions");
        channel.Feed({{"id", 4}, {"result", {{"thread", {{"id", "thread-test"}}}}}});
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
        attached.project = editor.GetProjectFilePath();
        attached.canvas = editor.GetCanvasPath();
        attached.instance = editor.GetSelectedObject()->GetInstanceId();
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
        auto* panel = editor.CreatePanel(AgentPanel::TypeName);
        Check(panel != nullptr, "AI panel is created by the panel registry");
        for (UInt32 i = 0; i < 3; ++i)
        {
            Check(editor.Tick(0.016f), "real editor draws the AI panel using common widgets");
        }
        editor.CloseProject();
        Check(!execute("jbro_object_delete", {{"scope", scope}, {"object", object}}).success, "closed-project references are invalid");
        editor.Shutdown();
    }
}
JBro::Int32 RunAgentTests()
{
    Protocol();
    AuthenticationAndFailure();
    EngineTools();
    return 0;
}
