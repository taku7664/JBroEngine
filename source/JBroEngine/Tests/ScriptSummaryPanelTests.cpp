#include <JBro/Editor/Code/LanguageClient.h>
#include <JBro/Editor/Code/Summary.h>
#include <JBro/Editor/EditorPanelRegistry.h>
#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/Widget/ScriptOutline.h>
#include <JBro/Editor/Widget/GuideFocus.h>
#include <JBro/Editor/Widget/Basic.h>
#include <imgui_internal.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Platform/WindowsPlatform.h>
#include "../Modules/JBroEditor/Source/Panel/ScriptSummaryPanel.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

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
        Float outlineHeight = 0.0f, outlineBottom = 0.0f, detailTop = 0.0f;
        Float outlineScroll = 0.0f, detailScroll = 0.0f, outerScroll = 0.0f;
        Bool commonTreeTargetConsumed = true;
        for (Int32 frame = 0; frame < 3; ++frame)
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
                Widget::SetNextItemTarget(GuideFocusTargets::Action("script_summary.outline_probe"));
                Widget::ScriptOutline({entries.Data(), entries.Size()}, selected);
                commonTreeTargetConsumed &= !Widget::Internal::TakeNextItemTarget().IsValid();
                outlineScroll = ImGui::GetScrollMaxY();
            }
            Widget::EndScriptOutline();
            if (Widget::BeginScriptDetails())
            {
                detailTop = ImGui::GetWindowPos().y;
                Widget::ScriptMemberHeader("value0", "Float", "const", Code::SummaryKind::Field);
                for (Int32 index = 0; index < 60; ++index)
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
        Check(commonTreeTargetConsumed, "script outline rows consume guide targets through the existing tree wrapper");
    }
    void TestNestedOutlineInteraction()
    {
        struct Context
        {
            ImGuiContext* previous = ImGui::GetCurrentContext();
            Context()
            {
                ImGui::CreateContext();
                auto& io = ImGui::GetIO();
                io.IniFilename = nullptr;
                io.DisplaySize = {360, 350};
                io.DeltaTime = 1.0f / 60.0f;
                unsigned char* pixels;
                int width, height;
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
        entries.Add({"Outer", "Outer", "class", Code::SummaryKind::Type, 0, "Outer\nclass"});
        entries.Add({"Outer.Inner", "Inner", "struct", Code::SummaryKind::Type, 1, "Inner\nstruct"});
        entries.Add({"Outer.Inner.value", "value", "Float", Code::SummaryKind::Field, 2, "value\nFloat"});
        entries.Add({"Outer.Run", "Run()", "Void", Code::SummaryKind::Function, 1, "Run()\nVoid"});
        entries.Add({"Empty", "Empty", "struct", Code::SummaryKind::Type, 0, "Empty\nstruct"});
        String selected = "Outer";
        ImVec2 origin;
        Float totalHeight = 0.0f, width = 340.0f;
        Bool empty = false;
        const auto draw = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({width, 330});
            ImGui::Begin("Nested summary outline", nullptr, ImGuiWindowFlags_NoSavedSettings);
            origin = ImGui::GetCursorScreenPos();
            Widget::ScriptOutline(empty ? ArrayView<const Widget::ScriptOutlineItem>{} : ArrayView<const Widget::ScriptOutlineItem>{entries.Data(), entries.Size()}, selected);
            totalHeight = ImGui::GetCursorScreenPos().y - origin.y;
            ImGui::End();
            ImGui::Render();
        };
        const auto click = [&](Float x, Float y) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(x, y);
            draw();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            draw();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            draw();
            for (Int32 frame = 0; frame < 20; ++frame)
            {
                draw();
            }
        };
        draw();
        draw();
        const Float fullHeight = totalHeight;
        const Float rowHeight = fullHeight / static_cast<Float>(entries.Size());
        click(origin.x + 130, origin.y + rowHeight * 2.5f);
        Check(selected == "Outer.Inner.value", "nested outline row selects the member source identity");
        click(origin.x + 7, origin.y + rowHeight * 0.5f);
        Check(totalHeight < fullHeight && selected == "Outer.Inner.value", "type arrow collapses descendants without selecting a different declaration");
        click(origin.x + 7, origin.y + rowHeight * 0.5f);
        Check(totalHeight == fullHeight, "type arrow restores the nested outline");
        click(origin.x + 19, origin.y + rowHeight * 1.5f);
        Check(totalHeight < fullHeight && totalHeight > rowHeight * 2, "nested type collapse preserves sibling declarations");
        width = 80.0f;
        draw();
        draw();
        Check(selected == "Outer.Inner.value", "narrow outline clips content without changing selection");
        empty = true;
        draw();
        Check(totalHeight == 0 && selected == "Outer.Inner.value", "empty outline adds no rows and retains its selected source identity");

        String initializer = "2";
        Bool present = true;
        Int32 commits = 0;
        ImVec2 inputPoint, blurPoint, removePoint;
        ImGuiID inputId = 0;
        const auto drawInitializer = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({340, 330});
            ImGui::Begin("Initializer editing", nullptr, ImGuiWindowFlags_NoSavedSettings);
            const ImVec2 start = ImGui::GetCursorScreenPos();
            inputPoint = {start.x + 20, start.y + ImGui::GetFrameHeight() * 0.5f};
            ImGui::PushID("initializer");
            inputId = ImGui::GetID("##value");
            ImGui::PopID();
            if (Widget::ScriptInitializerField("initializer", initializer, present, false))
            {
                ++commits;
            }
            removePoint = (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f;
            Widget::Button("Finish expression editing");
            blurPoint = (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f;
            ImGui::End();
            ImGui::Render();
        };
        const auto clickInitializer = [&](ImVec2 point) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            drawInitializer();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            drawInitializer();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            drawInitializer();
        };
        const auto eraseInitializer = [&] {
            clickInitializer(inputPoint);
            Check(ImGui::GetActiveID() == inputId, "initializer test focuses the actual native text field");
            auto& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            drawInitializer();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            drawInitializer();
            io.AddKeyEvent(ImGuiKey_Backspace, true);
            drawInitializer();
            io.AddKeyEvent(ImGuiKey_Backspace, false);
            drawInitializer();
            Check(initializer.empty() && present, "erasing source expression text keeps its initializer present");
        };
        drawInitializer();
        drawInitializer();
        eraseInitializer();
        Check(commits == 0, "expression edits remain a draft until Enter or focus loss");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
        drawInitializer();
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
        drawInitializer();
        const Bool enterCommitted = commits == 1;
        initializer = "3";
        commits = 0;
        drawInitializer();
        eraseInitializer();
        clickInitializer(blurPoint);
        const Bool blurCommitted = commits == 1;
        Check(enterCommitted, "an empty present initializer commits on Enter so its expression error can be shown");
        Check(blurCommitted, "an empty present initializer commits on focus loss so its expression error can be shown");
        initializer = "4";
        commits = 0;
        drawInitializer();
        clickInitializer(removePoint);
        Check(!present && commits == 1, "the remove button commits initializer absence separately from an empty expression");
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
    Json Hover(const char* text, UInt32 character)
    {
        return {{"contents", {{"kind", "plaintext"}, {"value", text}}},
            {"range", {{"start", {{"line", 2}, {"character", character.Get()}}},
                {"end", {{"line", 2}, {"character", character.Get() + 5}}}}}};
    }
    void TestVersionedAssistance()
    {
        MemoryPlatform platform;
        Code::LanguageClient client(platform);
        const String path = "C:/project/Mover.jscript";
        Check(client.Start("JBroc.exe", "C:/project/Game.jproject", "ko-KR"), "assistance client starts");
        Check(Request(platform.data.output, "initialize")["params"]["capabilities"]["textDocument"]["hover"]["contentFormat"]
            == Json::array({"plaintext"}), "native hover requests plain text for its native tooltip");
        client.Open(path, "script Mover { Float speed = 2.0 }\n");
        Reply(platform, client, {{"id", 1}, {"result", Json::object()}});
        const Int32 version = client.GetVersion(path);
        Check(client.RequestHover(path, version + 1, {}) == 0 && client.RequestReferences(path, version + 1, {}) == 0,
            "assistance never requests an outdated source version");
        client.RequestHover(path, version, {2, 17});
        const auto oldHover = Request(platform.data.output, "textDocument/hover");
        const UInt64 hoverToken = client.RequestHover(path, version, {2, 28});
        const auto currentHover = Request(platform.data.output, "textDocument/hover");
        Reply(platform, client, {{"id", oldHover["id"]}, {"result", Hover("retired", 17)}});
        Check(!client.GetHover(path)->ready && client.GetHover(path)->requestId == hoverToken,
            "an older mouse position cannot replace the newest hover request");
        Reply(platform, client, {{"id", currentHover["id"]}, {"result", Hover("Float speed\n\nDescription", 28)}});
        const auto* hover = client.GetHover(path);
        Check(hover->ready && hover->success && hover->requestId == hoverToken && hover->version == version
            && hover->position.character == 28 && hover->range.start.character == 28 && hover->text.Contains("Description"),
            "hover exposes the exact request identity, UTF-16 range, signature and description");
        client.RequestReferences(path, version, {2, 17});
        const auto oldReferences = Request(platform.data.output, "textDocument/references");
        const UInt64 referencesToken = client.RequestReferences(path, version, {2, 28});
        const auto referencesRequest = Request(platform.data.output, "textDocument/references");
        Check(referencesRequest["params"]["context"]["includeDeclaration"] == false,
            "reference highlights omit the separately selected declaration");
        const auto range = Hover("unused", 28)["range"];
        Reply(platform, client, {{"id", oldReferences["id"]}, {"result", Json::array()}});
        Check(!client.GetReferences(path)->ready && client.GetReferences(path)->requestId == referencesToken,
            "an older symbol selection cannot replace the newest reference request");
        Reply(platform, client, {{"id", referencesRequest["id"]}, {"result", Json::array({
            {{"uri", Code::LanguageClient::FileUri(path).Std()}, {"range", range}},
            {{"uri", "file:///C:/project/Other.jscript"}, {"range", range}}})}});
        const auto* references = client.GetReferences(path);
        Check(references->ready && references->success && references->requestId == referencesToken
            && references->version == version && references->ranges.Size() == 1 && references->ranges[0].start.character == 28,
            "native highlights expose only semantic references in this document");
        const UInt64 revision = hover->revision;
        client.Change(path, "script Mover { Float speed = 3.0 }\n");
        Check(!client.GetHover(path)->ready && client.GetHover(path)->text.IsEmpty() && client.GetHover(path)->revision > revision
            && !client.GetReferences(path)->ready && client.GetReferences(path)->ranges.IsEmpty(),
            "typing immediately removes old hover and highlights");
        client.RequestHover(path, client.GetVersion(path), {});
        auto request = Request(platform.data.output, "textDocument/hover");
        Check(platform.data.output.rfind("textDocument/didChange") < platform.data.output.rfind("textDocument/hover"),
            "the newest unsaved source reaches the server before hover lookup");
        client.CancelHover(path);
        Reply(platform, client, {{"id", request["id"]}, {"result", Hover("canceled", 0)}});
        Check(!client.GetHover(path)->ready && client.GetHover(path)->requestId == 0, "leaving the token retires its pending hover");
        client.RequestReferences(path, client.GetVersion(path), {});
        request = Request(platform.data.output, "textDocument/references");
        client.CancelReferences(path);
        Reply(platform, client, {{"id", request["id"]}, {"result", Json::array({{{"uri", Code::LanguageClient::FileUri(path).Std()}, {"range", range}}})}});
        Check(!client.GetReferences(path)->ready && client.GetReferences(path)->ranges.IsEmpty(), "clearing selection retires pending highlights");
        client.RequestHover(path, client.GetVersion(path), {});
        request = Request(platform.data.output, "textDocument/hover");
        client.Change(path, "script Mover {}\n");
        Reply(platform, client, {{"id", request["id"]}, {"result", Hover("changed", 0)}});
        Check(!client.GetHover(path)->ready, "a reply to an edited source never reappears");
        client.RequestHover(path, client.GetVersion(path), {});
        request = Request(platform.data.output, "textDocument/hover");
        Reply(platform, client, {{"id", request["id"]}, {"result", nullptr}});
        Check(client.GetHover(path)->ready && client.GetHover(path)->success && client.GetHover(path)->text.IsEmpty(),
            "ordinary whitespace has no hover without being a protocol error");
        client.RequestHover(path, client.GetVersion(path), {});
        request = Request(platform.data.output, "textDocument/hover");
        Reply(platform, client, {{"id", request["id"]}, {"error", {{"code", -32602}, {"message", "Invalid source position"}}}});
        Check(client.GetHover(path)->ready && !client.GetHover(path)->success && !client.GetHover(path)->error.IsEmpty()
            && client.GetState() == Code::ClientState::Ready, "a failed hover leaves the language connection usable");
        client.RequestReferences(path, client.GetVersion(path), {});
        request = Request(platform.data.output, "textDocument/references");
        client.Close(path);
        client.Open(path, "script Reopened {}\n");
        client.Poll(0.01f);
        Reply(platform, client, {{"id", request["id"]}, {"result", Json::array({{{"uri", Code::LanguageClient::FileUri(path).Std()}, {"range", range}}})}});
        Check(!client.GetReferences(path)->ready && client.GetReferences(path)->ranges.IsEmpty(),
            "references from a closed buffer cannot reach its reopened lifetime");
    }
    void TestReconnectRestoresSummaryEdits()
    {
        MemoryPlatform platform;
        Code::LanguageClient client(platform);
        const String path = "C:/project/Mover.jscript";
        Check(client.Start("JBroc.exe", "C:/project/Game.jproject", "ko-KR"), "reconnect client starts");
        client.Open(path, "script Mover { [prop] Float speed = 2.0 }\n");
        Reply(platform, client, {{"id", 1}, {"result", Json::object()}});
        const auto retiredSummary = Request(platform.data.output, "jbro/scriptSummary");
        client.Change(path, "script Mover { [prop] Float speed = 3.0 }\n");
        const Int32 version = client.GetVersion(path);
        client.RequestHover(path, version, {});
        platform.data.alive = false;
        client.Poll(0.01f);
        Check(client.GetState() == Code::ClientState::Failed && !client.GetSummary(path)->valid && !client.GetHover(path)->ready,
            "unexpected process exit disables summary and semantic assistance");
        platform.data = {};
        Check(client.Start("JBroc.exe", "C:/project/Game.jproject", "ko-KR"), "a failed language connection can restart");
        Reply(platform, client, {{"id", 1}, {"result", Json::object()}});
        const auto reopened = Request(platform.data.output, "textDocument/didOpen");
        Check(reopened["params"]["textDocument"]["version"] == version.Get()
            && reopened["params"]["textDocument"]["text"] == "script Mover { [prop] Float speed = 3.0 }\n",
            "reconnect restores the newest unsaved source with its exact version");
        const auto summaryRequest = Request(platform.data.output, "jbro/scriptSummary");
        Check(summaryRequest["id"] != retiredSummary["id"], "reconnect never reuses retired request identities");
        Reply(platform, client, {{"id", retiredSummary["id"]}, {"result", Summary(version, true)}});
        Check(!client.GetSummary(path)->valid, "a retired response cannot unlock a newly connected summary");
        Reply(platform, client, {{"id", summaryRequest["id"]}, {"result", Summary(version, true)}});
        Check(client.GetSummary(path)->valid && client.GetSummary(path)->version == version,
            "fresh post-reconnect analysis unlocks the source summary");
        Code::FieldSettings settings;
        settings.replaceInitializer = true;
        settings.initializerPresent = true;
        settings.initializer = "4.0";
        const UInt64 token = client.EditFieldSettings(path, version, {}, settings);
        const auto editRequest = Request(platform.data.output, "jbro/editFieldSettings");
        Reply(platform, client, {{"id", editRequest["id"]}, {"result", {{"version", version.Get()}, {"edits", Json::array()}}}});
        Check(token != 0 && client.GetFieldEditResult(path)->requestId == token && client.GetFieldEditResult(path)->ready
            && client.GetFieldEditResult(path)->success, "versioned source editing resumes after fresh reconnect analysis");
    }
    String Utf8(const std::filesystem::path& path)
    {
        const auto bytes = path.generic_u8string();
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }
    void TestActualSemanticAssistanceAndReconnect()
    {
        namespace fs = std::filesystem;
        struct Files
        {
            fs::path root = fs::temp_directory_path() / ("JBroNativeAssistance-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            Files()
            {
                fs::create_directories(root / "Contents");
                Write("Game.jproject", "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nRootPath: .\nScriptSourceDirectory: Contents\n");
            }
            void Write(const char* name, std::string_view text)
            {
                std::ofstream output(root / name, std::ios::binary);
                output.write(text.data(), static_cast<std::streamsize>(text.size()));
                Check(output.good(), "the actual native language client fixture is written");
            }
            ~Files()
            {
                std::error_code ignored;
                fs::remove(root / "Game.jproject", ignored);
                fs::remove(root / "Contents/Counter.jscript", ignored);
                fs::remove(root / "Contents/Other.jscript", ignored);
                fs::remove(root / "Contents", ignored);
                fs::remove(root, ignored);
            }
        } files;
        const String source = "\xEF\xBB\xBF" "class Counter\r\n{\r\n"
            "    [prop, name(\"\xED\x95\x9C\xF0\x9F\x98\x80\")] public Int value = 2\r\n"
            "    public fn Read() -> Int\r\n    {\r\n"
            "        Int local = value\r\n        return local + value\r\n    }\r\n"
            "    public fn Shadow() -> Int\r\n    {\r\n"
            "        Int value = 9\r\n        return value\r\n    }\r\n}\r\n";
        const String other = "class Other\r\n{\r\n    public Int value = 9\r\n"
            "    public fn Read() -> Int\r\n    {\r\n        return value\r\n    }\r\n}\r\n";
        files.Write("Contents/Counter.jscript", source.View());
        files.Write("Contents/Other.jscript", other.View());
        WindowsPlatform platform;
        const String executable = platform.GetExecutableFolder() + "/JBroc.exe";
        Check(fs::exists(fs::path(std::u8string(reinterpret_cast<const char8_t*>(executable.c_str()), executable.size()))),
            "the installed build includes its actual language server executable");
        Code::LanguageClient client(platform);
        const String project = Utf8(files.root / "Game.jproject");
        const String path = Utf8(files.root / "Contents/Counter.jscript");
        const String otherPath = Utf8(files.root / "Contents/Other.jscript");
        auto pump = [&](auto done, const char* message)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (!done())
            {
                client.Poll(0.01f);
                if (client.GetState() == Code::ClientState::Failed)
                {
                    throw std::runtime_error("Actual language server failed: " + client.GetError().Std());
                }
                Check(std::chrono::steady_clock::now() < deadline, message);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        };
        Check(client.Start(executable, project, "ko-KR"), "the actual native language client starts JBroc");
        client.Open(path, source);
        client.Open(otherPath, other);
        pump([&] { return client.GetSummary(path)->valid && client.GetSummary(otherPath)->valid; },
            "both actual document summaries arrive");
        const Int32 version = client.GetVersion(path);
        const auto& field = client.GetSummary(path)->types[0].members[0];
        Check(field.name == "value" && field.selectionRange.start.line == 2 && field.selectionRange.start.character == 35,
            "BOM and CRLF do not shift lines and Korean plus emoji use UTF-16 positions");
        const Code::Position position = field.selectionRange.start;
        const UInt64 hoverToken = client.RequestHover(path, version, position);
        const UInt64 referenceToken = client.RequestReferences(path, version, position);
        pump([&] { return client.GetHover(path)->ready && client.GetReferences(path)->ready; },
            "actual semantic hover and references arrive");
        Check(hoverToken != 0 && client.GetHover(path)->requestId == hoverToken && client.GetHover(path)->success
            && client.GetHover(path)->text.Contains("Int") && client.GetHover(path)->text.Contains("value")
            && !client.GetHover(path)->text.Contains("```"), "actual hover provides a plain native signature");
        Check(client.RequestHover(path, version, {2, 31}) != 0, "actual type hover resolves the type token independently of its member");
        pump([&] { return client.GetHover(path)->ready; }, "actual type hover arrives");
        Check(client.GetHover(path)->success && client.GetHover(path)->text == "Int",
            "the native tooltip receives the semantic type information for Int");
        const auto* references = client.GetReferences(path);
        Check(referenceToken != 0 && references->requestId == referenceToken && references->success && references->ranges.Size() == 2
            && references->ranges[0].start.line == 5 && references->ranges[1].start.line == 6,
            "semantic identity excludes the declaration, same-name local and other document field");
        Code::FieldSettings settings;
        settings.replaceInitializer = true;
        settings.initializerPresent = true;
        settings.initializer = "4";
        settings.attributes = field.attributes;
        Check(client.EditFieldSettings(path, version, position, settings) != 0, "actual field edit is requested at the semantic position");
        pump([&] { return client.GetFieldEditResult(path)->ready; }, "actual field edit result arrives");
        const auto* edit = client.GetFieldEditResult(path);
        Check(edit->success && edit->edits.Size() == 1 && edit->edits[0].newText == "4"
            && edit->edits[0].range.start.line == 2 && edit->edits[0].range.start.character == 43,
            "actual source edits target only the initializer after UTF-16 attributes");
        settings.initializer.clear();
        Check(client.EditFieldSettings(path, version, position, settings) != 0, "an empty present initializer reaches the actual compiler validation");
        pump([&] { return client.GetFieldEditResult(path)->ready; }, "actual empty initializer validation arrives");
        edit = client.GetFieldEditResult(path);
        Check(!edit->success && !edit->error.empty() && edit->edits.IsEmpty(),
            "the actual compiler rejects an empty expression without changing the source");
        settings.initializerPresent = false;
        Check(client.EditFieldSettings(path, version, position, settings) != 0, "explicit initializer removal reaches the actual compiler");
        pump([&] { return client.GetFieldEditResult(path)->ready; }, "actual initializer removal result arrives");
        Check(client.GetFieldEditResult(path)->success && !client.GetFieldEditResult(path)->edits.IsEmpty(),
            "the actual compiler permits explicit removal while rejecting empty present expressions");
        settings.initializerPresent = true;
        settings.initializer = "4";
        String unsaved = source;
        unsaved.ReplaceAll("value = 2", "value = 7");
        client.Change(path, unsaved);
        const Int32 unsavedVersion = client.GetVersion(path);
        client.Stop();
        Check(!client.GetSummary(path)->valid && !client.GetHover(path)->ready && !client.GetReferences(path)->ready,
            "actual server shutdown disables source summary and semantic decorations");
        Check(client.Start(executable, project, "ko-KR"), "the actual native language client reconnects");
        pump([&] { return client.GetSummary(path)->valid && client.GetSummary(otherPath)->valid; },
            "actual reconnect restores both documents without reopening editor tabs");
        Check(client.GetSummary(path)->version == unsavedVersion
            && client.GetSummary(path)->types[0].members[0].initializer == "7",
            "actual reconnect analyzes the unsaved buffer rather than the older disk file");
        Check(client.EditFieldSettings(path, unsavedVersion, position, settings) != 0,
            "actual summary editing is enabled after reconnection");
        pump([&] { return client.GetFieldEditResult(path)->ready; }, "actual post-reconnect edit result arrives");
        Check(client.GetFieldEditResult(path)->success && client.GetFieldEditResult(path)->edits.Size() == 1,
            "actual source edits work after reconnect using the restored source version");
        client.Stop();
    }
}
JBro::Int32 RunScriptSummaryPanelTests()
{
    TestCompactSummaryLayout();
    TestNestedOutlineInteraction();
    TestSummaryAndVersionedEdits();
    TestClosedDocumentLifetime();
    TestVersionedAssistance();
    TestReconnectRestoresSummaryEdits();
    TestActualSemanticAssistanceAndReconnect();
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
