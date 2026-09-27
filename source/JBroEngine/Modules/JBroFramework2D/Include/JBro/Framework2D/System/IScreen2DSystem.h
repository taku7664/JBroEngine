#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Math2D.h>

namespace JBro::System
{
    // 스크립트 서비스가 호스트의 화면 역투영과 버튼 상태에 묻는 길이다(D-237). 가상 함수 표가 스크립트 DLL 과의 ABI 이므로 바꾸면
    // `Framework2DSystemContextAbiVersion` 을 올린다(D-28).
    //
    // Main-thread only. 좌표는 **지난 프레임에 그린 화면**의 것이다 - 입력 체인은 이번 프레임의 갱신보다 먼저 돈다.
    class IScreen2DSystem
    {
    public:
        virtual ~IScreen2DSystem() = default;

        // 게임 화면 픽셀(왼쪽 위 원점, y 아래 - 마우스·터치의 좌표)을 `object` 가 선 레이어의 좌표로 옮긴다. 월드 레이어면 월드 좌표(주 카메라),
        // 화면 레이어면 기준 픽셀(가운데 원점, y 위)이다. 오브젝트가 없거나 월드 레이어에 카메라가 없으면 거짓이고 결과를 건드리지 않는다.
        virtual bool ScreenToLayer(Vector2 pixel, GameObjectHandle object, Vector2& point) const = 0;
        virtual bool LayerToScreen(Vector2 point, GameObjectHandle object, Vector2& pixel) const = 0;
        // 이번 프레임에 포인터가 버튼 위에 있었거나 버튼을 누르고 있는가. 그러면 입력 레이어 `"UI"` 아래는 빈 포인터를 본다.
        virtual bool IsPointerOverButton() const = 0;
    };
}
