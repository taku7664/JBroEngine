#pragma once

#include <JBro/Editor/Gizmo/GizmoModel.h>
#include <JBro/Editor/Widget/Common.h>

namespace JBro::Widget
{
    // 프레임을 넘어 남는 기즈모 상태다. 부르는 쪽(패널)이 들고 있다.
    struct GizmoState
    {
        GizmoDrag drag;
        GizmoAxis hovered = GizmoAxis::None;
        bool dragging = false;
    };

    // 한 프레임의 결과다. `dragStarted` 프레임에 부르는 쪽이 편집 전 값을 떠 두고, `dragging` 동안
    // `subject` 를 대상에 쓰고, `dragEnded` 프레임에 커맨드로 확정한다(§11.3).
    struct GizmoOutput
    {
        GizmoSubject subject;
        GizmoAxis axis = GizmoAxis::None;
        bool hovered = false;
        bool dragStarted = false;
        bool dragging = false;
        bool dragEnded = false;
    };

    // 게임 뷰 그림 위에 손잡이를 그리고 마우스로 끈다(D-109). `camera` 의 사각형이 그림이 붙은 자리다.
    // `interactive` 가 거짓이면 그리기만 한다(선택이 없거나 창이 가려졌을 때).
    GizmoOutput Gizmo(GizmoMode mode, const GizmoCamera& camera, const GizmoSubject& subject,
        GizmoState& state, bool interactive);

    // 그림 위에 그린 손잡이 하나를 ImGui 항목으로 올린다(폴리곤 버텍스 손잡이). 누가 가리켰는지·잡았는지는
    // 부르는 쪽이 제 모양으로 잰다 - 여기서는 그 결과를 창의 hover·active 로 알릴 뿐이다. 그래야 창 이동이나
    // 뒤의 입력 자리가 같은 마우스를 받지 않고, 테스트가 hover id 로 손잡이를 찾는다.
    // `pressed` 는 잡는 프레임이다(창에 포커스를 준다).
    void OverlayHandle(const char* id, bool hovered, bool holding, bool pressed);

    // 이동·회전·크기 셋 중 하나를 고르는 단추 줄이다. 라벨은 부르는 쪽이 로컬라이징해 준다.
    // `hotkeys` 가 참이면 이 창에 포커스가 있을 때 W·E·R 로도 바뀐다. 바뀌었으면 참이다.
    bool GizmoModeBar(GizmoMode& mode, const char* translateLabel, const char* rotateLabel, const char* scaleLabel,
        bool hotkeys);
}
