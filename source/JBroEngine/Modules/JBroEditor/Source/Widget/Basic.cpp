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

    void WrappedText(const char* text)
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(text != nullptr ? text : "");
        ImGui::PopTextWrapPos();
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

    bool ActionButton(const char* label, Severity severity, bool enabled,
        const char* disabledReason, const ImVec2& size)
    {
        // `Info` 는 테마의 단추 그대로다 - 모든 단추가 물들면 무게가 뜻을 잃는다.
        const bool tinted = severity != Severity::Info;
        if (tinted)
        {
            const ImVec4 base = SeverityColor(severity);
            ImGui::PushStyleColor(ImGuiCol_Button, WithAlpha(base, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(ScaleColor(base, 1.12f), 0.72f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, WithAlpha(ScaleColor(base, 0.92f), 0.85f));
        }
        if (false == enabled)
        {
            ImGui::BeginDisabled();
        }
        const bool clicked = ImGui::Button(label, size);
        if (false == enabled)
        {
            ImGui::EndDisabled();
        }
        if (tinted)
        {
            ImGui::PopStyleColor(3);
        }
        DisabledReason(false == enabled, disabledReason);
        return clicked;
    }

    bool HitArea(const char* id, const ImVec2& size, ImGuiButtonFlags buttons)
    {
        // 크기가 0 이면 ImGui 가 단언한다. 접힌 칸에서도 죽지 않게 한 픽셀은 둔다.
        const ImVec2 safe(size.x > 1.0f ? size.x : 1.0f, size.y > 1.0f ? size.y : 1.0f);
        return ImGui::InvisibleButton(id, safe, buttons);
    }

    bool MenuItem(const char* label, const char* shortcut, bool enabled,
        const char* disabledReason)
    {
        const bool chosen = ImGui::MenuItem(label, shortcut, false, enabled);
        DisabledReason(false == enabled, disabledReason);
        return chosen;
    }

    void DisabledReason(bool disabled, const char* reason)
    {
        if (false == disabled || reason == nullptr || reason[0] == '\0')
        {
            return;
        }
        // 회색 항목은 기본 hover 판정에서 빠진다. 그 자리에 뜨게 하려면 이 플래그가 필요하다.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", reason);
        }
    }

    bool MenuToggle(const char* label, bool& checked, bool enabled)
    {
        return ImGui::MenuItem(label, nullptr, &checked, enabled);
    }

    bool BeginMenuBar()
    {
        return ImGui::BeginMenuBar();
    }

    void EndMenuBar()
    {
        ImGui::EndMenuBar();
    }

    bool BeginMenu(const char* label, bool enabled)
    {
        return ImGui::BeginMenu(label, enabled);
    }

    void EndMenu()
    {
        ImGui::EndMenu();
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

    void OpenContextMenu(const char* id)
    {
        ImGui::OpenPopup(id);
    }

    bool BeginOpenedContextMenu(const char* id)
    {
        return ImGui::BeginPopup(id);
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

    void Image(TextureHandle texture, const ImVec2& size, const ImVec2& uvMin, const ImVec2& uvMax)
    {
        if (false == texture.IsValid())
        {
            // 텍스처가 없어도 자리는 지킨다. 접히면 그 아래 줄이 프레임마다 움직인다.
            ImGui::Dummy(size);
            return;
        }
        ImGui::Image(static_cast<ImTextureID>(EditorUI::ToTextureId(texture)), size, uvMin, uvMax);
    }

    ImVec2 FitInside(std::uint32_t width, std::uint32_t height, const ImVec2& box)
    {
        if (width == 0 || height == 0 || box.x <= 0.0f || box.y <= 0.0f)
        {
            return box;
        }
        const float scaleX = box.x / static_cast<float>(width);
        const float scaleY = box.y / static_cast<float>(height);
        const float scale = scaleX < scaleY ? scaleX : scaleY;
        return ImVec2(static_cast<float>(width) * scale, static_cast<float>(height) * scale);
    }

    bool BeginTabs(const char* id)
    {
        return ImGui::BeginTabBar(id, ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll);
    }

    void EndTabs()
    {
        ImGui::EndTabBar();
    }

    bool BeginTab(const char* label, bool* open, bool select)
    {
        return ImGui::BeginTabItem(label, open, select ? ImGuiTabItemFlags_SetSelected : 0);
    }

    void EndTab()
    {
        ImGui::EndTabItem();
    }
}
