#pragma once

#include <JBro/Host/IFramework.h>

namespace JBro
{
    class Renderer;
    class RenderWorld2D;
    namespace System
    {
        class DebugDrawSystem;
    }
    namespace Internal
    {
        // Submits views inside a host-owned Renderer frame.
        // `debugDraw` 가 있으면 스프라이트 뒤에 디버그 선을 그린다(D-243). 선은 프레임의 성패에 들지 않는다.
        RenderResult SubmitRenderWorld2D(const RenderWorld2D& world, Renderer& renderer, const System::DebugDrawSystem* debugDraw);
        // 같은 그릴 것을 **편집 카메라로** 에디터의 텍스처에 한 번 더 낸다(D-130).
        // 게임 카메라가 없어도 그린다 - 캔버스 뷰는 카메라가 없는 캔버스도 보여야 한다.
        RenderResult SubmitEditorView2D(
            const RenderWorld2D& world, Renderer& renderer, const EditorViewDesc& view, const System::DebugDrawSystem* debugDraw);
    }
}
