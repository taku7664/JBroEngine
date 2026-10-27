#pragma once

#include <JBro/Types/Float.h>
#include <JBro/Types/Math2D.h>

namespace JBro::Component
{
    class PostProcessVolume2D;
    class Transform2D;
}

namespace JBro
{
    // **카메라 자리에서 2D 볼륨의 무게**(D-346). `weight` x 거리 비율이다 - 모양 안이면 1, 밖 `blendDistance` 안이면 경계에서 멀어질수록 0 으로 줄고,
    // 그 밖은 0 이다. `Global` 은 트랜스폼을 보지 않고 `weight` 다. 상자·원은 트랜스폼의 월드 위치·회전·크기에 놓인다(크기는 `size`·`radius` 에 곱한다).
    // 트랜스폼이 없으면(nullptr) 모양 볼륨은 0 이다. 0..1 로 자른다.
    Float ComputePostProcessVolumeWeight2D(
        const Component::PostProcessVolume2D& volume, const Component::Transform2D* transform, Vector2 camera);
}
