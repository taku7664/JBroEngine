#include <JBro/Editor/Widget/FormLayout.h>

namespace JBro::Widget
{
    FormLayout::FormLayout(const char* id, float spacing, ImVec2 padding, float labelWidth)
        : m_spacing(spacing)
        , m_labelWidth(labelWidth)
    {
        const ImGuiTableFlags flags =
            ImGuiTableFlags_SizingStretchProp
            | ImGuiTableFlags_NoSavedSettings
            | ImGuiTableFlags_NoBordersInBody
            | ImGuiTableFlags_NoPadOuterX;
        m_open = ImGui::BeginTable(id, 2, flags);
        if (false == m_open)
        {
            return;
        }
        m_style.PushVar(ImGuiStyleVar_CellPadding, padding);
        // 라벨 칸은 폭이 고정, 값 칸이 나머지를 늘여 받는다. 라벨 폭 0 이면
        // ImGui 가 내용에 맞춘다.
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, m_labelWidth);
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);
    }

    FormLayout::~FormLayout()
    {
        if (m_open)
        {
            // 스타일을 표보다 먼저 빼낸다. 반대로 하면 `EndTable` 이 자기 것이
            // 아닌 스택 위에서 끝난다.
            m_style.Pop();
            ImGui::EndTable();
        }
    }

    bool FormLayout::IsOpen() const
    {
        return m_open;
    }
}
