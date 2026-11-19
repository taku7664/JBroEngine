#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Code/LanguageClient.h>
#include <JBro/Editor/EditorActionRegistry.h>
#include <imgui_internal.h>
#include "../Modules/JBroEditor/Source/Panel/CodeEditorPanel.h"
#include "../Modules/JBroEditor/Source/Panel/ScriptSummaryPanel.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    using namespace JBro;
    namespace fs = std::filesystem;
    void Check(Bool condition, const char* message)
    {
        if (!condition)
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
        fs::path root = fs::temp_directory_path() / ("JBroSummaryIntegration-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Files()
        {
            fs::create_directories(root / "Contents/Scripts");
            fs::create_directories(root / "Assets");
            Write("Game.jproject", "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nRootPath: .\n"
                "AssetDirectory: Assets\nScriptSourceDirectory: Contents\nScriptOutputLibraryPath: \"\"\n");
        }
        void Write(const char* name, const String& contents)
        {
            std::ofstream file(root / name, std::ios::binary);
            file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            Check(file.good(), "the summary integration fixture is written");
        }
        String Read(const char* name)
        {
            std::ifstream file(root / name, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file), {});
        }
        String Path(const char* name) const { return Utf8(root / name); }
    };
    ImGuiWindow* PanelWindow(const EditorPanel& panel)
    {
        String name = String(panel.GetDisplayTitle()) + "###" + panel.GetTitle();
        if (panel.GetKind() == EditorPanelKind::Instance)
        {
            char identity[Uuid::TextCapacity]{};
            panel.GetId().ToText(identity, sizeof(identity));
            name += String("/") + identity;
        }
        return ImGui::FindWindowByName(name.c_str());
    }
    Bool SourceTabSelected(const CodeEditorPanel& document)
    {
        const auto* window = PanelWindow(document);
        if (!document.IsVisible() || window == nullptr || !window->Active || window->Hidden)
        {
            return false;
        }
        if (!window->DockIsActive)
        {
            return true;
        }
        const auto* dock = window->DockNode;
        return window->DockTabIsVisible && dock != nullptr && dock->VisibleWindow == window
            && dock->TabBar != nullptr && dock->TabBar->SelectedTabId == window->TabId;
    }
    Bool NavRootIs(const EditorPanel& panel)
    {
        const auto* state = ImGui::GetCurrentContext();
        const auto* nav = state == nullptr ? nullptr : state->NavWindow;
        return nav != nullptr && nav->RootWindow == PanelWindow(panel);
    }
    Bool SourceTabFocused(EditorApplication& editor, const CodeEditorPanel& document)
    {
        return document.IsFocused() && SourceTabSelected(document) && NavRootIs(document)
            && CodeEditorPanel::FindActive(editor) == &document;
    }
    void ReportCodeFocus(EditorApplication& editor, const char* phase)
    {
        const auto* state = ImGui::GetCurrentContext();
        const auto* nav = state == nullptr ? nullptr : state->NavWindow;
        const auto* active = CodeEditorPanel::FindActive(editor);
        std::cout << "  code focus [" << phase << "]: active=" << (active == nullptr ? "none" : active->GetPath().c_str())
            << " nav=" << (nav == nullptr ? "none" : nav->Name)
            << " root=" << (nav == nullptr || nav->RootWindow == nullptr ? "none" : nav->RootWindow->Name)
            << " dockRoot=" << (nav == nullptr || nav->RootWindowDockTree == nullptr ? "none" : nav->RootWindowDockTree->Name)
            << " AppFocusLost=" << (state != nullptr && state->IO.AppFocusLost)
            << " ActiveId=" << (state == nullptr ? 0 : state->ActiveId) << std::endl;
        Array<EditorPanel*> panels;
        editor.FindPanels(CodeEditorPanel::TypeName, panels);
        for (auto* panel : panels)
        {
            const auto* document = static_cast<CodeEditorPanel*>(panel);
            const auto* window = state == nullptr ? nullptr : PanelWindow(*document);
            const auto* dock = window == nullptr ? nullptr : window->DockNode;
            std::cout << "    document=" << document->GetPath() << " focused=" << document->IsFocused().Get()
                << " visible=" << document->IsVisible().Get() << " window=" << (window == nullptr ? "none" : window->Name)
                << " windowActive=" << (window != nullptr && window->Active)
                << " hidden=" << (window != nullptr && window->Hidden)
                << " dockTabVisible=" << (window != nullptr && window->DockTabIsVisible)
                << " tab=" << (window == nullptr ? 0 : window->TabId)
                << " dock=" << (dock == nullptr ? 0 : dock->ID)
                << " selected=" << (dock == nullptr || dock->TabBar == nullptr ? 0 : dock->TabBar->SelectedTabId)
                << " nextSelected=" << (dock == nullptr || dock->TabBar == nullptr ? 0 : dock->TabBar->NextSelectedTabId)
                << " visibleWindow=" << (dock == nullptr || dock->VisibleWindow == nullptr ? "none" : dock->VisibleWindow->Name)
                << std::endl;
        }
    }
    template<class Predicate>
    void Pump(EditorApplication& editor, Predicate done, const char* message)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
        while (!done() && std::chrono::steady_clock::now() < deadline)
        {
            Check(editor.Tick(1.0f / 60.0f), "the actual editor draws summary integration frames");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!done())
        {
            ReportCodeFocus(editor, message);
        }
        Check(done(), message);
    }
    Bool Same(Code::Position a, Code::Position b)
    {
        return a.line == b.line && a.character == b.character;
    }
    CodeEditorPanel* Document(EditorApplication& editor, const String& path)
    {
        Array<EditorPanel*> panels;
        editor.FindPanels(CodeEditorPanel::TypeName, panels);
        for (auto* panel : panels)
        {
            auto* document = static_cast<CodeEditorPanel*>(panel);
            if (document->GetPath() == Code::LanguageClient::NormalizePath(path))
            {
                return document;
            }
        }
        return nullptr;
    }
    const Code::ScriptSummary& CurrentSummary(EditorApplication& editor, CodeEditorPanel& document)
    {
        auto& client = *document.GetLanguageClient();
        Pump(editor, [&] {
            const auto* summary = client.GetSummary(document.GetPath());
            return client.GetState() == Code::ClientState::Ready && summary != nullptr && summary->valid
                && summary->version == client.GetVersion(document.GetPath());
        }, "the actual language server returns the current summary");
        return *client.GetSummary(document.GetPath());
    }
    void EditInitializer(EditorApplication& editor, CodeEditorPanel& document, const String& value)
    {
        const auto& summary = CurrentSummary(editor, document);
        Check(summary.types.Size() == 1 && !summary.types[0].members.IsEmpty(), "the summary has its source field");
        const auto field = summary.types[0].members[0];
        auto& client = *document.GetLanguageClient();
        Code::FieldSettings settings;
        settings.replaceInitializer = true;
        settings.initializerPresent = true;
        settings.initializer = value;
        settings.attributes = field.attributes;
        const UInt64 request = client.EditFieldSettings(document.GetPath(), summary.version, field.selectionRange.start, settings);
        Check(request != 0, "the current source initializer edit is requested");
        Pump(editor, [&] {
            const auto* result = client.GetFieldEditResult(document.GetPath());
            return result != nullptr && result->requestId == request && result->ready;
        }, "the actual server completes the source edit");
        const auto result = *client.GetFieldEditResult(document.GetPath());
        Check(result.success && document.ApplySourceEdits(result.version, result.edits), "the actual buffer applies the validated source ranges");
    }
    void TestCodeActions(EditorApplication& editor, CodeEditorPanel& document, Files& files)
    {
        struct ClipboardRestore
        {
            ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
            decltype(platform.Platform_GetClipboardTextFn) originalGet = platform.Platform_GetClipboardTextFn;
            decltype(platform.Platform_SetClipboardTextFn) originalSet = platform.Platform_SetClipboardTextFn;
            void* originalUser = platform.Platform_ClipboardUserData;
            String text;
            ClipboardRestore()
            {
                platform.Platform_ClipboardUserData = &text;
                platform.Platform_GetClipboardTextFn = [](ImGuiContext* context) {
                    return static_cast<String*>(context->PlatformIO.Platform_ClipboardUserData)->c_str();
                };
                platform.Platform_SetClipboardTextFn = [](ImGuiContext* context, const char* value) {
                    *static_cast<String*>(context->PlatformIO.Platform_ClipboardUserData) = value;
                };
            }
            ~ClipboardRestore()
            {
                platform.Platform_GetClipboardTextFn = originalGet;
                platform.Platform_SetClipboardTextFn = originalSet;
                platform.Platform_ClipboardUserData = originalUser;
            }
        } clipboard;
        EditorActionContext context;
        context.editor = &editor;
        context.panel = &document;
        auto action = [&](const char* name) -> const EditorActionInfo& {
            const auto* found = EditorActionRegistry::Get().Find(name);
            Check(found != nullptr && String(found->panelType) == CodeEditorPanel::TypeName, "code editing actions share the panel scope");
            return *found;
        };
        auto execute = [&](const char* name) {
            Check(EditorActionUi::Execute(action(name), context), "the shared menu action executes");
        };
        for (const char* name : {"code_editor.save", "code_editor.reload", "code_editor.external", "code_editor.close",
            "code_editor.undo", "code_editor.redo", "code_editor.cut", "code_editor.copy", "code_editor.paste", "code_editor.select_all",
            "code_editor.debug_start", "code_editor.debug_continue", "code_editor.debug_stop"})
        {
            const auto& registered = action(name);
            Check(registered.WhyBlocked != nullptr && registered.icon != nullptr,
                "every source menu and toolbar action supplies the common disabled reason and icon");
            if (!EditorActionUi::CanExecute(registered, context))
            {
                const char* reason = EditorActionUi::WhyBlocked(registered, context);
                Check(reason != nullptr && reason[0] != '\0', "every disabled source action explains its state");
            }
        }
        Bool typingBefore = false, typingPressed = false, focusedBefore = false;
        ImGuiID activeBefore = 0, textInputBefore = 0;
        auto chord = [&](ImGuiKey key) {
            auto& io = ImGui::GetIO();
            typingBefore = io.WantTextInput;
            focusedBefore = document.IsFocused();
            activeBefore = GImGui->ActiveId;
            textInputBefore = GImGui->InputTextState.ID;
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(key, true);
            Check(editor.Tick(1.0f / 60.0f), "the code shortcut key is pressed");
            typingPressed = io.WantTextInput;
            io.AddKeyEvent(key, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            Check(editor.Tick(1.0f / 60.0f), "the code shortcut key is released");
        };
        document.FocusAt(3);
        Pump(editor, [&] { return document.IsFocused(); }, "code actions receive the source panel focus");
        auto& shortcuts = editor.GetShortcuts();
        Check(shortcuts.SetBinding("code_editor.undo", 0, {ImGuiKey_U, true})
            && shortcuts.SetBinding("code_editor.paste", 0, {ImGuiKey_B, true}), "the isolated editor remaps source undo and paste");
        Check(std::strcmp(EditorActionUi::Keys(editor, action("code_editor.undo")).value, "Ctrl+U") == 0
            && std::strcmp(EditorActionUi::Keys(editor, action("code_editor.paste")).value, "Ctrl+B") == 0,
            "menu shortcuts display the current user bindings");
        for (const char* name : {"code_editor.cut", "code_editor.copy", "code_editor.paste", "code_editor.select_all"})
        {
            Check(action(name).whileTyping && action(name).duringGame,
                "source clipboard shortcuts remain available when the code buffer requests IME text input");
        }
        const String source = document.GetBufferText();
        auto& language = *document.GetLanguageClient();
        const Int32 version = language.GetVersion(document.GetPath());
        const Code::Range word{{2, 8}, {2, 13}};
        document.GetBuffer().SelectRange({word.start, word.start});
        execute("code_editor.copy");
        Check(String(ImGui::GetClipboardText()).Contains("Int count = 9") && language.GetVersion(document.GetPath()) == version,
            "copy without a selection retains native current-line copying without editing the source");
        document.GetBuffer().SelectRange(word);
        execute("code_editor.copy");
        Check(String(ImGui::GetClipboardText()) == "count" && language.GetVersion(document.GetPath()) == version,
            "copy uses the selected source string without changing its version");
        ImGui::SetClipboardText("legacy copy probe");
        chord(ImGuiKey_Insert);
        Check(String(ImGui::GetClipboardText()) == "count" && language.GetVersion(document.GetPath()) == version,
            "the registered Ctrl+Insert secondary binding preserves native source copying");
        execute("code_editor.select_all");
        Check(document.GetBuffer().HasSelection() && Same(document.GetBuffer().GetSelection().start, {})
            && document.GetBufferText() == source && language.GetVersion(document.GetPath()) == version,
            "select all changes selection without editing the source");
        document.GetBuffer().SelectRange(word);
        const UInt64 history = document.GetBuffer().GetHistoryIndex();
        execute("code_editor.cut");
        Check(document.GetBufferText() == String(source).ReplaceAll("count", "") && document.GetBuffer().GetHistoryIndex() == history + 1,
            "the shared cut action creates one source history transaction");
        chord(ImGuiKey_Z);
        Check(document.GetBufferText() != source, "remapping undo disables its old fixed shortcut");
        chord(ImGuiKey_U);
        Check(document.GetBufferText() == source && document.GetBuffer().GetHistoryIndex() == history,
            "the remapped undo shortcut restores the complete menu cut in one step");
        ImGui::SetClipboardText("X");
        document.GetBuffer().SelectRange(word);
        chord(ImGuiKey_V);
        Check(document.GetBufferText() == source, "remapping paste disables its old vendor shortcut");
        chord(ImGuiKey_B);
        if (document.GetBufferText() != String(source).ReplaceAll("count", "X") || document.GetBuffer().GetHistoryIndex() != history + 1)
        {
            const auto selection = document.GetBuffer().GetSelection();
            std::cout << "paste diagnostic: history=" << document.GetBuffer().GetHistoryIndex().Get() << " expected=" << history.Get() + 1
                << " focused(before/current)=" << focusedBefore.Get() << '/' << document.IsFocused().Get()
                << " WantTextInput(before/pressed/current)=" << typingBefore.Get() << '/' << typingPressed.Get() << '/' << ImGui::GetIO().WantTextInput
                << " ActiveId=" << activeBefore << " InputTextState.ID=" << textInputBefore
                << " selection=" << selection.start.line.Get() << ':' << selection.start.character.Get() << '-'
                << selection.end.line.Get() << ':' << selection.end.character.Get()
                << " CanPaste=" << document.GetBuffer().CanPaste().Get() << " clipboard=" << ImGui::GetClipboardText()
                << "\nsource-before:\n" << source << "source-after:\n" << document.GetBufferText() << std::endl;
        }
        Check(document.GetBufferText() == String(source).ReplaceAll("count", "X") && document.GetBuffer().GetHistoryIndex() == history + 1,
            "the remapped paste runs once without a second vendor insertion");
        chord(ImGuiKey_U);
        Check(document.GetBufferText() == source, "one undo restores the remapped paste");
        document.GetBuffer().SelectRange(word);
        execute("code_editor.paste");
        Check(document.GetBufferText() == String(source).ReplaceAll("count", "X"), "menu paste uses the same source edit as its shortcut");
        execute("code_editor.undo");
        Check(document.GetBufferText() == source, "menu undo restores the same transaction as the remapped shortcut");

        document.GetBuffer().SelectRange(word);
        const Int32 beforeTyping = language.GetVersion(document.GetPath());
        char field[32] = "summary expression";
        for (Int32 frame = 0; frame < 4; ++frame)
        {
            auto& io = ImGui::GetIO();
            if (frame == 3)
            {
                Check(io.WantTextInput, "the actual text field owns text input before the clipboard shortcut");
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_A, true);
            }
            ImGui::NewFrame();
            ImGui::Begin("Source clipboard typing guard", nullptr, ImGuiWindowFlags_NoSavedSettings);
            if (frame < 2)
            {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputText("Expression", field, sizeof(field));
            shortcuts.SetFocusedScope(CodeEditorPanel::TypeName);
            if (frame == 3)
            {
                shortcuts.ProcessInput(editor, io.WantTextInput, false);
            }
            ImGui::End();
            ImGui::Render();
        }
        ImGui::GetIO().AddKeyEvent(ImGuiKey_A, false);
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
        Check(editor.Tick(1.0f / 60.0f), "the text field releases its clipboard shortcut");
        Check(document.GetBufferText() == source && language.GetVersion(document.GetPath()) == beforeTyping
            && Same(document.GetBuffer().GetSelection().start, word.start) && Same(document.GetBuffer().GetSelection().end, word.end),
            "text field select-all cannot select or mutate the source buffer even when its scope remains active");

        files.Write("ReadOnly.jscript", "script ReadOnly { Int value = 1 }\n");
        Check(editor.OpenCodeFile(files.Path("ReadOnly.jscript")), "an outside-script-root source opens read only");
        auto* readOnly = Document(editor, files.Path("ReadOnly.jscript"));
        Check(readOnly != nullptr && readOnly->IsReadOnly(), "the source panel enforces its read-only boundary");
        context.panel = readOnly;
        readOnly->GetBuffer().SelectRange({{0, 7}, {0, 15}});
        Check(EditorActionUi::CanExecute(action("code_editor.copy"), context)
            && EditorActionUi::CanExecute(action("code_editor.select_all"), context)
            && !EditorActionUi::CanExecute(action("code_editor.cut"), context)
            && !EditorActionUi::CanExecute(action("code_editor.paste"), context)
            && !EditorActionUi::CanExecute(action("code_editor.undo"), context),
            "read-only source permits copying and selecting while blocking edits and source undo");
        Check(EditorActionUi::WhyBlocked(action("code_editor.cut"), context) != nullptr
            && EditorActionUi::WhyBlocked(action("code_editor.paste"), context) != nullptr,
            "read-only source clipboard menu items explain the same condition that blocks their shortcuts");
    }
    void TestEngineSummaryDocuments()
    {
        Files files;
        const String normalized = "script First\n{\n    [prop, name(\"\xed\x95\x9c\xf0\x9f\x98\x80\"), range(0.0, 10.0)] Float speed = 3.0 // keep\n"
            "    fn OnUpdate() override\n    {\n        Float step = speed * 0.016\n    }\n"
            "    fn Shadow()\n    {\n        Float speed = 7.0\n    }\n}\n";
        const String original = String("\xef\xbb\xbf") + String(normalized).ReplaceAll("\n", "\r\n");
        const String other = "script Second\n{\n    Int count = 7\n}\n";
        files.Write("Contents/Scripts/First.jscript", original);
        files.Write("Contents/Scripts/Second.jscript", other);
        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1200;
        config.windowHeight = 760;
        const String preferences = files.Path("EditorPreferences.yaml");
        config.preferencesPath = preferences.c_str();
        Check(editor.Initialize(config), "the actual editor initializes for summary document integration");
        struct Cleanup
        {
            EditorApplication& editor;
            ~Cleanup() { editor.Shutdown(); }
        } cleanup{editor};
        ProjectFileError error;
        Check(editor.OpenProjectFile(files.Path("Game.jproject").c_str(), error) && editor.EnableEditorUi({64, 48}),
            "the project and native code dock open");
        Check(editor.OpenCodeFile(files.Path("Contents/Scripts/First.jscript"))
            && editor.OpenCodeFile(files.Path("Contents/Scripts/Second.jscript")), "two actual JBroScript tabs open");
        auto* first = Document(editor, files.Path("Contents/Scripts/First.jscript"));
        auto* second = Document(editor, files.Path("Contents/Scripts/Second.jscript"));
        auto* summaryPanel = editor.FindPanel(ScriptSummaryPanel::TypeName);
        Check(first != nullptr && second != nullptr && summaryPanel != nullptr, "the two buffers and shared summary panel exist");
        first->FocusAt(3);
        Check(editor.Tick(1.0f / 60.0f), "the first source focus request receives one actual frame");
        summaryPanel->RequestFocus();
        Check(editor.Tick(1.0f / 60.0f), "the immediate summary focus request receives one actual frame");
        Pump(editor, [&] {
            return SourceTabSelected(*first) && CodeEditorPanel::FindActive(editor) == first
                && summaryPanel->IsFocused() && NavRootIs(*summaryPanel);
        }, "rapid source-to-summary focus retains the selected first source tab and actual summary focus");
        CurrentSummary(editor, *second);
        first->FocusAt(3);
        Pump(editor, [&] { return SourceTabFocused(editor, *first); },
            "the actual first script tab receives focus before switching to its summary");
        const auto firstSummary = CurrentSummary(editor, *first);
        Check(first->GetText() == normalized, "the .jscript BOM and CRLF are normalized only in memory");
        Check(SourceTabFocused(editor, *first), "the selected visible source tab and native focus agree before summary focus");
        summaryPanel->RequestFocus();
        for (Int32 frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(1.0f / 60.0f), "the summary receives focus");
        }
        Check(SourceTabSelected(*first) && CodeEditorPanel::FindActive(editor) == first && NavRootIs(*summaryPanel),
            "summary focus retains the selected visible JBroScript target");
        const auto definition = firstSummary.types[0].members[0].selectionRange;
        first->SelectDefinition(definition);
        Pump(editor, [&] {
            const auto selection = first->GetBuffer().GetSelection();
            return first->GetBuffer().HasSelection() && Same(selection.start, definition.start) && Same(selection.end, definition.end);
        }, "summary navigation selects only the definition name using UTF16 source coordinates");
        auto& language = *first->GetLanguageClient();
        Pump(editor, [&] {
            const auto* result = language.GetReferences(first->GetPath());
            return result != nullptr && result->ready && result->success;
        }, "definition selection requests actual semantic references");
        const auto* references = language.GetReferences(first->GetPath());
        Check(references->ranges.Size() == 1 && references->ranges[0].start.line == 5,
            "only the field use is highlighted, excluding its declaration and a same-named local");
        const Int32 otherVersion = language.GetVersion(second->GetPath());
        EditInitializer(editor, *first, "4.5");
        Check(first->HasUnsavedChanges() && second->GetBufferText() == other
            && language.GetVersion(second->GetPath()) == otherVersion, "summary edits affect only the active script buffer");
        Check(files.Read("Contents/Scripts/First.jscript") == original, "summary edits do not automatically save the source");
        first->Undo();
        Check(first->GetBufferText() == normalized && !first->HasUnsavedChanges(), "one source undo restores the complete summary gesture");
        first->Redo();
        Check(first->Save(), "the explicitly saved .jscript buffer is written");
        const String saved = String("\xef\xbb\xbf") + String(normalized).ReplaceAll("3.0", "4.5").ReplaceAll("\n", "\r\n");
        Check(files.Read("Contents/Scripts/First.jscript") == saved, "summary editing and saving preserve .jscript BOM, CRLF, Unicode and comments");
        second->FocusAt(3);
        Pump(editor, [&] { return SourceTabFocused(editor, *second); },
            "the actual second script tab receives focus before switching to its summary");
        summaryPanel->RequestFocus();
        Check(editor.Tick(1.0f / 60.0f) && SourceTabSelected(*second) && CodeEditorPanel::FindActive(editor) == second
            && NavRootIs(*summaryPanel),
            "the summary follows a second .jscript tab without changing back on focus");
        for (const auto* suffix : {"save", "undo", "redo"})
        {
            const auto* summaryAction = EditorActionRegistry::Get().Find((String("script_summary.") + suffix).c_str());
            const auto* sourceAction = EditorActionRegistry::Get().Find((String("code_editor.") + suffix).c_str());
            Check(summaryAction != nullptr && sourceAction != nullptr && summaryAction->WhyBlocked != nullptr
                && summaryAction->icon != nullptr, "summary actions retain the common icon and disabled reason");
            EditorActionContext summaryContext;
            summaryContext.editor = &editor;
            summaryContext.panel = summaryPanel;
            EditorActionContext sourceContext = summaryContext;
            sourceContext.panel = second;
            Check(!EditorActionUi::CanExecute(*summaryAction, summaryContext)
                && !EditorActionUi::CanExecute(*sourceAction, sourceContext)
                && String(EditorActionUi::WhyBlocked(*summaryAction, summaryContext))
                    == EditorActionUi::WhyBlocked(*sourceAction, sourceContext),
                "unchanged source and summary actions explain the same blocked state on the retained document");
        }
        EditInitializer(editor, *second, "8");
        Check(first->GetBufferText().Contains("speed = 4.5") && !first->HasUnsavedChanges()
            && second->GetBufferText().Contains("count = 8"), "a second tab edit cannot change the first document");
        editor.ReconnectCodeLanguageClient();
        Check(!language.GetSummary(second->GetPath())->valid, "reconnect locks the old summary immediately");
        const auto restored = CurrentSummary(editor, *second);
        Check(restored.types[0].members[0].initializer == "8", "reconnect restores the current unsaved script summary");
        EditInitializer(editor, *second, "9");
        Check(second->GetBufferText().Contains("count = 9") && files.Read("Contents/Scripts/Second.jscript") == other,
            "current summary editing resumes after a real server reconnect without autosaving");
        TestCodeActions(editor, *second, files);
        editor.CloseProject();
        std::cout << "  Native summary tabs, focus, name selection, semantic uses, Unicode save and reconnect verified\n";
    }
}
JBro::Int32 RunScriptSummaryIntegrationTests()
{
    TestEngineSummaryDocuments();
    return 0;
}
