#pragma once

#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/Gizmo/GizmoModel.h>
#include <JBro/Editor/Widget/Gizmo.h>
#include <JBro/RHI/RHI.h>

#include "../Gizmo/GizmoEditing.h"

namespace JBro
{
    // 게임 화면을 보여 주는 패널이다. 게임은 에디터가 잡아 둔 텍스처에 그려지고
    // 여기서는 그것을 붙이기만 한다(D-63). 그 위에 고른 오브젝트의 트랜스폼 기즈모가 얹힌다(D-109) -
    // 이동·회전·크기 손잡이를 끌면 커맨드 하나로 편집된다. 3D 와 2D 가 같은 기즈모다.
    class GameViewPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;

    private:
        void DrawGizmo(float imageLeft, float imageTop, float imageWidth, float imageHeight);

        EditorApplication* m_editor = nullptr;
        GizmoMode m_gizmoMode = GizmoMode::Translate;
        Widget::GizmoState m_gizmoState;
        GizmoEditing m_editing;
    };
}
