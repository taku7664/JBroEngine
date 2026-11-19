#pragma once

#include <JBro/Framework3D/Component/Light3D.h>
#include <JBro/Types/Angle.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/Math3D.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

// 캔버스 뷰의 3D 라이트 기즈모(D-353, lighting3d-plan 3 단계)에서 화면을 모르는 부분이다. 2D 의 `LightGizmoModel` 과 같은 자리다.
// 손잡이는 셋이다 - `range`(점광은 구 위, 스포트는 축 끝), `Spot` 의 바깥 각과 안쪽 각(원뿔 끝 원 위, 서로 맞은편). 손잡이 하나는 필드 하나를 고친다.
// 끌기는 화면 좌표로 잰다 - 손잡이가 놓인 자리와 축을 화면으로 투영한 것과 마우스만 받는다(3D 의 깊이를 마우스 하나로 정할 수 없다).
namespace JBro::LightGizmo3DModel
{
    // 손잡이를 잡는 반지름(화면 픽셀). 2D 와 같다.
    inline constexpr Float HandlePickRadius = 7.0f;
    // `range` 가 0(끝없음)인 라이트를 그릴 길이다(월드 단위). 손잡이도 여기 선다 - 끌면 그 거리가 `range` 가 된다.
    inline constexpr Float UnboundedLength = 5.0f;
    // 방향광의 화살표 길이(월드 단위).
    inline constexpr Float ArrowLength = 1.5f;

    enum class Handle : std::uint8_t
    {
        None,
        Range,
        OuterAngle,
        InnerAngle,
    };

    // 라이트의 월드 자리와 방향이다. `side` 는 방향에 수직인 오브젝트의 로컬 +X 다(각 손잡이가 놓이는 쪽).
    struct Pose
    {
        Vector3 position;
        Vector3 direction{0.0f, 0.0f, -1.0f};
        Vector3 side{1.0f, 0.0f, 0.0f};
    };

    // 오브젝트의 월드 자리와 회전에서 뜬다. 빛은 로컬 -Z 로 간다(`Light3DSystem` 과 같다).
    Pose PoseFrom(const Vector3& position, const Quaternion& rotation);

    // 손잡이가 있는 라이트인가. `Ambient` 와 `Directional` 은 고칠 거리·각이 없다.
    Bool HasHandles(const Component::Light3D& light);
    Bool HasHandle(const Component::Light3D& light, Handle handle);

    // 그리는 길이다 - `range` 가 있으면 그것, 없으면 `UnboundedLength`.
    Float ShownLength(const Component::Light3D& light);
    // 원뿔 끝 원의 반지름이다(그 길이에서). 전체 각의 반각 탄젠트 × 길이.
    Float ConeRadius(Degree fullAngle, Float length);

    // 손잡이의 월드 자리다. 점광의 `range` 손잡이는 구 위에서 화면 오른쪽(`cameraRight`)이고, 스포트의 것은 축 끝이다.
    // 바깥 각은 원뿔 끝 원 위의 `side` 쪽, 안쪽 각은 그 맞은편이다.
    Vector3 HandlePosition(const Component::Light3D& light, const Pose& pose, Handle handle, const Vector3& cameraRight);

    // `range` 를 끈 값이다. `center` 는 라이트 자리, `unit` 은 거기서 1 유닛 간 자리(점광은 화면 오른쪽, 스포트는 축)를 화면에 투영한 것이다.
    // 점광은 마우스와 가운데 사이의 화면 거리, 스포트는 축에 내린 화면 거리를 유닛으로 바꾼다. 0 과 1000 사이다.
    Float DragRange(Bool alongAxis, Vector2 center, Vector2 unit, Vector2 mouse);
    // 마우스가 가리키는 원뿔 **전체** 각이다. `apex` 는 라이트 자리, `axisEnd` 는 축 위 한 점을 화면에 투영한 것이다.
    // 축에서 마우스까지의 화면 거리와 축을 따라 간 거리의 각의 두 배이고 0 과 179 사이다. 마우스가 꼭짓점 뒤에 있으면 `fallback` 이다.
    Degree DragAngle(Vector2 apex, Vector2 axisEnd, Vector2 mouse, Degree fallback);

    // 손잡이가 고치는 필드의 이름과 손잡이의 ImGui Id 다.
    const char* FieldName(Handle handle);
    const char* HandleId(Handle handle);
}
