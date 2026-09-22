#include <JBro/Editor/Widget/Basic.h>

#include <JBro/Editor/EditorUI.h>

#include <cstdarg>

namespace JBro::Widget
{
    void Text(const char* text)
    {
        ImGui::TextUnformatted(text != nullptr ? text : "");
    }

    void TextF(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        ImGui::TextV(format, args);
        va_end(args);
    }

    void HintText(const char* text)
    {
        // `TextDisabled(text)` 는 글자를 서식으로 읽는다. 파일 이름에 `%` 가 있으면 깨진다.
        ImGui::TextDisabled("%s", text != nullptr ? text : "");
    }

    void HintTextF(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        ImGui::TextDisabledV(format, args);
        va_end(args);
    }

    void SeverityTextF(Severity severity, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        ImGui::TextColoredV(SeverityColor(severity), format, args);
        va_end(args);
    }

    bool Button(const char* label)
    {
        return ImGui::Button(label);
    }

    bool HitArea(const char* id, const ImVec2& size, ImGuiButtonFlags buttons)
    {
        // 크기가 0 이면 ImGui 가 단언한다. 접힌 칸에서도 죽지 않게 한 픽셀은 둔다.
        const ImVec2 safe(size.x > 1.0f ? size.x : 1.0f, size.y > 1.0f ? size.y : 1.0f);
        return ImGui::InvisibleButton(id, safe, buttons);
    }

    bool MenuItem(const char* label, const char* shortcut, bool enabled)
    {
        return ImGui::MenuItem(label, shortcut, false, enabled);
    }

    bool MenuToggle(const char* label, bool& checked, bool enabled)
    {
        return ImGui::MenuItem(label, nullptr, &checked, enabled);
    }

    bool BeginContextMenu(const char* id, bool ofWindow)
    {
        if (ofWindow)
        {
            return ImGui::BeginPopupContextWindow(id,
                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems);
        }
        return ImGui::BeginPopupContextItem(id);
    }

    void EndContextMenu()
    {
        ImGui::EndPopup();
    }

    void OpenModal(const char* id)
    {
        ImGui::OpenPopup(id);
    }

    bool BeginModal(const char* id)
    {
        // 크기는 내용이 정한다. 고정 크기면 번역된 문구가 길 때 잘린다.
        return ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    }

    void CloseModal()
    {
        ImGui::CloseCurrentPopup();
    }

    void EndModal()
    {
        ImGui::EndPopup();
    }

    bool FoldNode(const char* label, ImGuiTreeNodeFlags flags)
    {
        return ImGui::TreeNodeEx(label, flags);
    }

    void TreePop()
    {
        ImGui::TreePop();
    }

    bool CollapsingSection(const char* title, bool defaultOpen)
    {
        return ImGui::CollapsingHeader(title,
            defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
    }

    void Image(TextureHandle texture, const ImVec2& size, const ImVec2& uvMax)
    {
        if (false == texture.IsValid())
        {
            // 텍스처가 없어도 자리는 지킨다. 접히면 그 아래 줄이 프레임마다 움직인다.
            ImGui::Dummy(size);
            return;
        }
        ImGui::Image(static_cast<ImTextureID>(EditorUI::ToTextureId(texture)), size,
            ImVec2(0.0f, 0.0f), uvMax);
    }
}
