#pragma once

// 3D 빌트인 컴포넌트의 열거형이다. 컴포넌트 데이터 헤더와 스크립트가 함께 본다(D-302 - 데이터 헤더는 스크립트 대상에서 막혀 있다).

#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Types/PhysicsLayerMask.h>

#include <cstdint>

namespace JBro::Component
{
    enum class CameraProjection3D { Perspective, Orthographic };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::CameraProjection3D, "Component::CameraProjection3D",
        { Component::CameraProjection3D::Perspective,  "Perspective" },
        { Component::CameraProjection3D::Orthographic, "Orthographic" });
}

namespace JBro::Component
{
    // 후처리 볼륨의 모양이다(D-346). `Global` 은 캔버스 어디서나, `Box`·`Sphere` 는 오브젝트의 트랜스폼에 놓인 모양 안이다.
    enum class PostProcessVolumeShape3D : std::uint8_t { Global, Box, Sphere };
    // 3D 라이트의 종류다(D-407). 값의 차례는 파일과 렌더러의 `Light3DKind` 가 함께 쓰므로 뒤에만 더한다.
    enum class Light3DType : std::uint8_t
    {
        // 캔버스 전체에 같은 빛이다(환경광). 자리·방향을 보지 않는다. 여럿이면 더한다. 2D 의 `Global` 이다.
        Ambient,
        // 해처럼 끝없이 먼 빛이다. 오브젝트의 로컬 -Z 쪽으로 간다. 자리를 보지 않는다.
        Directional,
        // 둘레로 퍼지는 빛이다.
        Point,
        // 오브젝트의 로컬 -Z 를 향한 원뿔이다.
        Spot,
    };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::PostProcessVolumeShape3D, "Component::PostProcessVolumeShape3D",
        { Component::PostProcessVolumeShape3D::Global, "Global" },
        { Component::PostProcessVolumeShape3D::Box,    "Box" },
        { Component::PostProcessVolumeShape3D::Sphere, "Sphere" });
    JBRO_DEFINE_ENUM_TYPE(Component::Light3DType, "Component::Light3DType",
        { Component::Light3DType::Ambient,     "Ambient" },
        { Component::Light3DType::Directional, "Directional" },
        { Component::Light3DType::Point,       "Point" },
        { Component::Light3DType::Spot,        "Spot" });
}

namespace JBro::Component
{
    // 글자 판이 어디를 보나(D-222). Transform 은 오브젝트의 회전을 따르는 판(간판·벽의 글씨), Billboard 는 늘 카메라를 보는 판(이름표)이다.
    // Billboard 는 오브젝트의 위치와 크기만 쓰고 회전은 뷰마다 카메라의 것으로 바꾼다 - 시뮬레이션 뷰와 편집 뷰가 각자 제 카메라를 본다.
    enum class TextFacing3D : std::uint8_t { Transform, Billboard };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::TextFacing3D, "Component::TextFacing3D",
        { Component::TextFacing3D::Transform, "Transform" },
        { Component::TextFacing3D::Billboard, "Billboard" });
}

namespace JBro::Component
{
    // 3D 물리 몸의 종류다(D-416). 2D 의 `BodyType2D` 와 같은 값·같은 차례다.
    enum class BodyType3D : std::uint8_t { Static, Kinematic, Dynamic };
    // 3D 콜라이더의 모양이다(D-416). 값의 차례는 파일이 쓰므로 뒤에만 더한다. `Mesh` 는 D-418 이다.
    enum class ColliderShape3D : std::uint8_t { Box, Sphere, Capsule, Mesh };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::BodyType3D, "Component::BodyType3D",
        { Component::BodyType3D::Static,    "Static" },
        { Component::BodyType3D::Kinematic, "Kinematic" },
        { Component::BodyType3D::Dynamic,   "Dynamic" });
    JBRO_DEFINE_ENUM_TYPE(Component::ColliderShape3D, "Component::ColliderShape3D",
        { Component::ColliderShape3D::Box,     "Box" },
        { Component::ColliderShape3D::Sphere,  "Sphere" },
        { Component::ColliderShape3D::Capsule, "Capsule" },
        { Component::ColliderShape3D::Mesh,    "Mesh" });
}

namespace JBro
{
    // 충돌·트리거 훅이 받는 값이다(D-417, 2D 의 `Collision2D` 와 같은 모양). DLL 경계를 넘으므로 POD 다. normal 은 받는 쪽에서 상대 쪽으로 간다.
    // point 는 대표 접촉점(월드)이고, 끝(Exit) 훅에서는 둘 다 0 이다 - 이미 떨어졌다.
    struct Collision3D
    {
        Handle::GameObject other;
        Component::BodyType3D bodyType = Component::BodyType3D::Dynamic;
        Vector3 point;
        Vector3 normal;
    };

    // 반직선·밀기 질의의 결과다(D-417, 2D 의 `RaycastHit2D` 와 같은 모양). normal 은 맞은 표면의 바깥(쏜 쪽을 향한다)이고 distance 는
    // 출발점에서 맞은 자리까지다. 출발부터 겹쳐 있으면 distance 0 이다.
    struct RaycastHit3D
    {
        Handle::GameObject other;
        Component::BodyType3D bodyType = Component::BodyType3D::Static;
        Vector3 point;
        Vector3 normal;
        Float distance = 0.0f;
    };
}
