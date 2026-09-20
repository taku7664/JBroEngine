#pragma once

#include <JBro/Host/IFramework.h>

namespace JBro
{
    class Renderer;
    class RenderWorld2D;
    namespace Internal
    {
        // Submits views inside a host-owned Renderer frame.
        RenderResult SubmitRenderWorld2D(const RenderWorld2D& world, Renderer& renderer);
        // 같은 그릴 것을 **편집 카메라로** 에디터의 텍스처에 한 번 더 낸다(D-130).
        // 게임 카메라가 없어도 그린다 - 캔버스 뷰는 카메라가 없는 캔버스도 보여야 한다.
        RenderResult SubmitEditorView2D(
            const RenderWorld2D& world, Renderer& renderer, const EditorViewDesc& view);
    }
}
