#include <JBro/Editor/Widget/CodeEditor.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/GuideFocus.h>
#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <TextEditor.h>
#include <JBro/Types/ValueMath.h>

namespace JBro::Widget
{
    namespace
    {
        const TextEditor::Language* JBroLanguage()
        {
            static const TextEditor::Language language = [] {
                TextEditor::Language result = *TextEditor::Language::Cpp();
                result.name = "JBroScript";
                result.keywords = {"script", "class", "struct", "interface", "enum", "fn", "ref", "public", "private",
                    "protected", "static", "const", "virtual", "override", "abstract", "callback", "if", "else", "for", "in",
                    "while", "do", "switch", "case", "default", "return", "break", "continue", "new", "this", "base",
                    "and", "or", "not", "is", "null", "true", "false", "operator", "using", "namespace"};
                result.declarations = {"Int", "Int32", "Int64", "UInt32", "UInt64", "Float", "Double", "Bool", "String",
                    "Void", "Array", "Table", "Vector2", "Vector3", "Vector4", "Color", "GameObject", "Transform2D"};
                result.identifiers = {"Time", "Input", "Physics", "Math", "Debug", "Random"};
                return result;
            }();
            return &language;
        }
        ImU32 SemanticColor(UInt32 kind)
        {
            switch (kind.Get())
            {
            case 0: case 1: case 2: case 3: case 4: return IM_COL32(78, 201, 176, 255);
            case 5: return IM_COL32(181, 206, 168, 255);
            case 6: case 7: return IM_COL32(220, 220, 170, 255);
            case 8: return IM_COL32(156, 220, 254, 255);
            case 9: return IM_COL32(197, 217, 241, 255);
            case 10: return IM_COL32(228, 232, 238, 255);
            case 11: return IM_COL32(180, 180, 220, 255);
            case 12: return IM_COL32(197, 134, 192, 255);
            case 13: return IM_COL32(206, 145, 120, 255);
            case 14: return IM_COL32(181, 206, 168, 255);
            case 15: return IM_COL32(106, 153, 85, 255);
            default: return IM_COL32(212, 212, 212, 255);
            }
        }

    }
    struct CodeEditor::State
    {
        TextEditor editor;
        UInt64 revision = 0;
        Bool changed = false;
        Code::SourceChanges changes;
        Array<CodeEditor::Marker> markers;
        UInt32 executionLine = 0, toggledLine = 0;
        Bool mouseOverEditor = false;
        ImFont* menuFont = nullptr;
        Float menuFontBaseSize = 0.0f;
        EditorApplication* menuEditor = nullptr;
        EditorPanel* menuPanel = nullptr;
        Code::Range tooltipRange;
        String tooltip;
        struct DiagnosticTooltip
        {
            TextEditor::DocPos start, end;
            String message;
        };
        Array<DiagnosticTooltip> diagnosticTooltips;
        mutable String text;
        mutable Array<std::size_t> lines;
        mutable UInt64 textRevision = UInt64(-1);
        void ClearAssistance()
        {
            editor.ClearRangeHighlights();
            tooltip.clear();
            diagnosticTooltips.Clear();
        }
        const String& Text() const
        {
            if (textRevision != revision)
            {
                text = editor.GetText();
                lines.Clear();
                lines.Add(0);
                for (std::size_t at = 0; at < text.size(); ++at)
                {
                    if (text[at] == '\n')
                    {
                        lines.Add(at + 1);
                    }
                }
                textRevision = revision;
            }
            return text;
        }
        std::string_view Line(UInt32 row) const
        {
            Text();
            if (row >= lines.Size())
            {
                return {};
            }
            const auto start = lines[row.Get()];
            const auto end = row + 1 < lines.Size() ? lines[row.Get() + 1] - 1 : text.size();
            return text.View().substr(start, end - start);
        }
        State()
        {
            auto palette = TextEditor::GetDarkPalette();
            palette[static_cast<std::size_t>(TextEditor::Color::background)] = ImGui::ColorConvertFloat4ToU32(EditorTheme::Panel);
            palette[static_cast<std::size_t>(TextEditor::Color::text)] = ImGui::ColorConvertFloat4ToU32(EditorTheme::Ink);
            palette[static_cast<std::size_t>(TextEditor::Color::keyword)] = IM_COL32(197, 134, 192, 255);
            palette[static_cast<std::size_t>(TextEditor::Color::declaration)] = IM_COL32(78, 201, 176, 255);
            palette[static_cast<std::size_t>(TextEditor::Color::comment)] = IM_COL32(106, 153, 85, 255);
            palette[static_cast<std::size_t>(TextEditor::Color::lineNumberHover)] = ImGui::ColorConvertFloat4ToU32(EditorTheme::Hover);
            editor.SetPalette(palette);
            editor.SetInsertSpacesOnTabs(false);
            editor.SetShowWhitespacesEnabled(false);
            editor.SetLanguage(TextEditor::Language::Cpp());
            editor.SetFindReplaceEnabled(false);
            editor.SetExternalUndoRedo(true);
            editor.SetTransactionCallback([this](const std::vector<TextEditor::Change>& actions) {
                Code::SourceTransaction transaction;
                transaction.history = static_cast<UInt64>(editor.GetUndoIndex());
                transaction.empty = editor.IsEmpty();
                const auto kind = editor.GetLastTransactionKind();
                transaction.kind = kind == TextEditor::TransactionKind::Undo ? Code::SourceChangeKind::Undo
                    : kind == TextEditor::TransactionKind::Redo ? Code::SourceChangeKind::Redo : Code::SourceChangeKind::Edit;
                for (std::size_t index = 0; index < actions.size(); ++index)
                {
                    const auto& action = actions[index];
                    const auto* insertion = action.insert ? &action : nullptr;
                    if (!action.insert && index + 1 < actions.size() && actions[index + 1].insert
                        && actions[index + 1].start == action.start)
                    {
                        insertion = &actions[++index];
                    }
                    Code::SourceChange change;
                    change.startLine = static_cast<UInt32>(action.start.line);
                    change.endLine = static_cast<UInt32>(action.insert ? action.start.line : action.end.line);
                    change.startsAtLineStart = action.start.index == 0;
                    change.endsAtLineStart = (action.insert ? action.start.index : action.end.index) == 0;
                    change.removesText = !action.insert && !action.text.empty();
                    change.insertsText = insertion != nullptr && !insertion->text.empty();
                    if (insertion != nullptr)
                    {
                        change.insertedLines = static_cast<UInt32>(insertion->end.line - insertion->start.line);
                    }
                    if (change.removesText || change.insertsText)
                    {
                        transaction.edits.Add(change);
                    }
                }
                changes.transactions.Add(std::move(transaction));
                ++revision;
                changed = true;
                editor.ClearTextColors();
                ClearAssistance();
            });
            editor.SetTextContextMenuRenderer([this](bool requested, TextEditor::PopupData& data) {
                ImGui::PushFont(menuFont, menuFontBaseSize);
                if (requested)
                {
                    OpenContextMenu("TextContextMenu");
                }
                if (BeginOpenedContextMenu("TextContextMenu"))
                {
                    if (ImGui::IsWindowAppearing())
                    {
                        const auto selection = editor.GetMainCursorSelection();
                        if (data.pos < selection.start || !(data.pos < selection.end))
                        {
                            editor.SetCursor(data.pos);
                        }
                    }
                    if (menuEditor != nullptr && menuPanel != nullptr)
                    {
                        EditorActionContext context;
                        context.editor = menuEditor;
                        context.panel = menuPanel;
                        context.menu = EditorActionMenu::Edit;
                        EditorActionUi::DrawItem("code_editor.undo", context);
                        EditorActionUi::DrawItem("code_editor.redo", context);
                        ImGui::Separator();
                        EditorActionUi::DrawItem("code_editor.cut", context);
                        EditorActionUi::DrawItem("code_editor.copy", context);
                        EditorActionUi::DrawItem("code_editor.paste", context);
                        ImGui::Separator();
                        EditorActionUi::DrawItem("code_editor.select_all", context);
                    }
                    else
                    {
                        const char* readOnly = editor.IsReadOnlyEnabled() ? Loc::Text(LocKeys::CodeEditorReadOnly) : nullptr;
                        const char* noChanges = Loc::Text(LocKeys::CodeEditorNoChanges);
                        const char* emptyDocument = editor.IsEmpty() ? Loc::Text(LocKeys::CodeEditorEmptyDocument) : nullptr;
                        const char* undoBlocked = readOnly != nullptr ? readOnly : !editor.CanUndo() ? noChanges : nullptr;
                        const char* redoBlocked = readOnly != nullptr ? readOnly : !editor.CanRedo() ? noChanges : nullptr;
                        const char* cutBlocked = readOnly != nullptr ? readOnly : !editor.AnyCursorHasSelection()
                            ? Loc::Text(LocKeys::CodeEditorNoSelection) : nullptr;
                        if (MenuItem(Loc::Text(LocKeys::MenuUndo), nullptr, undoBlocked == nullptr, undoBlocked, Icons::Undo))
                        {
                            editor.Undo();
                        }
                        if (MenuItem(Loc::Text(LocKeys::MenuRedo), nullptr, redoBlocked == nullptr, redoBlocked, Icons::Redo))
                        {
                            editor.Redo();
                        }
                        ImGui::Separator();
                        if (MenuItem(Loc::Text(LocKeys::MenuCut), nullptr, cutBlocked == nullptr, cutBlocked, Icons::Cut))
                        {
                            editor.Cut();
                        }
                        if (MenuItem(Loc::Text(LocKeys::MenuCopy), nullptr, emptyDocument == nullptr, emptyDocument, Icons::Copy))
                        {
                            editor.Copy();
                        }
                        const char* clipboard = ImGui::GetClipboardText();
                        const char* pasteBlocked = readOnly != nullptr ? readOnly : clipboard == nullptr || clipboard[0] == '\0'
                            ? Loc::Text(LocKeys::BlockedClipboardEmpty) : nullptr;
                        if (MenuItem(Loc::Text(LocKeys::MenuPaste), nullptr, pasteBlocked == nullptr, pasteBlocked, Icons::Paste))
                        {
                            editor.Paste();
                        }
                        ImGui::Separator();
                        if (MenuItem(Loc::Text(LocKeys::MenuSelectAll), nullptr, emptyDocument == nullptr, emptyDocument, Icons::SelectAll))
                        {
                            editor.SelectAll();
                        }
                    }
                    EndContextMenu();
                }
                ImGui::PopFont();
            });
            editor.SetLineDecorator(2, [this](TextEditor::Decorator& data) {
                const UInt32 line = static_cast<UInt32>(data.line + 1);
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                const ImVec2 center(origin.x + data.width * 0.5f, origin.y + data.height * 0.5f);
                const Float radius = Min(data.width, data.height) * 0.28f;
                if (ImGui::InvisibleButton("##breakpoint", {data.width, data.height}))
                {
                    toggledLine = line;
                }
                auto* draw = ImGui::GetWindowDrawList();
                const Bool hovered = ImGui::IsItemHovered();
                const CodeEditor::Marker* activeMarker = nullptr;
                for (const auto& marker : markers)
                {
                    if (marker.line != line)
                    {
                        continue;
                    }
                    activeMarker = &marker;
                    const ImU32 color = ImGui::ColorConvertFloat4ToU32(EditorTheme::Danger);
                    if (marker.verified)
                    {
                        draw->AddCircleFilled(center, radius, color);
                    }
                    else
                    {
                        draw->AddCircle(center, radius, color, 0, 1.5f);
                    }
                }
                if (hovered)
                {
                    if (activeMarker == nullptr)
                    {
                        draw->AddCircleFilled(center, radius, ImGui::ColorConvertFloat4ToU32(EditorTheme::Fade(EditorTheme::Danger, 0.45f)));
                    }
                    HoveredTooltip(activeMarker != nullptr ? activeMarker->message.c_str()
                        : Loc::Text(LocKeys::CodeEditorBreakpointAdd));
                }
                if (executionLine == line)
                {
                    const ImU32 color = ImGui::ColorConvertFloat4ToU32(EditorTheme::Amber);
                    draw->AddTriangleFilled({origin.x, center.y - radius}, {origin.x, center.y + radius},
                        {origin.x + data.width, center.y}, color);
                }
            });
        }
        TextEditor::DocPos Position(Code::Position point) const
        {
            return {point.line.Get(), CodeEditor::Utf16ToGlyph(Line(point.line), point.character).Get()};
        }
        Code::Position SourcePosition(TextEditor::DocPos point) const
        {
            const auto line = Line(static_cast<UInt32>(point.line));
            std::size_t offset = 0;
            UInt32 units = 0;
            for (std::size_t glyph = 0; glyph < point.index && offset < line.size(); ++glyph)
            {
                const auto lead = static_cast<unsigned char>(line[offset]);
                const std::size_t bytes = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
                units += bytes == 4 ? 2 : 1;
                offset += Min(bytes, line.size() - offset);
            }
            return {static_cast<UInt32>(point.line), units};
        }
    };
    CodeEditor::CodeEditor() : m_state(MakeOwnerPtr<State>()) {}
    CodeEditor::~CodeEditor() = default;
    void CodeEditor::SetText(const String& text)
    {
        m_state->editor.SetText(text.View());
        m_state->changes = {};
        m_state->changes.reset = true;
        m_state->editor.ClearTextColors();
        m_state->editor.ClearSquiggles();
        m_state->ClearAssistance();
        ++m_state->revision;
    }
    String CodeEditor::GetText() const { return m_state->Text(); }
    void CodeEditor::SetJBroScript(Bool enabled) { m_state->editor.SetLanguage(enabled ? JBroLanguage() : TextEditor::Language::Cpp()); }
    void CodeEditor::SetReadOnly(Bool enabled) { m_state->editor.SetReadOnlyEnabled(enabled.Get()); }
    void CodeEditor::BindMenu(EditorApplication* editor, EditorPanel* panel)
    {
        m_state->menuEditor = editor;
        m_state->menuPanel = panel;
        m_state->editor.SetExternalClipboardActions(editor != nullptr && panel != nullptr);
    }
    void CodeEditor::Cut() { m_state->editor.Cut(); }
    void CodeEditor::Copy() const { m_state->editor.Copy(); }
    void CodeEditor::Paste()
    {
        if (CanPaste())
        {
            m_state->editor.Paste();
        }
    }
    void CodeEditor::SelectAll() { m_state->editor.SelectAll(); }
    Bool CodeEditor::CanPaste() const
    {
        if (m_state->editor.IsReadOnlyEnabled() || ImGui::GetCurrentContext() == nullptr)
        {
            return false;
        }
        const char* text = ImGui::GetClipboardText();
        return text != nullptr && text[0] != '\0';
    }
    UInt64 CodeEditor::GetHistoryIndex() const { return static_cast<UInt64>(m_state->editor.GetUndoIndex()); }
    Code::SourceChanges CodeEditor::TakeChanges()
    {
        Code::SourceChanges changes = std::move(m_state->changes);
        m_state->changes = {};
        return changes;
    }
    void CodeEditor::SetDebugMarkers(const Array<Marker>& markers, UInt32 executionLine)
    {
        m_state->markers = markers;
        const char* action = Loc::Text(LocKeys::CodeEditorBreakpointRemove);
        for (auto& marker : m_state->markers)
        {
            marker.message = marker.message.empty() ? String(action) : String(action) + "\n" + marker.message;
        }
        m_state->executionLine = executionLine;
    }
    UInt32 CodeEditor::TakeBreakpointToggle()
    {
        const auto line = m_state->toggledLine;
        m_state->toggledLine = 0;
        return line;
    }
    Bool CodeEditor::ApplyEdits(const Array<Code::TextEdit>& edits)
    {
        if (m_state->editor.IsReadOnlyEnabled())
        {
            return false;
        }
        const String original = GetText();
        struct Replacement { std::size_t begin, end; String text; Code::Range range; };
        Array<Replacement> replacements;
        const auto offset = [&](Code::Position position, std::size_t& result) -> Bool {
            result = 0;
            UInt32 line = 0;
            while (line < position.line && result < original.size())
            {
                if (original[result++] == '\n')
                {
                    ++line;
                }
            }
            if (line != position.line)
            {
                return false;
            }
            UInt32 units = 0;
            while (units < position.character && result < original.size() && original[result] != '\n')
            {
                const auto lead = static_cast<unsigned char>(original[result]);
                const std::size_t bytes = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
                units += bytes == 4 ? 2 : 1;
                result += bytes;
            }
            return units == position.character && result <= original.size();
        };
        for (const auto& edit : edits)
        {
            Replacement value;
            value.range = edit.range;
            value.text = edit.newText;
            if (!offset(edit.range.start, value.begin) || !offset(edit.range.end, value.end) || value.end < value.begin)
            {
                return false;
            }
            replacements.Add(std::move(value));
        }
        if (replacements.IsEmpty())
        {
            return true;
        }
        std::sort(replacements.begin(), replacements.end(), [](const Replacement& a, const Replacement& b) { return a.begin < b.begin; });
        for (std::size_t at = 1; at < replacements.Size(); ++at)
        {
            if (replacements[at].begin < replacements[at - 1].end
                || replacements[at].begin == replacements[at - 1].begin)
            {
                return false;
            }
        }
        std::vector<TextEditor::Replacement> native;
        native.reserve(replacements.Size());
        for (const auto& edit : replacements)
        {
            native.push_back({m_state->Position(edit.range.start), m_state->Position(edit.range.end), edit.text.Std()});
        }
        m_state->editor.ReplaceSectionsText(native);
        ++m_state->revision;
        m_state->editor.ClearTextColors();
        m_state->editor.ClearSquiggles();
        return true;
    }
    Bool CodeEditor::Render(const char* id, Bool reserveStatusLine)
    {
        const GuideFocusTarget target = Internal::TakeNextItemTarget();
        m_state->changed = false;
        m_state->menuFont = ImGui::GetFont();
        m_state->menuFontBaseSize = ImGui::GetStyle().FontSizeBase;
        ImFont* font = EditorTheme::GetCodeFont();
        if (font != nullptr)
        {
            ImGui::PushFont(font, font->LegacySize);
        }
        const ImVec2 size(0.0f, reserveStatusLine ? Max(1.0f, ImGui::GetContentRegionAvail().y
            - ImGui::GetTextLineHeightWithSpacing()) : 0.0f);
        m_state->editor.Render(id, size);
        Internal::ReportLastItem(target, false, m_state->changed || ImGui::IsItemClicked());
        m_state->mouseOverEditor = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const String* diagnosticTooltip = nullptr;
        const ImVec2 mouse = ImGui::GetMousePos();
        if (m_state->mouseOverEditor && m_state->editor.IsMousePosOverGlyph(mouse))
        {
            const auto position = m_state->editor.GetDocPosAtMousePos(mouse);
            for (std::size_t index = m_state->diagnosticTooltips.Size(); index > 0; --index)
            {
                const auto& diagnostic = m_state->diagnosticTooltips[index - 1];
                if (position >= diagnostic.start && position < diagnostic.end)
                {
                    diagnosticTooltip = &diagnostic.message;
                    break;
                }
            }
        }
        Code::Position hovered;
        if (diagnosticTooltip != nullptr && !diagnosticTooltip->empty())
        {
            HoveredTooltip(diagnosticTooltip->c_str());
        }
        else if (!m_state->tooltip.empty() && GetHoveredPosition(hovered))
        {
            const auto less = [](Code::Position a, Code::Position b) { return a.line < b.line || (a.line == b.line && a.character < b.character); };
            if (!less(hovered, m_state->tooltipRange.start) && less(hovered, m_state->tooltipRange.end))
            {
                HoveredTooltip(m_state->tooltip.c_str());
            }
        }
        if (font != nullptr)
        {
            ImGui::PopFont();
        }
        return m_state->changed;
    }
    void CodeEditor::Focus(UInt32 line, UInt32 column)
    {
        m_state->editor.SetCursor(m_state->Position({line, column}));
        m_state->editor.SetFocus();
    }
    void CodeEditor::ClearAnalysis()
    {
        m_state->editor.ClearTextColors();
        m_state->editor.ClearSquiggles();
        m_state->diagnosticTooltips.Clear();
    }
    void CodeEditor::SetAnalysis(const Code::Analysis& analysis)
    {
        ClearAnalysis();
        for (const auto& diagnostic : analysis.diagnostics)
        {
            auto start = m_state->Position(diagnostic.range.start);
            auto end = m_state->Position(diagnostic.range.end);
            if (start == end)
            {
                ++end.index;
                end = m_state->Position(m_state->SourcePosition(end));
            }
            m_state->editor.AddSquiggle(start, end, 1,
                diagnostic.severity == 1 ? IM_COL32(224, 90, 82, 255) : IM_COL32(224, 163, 60, 255));
            m_state->diagnosticTooltips.Add({start, end, diagnostic.message});
        }
        for (const auto& token : analysis.tokens)
        {
            m_state->editor.SetTextColor(m_state->Position(token.range.start), m_state->Position(token.range.end), SemanticColor(token.kind));
        }
    }
    void CodeEditor::Undo()
    {
        if (CanUndo())
        {
            m_state->editor.Undo();
            ++m_state->revision;
            ClearAnalysis();
        }
    }
    void CodeEditor::Redo()
    {
        if (CanRedo())
        {
            m_state->editor.Redo();
            ++m_state->revision;
            ClearAnalysis();
        }
    }
    Bool CodeEditor::CanUndo() const { return m_state->editor.CanUndo(); }
    Bool CodeEditor::CanRedo() const { return m_state->editor.CanRedo(); }
    UInt64 CodeEditor::GetRevision() const { return m_state->revision; }
    Code::Position CodeEditor::GetCursor() const
    {
        return m_state->SourcePosition(m_state->editor.GetMainCursorPosition());
    }
    void CodeEditor::SelectRange(const Code::Range& range)
    {
        m_state->editor.SetCursor(m_state->Position(range.start));
        m_state->editor.SelectRegion(m_state->Position(range.start), m_state->Position(range.end));
        m_state->editor.SetFocus();
    }
    Code::Range CodeEditor::GetSelection() const
    {
        const auto selection = m_state->editor.GetMainCursorSelection();
        return {m_state->SourcePosition(selection.start), m_state->SourcePosition(selection.end)};
    }
    Bool CodeEditor::HasSelection() const { return m_state->editor.MainCursorHasSelection(); }
    Bool CodeEditor::GetHoveredPosition(Code::Position& position) const
    {
        const auto mouse = ImGui::GetMousePos();
        if (!m_state->mouseOverEditor || !m_state->editor.IsMousePosOverGlyph(mouse))
        {
            return false;
        }
        const auto native = m_state->editor.GetDocPosAtMousePos(mouse);
        const auto line = m_state->Line(static_cast<UInt32>(native.line));
        std::size_t offset = 0;
        for (std::size_t glyph = 0; glyph < native.index && offset < line.size(); ++glyph)
        {
            const auto lead = static_cast<unsigned char>(line[offset]);
            offset += lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        }
        if (offset >= line.size())
        {
            return false;
        }
        const auto glyph = static_cast<unsigned char>(line[offset]);
        if (!(glyph >= 0x80 || glyph == '_' || (glyph >= 'A' && glyph <= 'Z') || (glyph >= 'a' && glyph <= 'z') || (glyph >= '0' && glyph <= '9')))
        {
            return false;
        }
        position = m_state->SourcePosition(native);
        return true;
    }
    void CodeEditor::SetHoverTooltip(const Code::Range& range, const String& text)
    {
        m_state->tooltipRange = range;
        m_state->tooltip = text;
    }
    void CodeEditor::SetReferenceHighlights(const Array<Code::Range>& ranges)
    {
        std::vector<TextEditor::DocSelection> native;
        native.reserve(ranges.Size());
        for (const auto& range : ranges)
        {
            native.emplace_back(m_state->Position(range.start), m_state->Position(range.end));
        }
        m_state->editor.SetRangeHighlights(native, ImGui::ColorConvertFloat4ToU32(EditorTheme::Fade(EditorTheme::Amber, 0.30f)));
    }
    UInt32 CodeEditor::Utf16ToGlyph(std::string_view line, UInt32 character)
    {
        std::size_t at = 0;
        UInt32 units = 0, glyph = 0;
        while (at < line.size() && units < character)
        {
            const auto lead = static_cast<unsigned char>(line[at]);
            const std::size_t bytes = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
            const UInt32 width = bytes == 4 ? 2 : 1;
            if (units + width > character)
            {
                break;
            }
            units += width;
            at += Min(bytes, line.size() - at);
            ++glyph;
        }
        return glyph;
    }
}
