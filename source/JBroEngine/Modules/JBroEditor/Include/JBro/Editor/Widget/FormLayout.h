#pragma once

#include <JBro/Editor/Widget/Common.h>

namespace JBro::Widget
{
    // 라벨 한 칸, 위젯 한 칸의 두 칸짜리 줄이다(ProjectRule §11.3).
    //
    // **위젯에 라벨을 넘기지 않는 이유가 이것이다.** ImGui 는 라벨을 위젯 오른쪽에
    // 붙이는데, 패널이 좁아지면 그 라벨이 잘린다 - `orthographicSi…` 가 실제로
    // 그렇게 났다(D-78). 라벨을 표의 왼쪽 칸이 그리면 잘리는 쪽은 위젯이고,
    // 위젯은 줄어들어도 여전히 만질 수 있다.
    //
    // 오른쪽 칸의 위젯은 `SetNextItemWidth(-FLT_MIN)` 으로 남은 폭을 다 쓴다.
    // 그러므로 **위젯에 넘기는 라벨은 `"##이름"`** 이어야 한다 - 보이는 이름은
    // 왼쪽 칸이 이미 그렸다.
    class FormLayout
    {
    public:
        explicit FormLayout(
            const char* id,
            float spacing = 4.0f,
            ImVec2 padding = ImVec2(2.0f, 1.0f),
            float labelWidth = 0.0f);
        ~FormLayout();

        FormLayout(const FormLayout&) = delete;
        FormLayout& operator=(const FormLayout&) = delete;
        FormLayout(FormLayout&&) = delete;
        FormLayout& operator=(FormLayout&&) = delete;

        // 한 줄. 왼쪽을 그리고 오른쪽을 그린다.
        //
        // 표를 열지 못했으면 **아무것도 그리지 않는다.** 그 자리에 그냥 그려 버리면
        // 칸 없이 쏟아져 화면이 무너지고, 왜 그런지는 화면에서 알 수 없다.
        template <typename TLabel, typename TField>
        void Row(TLabel&& label, TField&& field)
        {
            if (false == m_open)
            {
                return;
            }
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            // 라벨 글자를 위젯 프레임의 가운데에 맞춘다. 없으면 한 줄 안에서
            // 라벨만 위로 붙어 눈에 거슬린다.
            ImGui::AlignTextToFramePadding();
            label();

            ImGui::TableSetColumnIndex(1);
            if (m_spacing > 0.0f)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + m_spacing);
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            field();
        }

        // 칸을 나누지 않고 줄 전체를 쓴다. 여러 줄짜리 글자 칸이나 목록처럼
        // 라벨 옆에 두면 너무 좁아지는 것들이 쓴다.
        template <typename TField>
        void FullRow(TField&& field)
        {
            if (false == m_open)
            {
                return;
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            field();
        }

        bool IsOpen() const;

    private:
        bool m_open = false;
        float m_spacing = 0.0f;
        float m_labelWidth = 0.0f;
        StyleScope m_style;
    };
}
