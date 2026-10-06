#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/Bool.h>

namespace JBro::Service
{
    // 스크립트가 마우스·터치의 픽셀을 오브젝트의 좌표로 옮기고, 포인터가 버튼 위인지 묻는다(D-237, ui-plan §2.5).
    // 뜻은 `System::IScreen2DSystem` 과 같다. 시스템이 없으면 거짓이다. Main-thread only.
    //
    //     Vector2 point;
    //     if (Screen2D.ScreenToLayer({ mouse.x, mouse.y }, GetGameObject(), point)) { ... }
    class Screen2DService
    {
    public:
        Bool ScreenToLayer(Vector2 pixel, GameObjectHandle object, Vector2& point) const;
        Bool LayerToScreen(Vector2 point, GameObjectHandle object, Vector2& pixel) const;
        Bool IsPointerOverButton() const;
    };
}
