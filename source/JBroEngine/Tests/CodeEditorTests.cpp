#include <JBro/Editor/Code/LanguageClient.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorPanelRegistry.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Types/ValueMath.h>
#include <JBro/Editor/Widget/CodeEditor.h>
#include <JBro/Editor/Widget/GuideFocus.h>
#include <JBro/Editor/Widget/ItemLocator.h>
#include <JBro/Editor/Code/SourceDebugger.h>
#include <nlohmann/json.hpp>
#include <JBro/Platform/WindowsPlatform.h>
#include "../Modules/JBroEditor/Source/Panel/CodeEditorPanel.h"
#include <TextEditor.h>
#include <imgui.h>
#include <imgui_internal.h>
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
    Int32 LatestRequest(const String& framed, const char* method)
    {
        Int32 result = 0;
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
        String clipboard;
        ImFont* alternateFont = nullptr;
        explicit Ui(Bool codeFont = false)
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {900, 700};
            io.DeltaTime = 1.0f / 60.0f;
            io.Fonts->AddFontDefault();
            if (codeFont)
            {
                ImFontConfig alternate;
                alternate.SizePixels = 19;
                alternateFont = io.Fonts->AddFontDefault(&alternate);
                WindowsPlatform platform;
                EditorTheme::LoadCodeFont(platform);
                Check(EditorTheme::GetCodeFont() != nullptr, "code menu font test loads the actual engine code font");
            }
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            io.Fonts->SetTexID(1);
            auto& platform = ImGui::GetPlatformIO();
            platform.Platform_ClipboardUserData = &clipboard;
            platform.Platform_GetClipboardTextFn = [](ImGuiContext* context) {
                return static_cast<String*>(context->PlatformIO.Platform_ClipboardUserData)->c_str();
            };
            platform.Platform_SetClipboardTextFn = [](ImGuiContext* context, const char* text) {
                *static_cast<String*>(context->PlatformIO.Platform_ClipboardUserData) = text;
            };
        }
        ~Ui() { EditorTheme::ReleaseCodeFont(); ImGui::DestroyContext(); }
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
            for (Int32 list = 0; list < data->CmdListsCount; ++list)
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
        ImRect ColorBounds(ImU32 color)
        {
            ImRect result;
            auto* data = ImGui::GetDrawData();
            for (Int32 list = 0; list < data->CmdListsCount; ++list)
            {
                for (const auto& vertex : data->CmdLists[list]->VtxBuffer)
                {
                    if (vertex.col == color)
                    {
                        result.Add(vertex.pos);
                    }
                }
            }
            return result;
        }
    };
    void TestCodeEditorGuideBoundary()
    {
        Ui ui;
        Widget::CodeEditor buffer;
        buffer.SetText("Int value = 1");
        EditorGuideFocus focus;
        const auto target = GuideFocusTargets::Action("code_editor.guide_probe");
        GuideFocusPath path;
        Check(path.Push(target) && focus.Begin(path), "code editor guide path starts");
        struct RestoreFocus
        {
            EditorGuideFocus* previous = Widget::GetGuideFocus();
            ~RestoreFocus() { Widget::SetGuideFocus(previous); }
        } restore;
        Widget::SetGuideFocus(&focus);
        Bool consumed = true;
        ImVec2 minimum, maximum;
        for (Int32 frame = 0; frame < 3; ++frame)
        {
            focus.BeginFrame();
            ui.Render([&] {
                Widget::SetNextItemTarget(target);
                buffer.Render("guide-boundary");
                minimum = ImGui::GetItemRectMin();
                maximum = ImGui::GetItemRectMax();
                consumed &= !Widget::Internal::TakeNextItemTarget().IsValid();
            });
            focus.Update(1.0f / 60.0f);
        }
        Check(consumed, "code editor wrapper consumes its guide target before rendering nested controls");
        const auto& hole = focus.GetHoleRect();
        Check(focus.HasHole() && hole.Width() >= maximum.x - minimum.x && hole.Height() >= maximum.y - minimum.y
            && hole.Contains(Vector2{(minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f}),
            "code editor reports the actual child rectangle to the common guide focus model");
    }
    void TestTextHistoryAndDelete()
    {
        Ui ui;
        TextEditor first, second;
        first.SetText("first\nlast");
        second.SetText("other");
        Check(first.GetText() == "first\nlast", "loading a file must not add a final newline");
        for (const String text : {String(), String("\n"), String("a\n"), String("a\tb\n\n")})
        {
            first.SetText(text);
            Check(first.GetText() == text, "exact newline/tab round trip");
        }
        String text;
        for (Int32 line = 0; line < 200; ++line)
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
    void TestLineNumberInteraction()
    {
        Ui ui;
        TextEditor editor;
        editor.SetShowCurrentLineHighlightEnabled(false);
        editor.SetLineNumberLeftMargin(3);
        editor.SetText("first\n\t\xed\x95\x9c\xea\xb8\x80 second\n\nlast");
        ImGuiWindow* window = nullptr;
        Float scrollX = -1.0f, fontSize = 13.0f;
        editor.SetCustomLineNumberRenderer([&](const TextEditor::CustomLineNumber&) {
            window = ImGui::GetCurrentWindow();
            if (scrollX >= 0)
            {
                ImGui::SetScrollX(scrollX);
                scrollX = -1.0f;
            }
        });
        const auto draw = [&] { ui.Render([&] { ImGui::PushFont(nullptr, fontSize); editor.Render("gutter", {700, 300}); ImGui::PopFont(); }); };
        draw();
        draw();
        Check(window != nullptr, "gutter test finds the real editor child");
        const auto rowPoint = [&](std::size_t line, Float x) {
            return ImVec2(window->Pos.x + x, editor.DocPos2ScreenPos({line, 0}).y + editor.GetLineHeight() * 0.5f);
        };
        const auto click = [&](ImVec2 point) {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
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
        auto& io = ImGui::GetIO();
        const ImVec2 second = rowPoint(1, editor.GetGlyphWidth());
        io.AddMousePosEvent(second.x, second.y);
        draw();
        Check(ui.HasColor(IM_COL32(255, 255, 255, 24)), "line number hover draws the full-row highlight");
        const auto bounds = ui.ColorBounds(IM_COL32(255, 255, 255, 24));
        Check(bounds.Min.x <= window->InnerClipRect.Min.x && bounds.Max.x >= window->InnerClipRect.Max.x,
            "gutter hover rectangle spans the full visible editor row");
        click(second);
        Check(editor.GetCursorText(0) == "\t\xed\x95\x9c\xea\xb8\x80 second\n", "left gutter padding selects the whole UTF8/tab line");
        click(rowPoint(2, editor.GetGlyphWidth()));
        Check(editor.GetCursorText(0) == "\n", "empty line gutter selects its newline");
        click(rowPoint(3, editor.GetGlyphWidth()));
        Check(editor.GetCursorText(0) == "last", "last line without final newline is selected");
        Check(editor.GetText() == "first\n\t\xed\x95\x9c\xea\xb8\x80 second\n\nlast" && !editor.CanUndo(),
            "gutter selection never edits the document");
        io.AddMousePosEvent(second.x, second.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        draw();
        const auto last = rowPoint(3, editor.GetGlyphWidth());
        io.AddMousePosEvent(last.x, last.y);
        draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        draw();
        Check(editor.GetCursorText(0) == "\t\xed\x95\x9c\xea\xb8\x80 second\n\nlast", "gutter drag extends by whole lines");
        click(rowPoint(3, editor.GetGlyphWidth()));
        io.AddMousePosEvent(last.x, last.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); draw();
        const auto first = editor.DocPos2ScreenPos({0, 0});
        io.AddMousePosEvent(first.x + editor.GetGlyphWidth() * 2.5f, first.y + editor.GetLineHeight() * 0.5f); draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); draw();
        Check(editor.GetCursorText(0) == editor.GetText(), "upward gutter drag into text still selects complete lines");
        io.AddMousePosEvent(window->Pos.x + editor.GetGlyphWidth(), rowPoint(3, 0.0f).y + editor.GetLineHeight() * 2.0f); draw();
        Check(!ui.HasColor(IM_COL32(255, 255, 255, 24)), "empty space below the last line is not a hover row");
        String longText;
        for (Int32 row = 0; row < 100; ++row)
        {
            longText += "row " + std::to_string(row) + String(160, 'x') + "\n";
        }
        editor.SetText(longText);
        fontSize = 22.0f;
        draw(); draw();
        editor.ScrollToLine(60, TextEditor::Scroll::alignTop);
        scrollX = 250.0f;
        draw(); draw(); draw();
        const auto scrolled = rowPoint(60, editor.GetGlyphWidth());
        io.AddMousePosEvent(scrolled.x, scrolled.y); draw();
        Check(window->Scroll.x > 0 && window->Scroll.y > 0 && ui.HasColor(IM_COL32(255, 255, 255, 24)),
            "zoomed, horizontally and vertically scrolled gutter still hovers the requested source line");
        click(scrolled);
        Check(editor.GetCursorText(0) == "row 60" + String(160, 'x') + "\n",
            "zoomed scrolled gutter selection targets the actual source line");
    }
    void TestPopupUsesCallerLayout()
    {
        Ui ui;
        TextEditor editor;
        editor.SetText("Int value");
        auto& style = ImGui::GetStyle();
        style.ItemSpacing = {8, 6};
        ImVec2 popupSpacing;
        Float rowPitch = 0.0f, rowTextHeight = 0.0f, shortcutGap = 0.0f;
        Int32 popupFrames = 0;
        editor.SetTextContextMenuCallback([&](TextEditor::PopupData&) {
            popupSpacing = ImGui::GetStyle().ItemSpacing;
            const Float top = ImGui::GetCursorScreenPos().y;
            ImGui::MenuItem("Copy", "Ctrl+C");
            rowPitch = ImGui::GetCursorScreenPos().y - top;
            rowTextHeight = ImGui::GetTextLineHeight();
            ImGui::MenuItem("Paste", "Ctrl+V");
            const auto& columns = ImGui::GetCurrentWindow()->DC.MenuColumns;
            shortcutGap = columns.OffsetShortcut - columns.OffsetLabel - ImGui::CalcTextSize("Paste").x;
            ++popupFrames;
        });
        const auto draw = [&] {
            ui.Render([&] {
                const auto incoming = ImGui::GetStyle().ItemSpacing;
                const Int32 stack = ImGui::GetCurrentContext()->StyleVarStack.Size;
                editor.Render("popup-layout", {700, 300});
                Check(ImGui::GetStyle().ItemSpacing.x == incoming.x && ImGui::GetStyle().ItemSpacing.y == incoming.y
                    && ImGui::GetCurrentContext()->StyleVarStack.Size == stack, "editor restores caller spacing and style stack after rendering");
            });
        };
        draw(); draw();
        const auto text = editor.DocPos2ScreenPos({0, 1});
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(text.x + editor.GetGlyphWidth() * 0.5f, text.y + editor.GetLineHeight() * 0.5f);
        draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, true); draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, false); draw();
        draw();
        Check(popupFrames >= 2, "layout regression opens the real native editor popup");
        Check(popupSpacing.x == 8 && popupSpacing.y == 6, "editor popup inherits caller menu item spacing");
        Check(rowPitch == rowTextHeight + 6, "editor popup rows use the common menu row pitch");
        Check(shortcutGap >= 7, "editor popup separates captions and shortcuts by caller spacing");
        style.ItemSpacing = {13, 9};
        draw(); draw();
        Check(popupSpacing.x == 13 && popupSpacing.y == 9 && rowPitch == rowTextHeight + 9 && shortcutGap >= 12,
            "popup layout follows a different caller spacing without fixed menu constants");
    }
    void TestDelegatedPopupOwnership()
    {
        Ui ui;
        TextEditor editor;
        editor.SetText("Int value");
        Int32 rendererFrames = 0, requests = 0, legacyFrames = 0;
        editor.SetTextContextMenuCallback([&](TextEditor::PopupData&) { ++legacyFrames; });
        editor.SetTextContextMenuRenderer([&](bool requested, TextEditor::PopupData& data) {
            ++rendererFrames;
            if (requested)
            {
                ++requests;
                Check(data.pos.line == 0, "delegated popup receives the clicked source location");
            }
        });
        const auto draw = [&] { ui.Render([&] { editor.Render("popup-owner", {700, 300}); }); };
        draw(); draw();
        const auto click = [&] {
            const auto text = editor.DocPos2ScreenPos({0, 1});
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(text.x + editor.GetGlyphWidth() * 0.5f, text.y + editor.GetLineHeight() * 0.5f); draw();
            io.AddMouseButtonEvent(ImGuiMouseButton_Right, true); draw();
            io.AddMouseButtonEvent(ImGuiMouseButton_Right, false); draw(); draw();
        };
        click();
        Check(rendererFrames >= 4 && requests == 1 && legacyFrames == 0 && ImGui::GetCurrentContext()->OpenPopupStack.empty(),
            "delegated renderer receives one request and exclusively owns popup opening");
        editor.SetTextContextMenuRenderer(nullptr);
        click();
        Check(legacyFrames > 0 && !ImGui::GetCurrentContext()->OpenPopupStack.empty(),
            "removing delegated renderer restores the upstream context callback lifecycle");
    }
    void TestWidgetMenuUsesCallerFont()
    {
        Ui ui(true);
        Widget::CodeEditor buffer;
        buffer.SetText("Int value");
        ImGui::GetStyle().ItemSpacing = {8, 6};
        ImFont* callerFont = ImGui::GetIO().Fonts->Fonts.front();
        Float callerSize = 13.0f, renderedCallerSize = 0.0f, expectedLabelWidth = 0.0f, codeGlyph = 0.0f;
        ImGuiWindow* child = nullptr;
        const auto draw = [&] {
            ui.Render([&] {
                ImGui::PushFont(callerFont, callerSize);
                renderedCallerSize = ImGui::GetFontSize();
                expectedLabelWidth = 0.0f;
                for (const auto* key : {LocKeys::MenuUndo, LocKeys::MenuRedo, LocKeys::MenuCut, LocKeys::MenuCopy, LocKeys::MenuPaste, LocKeys::MenuSelectAll})
                {
                    expectedLabelWidth = JBro::Max(expectedLabelWidth, ImGui::CalcTextSize(Loc::Text(key)).x);
                }
                ImGui::PushFont(EditorTheme::GetCodeFont(), 16);
                codeGlyph = ImGui::CalcTextSize("#").x;
                ImGui::PopFont();
                const Int32 stack = ImGui::GetCurrentContext()->FontStack.Size;
                buffer.Render("menu-font");
                child = ImGui::GetCurrentWindow()->DC.ChildWindows.back();
                Check(ImGui::GetFont() == callerFont && ImGui::GetFontSize() == renderedCallerSize
                    && ImGui::GetCurrentContext()->FontStack.Size == stack, "code widget restores caller font, size and font stack");
                ImGui::PopFont();
            });
        };
        draw(); draw();
        Check(child->FontRefSize == 16, "font regression uses a 16px code canvas");
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(child->DC.CursorStartPos.x + codeGlyph * 8.5f, child->DC.CursorStartPos.y + 8); draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, true); draw();
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, false); draw();
        const auto check = [&] {
            draw(); draw();
            const auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
            Check(!popups.empty() && popups.back().Window != nullptr, "font regression opens the real widget context menu");
            const auto* popup = popups.back().Window;
            const Float height = popup->DC.CursorPos.y - popup->DC.CursorStartPos.y;
            Check(height == 6 * (renderedCallerSize + 6) + 2 * (1 + 6), "code context menu row height uses the caller UI font size");
            Check(std::abs(popup->DC.MenuColumns.Widths[1] - expectedLabelWidth) <= 1,
                "code context menu measures captions using the caller UI font");
            Check(popup->DC.MenuColumns.Widths[2] == 0,
                "standalone context menu does not invent application shortcut bindings");
        };
        check();
        callerFont = ui.alternateFont;
        callerSize = 19.0f;
        check();
        callerFont = ImGui::GetIO().Fonts->Fonts.front();
        callerSize = 21.0f;
        check();
        ImGui::GetStyle().FontScaleMain = 1.5f;
        callerSize = 14.0f;
        check();
    }
    void TestExternalClipboardOwnership()
    {
        Ui ui;
        TextEditor editor;
        editor.SetText("first\nlast");
        editor.SetExternalClipboardActions(true);
        const auto draw = [&] { ui.Render([&] { editor.Render("clipboard-owner"); }); };
        draw();
        editor.SetFocus(); draw(); draw();
        editor.SelectRegion({0, 1}, {0, 3});
        ui.clipboard = "paste";
        const auto key = [&](ImGuiKey item, Bool ctrl, Bool shift) {
            auto& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiMod_Ctrl, ctrl); io.AddKeyEvent(ImGuiMod_Shift, shift); io.AddKeyEvent(item, true); draw();
            io.AddKeyEvent(item, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); io.AddKeyEvent(ImGuiMod_Shift, false); draw();
        };
        for (const auto item : {ImGuiKey_X, ImGuiKey_C, ImGuiKey_V, ImGuiKey_A, ImGuiKey_Insert, ImGuiKey_Delete})
        {
            key(item, true, false);
            Check(editor.GetText() == "first\nlast" && editor.GetCursorText(0) == "ir" && ui.clipboard == "paste",
                "external manager owns control clipboard/select shortcuts without native duplicates");
        }
        key(ImGuiKey_Insert, false, true);
        Check(editor.GetText() == "first\nlast", "external manager also owns legacy shift insert paste");
        const auto copyRoute = [&]() -> ImGuiID {
            auto& routes = GImGui->KeysRoutingTable;
            for (auto index = routes.Index[ImGuiKey_C - ImGuiKey_NamedKey_BEGIN]; index != -1; index = routes.Entries[index].NextEntryIndex)
            {
                if (routes.Entries[index].Mods == ImGuiMod_Ctrl)
                {
                    return routes.Entries[index].RoutingCurr;
                }
            }
            return ImGuiKeyOwner_NoOwner;
        };
        editor.SetExternalClipboardActions(false);
        // Shortcut routing is registered during drawing and becomes current on the next frame.
        draw(); draw();
        key(ImGuiKey_C, true, false);
        if (ui.clipboard != "ir")
        {
            std::cout << "  standalone copy diagnostic: text=" << editor.GetText() << " selection=" << editor.GetCursorText(0)
                << " clipboard=" << ui.clipboard << " route=" << copyRoute()
                << " focused=" << (GImGui->NavWindow == nullptr ? "none" : GImGui->NavWindow->Name) << std::endl;
        }
        Check(ui.clipboard == "ir", "standalone editor retains its own clipboard shortcut path");
        ui.clipboard = "x";
        key(ImGuiKey_Insert, false, true);
        Check(editor.GetText() == "fxst\nlast", "standalone shift insert uses its native paste path");
        editor.SetReadOnlyEnabled(true);
        key(ImGuiKey_X, true, false);
        key(ImGuiKey_Delete, true, false);
        Check(editor.GetText() == "fxst\nlast", "standalone readonly mode also blocks native cut shortcuts");
    }
    void TestDiagnosticTooltipsUseCommonDelay()
    {
        Ui ui;
        auto& style = ImGui::GetStyle();
        style.HoverFlagsForTooltipMouse = ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_DelayNormal;
        style.HoverDelayNormal = 0.10f;
        style.HoverStationaryDelay = 0.05f;
        Widget::CodeEditor buffer;
        const String source = "// \xf0\x9f\x98\x80\nInt value = 1\n";
        buffer.SetText(source);
        Code::Analysis analysis;
        analysis.diagnostics.Add({{{1, 4}, {1, 9}}, 1, {}, "diagnostic message"});
        analysis.diagnostics.Add({{{1, 10}, {1, 10}}, 1, {}, "zero width punctuation"});
        analysis.diagnostics.Add({{{1, 13}, {1, 13}}, 1, {}, "zero width end"});
        analysis.diagnostics.Add({{{0, 3}, {0, 3}}, 1, {}, "zero width surrogate pair"});
        buffer.SetAnalysis(analysis);
        buffer.SetHoverTooltip({{1, 4}, {1, 9}}, "semantic message");
        ImGuiWindow* window = nullptr;
        Float glyph = 0.0f, height = 0.0f;
        String tooltipText;
        const auto draw = [&] {
            ui.Render([&] {
                glyph = ImGui::CalcTextSize("#").x;
                height = ImGui::GetTextLineHeightWithSpacing();
                ImGui::LogToBuffer();
                buffer.Render("diagnostic-tooltips");
                tooltipText = ImGui::GetCurrentContext()->LogBuffer.c_str();
                ImGui::LogFinish();
                window = ImGui::GetCurrentWindow()->DC.ChildWindows.back();
            });
        };
        const auto tooltipCount = [] {
            Int32 count = 0;
            for (auto* item : ImGui::GetCurrentContext()->Windows)
            {
                if (item->Active && (item->Flags & ImGuiWindowFlags_Tooltip))
                {
                    ++count;
                }
            }
            return count;
        };
        const auto hover = [&](Float column, std::size_t line) {
            ImGui::GetIO().AddMousePosEvent(window->DC.CursorStartPos.x + glyph * (7.0f + column),
                window->DC.CursorStartPos.y + height * (line + 0.5f));
            draw();
        };
        const auto waitForTooltip = [&] {
            for (Int32 frame = 0; frame < 20; ++frame)
            {
                draw();
            }
        };
        draw();
        draw();
        hover(4.2f, 1);
        Check(tooltipCount() == 0, "diagnostic tooltip also waits for the common hover delay");
        waitForTooltip();
        Check(tooltipCount() == 1 && tooltipText.Contains("diagnostic message") && !tooltipText.Contains("semantic message"),
            "one delayed diagnostic tooltip takes priority over semantic assistance");
        hover(10.2f, 1);
        waitForTooltip();
        Code::Position symbol;
        Check(!buffer.GetHoveredPosition(symbol) && tooltipText.Contains("zero width punctuation"),
            "a zero width diagnostic covers its underlined punctuation without widening symbol hover requests");
        hover(11.2f, 1);
        Check(tooltipCount() == 0, "zero width diagnostic does not extend into the next glyph");
        hover(13.2f, 1);
        waitForTooltip();
        Check(tooltipCount() == 0, "an end of line diagnostic cannot hover a glyph without an underline");
        hover(3.2f, 0);
        waitForTooltip();
        Check(tooltipText.Contains("zero width surrogate pair"), "zero width UTF16 diagnostic expands over one complete native glyph");
        hover(4.2f, 1);
        waitForTooltip();
        buffer.SetHoverTooltip({}, {});
        buffer.ClearAnalysis();
        draw();
        Check(tooltipCount() == 0, "clearing analysis removes the cached diagnostic tooltip immediately");
        buffer.SetAnalysis(analysis);
        waitForTooltip();
        Check(tooltipText.Contains("diagnostic message"), "fresh analysis restores the diagnostic tooltip");
        Check(buffer.ApplyEdits({{{{1, 12}, {1, 13}}, "2"}}), "diagnostic invalidation edit applies");
        draw();
        Check(tooltipCount() == 0, "unsaved source edits invalidate old diagnostic tooltips");
        buffer.SetAnalysis(analysis);
        waitForTooltip();
        Check(tooltipText.Contains("diagnostic message"), "diagnostic cache accepts a new analysis after editing");
        buffer.SetText(source);
        draw();
        Check(tooltipCount() == 0, "replacing the source also invalidates old diagnostic tooltips");
    }
    void TestSourceAssistanceWidget()
    {
        Ui ui;
        ImGui::GetStyle().HoverFlagsForTooltipMouse = ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_DelayNormal;
        ImGui::GetStyle().HoverDelayNormal = 0.10f;
        ImGui::GetStyle().HoverStationaryDelay = 0.05f;
        Widget::CodeEditor buffer;
        const String source = "// \xf0\x9f\x98\x80\nInt value = 1\nvalue\n";
        buffer.SetText(source);
        buffer.SetJBroScript(true);
        ImGuiWindow* window = nullptr;
        Float glyph = 0.0f, height = 0.0f;
        const auto draw = [&] {
            ui.Render([&] {
                const Bool codeFont = EditorTheme::GetCodeFont() != nullptr;
                if (codeFont)
                {
                    ImGui::PushFont(EditorTheme::GetCodeFont(), 16.0f);
                }
                glyph = ImGui::CalcTextSize("#").x;
                height = ImGui::GetTextLineHeightWithSpacing();
                if (codeFont)
                {
                    ImGui::PopFont();
                }
                buffer.Render("assistance");
                window = ImGui::GetCurrentWindow()->DC.ChildWindows.back();
            });
        };
        draw(); draw();
        const auto point = [&](Float column, std::size_t line) {
            return ImVec2(window->DC.CursorStartPos.x + glyph * (7.0f + column), window->DC.CursorStartPos.y + height * (line + 0.5f));
        };
        const auto hover = [&](ImVec2 position) {
            ImGui::GetIO().AddMousePosEvent(position.x, position.y);
            draw();
        };
        Code::Position hovered;
        hover(point(4.2f, 1));
        Check(buffer.GetHoveredPosition(hovered) && hovered.line == 1 && hovered.character == 4,
            "widget reports hovered identifier in UTF16 source coordinates");
        buffer.SetHoverTooltip({{1, 4}, {1, 9}}, "Int value");
        draw();
        const auto hasTooltip = [] {
            for (auto* item : ImGui::GetCurrentContext()->Windows)
            {
                if (item->Active && (item->Flags & ImGuiWindowFlags_Tooltip))
                {
                    return true;
                }
            }
            return false;
        };
        Check(!hasTooltip(), "symbol tooltip waits for the common hover delay");
        for (Int32 frame = 0; frame < 20; ++frame)
        {
            draw();
        }
        Check(hasTooltip(), "matching current symbol displays the supplied plain text tooltip after the common delay");
        hover(point(3.2f, 1));
        Check(!buffer.GetHoveredPosition(hovered) && !hasTooltip(), "whitespace has no symbol hover or stale tooltip");
        hover({window->Pos.x + glyph * 0.5f, point(0.0f, 1).y});
        Check(!buffer.GetHoveredPosition(hovered), "line numbers never produce symbol hover requests");
        buffer.SelectRange({{0, 3}, {0, 5}});
        const auto selection = buffer.GetSelection();
        Check(buffer.HasSelection() && selection.start.character == 3 && selection.end.character == 5,
            "selection API round trips a complete UTF16 surrogate pair");
        buffer.SelectRange({{1, 4}, {1, 9}});
        const ImVec2 breakpoint(window->Pos.x + glyph * 4.0f, point(0.0f, 1).y);
        hover(breakpoint);
        Check(ui.HasColor(ImGui::ColorConvertFloat4ToU32(EditorTheme::Fade(EditorTheme::Danger, 0.45f))) && buffer.TakeBreakpointToggle() == 0,
            "unused breakpoint gutter previews a marker without installing it");
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true); draw();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false); draw();
        Check(buffer.TakeBreakpointToggle() == 2 && buffer.GetSelection().start.character == 4 && buffer.GetSelection().end.character == 9,
            "breakpoint click requests its own line and preserves text selection");
        buffer.SetDebugMarkers({{2, false, "pending"}}, 2);
        draw();
        Check(ui.HasColor(ImGui::ColorConvertFloat4ToU32(EditorTheme::Danger)) && ui.HasColor(ImGui::ColorConvertFloat4ToU32(EditorTheme::Amber)),
            "pending breakpoint and stopped line retain separate marker colors");
        hover({window->Pos.x + glyph * 0.5f, point(0.0f, 1).y});
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true); draw();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false); draw();
        Check(buffer.TakeBreakpointToggle() == 0 && buffer.GetSelection().start.line == 1 && buffer.GetSelection().start.character == 0
            && buffer.GetSelection().end.line == 2 && buffer.GetSelection().end.character == 0,
            "number gutter selects one whole line without toggling a breakpoint");
        buffer.SelectRange({{1, 4}, {1, 9}});
        Code::Analysis analysis;
        analysis.tokens.Add({{{1, 4}, {1, 9}}, 8, 0});
        buffer.SetAnalysis(analysis);
        buffer.SetReferenceHighlights({{{1, 4}, {1, 9}}, {{2, 0}, {2, 5}}});
        draw();
        const auto highlight = ImGui::ColorConvertFloat4ToU32(EditorTheme::Fade(EditorTheme::Amber, 0.30f));
        Check(ui.HasColor(highlight) && ui.HasColor(IM_COL32(156, 220, 254, 255)),
            "reference backgrounds preserve semantic foreground colors");
        Check(buffer.ApplyEdits({{{{1, 12}, {1, 13}}, "2"}}), "source assistance test edit applies");
        draw();
        Check(!ui.HasColor(highlight), "source changes immediately invalidate old reference backgrounds");

        const auto menu = [&](const char* label) {
            auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
            if (popups.empty())
            {
                hover(point(4.2f, 1));
                auto& io = ImGui::GetIO();
                io.AddMouseButtonEvent(ImGuiMouseButton_Right, true); draw();
                io.AddMouseButtonEvent(ImGuiMouseButton_Right, false); draw();
            }
            if (popups.empty() || popups.back().Window == nullptr)
            {
                const auto* hoveredWindow = ImGui::GetCurrentContext()->HoveredWindow;
                throw std::runtime_error(String("right click opens the editor menu: ") + label + "; hovered="
                    + (hoveredWindow == nullptr ? "none" : hoveredWindow->Name));
            }
            ImGui::ActivateItemByID(popups.back().Window->GetID(Loc::Text(label)));
            draw(); draw();
        };
        menu(LocKeys::MenuUndo);
        Check(buffer.GetText() == source && !buffer.CanUndo(), "context menu undo runs exactly once");
        menu(LocKeys::MenuRedo);
        Check(buffer.GetText().find("value = 2") != String::npos, "context menu redo restores its one transaction");
        buffer.SelectRange({{1, 4}, {1, 9}});
        menu(LocKeys::MenuCopy);
        Check(ui.clipboard == "value", "context menu copy uses the selected source text");
        menu(LocKeys::MenuCut);
        Check(buffer.GetText().find("Int  = 2") != String::npos, "context menu cut changes the selected text");
        menu(LocKeys::MenuPaste);
        Check(buffer.GetText().find("Int value = 2") != String::npos, "context menu paste restores clipboard text");
        buffer.SetReadOnly(true);
        const auto locked = buffer.GetText();
        menu(LocKeys::MenuUndo);
        Check(buffer.GetText() == locked, "read only menu disables source mutations");
        menu(LocKeys::MenuRedo);
        menu(LocKeys::MenuCut);
        menu(LocKeys::MenuPaste);
        Check(buffer.GetText() == locked, "read only also disables redo, cut and paste");
        menu(LocKeys::MenuSelectAll);
        Check(buffer.HasSelection(), "read only menu still allows select all");
        menu(LocKeys::MenuCopy);
        Check(ui.clipboard == locked, "read only menu still allows copying the full document");
        buffer.SetText("Int " + String(200, 'x') + " = 1");
        draw(); draw();
        ImGui::SetScrollX(window, 200);
        draw(); draw();
        hover({window->Pos.x + glyph * 0.5f, window->DC.CursorStartPos.y + height * 0.5f});
        Check(window->Scroll.x > 0 && !buffer.GetHoveredPosition(hovered),
            "horizontal scrolling never turns the fixed number gutter into a symbol hover target");
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
        editor.SetText("script Probe\n{\n  [prop, range(0, 10)]\n  Int value = 1 // comment\n}\n");
        for (Int32 frame = 0; frame < 3; ++frame)
        {
            ui.Render([&] { editor.Render("buffer"); });
        }
        Check(ui.HasColor(IM_COL32(197, 134, 192, 255)), "JBroScript keywords are highlighted");
        Check(ui.HasColor(IM_COL32(78, 201, 176, 255)), "built-in types are highlighted");
        Check(ui.HasColor(IM_COL32(106, 153, 85, 255)), "comments are highlighted");
        Code::Analysis analysis;
        analysis.tokens.Add({{{2, 3}, {2, 7}}, 17, 0});
        analysis.tokens.Add({{{2, 9}, {2, 14}}, 17, 0});
        analysis.tokens.Add({{{3, 6}, {3, 11}}, 8, 0});
        Code::Diagnostic diagnostic;
        diagnostic.range = {{3, 6}, {3, 11}};
        diagnostic.severity = 1;
        diagnostic.message = "Test diagnostic";
        analysis.diagnostics.Add(diagnostic);
        editor.SetAnalysis(analysis);
        ui.Render([&] { editor.Render("buffer"); });
        Check(ui.HasColor(IM_COL32(156, 220, 254, 255)), "semantic property color reaches the renderer");
        Check(ui.HasColor(IM_COL32(196, 181, 253, 255)), "attribute semantic tokens render in the theme's light purple");
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
        for (Int32 frame = 0; frame < 3; ++frame)
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
    void TestHiddenDockReleasesFocus()
    {
        Files files;
        files.Write("Contents/Scripts/Focus.cpp", "int value = 1;\n");
        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        Check(editor.Initialize(config), "engine initializes for dock focus regression");
        ProjectFileError error;
        Check(editor.OpenProjectFile(files.Path("Game.jproject").c_str(), error), "focus project opens");
        Check(editor.EnableEditorUi({64, 48}), "focus UI initializes");
        Check(editor.OpenCodeFile(files.Path("Contents/Scripts/Focus.cpp")), "focus document opens");
        const auto tick = [&] {
            Check(editor.Tick(0.02f), "dock focus frame succeeds");
        };
        for (Int32 frame = 0; frame < 8; ++frame)
        {
            tick();
        }
        ImGuiWindow* dock = ImGui::FindWindowByName("###CodeEditor");
        Check(dock != nullptr && dock->DockNode != nullptr, "code container is docked");
        const ImGuiID right = ImGui::DockBuilderSplitNode(dock->DockNode->ID, ImGuiDir_Right, 0.4f, nullptr, nullptr);
        ImGui::DockBuilderDockWindow(dock->Name, right);
        ImGui::DockBuilderFinish(right);
        for (Int32 frame = 0; frame < 4; ++frame)
        {
            tick();
        }
        ImGuiDockNode* node = dock->DockNode;
        Check(node != nullptr && node->TabBar != nullptr, "split dock retains its tab bar state");
        node->LocalFlags |= ImGuiDockNodeFlags_HiddenTabBar;
        node->UpdateMergedFlags();
        node->TabBar->SelectedTabId = 0;
        Array<EditorPanel*> documents;
        editor.FindPanels(CodeEditorPanel::TypeName, documents);
        documents[0]->RequestFocus();
        for (Int32 frame = 0; frame < 6; ++frame)
        {
            tick();
        }
        Check(dock->DockTabIsVisible && node->IsHiddenTabBar(), "hidden dock is actually visible");
        ImGuiWindow* root = ImGui::FindWindowByName("##EditorRoot");
        Check(root != nullptr, "root menu exists");
        const ImGuiID menuBar = ImHashStr("##MenuBar", 0, root->ID);
        Widget::ItemLocator::Watch(ImHashStr(Loc::Text(LocKeys::MenuFile), 0, menuBar));
        tick();
        Widget::ItemLocator::ItemRect rect;
        Check(Widget::ItemLocator::Take(rect), "file menu position comes from the item locator");
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent((rect.minX + rect.maxX).Get() * 0.5f, (rect.minY + rect.maxY).Get() * 0.5f);
        tick();
        io.AddMouseButtonEvent(0, true);
        tick();
        io.AddMouseButtonEvent(0, false);
        for (Int32 frame = 0; frame < 4; ++frame)
        {
            tick();
            Check(!GImGui->OpenPopupStack.empty(), "hidden code dock must not steal focus from an opened menu");
        }
        io.AddKeyEvent(ImGuiKey_Escape, true);
        tick();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        tick();
        ImGuiWindow* buffer = nullptr;
        for (auto* window : GImGui->Windows)
        {
            if (window->Active && std::strstr(window->Name, "##CodeBuffer") != nullptr)
            {
                buffer = window;
                break;
            }
        }
        Check(buffer != nullptr, "code child window is visible");
        const ImVec2 codePoint = buffer->InnerRect.GetCenter();
        io.AddMousePosEvent(codePoint.x, codePoint.y);
        tick();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        tick();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        tick();
        tick();
        Check(GImGui->OpenPopupStack.empty(), "clicking outside dismisses the file menu");
        Check(GImGui->HoveredWindow == buffer, "the context click targets the visible code child");
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
        tick();
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
        for (Int32 frame = 0; frame < 4; ++frame)
        {
            tick();
            Check(!GImGui->OpenPopupStack.empty(), "the code context menu remains usable after dock focus completes");
        }
        io.AddKeyEvent(ImGuiKey_Escape, true);
        tick();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        tick();
        EditorPanel* canvas = editor.FindPanel("CanvasView");
        Check(canvas != nullptr, "canvas panel exists");
        canvas->RequestFocus();
        for (Int32 frame = 0; frame < 4; ++frame)
        {
            tick();
        }
        Check(canvas->IsFocused(), "another panel can take focus after the code dock is shown");
        documents[0]->RequestFocus();
        for (Int32 frame = 0; frame < 4; ++frame)
        {
            tick();
        }
        Check(documents[0]->IsFocused(), "a new request can focus the code document again");
        ImGui::DockContextQueueUndockWindow(GImGui, dock);
        documents[0]->RequestFocus();
        for (Int32 frame = 0; frame < 6; ++frame)
        {
            tick();
        }
        Check(dock->DockNode == nullptr, "code container can float outside the root dock");
        ImGui::FocusWindow(root);
        for (Int32 frame = 0; frame < 4; ++frame)
        {
            tick();
        }
        Check(GImGui->NavWindow == root, "a floating container also releases its completed focus request");
        editor.Shutdown();
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
        for (Int32 frame = 0; frame < 8; ++frame)
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
    TestCodeEditorGuideBoundary();
    TestTextHistoryAndDelete();
    TestLineNumberInteraction();
    TestPopupUsesCallerLayout();
    TestDelegatedPopupOwnership();
    TestWidgetMenuUsesCallerFont();
    TestExternalClipboardOwnership();
    TestDiagnosticTooltipsUseCommonDelay();
    TestSourceAssistanceWidget();
    TestAtomicSourceEditsAndAnchors();
    TestExactNativeAnchorChanges();
    TestHighlightingAndUtf16();
    TestUndoHasOneOwner();
    TestRealLanguageServer();
    TestHiddenDockReleasesFocus();
    TestEngineDocuments();
    std::cout << "Code editor: protocol, text/history, highlighting, real server and engine documents passed\n";
    return 0;
}
