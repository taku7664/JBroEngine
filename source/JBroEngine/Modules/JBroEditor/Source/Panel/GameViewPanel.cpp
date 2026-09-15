#include "GameViewPanel.h"

#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorUI.h>

#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>

#include <imgui.h>

namespace JBro
{
    const char* GameViewPanel::GetTitle() const
    {
        // 안정된 이름이다. 번역하지 않는다 - 창의 정체가 여기 달려 있다.
        return "Game";
    }

    const char* GameViewPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelGame, "Game");
    }

    bool GameViewPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    void GameViewPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        const TextureHandle gameView = m_editor->GetGameViewTexture();
        const Extent2D extent = m_editor->GetGameViewExtent();
        const ImVec2 panel = ImGui::GetContentRegionAvail();
        if (panel.x <= 0.0f || panel.y <= 0.0f
            || false == gameView.IsValid()
            || extent.width == 0 || extent.height == 0)
        {
            return;
        }

        // **비율을 지켜 패널 안에 맞춘다(레터박스).** 늘려 붙이면 에디터 창 모양에
        // 따라 게임이 찌그러져 보인다.
        const float viewAspect =
            static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const float panelAspect = panel.x / panel.y;
        ImVec2 size = panel;
        if (viewAspect > panelAspect)
        {
            size.y = panel.x / viewAspect;
        }
        else
        {
            size.x = panel.y * viewAspect;
        }
        const ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(
            cursor.x + (panel.x - size.x) * 0.5f,
            cursor.y + (panel.y - size.y) * 0.5f));
        ImGui::Image(static_cast<ImTextureID>(EditorUI::ToTextureId(gameView)), size);
        // 이 프레임에 게임 화면을 붙였다. 붙이지 않은 프레임(닫힘·다른 탭에 가림)에는
        // 게임을 그리지 않는다(D-63).
        m_editor->RequestGameView();
    }
}
