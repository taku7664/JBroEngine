#include <JBro/Editor/Widget/PathField.h>

#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/TextField.h>

#include <imgui.h>

namespace JBro::Widget
{
    PathField::PathField(const char* id, String& path)
        : m_id(id)
        , m_path(path)
    {
    }

    PathField& PathField::Hint(const char* text)
    {
        m_hint = text;
        return *this;
    }

    PathField& PathField::Invalid(bool invalid)
    {
        m_invalid = invalid;
        return *this;
    }

    PathFieldResult PathField::Draw() const
    {
        PathFieldResult result;
        ImGui::PushID(m_id != nullptr ? m_id : "##path");
        // **단추는 폴더 그림이다.** 기존은 "찾아보기" 글자였는데 넓은 빌드 설정 창에서였다. 오른쪽 도크처럼 좁은 칸에서는
        // 글자 단추가 칸을 먹어 경로가 몇 글자만 보이고 단추마저 잘렸다(실제 에디터에서 그랬다). 이름은 툴팁이 말한다.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttonWidth = ImGui::GetFrameHeight();
        const float available = ImGui::GetContentRegionAvail().x;
        const float fieldWidth = available - buttonWidth - style.ItemInnerSpacing.x;

        TextField field("##value", m_path);
        field.Width(fieldWidth > 1.0f ? fieldWidth : 1.0f).Invalid(m_invalid);
        if (m_hint != nullptr)
        {
            field.Hint(m_hint);
        }
        result.edited = field.Draw();
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        result.browse = ImGui::Button(Icons::FolderOpen, ImVec2(buttonWidth, buttonWidth));
        HoveredTooltip(Loc::TextOr(LocKeys::CommonBrowse, "Browse"));
        ImGui::PopID();
        return result;
    }
}
