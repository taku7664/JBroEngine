#pragma once

#include <JBro/Canvas/Canvas.h>
#include <JBro/Runtime/Component.h>

namespace JBro::Internal
{
    // 호스트·에디터·시스템만 쓰는 실 객체 접근점이다. 스크립트 타깃의 include 경로에 없으므로
    // 사용자 코드에서는 이름 자체가 보이지 않는다. 구 엔진의 CanvasRuntimeAccess 와 같은 역할이다.
    //
    // 스크립트 표면은 GameObjectHandle 과 Ref<T> 두 가지뿐이고(D-5), 그 해석은 레지스트리를 탄다.
    // 시스템은 매 프레임 도는 경로이므로 여기서는 해석 없이 소유 포인터를 그대로 준다.
    class CanvasAccess final
    {
    public:
        static GameObject* GetOwner(const ComponentBase& component)
        {
            return component.GetOwnerObject();
        }
    };
}
