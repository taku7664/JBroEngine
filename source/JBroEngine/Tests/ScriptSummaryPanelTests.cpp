#include <JBro/Editor/Code/LanguageClient.h>
#include <JBro/Editor/Code/Summary.h>
#include <JBro/Editor/EditorPanelRegistry.h>
#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/Widget/ScriptOutline.h>
#include <imgui_internal.h>
#include <JBro/Platform/Platform.h>
#include "../Modules/JBroEditor/Source/Panel/ScriptSummaryPanel.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
    using namespace JBro;
    using Json = nlohmann::json;
    void Check(Bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
    void TestCompactSummaryLayout()
    {
        struct Context
        {
            ImGuiContext* previous = ImGui::GetCurrentContext();
            Context()
            {
                ImGui::CreateContext();
                auto& io = ImGui::GetIO();
                io.IniFilename = nullptr;
                io.DisplaySize = {360.0f, 700.0f};
                io.DeltaTime = 1.0f / 60.0f;
                unsigned char* pixels = nullptr;
                int width = 0, height = 0;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                io.Fonts->SetTexID(1);
            }
            ~Context()
            {
                ImGui::DestroyContext();
                ImGui::SetCurrentContext(previous);
            }
        } context;
        Array<Widget::ScriptOutlineItem> entries;
        entries.Add({"Mover", "Mover", "script", Code::SummaryKind::Type, 0});
        for (std::size_t index = 0; index < 60; ++index)
        {
            entries.Add({"Mover.value" + std::to_string(index), "value" + std::to_string(index), "Float", Code::SummaryKind::Field, 1});
        }
        String selected = "Mover.value0";
        float outlineHeight = 0.0f, outlineBottom = 0.0f, detailTop = 0.0f;
        float outlineScroll = 0.0f, detailScroll = 0.0f, outerScroll = 0.0f;
        for (int frame = 0; frame < 3; ++frame)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0.0f, 0.0f});
            ImGui::SetNextWindowSize({360.0f, 700.0f});
            ImGui::Begin("Summary layout", nullptr, ImGuiWindowFlags_NoSavedSettings);
            Widget::ScriptSummaryHeader("Mover.jscript", "C:/very/long/source/location/that/must/not/wrap/in/the/header/Mover.jscript", 60, 0);
            if (Widget::BeginScriptOutline())
            {
                outlineHeight = ImGui::GetWindowHeight();
                outlineBottom = ImGui::GetWindowPos().y + outlineHeight;
                Widget::ScriptOutline({entries.Data(), entries.Size()}, selected);
                outlineScroll = ImGui::GetScrollMaxY();
            }
            Widget::EndScriptOutline();
            if (Widget::BeginScriptDetails())
            {
                detailTop = ImGui::GetWindowPos().y;
                Widget::ScriptMemberHeader("value0", "Float", "const", Code::SummaryKind::Field);
                for (int index = 0; index < 60; ++index)
                {
                    Widget::ScriptSummaryNote("Detail content");
                }
                detailScroll = ImGui::GetScrollMaxY();
            }
            Widget::EndScriptDetails();
            outerScroll = ImGui::GetScrollMaxY();
            ImGui::End();
            ImGui::Render();
        }
        Check(outlineHeight >= 140.0f && outlineHeight <= 240.0f, "outline keeps a bounded height with many declarations");
        Check(detailTop >= outlineBottom, "details occupy an independent region below the outline");
        Check(outlineScroll > 0.0f && detailScroll > 0.0f && outerScroll == 0.0f, "outline and settings scroll independently without scrolling the whole panel");
        Check(selected == "Mover.value0", "drawing the outline preserves the selected source identity");
    }
    struct ChannelData { String input, output; Bool alive = true; };
    class MemoryChannel final : public IProcessChannel
    {
    public:
        explicit MemoryChannel(ChannelData& data) : m_data(data) {}
        Bool Write(std::string_view text) override { m_data.output += text; return true; }
        Bool Read(String& text, Bool error) override
        {
            text.clear();
            if (!error)
            {
                text.swap(m_data.input);
            }
            return !text.empty();
        }
        Bool IsRunning() const override { return m_data.alive; }
    private:
        ChannelData& m_data;
    };
    class MemoryPlatform final : public IPlatform
    {
    public:
        Bool Initialize(const JMemoryContext&) override { return true; }
        void Shutdown() override {}
        WindowHandle OpenPlatformWindow(const WindowDesc&) override { return {}; }
        void ClosePlatformWindow(WindowHandle) override {}
        SurfaceHandle CreateSurface(WindowHandle) override { return {}; }
        void PumpEvents() override {}
        JArrayView<InputEvent> GetInputEvents() const override { return {}; }
        void WaitForEvents(UInt32) override {}
        Bool ShouldClose(WindowHandle) const override { return false; }
        Bool GetWindowState(WindowHandle, WindowState&) const override { return false; }
        DynamicLibrary LoadDynamicLibrary(const char*) override { return {}; }
        void* GetSymbol(DynamicLibrary, const char*) override { return nullptr; }
        void UnloadDynamicLibrary(DynamicLibrary) override {}
        OwnerPtr<IProcessChannel> StartProcessChannel(const char*, const char*) override
        {
            return MakeOwnerPtr<MemoryChannel>(data);
        }
        ChannelData data;
    };
    Json Request(const String& frames, const char* method)
    {
        Json found;
        std::size_t offset = 0;
        while (offset < frames.size())
        {
            const auto body = frames.find("\r\n\r\n", offset);
            Check(body != String::npos, "complete frame header");
            const auto size = std::stoull(frames.substr(offset + 16, body - offset - 16));
            const Json message = Json::parse(frames.substr(body + 4, size));
            if (message.value("method", "") == method)
            {
                found = message;
            }
            offset = body + 4 + size;
        }
        Check(!found.is_null(), "expected request sent");
        return found;
    }
    void Reply(MemoryPlatform& platform, Code::LanguageClient& client, Json message)
    {
        const String body = message.dump();
        platform.data.input = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        client.Poll(0.01f);
    }
    Json Summary(Int32 version, Bool valid)
    {
        return {{"version", version.Get()}, {"valid", valid.Get()}, {"types", Json::array({
            {{"kind", "type"}, {"name", "Mover"}, {"type", "script"}, {"members", Json::array({
                {{"kind", "field"}, {"name", "speed"}, {"type", "Float"}, {"isStatic", true},
                    {"initializer", {{"present", true}, {"text", "2.0"}}},
                    {"attributes", Json::array({{{"name", "prop"}, {"arguments", Json::array()}}})},
                    {"effective", {{"property", true}, {"noSerialize", true}}}},
                {{"kind", "function"}, {"name", "OnUpdate"}, {"signature", "OnUpdate()"}, {"isCallback", true}}
            })}}
        })}};
    }
    void TestSummaryAndVersionedEdits()
    {
        MemoryPlatform platform;
        Code::LanguageClient client(platform);
        const String path = "C:/project/Mover.jscript";
        Check(client.Start("JBroc.exe", "C:/project/Game.jproject", "ko-KR"), "client starts");
        client.Open(path, "script Mover { [prop] static Float speed = 2.0 }\n");
        Reply(platform, client, {{"id", 1}, {"result", Json::object()}});
        Json request = Request(platform.data.output, "jbro/scriptSummary");
        Check(request["params"]["textDocument"]["version"] == 1, "summary requests exact source version");
        Reply(platform, client, {{"id", request["id"]}, {"result", Summary(1, true)}});
        const Code::ScriptSummary* summary = client.GetSummary(path);
        Check(summary != nullptr && summary->valid && summary->types.Size() == 1, "valid summary accepted");
        Check(summary->types[0].members.Size() == 2 && summary->types[0].members[0].effective.noSerialize,
            "source order and effective settings retained");
        Check(summary->types[0].members[1].isCallback, "callback metadata retained");
        client.Change(path, "script Mover { [prop] static Float speed = 3.0 }\n");
        Check(!client.GetSummary(path)->valid && client.GetSummary(path)->types.Size() == 1,
            "editing retains last snapshot but immediately disables it");
        Code::FieldSettings settings;
        settings.replaceInitializer = true;
        settings.initializerPresent = true;
        settings.initializer = "4.0";
        settings.attributes.Add({"prop", {}});
        Check(client.EditFieldSettings(path, 1, {}, settings) == 0, "stale edit not sent");
        const UInt64 token = client.EditFieldSettings(path, 2, {}, settings);
        Check(token != 0, "current edit request sent");
        const auto changeAt = platform.data.output.rfind("textDocument/didChange");
        const auto editAt = platform.data.output.rfind("jbro/editFieldSettings");
        Check(changeAt != String::npos && changeAt < editAt, "pending source flushed before edit");
        request = Request(platform.data.output, "jbro/editFieldSettings");
        Check(request["params"]["changes"]["initializer"] == "4.0", "source initializer sent without evaluation");
        const Json range = {{"start", {{"line", 0}, {"character", 47}}}, {"end", {{"line", 0}, {"character", 50}}}};
        const Json edits = Json::array({{{"range", range}, {"newText", "4.0"}}});
        Reply(platform, client, {{"id", request["id"]}, {"result", {{"version", 2}, {"edits", edits}}}});
        const Code::FieldEditResult* edit = client.GetFieldEditResult(path);
        Check(edit != nullptr && edit->requestId == token && edit->ready && edit->success && edit->edits.Size() == 1,
            "versioned edit is exposed for atomic source application");
        const UInt64 invalid = client.EditFieldSettings(path, 2, {}, settings);
        request = Request(platform.data.output, "jbro/editFieldSettings");
        Reply(platform, client, {{"id", request["id"]}, {"error", {{"code", -32602}, {"message", "Invalid initializer"}}}});
        edit = client.GetFieldEditResult(path);
        Check(edit->requestId == invalid && edit->ready && !edit->success && !edit->error.empty(), "invalid expression becomes a local edit error");
        const UInt64 stale = client.EditFieldSettings(path, 2, {}, settings);
        request = Request(platform.data.output, "jbro/editFieldSettings");
        client.Change(path, "script Mover { broken\n");
        Reply(platform, client, {{"id", request["id"]}, {"result", {{"version", 2}, {"edits", Json::array()}}}});
        edit = client.GetFieldEditResult(path);
        Check(edit->requestId == stale && edit->ready && !edit->success, "edit reply cannot overwrite newer typing");
        client.Poll(0.2f);
        request = Request(platform.data.output, "jbro/scriptSummary");
        Reply(platform, client, {{"id", request["id"]}, {"result", Summary(3, false)}});
        Check(!client.GetSummary(path)->valid && client.GetSummary(path)->types.Size() == 1, "invalid syntax does not erase last outline");
        client.Stop();
        Check(!client.GetSummary(path)->valid, "disconnect disables navigation and edits");
    }
    void TestClosedDocumentLifetime()
    {
        MemoryPlatform platform;
        Code::LanguageClient client(platform);
        const String path = "C:/project/Mover.jscript";
        Check(client.Start("JBroc.exe", "C:/project/Game.jproject", "ko-KR"), "lifetime client starts");
        client.Open(path, "script Old {}\n");
        Reply(platform, client, {{"id", 1}, {"result", Json::object()}});
        const auto oldSummary = Request(platform.data.output, "jbro/scriptSummary");
        const Int32 oldVersion = client.GetVersion(path);
        Code::FieldSettings settings;
        client.EditFieldSettings(path, oldVersion, {}, settings);
        const auto oldEdit = Request(platform.data.output, "jbro/editFieldSettings");
        client.Close(path);
        client.Open(path, "script Reopened {}\n");
        client.Poll(0.01f);
        Check(client.GetVersion(path) > oldVersion, "reopen never reuses a retired document version");
        Reply(platform, client, {{"id", oldSummary["id"]}, {"result", Summary(oldVersion, true)}});
        Check(!client.GetSummary(path)->valid, "closed lifetime summary cannot enable reopened outline");
        Reply(platform, client, {{"id", oldEdit["id"]}, {"result", {{"version", oldVersion.Get()}, {"edits", Json::array()}}}});
        Check(!client.GetFieldEditResult(path)->ready, "closed lifetime field edit is retired");
        Reply(platform, client, {{"method", "textDocument/publishDiagnostics"}, {"params", {{"uri", Code::LanguageClient::FileUri(path).Std()},
            {"version", oldVersion.Get()}, {"diagnostics", Json::array({{{"message", "retired"}, {"range", Json::object()}}})}}}});
        Check(client.GetAnalysis(path)->diagnostics.IsEmpty(), "retired diagnostics cannot leak into reopened buffer");
    }
}
JBro::Int32 RunScriptSummaryPanelTests()
{
    TestCompactSummaryLayout();
    TestSummaryAndVersionedEdits();
    TestClosedDocumentLifetime();
    JBro::RegisterBuiltinEditorPanelTypes();
    const auto* info = JBro::EditorPanelRegistry::Get().Find(JBro::ScriptSummaryPanel::TypeName);
    Check(info != nullptr && JBro::String(info->dockArea) == "CodeEditor", "summary is registered in the dedicated code dock");
    const auto* undo = JBro::EditorActionRegistry::Get().Find("script_summary.undo");
    const auto* redo = JBro::EditorActionRegistry::Get().Find("script_summary.redo");
    const auto* save = JBro::EditorActionRegistry::Get().Find("script_summary.save");
    Check(undo != nullptr && redo != nullptr && save != nullptr, "summary owns save and source history actions");
    Check(!undo->whileTyping && !redo->whileTyping && save->whileTyping, "expression typing keeps local undo and saves only committed source");
    return 0;
}
