#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Math2DReflection.h>
#include <JBro/Reflection/ContainerTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Array.h>

#include <cstdint>

namespace JBro::Component
{
    enum class BodyType2D      : std::uint8_t { Static, Kinematic, Dynamic };
    enum class ColliderShape2D : std::uint8_t { Box, Circle, Capsule, Polygon };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::BodyType2D, "Component::BodyType2D",
        { Component::BodyType2D::Static,    "Static" },
        { Component::BodyType2D::Kinematic, "Kinematic" },
        { Component::BodyType2D::Dynamic,   "Dynamic" });

    JBRO_DEFINE_ENUM_TYPE(Component::ColliderShape2D, "Component::ColliderShape2D",
        { Component::ColliderShape2D::Box,     "Box" },
        { Component::ColliderShape2D::Circle,  "Circle" },
        { Component::ColliderShape2D::Capsule, "Capsule" },
        { Component::ColliderShape2D::Polygon, "Polygon" });
}

namespace JBro::Component
{
    class Rigidbody2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Rigidbody2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Rigidbody2D)

        JBRO_FIELD(BodyType2D, bodyType) = BodyType2D::Dynamic;
        // 속도는 시뮬레이션이 매 프레임 다시 쓴다. 저장하면 씬을 열 때마다
        // 물체가 저장된 순간의 속도로 튀어 나간다.
        JBRO_FIELD(Vec2,  linearVelocity,  NoSerialize());
        JBRO_FIELD(float, angularVelocity, NoSerialize()) = 0.0f;
        JBRO_FIELD(float, mass,          Range(0, 1000)) = 1.0f;
        JBRO_FIELD(float, gravityScale)  = 1.0f;
        JBRO_FIELD(float, linearDamping) = 0.0f;
        JBRO_FIELD(bool,  fixedRotation) = false;
    };

    class Collider2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Collider2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Collider2D)

        JBRO_FIELD(ColliderShape2D, shape) = ColliderShape2D::Box;
        JBRO_FIELD(Vec2,  offset);
        JBRO_FIELD(Vec2,  size) { 1.0f, 1.0f };
        JBRO_FIELD(float, radius) = 0.5f;
        JBRO_FIELD(bool,  isTrigger) = false;
        // Polygon 의 꼭짓점이다(D-199). 오브젝트 로컬이고 offset 을 더한 뒤 트랜스폼의 크기를 곱한다. 오목해도 되고
        // 감긴 방향은 상관없다 - 물리 커널이 정리해 볼록 조각으로 나눈다. 자기 교차하면 그 콜라이더는 충돌하지 않는다.
        JBRO_FIELD(Array<Vec2>, points);
        // 표면 성질과 충돌 거르기는 도형의 것이다(D-199 (4)). 두 도형의 마찰은 기하 평균, 반발은 큰 쪽으로 섞는다.
        JBRO_FIELD(float, friction, Range(0, 2)) = 0.6f;
        JBRO_FIELD(float, restitution, Range(0, 1)) = 0.0f;
        // 두 콜라이더는 (A.layer & B.mask) 와 (B.layer & A.mask) 가 모두 0 이 아닐 때만 만난다.
        JBRO_FIELD(std::uint32_t, layer) = 0x00000001u;
        JBRO_FIELD(std::uint32_t, mask) = 0xFFFFFFFFu;
    };
}

namespace JBro
{
    // 충돌·트리거 훅이 받는 접촉이다(D-207). normal 은 받는 쪽에서 상대 쪽이다. 트리거와 끝 이벤트는 point·normal 이 0 이다.
    struct Collision2D
    {
        GameObjectHandle other;
        Component::BodyType2D bodyType = Component::BodyType2D::Dynamic;
        Vec2 point;
        Vec2 normal;
    };

    // 질의가 모든 레이어를 본다(콜라이더의 `layer` 비트와 AND 해서 0 이 아니면 대상이다).
    inline constexpr std::uint32_t AllPhysicsLayers = 0xFFFFFFFFu;

    // 반직선·스윕 질의의 결과다(physics-plan §4 의 6, 기존 엔진 `RaycastHit2D`). 스윕에서 "어디까지 갈 수 있는가" 가
    // 거리라서 접촉과 따로 둔다. normal 은 맞은 표면의 바깥(쏜 쪽을 향한다)이다. 출발부터 겹쳐 있으면 distance 0,
    // normal 은 쏜 방향의 반대, point 는 쏜 모양의 중심이다.
    struct RaycastHit2D
    {
        GameObjectHandle other;
        Component::BodyType2D bodyType = Component::BodyType2D::Static;
        Vec2 point;
        Vec2 normal;
        float distance = 0.0f;
    };
}
