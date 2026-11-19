#include "CodeHighlighting.h"
#include <JBro/Editor/EditorTheme.h>
namespace JBro::Widget
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
    TextEditor::Palette CodePalette()
    {
        auto palette = TextEditor::GetDarkPalette();
        palette[static_cast<std::size_t>(TextEditor::Color::background)] = ImGui::ColorConvertFloat4ToU32(EditorTheme::Panel);
        palette[static_cast<std::size_t>(TextEditor::Color::text)] = ImGui::ColorConvertFloat4ToU32(EditorTheme::Ink);
        palette[static_cast<std::size_t>(TextEditor::Color::keyword)] = IM_COL32(197, 134, 192, 255);
        palette[static_cast<std::size_t>(TextEditor::Color::declaration)] = IM_COL32(78, 201, 176, 255);
        palette[static_cast<std::size_t>(TextEditor::Color::comment)] = IM_COL32(106, 153, 85, 255);
        palette[static_cast<std::size_t>(TextEditor::Color::lineNumberHover)] = ImGui::ColorConvertFloat4ToU32(EditorTheme::Hover);
        return palette;
    }
}
