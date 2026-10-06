#include "GameViewPanel.h"

#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Button.h>

#include <imgui.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    const char* GameViewPanel::GetTitle() const
    {
        // 안정된 이름이다. 번역하지 않는다 - 창의 정체가 여기 달려 있다.
        return TypeName;
    }

    const char* GameViewPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelGame, "Game");
    }

    Bool GameViewPanel::OnCreate(EditorApplication& editor)
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
            ImGui::GetColorU32(EditorTheme::ViewportBackground));

        const Bool hasImage = gameView.IsValid() && extent.width != 0 && extent.height != 0;
        Float imageLeft = 0.0f;
        Float imageTop = 0.0f;
        Float imageWidth = 0.0f;
        Float imageHeight = 0.0f;
        if (hasImage)
        {
            // **비율을 지켜 패널 안에 맞춘다(레터박스).** 늘려 붙이면 에디터 창 모양에
            // 따라 게임이 찌그러져 보인다.
            const Float viewAspect =
                static_cast<JBro::Float>(extent.width) / static_cast<JBro::Float>(extent.height);
            const Float panelAspect = panel.x / panel.y;
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
            imageLeft = imageMin.x;
            imageTop = imageMin.y;
            imageWidth = size.x;
            imageHeight = size.y;
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
        // 게임 입력(D-214). 포커스가 여기 있으면 다음 프레임부터 게임이 키를 받고, 마우스는 이 그림 사각형 기준의 게임 픽셀이다.
        m_editor->ReportGameView(IsFocused(), imageLeft, imageTop, imageWidth, imageHeight);

        DrawStatusOverlay(origin.x, origin.y, hasImage);
    }

    void GameViewPanel::DrawStatusOverlay(Float left, Float top, Bool hasImage) const
    {
        // 기존 엔진의 게임 뷰와 같은 자리, 같은 내용이다. 그림이 안 나올 때 **왜 안 나오는지**를
        // 말하지 않으면 고장과 구분되지 않는다.
        const Bool playing = m_editor->IsSimulationPlaying() && false == m_editor->IsSimulationPaused();
        const char* text = nullptr;
        // 글자 앞의 아이콘이 상태를 먼저 말한다(D-278) - 재생·멈춤·경고가 글자를 읽기 전에 갈린다.
        const char* icon = Icons::Warning;
        ImU32 color = IM_COL32(210, 216, 224, 255);
        if (m_editor->GetCanvas() == nullptr)
        {
            text = Loc::TextOr(LocKeys::GameViewNoCanvas, "no canvas is open");
        }
        else if (false == hasImage || false == m_editor->DidGameSubmitLastFrame())
        {
            // **그림 자리가 있어도 게임이 낸 것이 없으면 카메라가 없는 것이다**(D-178).
            // 텍스처가 있는지만 보면 카메라 없는 검은 화면을 "실행 중" 이라고 말한다 -
            // 기존 게임 뷰는 그 둘을 갈랐다.
            // 카메라가 있는데 값이 잘못되어 건너뛴 것이면 그렇게 말한다(D-239). "카메라 없음" 이라고 하면 붙어 있는 카메라를 찾아 헤맨다.
            const Bool unusable = m_editor->GetUnusableGameCameraCount() > 0;
            text = unusable
                ? Loc::TextOr(LocKeys::GameViewCameraUnusable, "the Camera2D values cannot be drawn")
                : Loc::TextOr(LocKeys::GameViewNoCamera, "there is no camera");
            icon = unusable ? Icons::Warning : Icons::NoCamera;
        }
        else if (playing)
        {
            text = Loc::TextOr(LocKeys::GameViewPlaying, "Playing");
            icon = Icons::Play;
            color = IM_COL32(100, 230, 120, 255);
        }
        else
        {
            text = Loc::TextOr(LocKeys::GameViewStopped, "Stopped");
            icon = Icons::Stop;
        }
        const Float lineHeight = ImGui::GetTextLineHeight();
        const Float gap = ImGui::GetStyle().ItemInnerSpacing.x;
        const auto statusLine = [&](Float y, const char* glyph, ImU32 tint, const char* message) {
            const ImVec2 iconMin(left + 12.0f, y);
            Widget::DrawGlyphCentered(glyph, iconMin, ImVec2(iconMin.x + lineHeight, iconMin.y + lineHeight), tint);
            ImGui::GetWindowDrawList()->AddText(ImVec2(iconMin.x + lineHeight + gap, y), tint, message);
        };
        statusLine(top + 10.0f, icon, color, text);

        // **어느 카메라로 그리고 있는지 애매하면 말한다**(D-187, 기존 캔버스 인스펙터의
        // `camera_ambiguous` 경고). `primary` 를 켠 카메라가 없으면 첫 활성 카메라로
        // 그리는데, 활성 카메라가 여럿이면 그중 무엇인지는 순회 순서가 정한다.
        if (Canvas* canvas = m_editor->GetCanvas())
        {
            std::size_t active = 0;
            Bool anyPrimary = false;
            canvas->ForEach<Component::Camera2D>([&](Component::Camera2D& camera) {
                if (false == camera.IsActiveComponent())
                {
                    return;
                }
                ++active;
                anyPrimary = anyPrimary || camera.primary;
            });
            if (false == anyPrimary && active > 1)
            {
                statusLine(top + 28.0f, Icons::Warning, IM_COL32(255, 200, 90, 255),
                    Loc::TextOr(LocKeys::GameViewCameraAmbiguous,
                        "no camera is primary, so the first active one is used"));
            }
        }
    }
}
