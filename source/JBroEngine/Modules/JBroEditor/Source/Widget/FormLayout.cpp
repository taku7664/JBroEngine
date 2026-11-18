#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro::Widget
{
    FormLayout::FormLayout(
        const char* id, Float spacing, ImVec2 padding, Float labelWidth, Float width)
        : m_id(id)
        , m_spacing(spacing)
        , m_padding(padding)
        , m_labelWidth(labelWidth)
        , m_width(width)
    {
        Open();
    }

    FormLayout::~FormLayout()
    {
        Close();
    }

    void FormLayout::Open()
    {
        const ImGuiTableFlags flags =
            ImGuiTableFlags_SizingStretchProp
            | ImGuiTableFlags_NoSavedSettings
            | ImGuiTableFlags_NoBordersInBody
            | ImGuiTableFlags_NoPadOuterX;
        // 표 바깥에서 잰다 - 이름 칸의 상한(절반)과 지난 프레임의 이름 폭을 둘 열쇠다.
        const Float available = ImGui::GetContentRegionAvail().x;
        ImGui::PushID(m_id);
        m_labelKey = ImGui::GetID("##labelWidth");
        ImGui::PopID();
        m_open = ImGui::BeginTable(m_id, 2, flags, ImVec2(m_width, 0.0f));
        if (false == m_open)
        {
            return;
        }
        m_style.PushVar(ImGuiStyleVar_CellPadding, m_padding);
        // 라벨 칸은 폭이 고정, 값 칸이 나머지를 늘여 받는다. 라벨 폭 0 이면 지난 프레임에 잰 가장 긴 이름에 맞추되 표 폭의 절반까지다 -
        // 처음(잰 것이 없을 때)은 ImGui 가 내용에 맞춘다. 고정 폭·크기를 바꿀 수 없는 칸은 ImGui 가 이 값을 프레임마다 다시 읽는다.
        Float labelWidth = m_labelWidth;
        if (labelWidth <= 0.0f)
        {
            const Float remembered = ImGui::GetStateStorage()->GetFloat(m_labelKey, 0.0f);
            const Float half = (m_width > 0.0f ? m_width : available) * 0.5f;
            labelWidth = remembered > 0.0f ? (remembered < half ? remembered : half) : Float(0.0f);
        }
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth.Get());
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);
    }

    void FormLayout::Close()
    {
        if (false == m_open)
        {
            return;
        }
        // 다음 프레임의 이름 칸 폭이다. 끊었다 다시 연 조각(`Break`)은 같은 표라 가장 넓은 것을 남긴다.
        if (m_labelWidth <= 0.0f && m_measuredLabel > 0.0f)
        {
            ImGui::GetStateStorage()->SetFloat(m_labelKey, m_measuredLabel.Get());
        }
        // 스타일을 표보다 먼저 빼낸다. 반대로 하면 `EndTable` 이 자기 것이
        // 아닌 스택 위에서 끝난다.
        m_style.Pop();
        ImGui::EndTable();
        m_open = false;
    }

    Bool FormLayout::IsOpen() const
    {
        return m_open;
    }
}
