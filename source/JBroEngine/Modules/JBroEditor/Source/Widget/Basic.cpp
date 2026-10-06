#include <JBro/Editor/Widget/Basic.h>

#include <JBro/Editor/EditorUI.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/GuideFocus.h>

// 메뉴 줄의 사각형은 공개 헤더에 없다.
#include <imgui_internal.h>

#include <cstdarg>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

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

    Bool Button(const char* label)
    {
        const GuideFocusTarget target = Internal::TakeNextItemTarget();
        const Bool pressed = ImGui::Button(label);
        Internal::ReportLastItem(target, false, pressed);
        return pressed;
    }

    Bool Button(const char* label, const char* icon)
    {
        if (icon == nullptr)
        {
            return Button(label);
        }
        const GuideFocusTarget target = Internal::TakeNextItemTarget();
        const ImGuiStyle& style = ImGui::GetStyle();
        const char* labelEnd = ImGui::FindRenderedTextEnd(label);
        const ImVec2 textSize = ImGui::CalcTextSize(label, labelEnd);
        const Float lineHeight = ImGui::GetTextLineHeight();
        const Float iconWidth = lineHeight + style.ItemInnerSpacing.x;
        Bool pressed = false;
        {
            // 이름은 ImGui 가 그리지 않게 하고(Id 만 받는다) 아이콘과 이름을 직접 그린다.
            StyleScope hidden;
            hidden.PushColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
            pressed = ImGui::Button(label, ImVec2(iconWidth + textSize.x + style.FramePadding.x * 2.0f, 0.0f));
        }
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
        const Float lineTop = min.y + (max.y - min.y - lineHeight) * 0.5f;
        const ImVec2 iconMin(min.x + style.FramePadding.x, lineTop);
        DrawGlyphCentered(icon, iconMin, ImVec2(iconMin.x + lineHeight, lineTop + lineHeight), color);
        ImGui::GetWindowDrawList()->AddText(ImVec2(iconMin.x + iconWidth, lineTop), color, label, labelEnd);
        Internal::ReportLastItem(target, false, pressed);
        return pressed;
    }

    Bool SelectableRow(const char* label, Bool selected)
    {
        return ImGui::Selectable(label, selected);
    }

    Bool ActionButton(const char* label, Severity severity, Bool enabled,
        const char* disabledReason, const ImVec2& size)
    {
        // `Info` 는 테마의 단추 그대로다 - 모든 단추가 물들면 무게가 뜻을 잃는다.
        const Bool tinted = severity != Severity::Info;
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
        const Bool clicked = ImGui::Button(label, size);
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

    Bool HitArea(const char* id, const ImVec2& size, ImGuiButtonFlags buttons)
    {
        // 크기가 0 이면 ImGui 가 단언한다. 접힌 칸에서도 죽지 않게 한 픽셀은 둔다.
        const ImVec2 safe(size.x > 1.0f ? size.x : 1.0f, size.y > 1.0f ? size.y : 1.0f);
        return ImGui::InvisibleButton(id, safe, buttons);
    }

    Bool MenuItem(const char* label, const char* shortcut, Bool enabled,
        const char* disabledReason, const char* icon)
    {
        const GuideFocusTarget target = Internal::TakeNextItemTarget();
        Bool chosen = false;
        if (icon == nullptr)
        {
            chosen = ImGui::MenuItem(label, shortcut, false, enabled);
        }
        else
        {
            // ImGui 의 아이콘 칸에는 글자 폭 한 칸(U+3000, 전각 빈칸)만 넘겨 자리를 받고, 아이콘은 그 칸에 직접 그린다 -
            // 아이콘을 넘기면 글자처럼 기준선에 앉아 처진다(D-277).
            constexpr const char* IconSlot = "\xE3\x80\x80";
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            chosen = ImGui::MenuItemEx(label, IconSlot, shortcut, false, enabled);
            const ImGuiWindow* window = ImGui::GetCurrentWindow();
            const Float slotLeft = rowMin.x + window->DC.MenuColumns.OffsetIcon;
            const Float slotWidth = ImGui::CalcTextSize(IconSlot).x;
            const Float lineHeight = ImGui::GetTextLineHeight();
            DrawGlyphCentered(icon, ImVec2(slotLeft, rowMin.y), ImVec2(slotLeft + slotWidth, rowMin.y + lineHeight),
                ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled));
        }
        Internal::ReportLastItem(target, false, chosen, enabled, disabledReason);
        DisabledReason(false == enabled, disabledReason);
        return chosen;
    }

    void DisabledReason(Bool disabled, const char* reason)
    {
        if (false == disabled || reason == nullptr || reason[0] == '\0')
        {
            return;
        }
        // 회색 항목은 기본 hover 판정에서 빠진다. 그 자리에 뜨게 하려면 이 플래그가 필요하다.
        HoveredTooltip(reason, ImGuiHoveredFlags_AllowWhenDisabled);
    }

    Bool MenuToggle(const char* label, Bool& checked, Bool enabled)
    {
        return ImGui::MenuItem(label, nullptr, &checked, enabled);
    }

    Bool BeginMenuBar()
    {
        return ImGui::BeginMenuBar();
    }

    void EndMenuBar()
    {
        // **메뉴 줄의 아래를 선으로 끊는다.** 메뉴 줄이 둘 겹쳐 서고 그 아래에 탭 띠가
        // 오는데, 셋 다 어두운 면이라 색만으로는 어디서 무엇이 끝나는지 보이지 않는다.
        // 색을 더 벌리는 대신 선을 하나 긋는다 - 어두운 화면에서 면을 자꾸 밝히면
        // 밝기 계층이 위로 밀린다.
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImRect bar = window->MenuBarRect();
        window->DrawList->AddLine(ImVec2(bar.Min.x, bar.Max.y - 0.5f),
            ImVec2(bar.Max.x, bar.Max.y - 0.5f),
            ImGui::GetColorU32(EditorTheme::Line), 1.0f);
        ImGui::EndMenuBar();
    }

    Bool BeginMenu(const char* label, Bool enabled)
    {
        // 메뉴는 사용자가 연다(ImGui 에 메뉴를 코드로 여는 길이 없다). 열렸는지만 알린다 - 열린 뒤에도
        // ImGui 가 마지막 항목을 메뉴 머리로 되돌려 두므로 그 사각형이 머리의 것이다.
        const GuideFocusTarget target = Internal::TakeNextItemTarget();
        const bool open = ImGui::BeginMenu(label, enabled);
        Internal::ReportLastItem(target, open, false, enabled);
        return open;
    }

    void EndMenu()
    {
        ImGui::EndMenu();
    }

    Bool BeginContextMenu(const char* id, Bool ofWindow)
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

    Bool BeginOpenedContextMenu(const char* id)
    {
        return ImGui::BeginPopup(id);
    }

    void OpenModal(const char* id)
    {
        ImGui::OpenPopup(id);
    }

    Bool BeginModal(const char* id)
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

    Bool FoldNode(const char* label, ImGuiTreeNodeFlags flags)
    {
        const GuideFocusTarget target = Internal::TakeNextItemTarget();
        Internal::OpenIfGuided(target);
        const bool open = ImGui::TreeNodeEx(label, flags);
        Internal::ReportLastItem(target, open, ImGui::IsItemClicked());
        return open;
    }

    void TreePop()
    {
        ImGui::TreePop();
    }

    Bool CollapsingSection(const char* title, Bool defaultOpen, Bool allowOverlap)
    {
        // **머리는 파랑이 아니다.** `ImGuiCol_Header` 는 고른 줄과 접기 머리가 함께 쓰는
        // 색인데, 파랑은 고른 것의 색이다 - 늘 서 있는 컴포넌트 머리가 그 색을 쓰면
        // 인스펙터 어디가 골라져 있는지 알 수 없게 된다. 머리는 떠 있는 면으로 둔다.
        StyleScope scope;
        scope.PushColor(ImGuiCol_Header, EditorTheme::Raised);
        scope.PushColor(ImGuiCol_HeaderHovered, EditorTheme::Hover);
        scope.PushColor(ImGuiCol_HeaderActive, EditorTheme::Pressed);
        const GuideFocusTarget target = Internal::TakeNextItemTarget();
        Internal::OpenIfGuided(target);
        ImGuiTreeNodeFlags flags = defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
        if (allowOverlap)
        {
            flags |= ImGuiTreeNodeFlags_AllowOverlap;
        }
        const bool open = ImGui::CollapsingHeader(title, flags);
        Internal::ReportLastItem(target, open, ImGui::IsItemClicked());
        return open;
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

    ImVec2 FitInside(UInt32 width, UInt32 height, const ImVec2& box)
    {
        if (width == 0 || height == 0 || box.x <= 0.0f || box.y <= 0.0f)
        {
            return box;
        }
        const Float scaleX = box.x / static_cast<JBro::Float>(width);
        const Float scaleY = box.y / static_cast<JBro::Float>(height);
        const Float scale = scaleX < scaleY ? scaleX : scaleY;
        return ImVec2(static_cast<JBro::Float>(width) * scale, static_cast<JBro::Float>(height) * scale);
    }

    Bool BeginTabs(const char* id)
    {
        return ImGui::BeginTabBar(id, ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll);
    }

    void EndTabs()
    {
        ImGui::EndTabBar();
    }

    Bool BeginTab(const char* label, Bool* open, Bool select)
    {
        // 탭은 "연다" 가 곧 앞으로 꺼내는 것이다.
        const GuideFocusTarget target = Internal::TakeNextItemTarget();
        const Bool guided = target.IsValid() && GetGuideFocus() != nullptr && GetGuideFocus()->ShouldOpen(target);
        bool rawOpen = open != nullptr ? open->Get() : true;
        const Bool front = ImGui::BeginTabItem(label, open != nullptr ? &rawOpen : nullptr,
            (select || guided) ? ImGuiTabItemFlags_SetSelected : 0);
        if (open != nullptr)
        {
            *open = rawOpen;
        }
        Internal::ReportLastItem(target, front, ImGui::IsItemClicked());
        return front;
    }

    void EndTab()
    {
        ImGui::EndTabItem();
    }
}
