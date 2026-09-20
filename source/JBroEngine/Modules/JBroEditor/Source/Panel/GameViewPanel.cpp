#include "GameViewPanel.h"

#include <JBro/Canvas/Canvas.h>
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
        if (panel.x <= 0.0f || panel.y <= 0.0f)
        {
            return;
        }
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        // 그림이 붙지 않는 자리의 바탕이다. 레터박스가 창 배경과 같은 색이면
        // 게임 화면이 어디까지인지 보이지 않는다.
        draw->AddRectFilled(origin, ImVec2(origin.x + panel.x, origin.y + panel.y),
            IM_COL32(20, 20, 24, 255));

        const bool hasImage = gameView.IsValid() && extent.width != 0 && extent.height != 0;
        if (hasImage)
        {
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
            const ImVec2 imageMin(
                origin.x + (panel.x - size.x) * 0.5f,
                origin.y + (panel.y - size.y) * 0.5f);
            draw->AddImage(
                static_cast<ImTextureID>(EditorUI::ToTextureId(gameView)),
                imageMin, ImVec2(imageMin.x + size.x, imageMin.y + size.y));
        }

        // 자리를 차지해 두어야 스크롤과 다음 줄이 어긋나지 않는다. **입력은 받지 않는다** -
        // 게임 뷰에서는 고르지도 끌지도 않는다(D-131).
        ImGui::Dummy(panel);
        // 이 프레임에 게임 화면을 붙였다. 붙이지 않은 프레임(닫힘·다른 탭에 가림)에는
        // 게임을 그리지 않는다(D-63).
        m_editor->RequestGameView();

        DrawStatusOverlay(origin.x, origin.y, hasImage);
    }

    void GameViewPanel::DrawStatusOverlay(float left, float top, bool hasImage) const
    {
        // 기존 엔진의 게임 뷰와 같은 자리, 같은 내용이다. 그림이 안 나올 때 **왜 안 나오는지**를
        // 말하지 않으면 고장과 구분되지 않는다.
        const bool playing = m_editor->IsSimulationPlaying() && false == m_editor->IsSimulationPaused();
        const char* text = nullptr;
        ImU32 color = IM_COL32(210, 216, 224, 255);
        if (m_editor->GetCanvas() == nullptr)
        {
            text = Loc::TextOr(LocKeys::GameViewNoCanvas, "no canvas is open");
        }
        else if (false == hasImage)
        {
            text = Loc::TextOr(LocKeys::GameViewNoCamera, "there is no camera");
        }
        else if (playing)
        {
            text = Loc::TextOr(LocKeys::GameViewPlaying, "Playing");
            color = IM_COL32(100, 230, 120, 255);
        }
        else
        {
            text = Loc::TextOr(LocKeys::GameViewStopped, "Stopped");
        }
        ImGui::GetWindowDrawList()->AddText(ImVec2(left + 12.0f, top + 10.0f), color, text);
    }
}
