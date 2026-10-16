#include <JBro/Editor/Document/Markdown.h>
#include <JBro/Editor/Widget/MarkdownView.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorPanelRegistry.h>
#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Task/TaskManager.h>
#include "../Modules/JBroEditor/Source/Panel/MarkdownPanel.h"
#include <TextEditor.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <stdexcept>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cmath>
#include <Windows.h>

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
    class CloseAfterDocumentDraw final : public InstancePanel
    {
    public:
        static constexpr const char* TypeName = "MarkdownCloseAfterDrawTest";
        const char* GetTitle() const override { return TypeName; }
        EditorDock GetPreferredDock() const override { return EditorDock::Right; }
        Bool OnCreate(EditorApplication& editor) override { app = &editor; return true; }
        void OnDraw() override
        {
            if (auto* panel = app->FindPanel(target))
            {
                if (panel->IsVisible())
                {
                    EditorActionContext context;
                    context.editor = app;
                    context.panel = panel;
                    const auto* close = EditorActionRegistry::Get().Find("markdown.close");
                    Check(close && close->CanExecute(context) && close->Execute(context), "shared document close action executes after drawing");
                    closed = true;
                }
            }
        }
        EditorApplication* app = nullptr;
        Uuid target;
        Bool closed = false;
    };
    void Model()
    {
        Markdown::Document doc;
        Check(Markdown::Parse("\xef\xbb\xbf# Hello World\r\n\r\n# Hello World\r\n\r\n"
            "**bold** *italic* ~~strike~~ &amp; &#x1f600; &NotEqualTilde; &unknown;\n"
            "<script>alert(1)</script>\n\n[visible](javascript:bad) ![alt](local.png)\n\n"
            "| left | right |\n| :--- | ---: |\n| one | two |\n\n"
            "- [x] done\n  - nested\n\n```cpp\nint x = 2;\n\treturn x;\n```\n", doc), "parse full fixture");
        Check(doc.blocks[1].anchor == "hello-world" && doc.blocks[2].anchor == "hello-world-1", "duplicate heading anchors");
        Check(doc.text.Contains("bold italic strike & \xf0\x9f\x98\x80") && doc.text.Contains("&unknown;"), "entities and visible text");
        Check(doc.text.Contains("<script>alert(1)</script>"), "HTML is literal text");
        Check(doc.text.Contains("left\tright\none\ttwo\n"), "table clipboard separators");
        Bool task = false, nesting = false, language = false, link = false, image = false;
        for (const auto& block : doc.blocks)
        {
            task = task || (block.task && block.checked);
            nesting = nesting || (block.kind == Markdown::Kind::UnorderedList && doc.blocks[block.parent.Get()].kind == Markdown::Kind::Item);
            language = language || (block.kind == Markdown::Kind::Code && block.language == "cpp"
                && Markdown::Copy(doc, block.begin, block.end) == "int x = 2;\n\treturn x;\n");
            for (const auto& run : block.runs)
            {
                link = link || run.target == "javascript:bad";
                image = image || (run.image && run.target == "local.png");
            }
        }
        Check(task, "checked task model");
        Check(nesting, "nested list model");
        Check(language, "code language and exact clipboard contents");
        Check(link, "link model");
        Check(image, "image model");
        Check(Markdown::Find(doc, "return x").Size() == 1, "search covers code");
        Check(Markdown::Copy(doc, 0, static_cast<UInt32>(doc.text.size())) == doc.text, "whole body copy");
        const String korean = "\xed\x95\x9c\xea\xb8\x80 \xf0\x9f\x98\x80";
        Check(Markdown::Boundary(korean, 2) == 0 && Markdown::Boundary(korean, 9) == 7, "UTF8 boundaries");
        Check(Markdown::Anchor(" A B ") == "a-b"
            && Markdown::Anchor(korean) == "\xed\x95\x9c\xea\xb8\x80-\xf0\x9f\x98\x80", "ASCII and Korean anchors");
        Check(Markdown::DecodeTarget("%ED%95%9C%EA%B8%80") == "\xed\x95\x9c\xea\xb8\x80"
            && Markdown::DecodeTarget("bad%00path").empty(), "URL decoding preserves UTF8 and rejects embedded NUL");
        Check(Markdown::IsExternalLink("HTTPS://example.com") && Markdown::IsExternalLink("MAILTO:example@example.com")
            && !Markdown::IsExternalLink("javascript:alert(1)"), "external scheme allowlist");
        Check(Markdown::Parse("**unfinished [text](\n\n~~~\ncode\n", doc), "unfinished markup remains renderable");
        String longSource;
        for (UInt32 line = 0; line < 10000; ++line)
        {
            longSource += "A paragraph with searchable text.\n\n";
        }
        Check(Markdown::Parse(longSource, doc) && Markdown::Find(doc, "searchable").Size() == 10000, "ten thousand paragraphs");
    }
    void Snapshot()
    {
        TextEditor editor;
        editor.SetText("int value = 1;\n// comment\n");
        editor.SetLanguage(TextEditor::Language::Cpp());
        const auto before = editor.GetText();
        const auto undo = editor.GetUndoIndex();
        const auto spans = editor.GetColorizedSpans();
        Bool declaration = false, comment = false;
        for (const auto& span : spans)
        {
            declaration = declaration || span.color == TextEditor::Color::declaration || span.color == TextEditor::Color::keyword;
            comment = comment || span.color == TextEditor::Color::comment;
        }
        Check(declaration && comment, "snapshot reuses C++ tokenizer");
        Check(before == editor.GetText() && undo == editor.GetUndoIndex(), "snapshot preserves document and history");
    }
    struct ViewContext
    {
        ViewContext()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {800, 600};
            io.DeltaTime = 1.0f / 60.0f;
            io.Fonts->AddFontDefault();
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            io.Fonts->SetTexID(1);
        }
        ~ViewContext() { ImGui::DestroyContext(); }
    };
    struct ViewHarness
    {
        ViewContext context;
        Widget::MarkdownView view;
        void Load(const String& source)
        {
            Markdown::Document document;
            Check(Markdown::Parse(source, document), "regression document parses");
            view.SetDocument(std::move(document));
        }
        void Render(Float width = 700.0f, Float height = 400.0f)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({width.Get(), height.Get()});
            ImGui::Begin("Markdown regression", nullptr, ImGuiWindowFlags_NoSavedSettings);
            view.Render("regression");
            ImGui::End();
            ImGui::Render();
        }
        ImGuiWindow& Body()
        {
            for (auto* window : ImGui::GetCurrentContext()->Windows)
            {
                if (String(window->Name).Contains("##body"))
                {
                    return *window;
                }
            }
            throw std::runtime_error("Markdown body is missing");
        }
    };
    void MixedListBlocks()
    {
        ViewHarness harness;
        const String item = "- BEFORE\n  ```text\n  CODE\n  ```\n  AFTER\n";
        harness.Load(item);
        Check(harness.view.GetDocument().text == "BEFORE\nCODE\nAFTER\n", "tight-list copy preserves block boundaries and source order");
        String source;
        for (UInt32 row = 0; row < 30; ++row)
        {
            source += "padding\n\n";
        }
        source += item + "\n";
        for (UInt32 row = 0; row < 30; ++row)
        {
            source += "padding\n\n";
        }
        harness.Load(source);
        harness.Render();
        harness.Render();
        const auto position = [&](const String& query) -> Float {
            harness.view.SetSearch(query);
            harness.Render();
            harness.Render();
            return harness.Body().Scroll.y;
        };
        const Float before = position("BEFORE"), code = position("CODE"), after = position("AFTER");
        Check(before < code && code < after, "tight-list rendering follows before/code/after source order");
        harness.Load("- **before**\n  > quote\n\n  ![alt](image.png)\n\n  after\n");
        Check(harness.view.GetDocument().text.Contains("before\nquote\nalt\nafter\n"), "nested quote and image keep their logical boundaries");
    }
    void HorizontalSearch()
    {
        ViewHarness harness;
        harness.Load("```text\nSTART" + String(300, 'x') + "NEEDLE\n```\n");
        harness.Render();
        harness.Render();
        harness.view.SetSearch("NEEDLE");
        harness.Render();
        harness.Render();
        Check(harness.view.MatchCount() == 1 && harness.Body().ScrollMax.x > 1000, "long code line has a searchable offscreen match");
        Check(harness.Body().Scroll.x > 1000, "search reveals the match on the horizontal axis");
        harness.view.SetSearch("START");
        harness.Render();
        harness.Render();
        Check(harness.Body().Scroll.x < 50, "search returns to a match at the start of the line");
        harness.Render(430.0f);
        harness.view.SetSearch("NEEDLE");
        harness.Render(430.0f);
        harness.Render(430.0f);
        Check(harness.Body().Scroll.x > 1000, "horizontal search remains correct after reflow");
        harness.Load("```text\n" + String(300, 'x') + "NEEDLE\n```\n");
        harness.view.SetSearch("NEEDLE");
        harness.Render();
        harness.Render();
        harness.Render();
        Check(harness.Body().Scroll.x > 1000, "a search requested before layout reveals the new document's match");
    }
    void LocalImageSizing()
    {
        ViewHarness harness;
        Widget::MarkdownImage image;
        image.texture = {123, 1};
        Widget::MarkdownCallbacks callbacks;
        callbacks.user = &image;
        callbacks.image = [](void* user, const String&) { return *static_cast<Widget::MarkdownImage*>(user); };
        harness.view.SetCallbacks(callbacks);
        harness.Load("![alt](local.png)\n");
        for (const auto extent : {Extent2D{288, 64}, Extent2D{1200, 300}, Extent2D{32, 256}})
        {
            image.width = extent.width;
            image.height = extent.height;
            harness.view.InvalidateLayout();
            harness.Render(430.0f);
            harness.Render(430.0f);
            Bool found = false;
            for (const auto* list : ImGui::GetDrawData()->CmdLists)
            {
                for (const auto& command : list->CmdBuffer)
                {
                    if (command.GetTexID() != EditorUI::ToTextureId(image.texture).Get() || command.ElemCount < 6)
                    {
                        continue;
                    }
                    const auto& first = list->VtxBuffer[list->IdxBuffer[command.IdxOffset] + command.VtxOffset];
                    const auto& opposite = list->VtxBuffer[list->IdxBuffer[command.IdxOffset + 2] + command.VtxOffset];
                    const Float available = harness.Body().InnerRect.GetWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f - ImGui::GetFontSize() * 2.0f;
                    const ImVec2 box(std::min(available.Get(), static_cast<Float>(image.width).Get()), static_cast<Float>(image.height).Get());
                    const auto expected = Widget::FitInside(image.width, image.height, box);
                    Check(std::abs(opposite.pos.x - first.pos.x - expected.x) < 0.1f
                        && std::abs(opposite.pos.y - first.pos.y - expected.y) < 0.1f, "Markdown images use common fitting without stretching or upscaling");
                    found = true;
                }
            }
            Check(found, "local image contributes a textured quad");
        }
    }
    void DeepNesting()
    {
        ViewHarness harness;
        harness.Load(String(16000, '>') + " nested\n");
        Check(harness.view.GetDocument().blocks.Size() >= 16000, "deep quotes remain supported rather than silently truncated");
        harness.Render();
        harness.Render();
        harness.view.SelectAll();
        Check(harness.view.SelectedText() == "nested\n", "deep quote layout preserves selectable text without stack recursion");
    }
    void RepeatedHeadings()
    {
        String source;
        for (UInt32 heading = 0; heading < 10000; ++heading)
        {
            source += "# Same\n\n";
        }
        Markdown::Document document;
        const auto start = std::chrono::steady_clock::now();
        Check(Markdown::Parse(source, document), "ten thousand repeated headings parse");
        const auto elapsed = std::chrono::steady_clock::now() - start;
        std::cout << "Markdown 10000 repeated headings: " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << " ms\n";
        Check(document.blocks[10000].anchor == "same-9999", "repeated heading suffixes stay deterministic");
        Check(elapsed < std::chrono::seconds(5), "repeated heading allocation must not regress to cubic rescanning");
        Check(Markdown::Parse("# Same\n\n# Same-1\n\n# Same\n\n# Same-1\n", document), "explicit suffix collisions parse");
        Check(document.blocks[3].anchor == "same-2" && document.blocks[4].anchor == "same-1-1", "generated and explicit anchor names share one collision domain");
        Check(Markdown::Parse("&#0; &#128; &#x10FFFF; &#x110000; &#xD800;", document), "numeric entities parse at Unicode boundaries");
        Check(document.text == "\xef\xbf\xbd \xe2\x82\xac \xf4\x8f\xbf\xbf \xef\xbf\xbd \xef\xbf\xbd\n", "engine integer conversion preserves numeric entity semantics");
    }
    void View()
    {
        ImGui::CreateContext();
        struct ContextCleanup { ~ContextCleanup() { ImGui::DestroyContext(); } } cleanup;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {800, 600};
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.Fonts->SetTexID(1);
        {
            Widget::MarkdownView view;
            Markdown::Document document;
            String source = "# Heading\n\nFirst **bold** paragraph.\n\n| A | B |\n|---|---|\n|cell|other|\n\n```cpp\nint value = 1;\nint other = value;\n```\n";
            for (UInt32 line = 0; line < 10000; ++line)
            {
                source += "A long document paragraph.\n\n";
            }
            Check(Markdown::Parse(source, document), "view model");
            const String logical = document.text;
            view.SetDocument(std::move(document));
            const auto render = [&](float windowWidth, float dpi = 1.0f) {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0});
                ImGui::SetNextWindowSize({windowWidth, 500});
                ImGui::Begin("Markdown test", nullptr, ImGuiWindowFlags_NoSavedSettings);
                ImGui::GetWindowViewport()->DpiScale = dpi;
                view.Render("view");
                ImGui::End();
                ImGui::Render();
            };
            render(700);
            render(700);
            const auto revision = view.LayoutRevision();
            render(700);
            Check(view.LayoutRevision() == revision, "unchanged document caches layout");
            Check(view.VisibleGlyphCount() < 2500, "offscreen glyphs culled");
            view.SelectAll();
            Check(view.HasSelection(), "nonempty document selection is available for the shared copy action");
            Check(view.SelectedText() == logical, "selection crosses paragraphs, table and code");
            render(430);
            Check(view.LayoutRevision() > revision && view.SelectedText() == logical, "reflow retains logical selection");
            view.SetSearch("cell");
            Check(view.MatchCount() == 1, "table searched");
            view.SetSearch("int value");
            Check(view.MatchCount() == 1, "code searched");
            Check(view.GoToAnchor("heading") && !view.GoToAnchor("missing"), "anchor lookup");
            view.SetSearch("");
            view.ShowFind();
            render(430);
            render(430);
            io.AddInputCharactersUTF8("value");
            render(430);
            Check(view.MatchCount() == 2, "typing into the common search box updates matches without Enter");
            render(430);
            ImGuiWindow* body = nullptr;
            for (auto* window : ImGui::GetCurrentContext()->Windows)
            {
                if (String(window->Name).Contains("##body"))
                {
                    body = window;
                    break;
                }
            }
            Check(body != nullptr, "view owns a scrollable body");
            const Float firstMatchY = body->Scroll.y;
            io.AddKeyEvent(ImGuiKey_Enter, true);
            render(430);
            io.AddKeyEvent(ImGuiKey_Enter, false);
            render(430);
            render(430);
            Check(body->Scroll.y > firstMatchY, "Enter advances the highlighted match after InputText deactivates");
            io.AddKeyEvent(ImGuiMod_Shift, true);
            io.AddKeyEvent(ImGuiKey_Enter, true);
            render(430);
            io.AddKeyEvent(ImGuiKey_Enter, false);
            io.AddKeyEvent(ImGuiMod_Shift, false);
            render(430);
            render(430);
            Check(body->Scroll.y == firstMatchY, "Shift Enter returns to the previous highlighted match");
            ImGui::GetStyle().FontScaleMain = 1.5f;
            render(430);
            Check(view.SelectedText() == logical, "font change retains selection");
            const auto beforeDpi = view.LayoutRevision();
            render(430, 1.5f);
            Check(view.LayoutRevision() > beforeDpi && view.SelectedText() == logical, "DPI change invalidates layout without changing logical selection");
        }
    }
    void Panels()
    {
        namespace fs = std::filesystem;
        struct Files
        {
            fs::path root = fs::temp_directory_path() / ("JBroMarkdown-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            Files() { fs::create_directories(root / "Assets"); }
            ~Files() { fs::remove_all(root); }
            String Path(const char* leaf)
            {
                const auto bytes = (root / leaf).generic_u8string();
                return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }
            void Write(const char* leaf, const String& text)
            {
                std::ofstream file(root / leaf, std::ios::binary);
                file.write(text.data(), text.size());
                Check(file.good(), "fixture write");
            }
        } files;
        const auto checkReleased = [&](const char* name) {
            const auto handle = CreateFileW((files.root / name).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            const Bool opened = handle != INVALID_HANDLE_VALUE;
            if (opened)
            {
                CloseHandle(handle);
            }
            Check(opened, "completed/canceled task history releases file streams immediately");
        };
        files.Write("Game.jproject", "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nRootPath: .\nAssetDirectory: Assets\nScriptOutputLibraryPath: \"\"\n");
        files.Write("One.md", "\xef\xbb\xbf# First\r\n\r\n[Second](Two.md#second)\r\n\r\n![local](checker.png)\r\n\r\n![missing](missing.png)\r\n");
        fs::copy_file(fs::path("Tests/Data/Markdown/checker.png"), files.root / "checker.png");
        files.Write("Two.md", "# Second\n\noriginal second document\n");
        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        config.taskWorkers = false;
        config.fileDialogUser = &files;
        config.fileDialog = [](const FileDialogDesc& desc, String& result, void* user) -> Bool {
            Check(!desc.pickFolder && String(desc.filterPattern) == "*.md", "Markdown open action uses the common file browser filter");
            result = static_cast<Files*>(user)->Path("One.md");
            return true;
        };
        Check(editor.Initialize(config), "editor initializes for Markdown integration");
        ProjectFileError error;
        Check(editor.OpenProjectFile(files.Path("Game.jproject").c_str(), error), "project opens");
        Check(editor.EnableEditorUi({800, 600}), "editor UI initializes");
        Check(ImGui::GetIO().Fonts->Fonts.front()->IsGlyphInFont(0x1f600), "common UI font provides supplementary emoji fallback");
        Check(EditorTheme::GetCodeFont() && EditorTheme::GetCodeFont()->IsGlyphInFont(0x1f600), "common code font uses the same supplementary fallback");
        const auto* openAction = EditorActionRegistry::Get().Find("markdown.open");
        Check(openAction && openAction->placedByEditor && openAction->menus == EditorActionMenu::File
            && openAction->panelType == nullptr, "open action is global and placed in the root File menu");
        EditorActionContext actionContext;
        actionContext.editor = &editor;
        Check(openAction->Execute(actionContext) && editor.Tick(0.02f), "common open action dispatches the file browser");
        Check(editor.OpenMarkdownFile(files.Path("Two.md"), "second"), "second Markdown tab opens");
        Check(editor.OpenMarkdownFile(files.Path("./One.md"), "first"), "normalized path reuses first tab");
        Array<EditorPanel*> panels;
        editor.FindPanels(MarkdownPanel::TypeName, panels);
        Check(panels.Size() == 2, "no duplicate document tab");
        auto* first = static_cast<MarkdownPanel*>(panels[0]);
        auto* second = static_cast<MarkdownPanel*>(panels[1]);
        const auto* registration = EditorPanelRegistry::Get().Find(MarkdownPanel::TypeName);
        Check(registration && String(registration->dockArea) == MarkdownPanel::DockAreaName, "dedicated documents dock");
        for (Int32 frame = 0; frame < 8; ++frame)
        {
            Check(editor.Tick(0.02f), "document frame");
        }
        Check(!first->IsLoading() && first->GetError().empty() && first->GetView().GetDocument().text.Contains("First"), "asynchronous BOM/CRLF load");
        EditorActionContext copyContext;
        copyContext.editor = &editor;
        copyContext.panel = first;
        const auto* copyAction = EditorActionRegistry::Get().Find("markdown.copy");
        Check(copyAction && !copyAction->CanExecute(copyContext) && copyAction->WhyBlocked(copyContext), "copy without a selection is disabled with a reason");
        first->GetView().SelectAll();
        Check(copyAction->CanExecute(copyContext) && !copyAction->WhyBlocked(copyContext), "copy becomes available for a document selection");
        first->GetView().Select(0, 0);
        checkReleased("One.md");
        checkReleased("checker.png");
        Check(first->GetView().GetDocument().text.Contains("missing"), "failed image retains alt text");
        second->GetView().SetSearch("original");
        second->GetView().SelectAll();
        first->Focus({});
        for (Int32 frame = 0; frame < 5; ++frame)
        {
            Check(editor.Tick(0.02f), "first tab focus frame");
        }
        Check(first->IsVisible() && !second->IsVisible(), "external modification starts while the second tab is hidden");
        files.Write("Two.md", "# Second\n\nreplacement second document\n");
        second->Focus({});
        for (Int32 frame = 0; frame < 6; ++frame)
        {
            Check(editor.Tick(0.02f), "tab activation frame");
        }
        Check(second->GetView().GetDocument().text.Contains("replacement"), "tab activation detects external modification");
        files.Write("Two.md", "# Second\n\nreplacement second document, latest version\n");
        second->Reload();
        second->Reload();
        for (Int32 frame = 0; frame < 5; ++frame)
        {
            Check(editor.Tick(0.02f), "reload frame");
        }
        Check(second->GetView().GetDocument().text.Contains("latest version") && second->GetView().MatchCount() == 0
            && second->GetView().SelectedText().empty(), "new version replaces source and resets selection/search matches");
        fs::remove(files.root / "Two.md");
        second->Reload();
        for (Int32 frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(0.02f), "failed read frame");
        }
        Check(!second->GetError().empty() && second->GetView().GetDocument().text.Contains("replacement"), "read failure retains last valid document");
        Check(!fs::exists(files.root / "One.md.jmeta") && !fs::exists(files.root / "missing.png.jmeta"), "viewer never imports document/image assets");
        Check(RegisterEditorPanelType<CloseAfterDocumentDraw>(false, MarkdownPanel::DockAreaName), "close-during-draw test panel registration");
        first->Focus({});
        for (Int32 frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(0.02f), "image document visible before same-frame close");
        }
        auto* closer = static_cast<CloseAfterDocumentDraw*>(editor.CreatePanel(CloseAfterDocumentDraw::TypeName));
        Check(closer != nullptr, "close-during-draw test panel creation");
        closer->target = first->GetId();
        for (Int32 frame = 0; frame < 5; ++frame)
        {
            Check(editor.Tick(0.02f), "an image drawn earlier in this frame remains valid when its document closes");
        }
        Check(closer->closed, "test closes the document after it has drawn");
        editor.ClosePanel(*closer);
        checkReleased("One.md");
        for (Int32 repetition = 0; repetition < 8; ++repetition)
        {
            Check(editor.OpenMarkdownFile(files.Path("One.md")), "repeat document open");
            if (repetition % 2 == 0)
            {
                for (Int32 frame = 0; frame < 5; ++frame)
                {
                    Check(editor.Tick(0.02f), "repeat document/image load frame");
                }
            }
            editor.FindPanels(MarkdownPanel::TypeName, panels);
            for (auto* panel : panels)
            {
                if (static_cast<MarkdownPanel*>(panel)->GetPath() == files.Path("One.md"))
                {
                    editor.ClosePanel(*panel);
                    break;
                }
            }
            checkReleased("One.md");
            checkReleased("checker.png");
        }
        editor.CloseProject();
        editor.FindPanels(MarkdownPanel::TypeName, panels);
        Check(panels.IsEmpty(), "project close cancels document/image tasks before destroying panels");
        editor.Shutdown();
    }
}
JBro::Int32 RunMarkdownTests()
{
    struct Case { const char* name; void (*run)(); };
    const Case cases[] = {{"model", Model}, {"lexical snapshot", Snapshot}, {"view", View}, {"panels", Panels},
        {"mixed list blocks", MixedListBlocks}, {"horizontal search", HorizontalSearch}, {"local image sizing", LocalImageSizing},
        {"deep nesting", DeepNesting}, {"repeated headings", RepeatedHeadings}};
    JBro::Int32 failures = 0;
    for (const auto& test : cases)
    {
        try
        {
            test.run();
            std::cout << "Markdown " << test.name << " passed\n";
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cout << "Markdown " << test.name << " failed: " << error.what() << '\n';
        }
    }
    return failures;
}
