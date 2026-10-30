#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Math2D.h>

// 마스크가 자르는 사각형이다(D-351, ui-plan §6.2 3 단계). 오브젝트와 그 조상의 `Mask2D` 를 모두 교집합한 축 정렬 사각형(레이어 좌표 -
// 화면 레이어는 기준 픽셀, 월드는 유닛)이고, 렌더 추출(스프라이트·글자)과 위젯 판정이 같은 함수로 잰다 - 그려지는 것과 눌리는 것이 어긋나지 않는다.
namespace JBro
{
    class Canvas;
    namespace Object
    {
        class GameObject;
    }

    // `rect` 는 {minX, minY, maxX, maxY} 다. 자르는 마스크가 하나도 없으면 거짓이고 `rect` 를 건드리지 않는다.
    // 마스크 자신의 오브젝트도 잘린다(자손에 자신을 포함한다). 사각형이 아직 서지 않은 마스크(`clipValid` 거짓)는 없는 것이다.
    Bool ComputeClipRect2D(Canvas& canvas, Object::GameObject* object, Float rect[4]);

    // 점이 사각형 안(경계 포함)인가.
    Bool ClipRectContains(const Float rect[4], Vector2 point);
}
