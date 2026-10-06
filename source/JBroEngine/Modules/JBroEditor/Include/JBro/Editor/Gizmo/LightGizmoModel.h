#pragma once

#include <JBro/Framework2D/Component/Light2D.h>
#include <JBro/Types/Angle.h>
#include <JBro/Types/Math2D.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

// 캔버스 뷰의 2D 라이트 기즈모(D-291 5 단계, tasks/lighting2d-plan.md)에서 화면을 모르는 부분이다. `PolygonEditModel` 처럼 ImGui 없이 월드 숫자로 잰다.
// 손잡이는 넷이다 - 안쪽·바깥 반지름(`innerRadius`·`outerRadius`), `Spot` 의 안쪽·바깥 각(`innerAngle`·`outerAngle`). 손잡이 하나는 필드 하나를 고친다.
namespace JBro::LightGizmoModel
{
    // 손잡이를 잡는 반지름(화면 픽셀).
    inline constexpr Float HandlePickRadius = 7.0f;
    // 반지름 손잡이가 가운데에서 이보다 가까이 그려지지 않는다(화면 픽셀) - 안쪽 반지름이 0 이어도 옮기기 기즈모의 가운데를 가리지 않는다.
    inline constexpr Float MinHandleDistance = 16.0f;

    enum class Handle : std::uint8_t
    {
        None,
        InnerRadius,
        OuterRadius,
        InnerAngle,
        OuterAngle,
    };

    // 라이트의 월드 자리와 축(오브젝트의 +x, 길이 1)이다. 렌더러가 받는 것과 같다(`Light2DSystem`).
    struct Pose
    {
        Vector2 center;
        Vector2 axis{ 1.0f, 0.0f };
    };

    // 월드 행렬에서 자리와 축을 뜬다. 크기는 축에 들지 않고, 크기가 0 이면 축은 +x 다.
    Pose PoseFromWorld(const Matrix3x2& world);

    // 손잡이를 둘 수 있는 라이트인가. `Global` 은 자리가 없어 손잡이도 없다.
    Bool HasHandles(const Component::Light2D& light);
    // 그 라이트에 있는 손잡이인가. 각 손잡이는 `Spot` 에만 있다.
    Bool HasHandle(const Component::Light2D& light, Handle handle);

    // 반지름 손잡이가 놓이는 방향(월드, 길이 1)이다. `Spot` 은 원뿔의 축이고, `Point` 는 축에서 시계 방향으로 45 도다 -
    // 옮기기 기즈모의 x·y 손잡이(오른쪽·위) 사이를 피한다.
    Vector2 RadiusDirection(const Component::Light2D& light, const Pose& pose);
    // 손잡이의 월드 자리다. 반지름 손잡이는 그 반지름에 있고, 각 손잡이는 바깥 반지름의 원 위에서 바깥 각은 축의 왼쪽(반시계),
    // 안쪽 각은 오른쪽(시계)의 반각에 있다 - 둘이 겹치지 않는다.
    Vector2 HandlePosition(const Component::Light2D& light, const Pose& pose, Handle handle);

    // 반지름을 끈 값이다. 잡은 자리에서 마우스가 반지름 방향으로 간 만큼 더한다(잡은 자리가 손잡이와 조금 어긋나도 튀지 않는다).
    // 0 과 필드의 상한(1000) 사이에 둔다.
    Float DragRadius(Float startRadius, Vector2 direction, Vector2 grabWorld, Vector2 mouseWorld);
    // 마우스 자리가 가리키는 원뿔 **전체** 각이다. 축과 마우스가 이루는 각의 두 배이고 0 과 360 사이다. 마우스가 가운데에 있으면 `fallback` 이다.
    Degree AngleAt(const Pose& pose, Vector2 mouseWorld, Degree fallback);

    // 손잡이가 고치는 필드의 이름과 손잡이의 ImGui Id 다.
    const char* FieldName(Handle handle);
    const char* HandleId(Handle handle);
}
