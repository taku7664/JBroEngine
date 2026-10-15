#include <JBro/Editor/Widget/CodeEditor.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Localization.h>
#include <TextEditor.h>
#include <algorithm>

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
        mutable String text;
        mutable Array<std::size_t> lines;
        mutable UInt64 textRevision = UInt64(-1);
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
            editor.SetPalette(palette);
            editor.SetInsertSpacesOnTabs(false);
            editor.SetShowWhitespacesEnabled(false);
            editor.SetLanguage(TextEditor::Language::Cpp());
            editor.SetFindReplaceEnabled(false);
            editor.SetExternalUndoRedo(true);
            editor.SetChangeCallback([this] { ++revision; changed = true; editor.ClearTextColors(); }, 0);
        }
        TextEditor::DocPos Position(Code::Position point) const
        {
            return {point.line.Get(), CodeEditor::Utf16ToGlyph(Line(point.line), point.character).Get()};
        }
    };
    CodeEditor::CodeEditor() : m_state(MakeOwnerPtr<State>()) {}
    CodeEditor::~CodeEditor() = default;
    void CodeEditor::SetText(const String& text)
    {
        m_state->editor.SetText(text.View());
        m_state->editor.ClearTextColors();
        m_state->editor.ClearSquiggles();
        ++m_state->revision;
    }
    String CodeEditor::GetText() const { return m_state->Text(); }
    void CodeEditor::SetJBroScript(Bool enabled) { m_state->editor.SetLanguage(enabled ? JBroLanguage() : TextEditor::Language::Cpp()); }
    void CodeEditor::SetReadOnly(Bool enabled) { m_state->editor.SetReadOnlyEnabled(enabled.Get()); }
    Bool CodeEditor::Render(const char* id)
    {
        m_state->changed = false;
        ImFont* font = EditorTheme::GetCodeFont();
        if (font != nullptr)
        {
            ImGui::PushFont(font, 16.0f);
        }
        m_state->editor.Render(id);
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
            }
            m_state->editor.AddSquiggle(start, end, 1,
                diagnostic.severity == 1 ? IM_COL32(224, 90, 82, 255) : IM_COL32(224, 163, 60, 255), diagnostic.message.View());
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
        const auto position = m_state->editor.GetMainCursorPosition();
        const auto line = m_state->Line(static_cast<std::uint32_t>(position.line));
        std::size_t offset = 0;
        UInt32 units = 0;
        for (std::size_t glyph = 0; glyph < position.index && offset < line.size(); ++glyph)
        {
            const auto lead = static_cast<unsigned char>(line[offset]);
            const std::size_t bytes = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
            units += bytes == 4 ? 2 : 1;
            offset += std::min(bytes, line.size() - offset);
        }
        return {static_cast<std::uint32_t>(position.line), units};
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
            at += std::min(bytes, line.size() - at);
            ++glyph;
        }
        return glyph;
    }
}
