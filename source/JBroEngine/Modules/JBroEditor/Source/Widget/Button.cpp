#include <JBro/Editor/Widget/Button.h>

#include <imgui_internal.h>

namespace JBro::Widget
{
    bool TextButton(
        const char* label, const ImVec2& size, const ImVec2& offset,
        ImGuiButtonFlags flags)
    {
        const ImVec2 startCursor = ImGui::GetCursorPos();
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        StyleScope style;
        style.PushColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        style.PushColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        style.PushColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
        ImGui::PushID(label);
        // 빈 이름으로 버튼을 그리고 글자를 그 위에 얹는다. 버튼에 이름을 주면
        // ImGui 가 제 나름대로 가운데를 잡아 `offset` 을 줄 자리가 없어진다.
        const bool pressed = ImGui::ButtonEx("", size, flags);
        const ImVec2 buttonSize = ImGui::GetItemRectSize();
        ImGui::SameLine();
        if (ImGui::IsItemHovered())
        {
            style.PushColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextSelectedBg));
        }
        const ImVec2 textPos =
            startCursor + (buttonSize - textSize) * 0.5f + offset;
        ImGui::SetCursorPos(textPos);
        ImGui::TextUnformatted(label);
        ImGui::PopID();
        return pressed;
    }
}
