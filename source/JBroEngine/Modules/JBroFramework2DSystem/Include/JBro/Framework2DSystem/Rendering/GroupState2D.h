#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

// 그룹(`CanvasGroup2D`, D-358)이 자손에 거는 것이다. 오브젝트와 조상의 그룹을 모두 본다 - 추출(스프라이트·글자의 알파)과 위젯 판정이 같은 함수로 잰다.
namespace JBro
{
    class Canvas;
    namespace Object
    {
        class GameObject;
    }
    // 조상 그룹의 알파를 모두 곱한 것이다. 그룹이 없으면 1 이다. 유한하지 않은 알파는 1 로 본다.
    Float ComputeGroupAlpha2D(Canvas& canvas, Object::GameObject* object);
    // 조상 그룹이 하나라도 `interactable` 이 아니면 거짓이다.
    Bool IsGroupInteractable2D(Canvas& canvas, Object::GameObject* object);
}
