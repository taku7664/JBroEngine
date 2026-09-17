#include "GameViewPanel.h"

#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Runtime/GameObject.h>

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
        // 기즈모 모드 단추 줄. 게임 그림 위에 놓는다.
        Widget::GizmoModeBar("##gizmo_mode", m_gizmoMode,
            Loc::TextOr(LocKeys::GizmoTranslate, "Move"),
            Loc::TextOr(LocKeys::GizmoRotate, "Rotate"),
            Loc::TextOr(LocKeys::GizmoScale, "Scale"), true);

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
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        // 이 프레임에 게임 화면을 붙였다. 붙이지 않은 프레임(닫힘·다른 탭에 가림)에는
        // 게임을 그리지 않는다(D-63).
        m_editor->RequestGameView();

        DrawGizmo(imageMin.x, imageMin.y, size.x, size.y);
    }

    void GameViewPanel::DrawGizmo(float imageLeft, float imageTop, float imageWidth, float imageHeight)
    {
        GameObject* selected = m_editor->GetSelectedObject();
        Renderer* renderer = m_editor->GetRenderer();
        const Extent2D extent = m_editor->GetGameViewExtent();
        CameraParams lastCamera;
        if (renderer == nullptr || false == renderer->GetLastViewCamera(lastCamera))
        {
            // 게임이 아직 카메라를 낸 프레임이 없다. 끌던 것이 있으면 놓는다.
            if (m_editing.IsActive())
            {
                m_editing.Cancel(*m_editor);
                m_gizmoState.dragging = false;
            }
            return;
        }
        GizmoSubject subject;
        const bool hasSubject = selected != nullptr && GizmoEditing::ReadSubject(*m_editor, *selected, subject);
        if (false == hasSubject && false == m_gizmoState.dragging)
        {
            return;
        }
        // 카메라의 뷰포트는 게임 텍스처 픽셀이다. 그림이 패널에 붙은 크기로 옮긴다.
        const float scaleX = imageWidth / static_cast<float>(extent.width);
        const float scaleY = imageHeight / static_cast<float>(extent.height);
        const float left = imageLeft + lastCamera.viewport.x * scaleX;
        const float top = imageTop + lastCamera.viewport.y * scaleY;
        GizmoCamera camera;
        if (false == GizmoModel::MakeCamera(lastCamera.view, lastCamera.projection, left, top,
                lastCamera.viewport.width * scaleX, lastCamera.viewport.height * scaleY, camera))
        {
            return;
        }
        // 끄는 동안은 시작 대상이 기준이다. 대상이 사라졌으면(선택이 바뀜) 끌기를 취소한다.
        if (m_gizmoState.dragging && false == hasSubject)
        {
            m_editing.Cancel(*m_editor);
            m_gizmoState.dragging = false;
            return;
        }
        const GizmoSubject shown = m_gizmoState.dragging ? m_gizmoState.drag.start : subject;
        const Widget::GizmoOutput output = Widget::Gizmo(m_gizmoMode, camera, shown, m_gizmoState, true);
        if (output.dragStarted)
        {
            if (false == m_editing.Begin(*m_editor, m_gizmoMode, shown))
            {
                m_gizmoState.dragging = false;
                return;
            }
        }
        if (output.dragging && m_editing.IsActive())
        {
            m_editing.Apply(*m_editor, output.subject);
        }
        if (output.dragEnded && m_editing.IsActive())
        {
            m_editing.Commit(*m_editor);
        }
    }
}
