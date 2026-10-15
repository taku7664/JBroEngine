#include <JBro/Editor/Code/LanguageClient.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorPanelRegistry.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/CodeEditor.h>
#include <JBro/Editor/Code/SourceDebugger.h>
#include <nlohmann/json.hpp>
#include <JBro/Platform/WindowsPlatform.h>
#include "../Modules/JBroEditor/Source/Panel/CodeEditorPanel.h"
#include <TextEditor.h>
#include <imgui.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    using namespace JBro;
    namespace fs = std::filesystem;
    void Check(Bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
    String Utf8(const fs::path& path)
    {
        const auto bytes = path.generic_u8string();
        return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    struct Files
    {
        fs::path root = fs::temp_directory_path() / ("JBroCodeEditor-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Files()
        {
            fs::create_directories(root / "Contents/Scripts");
            fs::create_directories(root / "Assets");
            Write("Game.jproject", "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nRootPath: .\nAssetDirectory: Assets\nScriptSourceDirectory: Contents\nScriptOutputLibraryPath: \"\"\n");
        }
        ~Files() { fs::remove_all(root); }
        void Write(const char* name, std::string_view text)
        {
            std::ofstream file(root / name, std::ios::binary);
            file.write(text.data(), text.size());
            Check(file.good(), "test fixture write");
        }
        String Read(const char* name)
        {
            std::ifstream file(root / name, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file), {});
        }
        String Path(const char* name) const { return Utf8(root / name); }
    };
    struct ChannelData
    {
        String in, out;
        Bool alive = true, writable = true, destroyed = false;
    };
    class MemoryChannel final : public IProcessChannel
    {
    public:
        explicit MemoryChannel(ChannelData& value) : data(value) {}
        ~MemoryChannel() override { data.destroyed = true; }
        Bool Write(std::string_view text) override
        {
            data.out += text;
            return data.writable;
        }
        Bool Read(String& text, Bool error) override
        {
            text.clear();
            if (!error)
            {
                text.swap(data.in);
            }
            return !text.empty();
        }
        Bool IsRunning() const override { return data.alive; }
        ChannelData& data;
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
            data.destroyed = false;
            return MakeOwnerPtr<MemoryChannel>(data);
        }
        ChannelData data;
    };
    String Frame(const String& body) { return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body; }
    int LatestRequest(const String& framed, const char* method)
    {
        int result = 0;
        std::size_t at = 0;
        while (at < framed.size())
        {
            const auto end = framed.find("\r\n\r\n", at);
            Check(end != String::npos, "fake client writes complete frame headers");
            const auto length = std::stoul(framed.substr(at + 16, end - at - 16));
            const auto value = nlohmann::json::parse(framed.View().substr(end + 4, length));
            if (value.value("method", "") == method)
            {
                result = value.value("id", 0);
            }
            at = end + 4 + length;
        }
        Check(result != 0, "expected language request was sent");
        return result;
    }
    void Reply(MemoryPlatform& platform, Code::LanguageClient& client, const String& body)
    {
        platform.data.in = Frame(body);
        client.Poll(0.01f);
    }
    void TestProtocolAndStaleResults()
    {
        MemoryPlatform platform;
        Code::LanguageClient client(platform);
        const String path = "C:/project/a.jscript";
        Check(client.Start("JBroc.exe", "C:/project/Game.jproject", "ko-KR"), "client starts");
        client.Open(path, "script A {}\n");
        const String init = Frame("{\"id\":1,\"result\":{}}");
        platform.data.in = init.substr(0, 10);
        client.Poll(0.01f);
        Check(client.GetState() == Code::ClientState::Starting, "partial frame waits without blocking");
        platform.data.in = init.substr(10);
        client.Poll(0.01f);
        Check(client.GetState() == Code::ClientState::Ready && platform.data.out.find("didOpen") != String::npos, "initialize flushes unsaved documents");
        Reply(platform, client, "{\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"file:///C:/project/a.jscript\",\"version\":1,\"diagnostics\":[{\"message\":\"bad\",\"range\":{}}]}}");
        Check(client.GetAnalysis(path)->diagnostics.Size() == 1, "matching diagnostic accepted");
        client.Change(path, "script A { Int value = 2 }\n");
        Check(client.GetAnalysis(path)->diagnostics.IsEmpty(), "an edit immediately clears old diagnostics");
        Reply(platform, client, "{\"id\":2,\"result\":{\"data\":[0,0,6,12,0]}}");
        Check(client.GetAnalysis(path)->tokens.IsEmpty(), "old semantic response ignored");
        Reply(platform, client, "{\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"file:///C:/project/a.jscript\",\"version\":1,\"diagnostics\":[{\"message\":\"old\"}]}}");
        Check(client.GetAnalysis(path)->diagnostics.IsEmpty(), "old diagnostic ignored");
        client.Poll(0.2f);
        Check(platform.data.out.find("didChange") != String::npos && platform.data.out.find("\"version\":2") != String::npos, "debounced change keeps its version");
        Reply(platform, client, "{\"id\":" + std::to_string(LatestRequest(platform.data.out, "textDocument/semanticTokens/full")) + ",\"result\":{\"data\":[0,0,6,12,0]}}");
        Check(client.GetAnalysis(path)->tokens.Size() == 1, "current semantic token accepted");
        Reply(platform, client, "{\"method\":\"textDocument/publishDiagnostics\",\"params\":7}");
        Check(client.GetState() == Code::ClientState::Failed && platform.data.destroyed, "malformed result stops the child safely");
        Check(client.GetAnalysis(path)->tokens.IsEmpty(), "disconnect clears stale colors");
        Check(client.Start("JBroc.exe", "C:/project/Game.jproject", "ko-KR"), "reconnect starts");
        platform.data.out.clear();
        Reply(platform, client, "{\"id\":1,\"result\":{}}");
        Check(platform.data.out.find("Int value = 2") != String::npos, "reconnect retains unsaved text");
        client.Close(path);
        Check(client.GetAnalysis(path) == nullptr && platform.data.out.find("didClose") != String::npos, "close removes document");
        platform.data.in = "Content-Length: 999999999\r\n\r\n";
        client.Poll(0.01f);
        Check(client.GetState() == Code::ClientState::Failed, "oversized frame is rejected");
        Check(Code::LanguageClient::FileUri("C:/space #/a%.jscript") == "file:///C:/space%20%23/a%25.jscript", "URI encodes reserved path bytes");
        Check(Code::LanguageClient::NormalizePath("c:\\project\\Scripts\\..\\a.jscript") == path, "lexical normalization handles Windows separators and parent segments");
        Check(Code::LanguageClient::FileUri("//server/share/a.jscript") == "file://server/share/a.jscript", "UNC URI preserves authority");
        Check(client.Start("JBroc.exe", "p", "en-US"), "third start");
        platform.data.writable = false;
        Reply(platform, client, "{\"id\":1,\"result\":{}}");
        Check(client.GetState() == Code::ClientState::Failed, "failed initialized write cannot become ready");
        platform.data.writable = true;
        Check(client.Start("JBroc.exe", "p", "en-US"), "framing test starts");
        platform.data.in = "Content-Length: 2garbage\r\n\r\n{}";
        client.Poll(0.01f);
        Check(client.GetState() == Code::ClientState::Failed, "non-numeric frame suffix is rejected");
    }
    struct Ui
    {
        Ui()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {900, 700};
            io.DeltaTime = 1.0f / 60.0f;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            io.Fonts->SetTexID(1);
        }
        ~Ui() { ImGui::DestroyContext(); }
        template<class Draw> void Render(Draw draw)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({900, 700});
            ImGui::Begin("Code test", nullptr, ImGuiWindowFlags_NoSavedSettings);
            draw();
            ImGui::End();
            ImGui::Render();
        }
        Bool HasColor(ImU32 color)
        {
            auto* data = ImGui::GetDrawData();
            for (int list = 0; list < data->CmdListsCount; ++list)
            {
                for (const auto& vertex : data->CmdLists[list]->VtxBuffer)
                {
                    if (vertex.col == color)
                    {
                        return true;
                    }
                }
            }
            return false;
        }
    };
    void TestTextHistoryAndDelete()
    {
        Ui ui;
        TextEditor first, second;
        first.SetText("first\nlast");
        second.SetText("other");
        Check(first.GetText() == "first\nlast", "loading a file must not add a final newline");
        for (const std::string text : {std::string(), std::string("\n"), std::string("a\n"), std::string("a\tb\n\n")})
        {
            first.SetText(text);
            Check(first.GetText() == text, "exact newline/tab round trip");
        }
        std::string text;
        for (int line = 0; line < 200; ++line)
        {
            text += "row " + std::to_string(line) + "\n";
        }
        first.SetText(text);
        first.SetCursor({180, 4});
        ui.Render([&] { first.Render("first"); });
        first.ReplaceSectionText({0, 0}, {199, 7}, "");
        ui.Render([&] { first.Render("first"); });
        first.Undo();
        ui.Render([&] { first.Render("first"); });
        Check(first.GetText() == text && second.GetText() == "other" && !second.CanUndo(), "delete/undo keeps other document history untouched");
        first.Redo();
        ui.Render([&] { first.Render("first"); });
        Check(first.GetMainCursorPosition().line < first.GetLineCount(), "delete after scrolling keeps a valid cursor");
    }
    void TestAtomicSourceEditsAndAnchors()
    {
        Ui ui;
        Widget::CodeEditor buffer;
        const String original = "// \xf0\x9f\x98\x80\nInt value = 1 // keep\nfn Run() {}\n";
        buffer.SetText(original);
        ui.Render([&] { buffer.Render("edits"); });
        Array<Code::TextEdit> edits;
        edits.Add({{{1, 0}, {1, 0}}, "[prop] "});
        edits.Add({{{1, 12}, {1, 13}}, "42"});
        Check(buffer.ApplyEdits(edits), "nonoverlapping source edits apply");
        Check(buffer.GetText() == "// \xf0\x9f\x98\x80\n[prop] Int value = 42 // keep\nfn Run() {}\n", "only target source ranges change");
        buffer.Undo();
        Check(buffer.GetText() == original && !buffer.CanUndo(), "one undo restores both attribute and initializer edits");
        buffer.Redo();
        Check(buffer.GetText().find("[prop]") != String::npos, "one redo restores source gesture");
        edits.Clear();
        edits.Add({{{0, 4}, {0, 4}}, "x"});
        Check(!buffer.ApplyEdits(edits), "UTF16 edit cannot split a surrogate pair");
        edits.Clear();
        edits.Add({{{0, 3}, {0, 5}}, "emoji"});
        Check(buffer.ApplyEdits(edits) && buffer.GetText().StartsWith("// emoji\n"), "UTF16 replacement targets complete emoji");

        Code::Breakpoints anchors;
        const String path = "C:/project/probe.jscript";
        anchors.Open(path, "first\nsecond\nthird", 0);
        anchors.Toggle(path, 2);
        anchors.Change(path, "new\nfirst\nsecond\nthird", 1);
        Check(anchors.Lines(path).Size() == 1 && anchors.Lines(path)[0] == 3, "line insertion moves anchor");
        anchors.Change(path, "new\nfirst\nthird", 2);
        Check(anchors.Lines(path).IsEmpty(), "deleting anchored line removes marker");
        anchors.Change(path, "new\nfirst\nsecond\nthird", 1);
        Check(anchors.Lines(path).Size() == 1 && anchors.Lines(path)[0] == 3, "undo restores deleted marker");
        anchors.Change(path, "first\nsecond\nthird", 0);
        Check(anchors.Lines(path)[0] == 2, "undo insertion restores old line");
        anchors.Toggle(path, 2);
        anchors.Change(path, "new\nfirst\nsecond\nthird", 1);
        Check(anchors.Lines(path).IsEmpty(), "text redo does not re-enable manually removed breakpoint");
        anchors.Toggle(path, 4);
        anchors.Open(path, "new\nfirst\nsecond\nthird", 0);
        Check(anchors.Lines(path)[0] == 4, "closing and reopening document preserves session marker");
        anchors.Clear();
        Check(anchors.Lines(path).IsEmpty(), "project close clears markers");
        anchors.Open(path, "first\nsecond\nthird\nlast", 0);
        anchors.Toggle(path, 2);
        anchors.Change(path, "FIRST\nsecond\nTHIRD\nlast", 1);
        Check(anchors.Lines(path).Size() == 1 && anchors.Lines(path)[0] == 2,
            "unchanged line inside a multi-range gesture keeps its breakpoint");
        anchors.Change(path, "FIRST\nreplacement\nTHIRD\nlast", 2);
        Check(anchors.Lines(path).Size() == 1 && anchors.Lines(path)[0] == 2,
            "replacing a statement without deleting its line keeps the anchor");
        anchors.Change(path, "", 3);
        Check(anchors.Lines(path).IsEmpty(), "deleting all text removes every marker");
        anchors.Change(path, "FIRST\nreplacement\nTHIRD\nlast", 2);
        Check(anchors.Lines(path).Size() == 1 && anchors.Lines(path)[0] == 2, "undo delete-all restores anchors");
    }
    void TestExactNativeAnchorChanges()
    {
        Ui ui;
        Widget::CodeEditor buffer;
        Code::Breakpoints anchors;
        const String path = "C:/project/exact.jscript";
        const auto sync = [&] {
            anchors.Change(path, buffer.GetText(), buffer.GetHistoryIndex(), buffer.TakeChanges());
        };
        const auto open = [&](const String& text, UInt32 line) {
            buffer.SetText(text);
            buffer.TakeChanges();
            anchors.Clear();
            anchors.Open(path, text, buffer.GetHistoryIndex());
            anchors.Toggle(path, line);
        };
        open("a\na\n", 2);
        Check(buffer.ApplyEdits({{{{0, 0}, {0, 0}}, "a\n"}}), "identical line is inserted at the exact beginning");
        sync();
        Check(anchors.Lines(path).Size() == 1 && anchors.Lines(path)[0] == 3,
            "identical-line prepend moves the existing anchor instead of matching duplicate text");
        buffer.Undo();
        sync();
        Check(anchors.Lines(path)[0] == 2, "undo identical-line prepend restores its exact source anchor");
        buffer.Redo();
        sync();
        Check(anchors.Lines(path)[0] == 3, "redo identical-line prepend restores the inserted line shift");
        anchors.Toggle(path, 3);
        buffer.Undo();
        sync();
        buffer.Redo();
        sync();
        Check(anchors.Lines(path).IsEmpty(), "undo and redo never undo a manual breakpoint removal");

        String large;
        for (UInt32 row = 0; row < 1500; ++row)
        {
            large += "line" + std::to_string(row.Get()) + "\n";
        }
        open(large, 750);
        Check(buffer.ApplyEdits({{{{0, 0}, {0, 5}}, "changed-first"}, {{{1499, 0}, {1499, 8}}, "changed-last"}}),
            "two far-apart source replacements share one native transaction");
        sync();
        Check(anchors.Lines(path).Size() == 1 && anchors.Lines(path)[0] == 750,
            "a 1500-line source gesture preserves an untouched middle anchor");
        buffer.Undo();
        sync();
        Check(buffer.GetText() == large && !buffer.CanUndo() && anchors.Lines(path)[0] == 750,
            "one undo restores both distant replacements and leaves middle anchor intact");
        buffer.Redo();
        sync();
        Check(anchors.Lines(path)[0] == 750, "redo distant replacements preserves middle anchor");

        open("head\nmarked\ntail\n", 2);
        Check(buffer.ApplyEdits({{{{0, 0}, {0, 0}}, "inserted\n"}})
            && buffer.ApplyEdits({{{{2, 0}, {3, 0}}, ""}}), "multiple transactions can queue before the panel consumes changes");
        sync();
        Check(anchors.Lines(path).IsEmpty(), "the queued exact deletion removes its anchor");
        buffer.Undo();
        sync();
        Check(anchors.Lines(path).Size() == 1 && anchors.Lines(path)[0] == 3,
            "undo restores an intermediate transaction that was never separately polled");
        buffer.Undo();
        sync();
        Check(anchors.Lines(path)[0] == 2, "the earlier queued insertion also undoes precisely");

        TextEditor native;
        native.SetText("old\nend");
        std::vector<TextEditor::Change> changes;
        native.SetTransactionCallback([&](const auto& value) { changes = value; });
        native.ReplaceSectionText({0, 0}, {0, 3}, "new\nextra");
        native.Undo();
        Check(changes.size() == 2 && !changes[0].insert && changes[0].text == "new\nextra"
            && changes[1].insert && changes[1].text == "old", "native undo reports inverse actions in actual execution order");
    }
    void TestHighlightingAndUtf16()
    {
        Ui ui;
        Widget::CodeEditor editor;
        editor.SetJBroScript(true);
        editor.SetText("script Probe\n{\n  Int value = 1 // comment\n}\n");
        for (int frame = 0; frame < 3; ++frame)
        {
            ui.Render([&] { editor.Render("buffer"); });
        }
        Check(ui.HasColor(IM_COL32(197, 134, 192, 255)), "JBroScript keywords are highlighted");
        Check(ui.HasColor(IM_COL32(78, 201, 176, 255)), "built-in types are highlighted");
        Check(ui.HasColor(IM_COL32(106, 153, 85, 255)), "comments are highlighted");
        Code::Analysis analysis;
        analysis.tokens.Add({{{2, 6}, {2, 11}}, 8, 0});
        Code::Diagnostic diagnostic;
        diagnostic.range = {{2, 6}, {2, 11}};
        diagnostic.severity = 1;
        diagnostic.message = "Test diagnostic";
        analysis.diagnostics.Add(diagnostic);
        editor.SetAnalysis(analysis);
        ui.Render([&] { editor.Render("buffer"); });
        Check(ui.HasColor(IM_COL32(156, 220, 254, 255)), "semantic property color reaches the renderer");
        Check(ui.HasColor(IM_COL32(224, 90, 82, 255)), "diagnostic squiggle reaches the renderer");
        const char* unicode = reinterpret_cast<const char*>(u8"가😀a");
        Check(Widget::CodeEditor::Utf16ToGlyph(unicode, 3) == 2 && Widget::CodeEditor::Utf16ToGlyph(unicode, 2) == 1, "UTF-16 columns respect surrogate pairs");
        editor.SetText(unicode);
        editor.Focus(0, 3);
        ui.Render([&] { editor.Render("buffer"); });
        Check(editor.GetText() == unicode && editor.GetCursor().character == 3, "Unicode text and caret round trip");
    }
    void TestUndoHasOneOwner()
    {
        Ui ui;
        Widget::CodeEditor editor;
        editor.SetText("");
        editor.Focus();
        for (int frame = 0; frame < 3; ++frame)
        {
            ui.Render([&] { editor.Render("buffer"); });
        }
        auto& io = ImGui::GetIO();
        io.AddInputCharacter('a');
        ui.Render([&] { editor.Render("buffer"); });
        io.AddInputCharacter('b');
        ui.Render([&] { editor.Render("buffer"); });
        Check(editor.GetText() == "ab", "two keyboard edits enter document");
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_Z, true);
        ui.Render([&] { editor.Undo(); editor.Render("buffer"); });
        Check(editor.GetText() == "a", "engine undo action and widget shortcut must not both run");
        io.AddKeyEvent(ImGuiKey_Z, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        ui.Render([&] { editor.Render("buffer"); });
        editor.Redo();
        Check(editor.GetText() == "ab", "one redo restores one edit");
    }
    template<class Predicate> void Until(Code::LanguageClient& client, Predicate ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
        while (!ready() && std::chrono::steady_clock::now() < deadline)
        {
            client.Poll(0.02f);
            Check(client.GetState() != Code::ClientState::Failed, client.GetError().c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Check(ready(), "real language server timed out");
    }
    void TestRealLanguageServer()
    {
        Files files;
        files.Write("Contents/Scripts/Probe.jscript", "script Probe\n{\n}\n");
        WindowsPlatform platform;
        Code::LanguageClient client(platform);
        Check(client.Start(platform.GetExecutableFolder() + "/JBroc.exe", files.Path("Game.jproject"), "ko-KR"), "actual JBroc starts over redirected pipes");
        const auto path = files.Path("Contents/Scripts/Probe.jscript");
        client.Open(path, "script Probe\n{\nInt value = missing\n}\n");
        Until(client, [&] { return !client.GetAnalysis(path)->diagnostics.IsEmpty(); });
        Check(client.GetState() == Code::ClientState::Ready, "actual server initialized");
        Check(files.Read("Contents/Scripts/Probe.jscript") == "script Probe\n{\n}\n", "unsaved analysis does not write the file");
        const UInt64 revision = client.GetAnalysis(path)->revision;
        client.Change(path, "script Probe\n{\nInt value = 2\n}\n");
        Until(client, [&] { const auto* result = client.GetAnalysis(path); return result->version == 2 && result->revision > revision && !result->tokens.IsEmpty(); });
        Check(client.GetAnalysis(path)->diagnostics.IsEmpty(), "fixing unsaved text clears the actual error");
        client.Stop();
        Check(client.GetState() == Code::ClientState::Stopped, "actual server shuts down");
    }
    void TestEngineDocuments()
    {
        Files files;
        const String original = "\xef\xbb\xbf// code\r\nint first = 1;";
        files.Write("Contents/Scripts/One.cpp", original.View());
        files.Write("Contents/Scripts/Two.cpp", "// second");
        files.Write("Contents/Scripts/Probe.jscript", "script Probe\n{\n}\n");
        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        Check(editor.Initialize(config), "engine initializes for code dock");
        ProjectFileError error;
        Check(editor.OpenProjectFile(files.Path("Game.jproject").c_str(), error), "engine opens project");
        Check(editor.EnableEditorUi({64, 48}), "engine UI initializes");
        Check(String(Loc::Text(LocKeys::CodeEditorServerReady)) != LocKeys::CodeEditorServerReady, "code editor translations load from actual tables");
        Check(editor.OpenCodeFile(files.Path("Contents/Scripts/One.cpp")), "first native document opens");
        Check(editor.OpenCodeFile(files.Path("Contents/Scripts/Two.cpp")), "second native document opens");
        Check(editor.OpenCodeFile(files.Path("Contents/Scripts/One.cpp"), 2, 4), "same file focuses its existing tab");
        Array<EditorPanel*> panels;
        editor.FindPanels(CodeEditorPanel::TypeName, panels);
        Check(panels.Size() == 2, "same file is not opened twice");
        const auto* registration = EditorPanelRegistry::Get().Find(CodeEditorPanel::TypeName);
        Check(registration != nullptr && String(registration->dockArea) == CodeEditorPanel::DockAreaName, "documents belong to dedicated code dock");
        auto* first = static_cast<CodeEditorPanel*>(panels[0]);
        Check(first->GetText() == "// code\nint first = 1;", "BOM/CRLF decode without changing disk");
        first->GetBuffer().SetText("// edited\nint first = 2;");
        first->OnUpdate(0.01f);
        Check(first->HasUnsavedChanges() && first->SaveChanges(), "dirty document saves");
        Check(files.Read("Contents/Scripts/One.cpp") == "\xef\xbb\xbf// edited\r\nint first = 2;", "save preserves BOM, CRLF and absent final newline");
        first->GetBuffer().SetText("// unsaved");
        first->OnUpdate(0.01f);
        files.Write("Contents/Scripts/One.cpp", "// external edit");
        Check(!first->Save() && files.Read("Contents/Scripts/One.cpp") == "// external edit" && first->HasUnsavedChanges(), "external file changes are not overwritten");
        Check(first->Reload() && !first->HasUnsavedChanges(), "explicit reload replaces buffer");
        Check(editor.OpenCodeFile(files.Path("Contents/Scripts/Probe.jscript")), "script opens using shared server");
        for (int frame = 0; frame < 8; ++frame)
        {
            Check(editor.Tick(0.02f), "engine draws native code tabs");
        }
        editor.CloseProject();
        editor.FindPanels(CodeEditorPanel::TypeName, panels);
        Check(panels.IsEmpty(), "closing project releases documents and server");
        editor.Shutdown();
    }
}
JBro::Int32 RunCodeEditorTests()
{
    TestProtocolAndStaleResults();
    TestTextHistoryAndDelete();
    TestAtomicSourceEditsAndAnchors();
    TestExactNativeAnchorChanges();
    TestHighlightingAndUtf16();
    TestUndoHasOneOwner();
    TestRealLanguageServer();
    TestEngineDocuments();
    std::cout << "Code editor: protocol, text/history, highlighting, real server and engine documents passed\n";
    return 0;
}
