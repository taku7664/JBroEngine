#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Math2D.h>

namespace JBro
{
    class Canvas;
    class GameObject;
}

namespace JBro::Internal
{
    // 오브젝트의 월드 자세. 물리는 고정 스텝에서 돌고 `Transform2D` 의 월드 캐시는 Update 가 채우므로,
    // 캐시를 믿지 않고 저작 값에서 지금 다시 계산한다(기존 엔진과 같은 판단).
    //
    // angle 은 행렬에서 크기를 나눈 뒤의 회전이다. 크기는 부호를 가진 채 도형의 로컬 점에 곱한다 - 음수 크기는 뒤집기다.
    struct ObjectPose
    {
        Matrix3x2 matrix;
        Vec2      position;
        float     angle = 0.0f;
        Vec2      scale{ 1.0f, 1.0f };
    };

    // 활성 `Transform2D` 가 없으면 false 다. 부모에 Transform 이 꺼져 있으면 그 아래도 자세가 없다(ProjectRule §6).
    bool CalculateObjectPose(Canvas& canvas, GameObject* object, ObjectPose& result);

    Component::BodyType2D GetBodyType(Canvas& canvas, GameObject* object);
}
