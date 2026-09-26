#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Math2DReflection.h>
#include <JBro/Reflection/ContainerTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/ScalarCodec.h>
#include <JBro/Reflection/TypeDescriptorOf.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Types/Array.h>

#include <cstdint>

namespace JBro::Component
{
    enum class BodyType2D      : std::uint8_t { Static, Kinematic, Dynamic };
    enum class ColliderShape2D : std::uint8_t { Box, Circle, Capsule, Polygon, Chain };
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
        { Component::ColliderShape2D::Polygon, "Polygon" },
        { Component::ColliderShape2D::Chain,   "Chain" });
}

namespace JBro
{
    // 물리 레이어 비트 묶음이다(D-232). 메모리와 파일은 부호 없는 32 비트 정수 그대로이고(옛 파일과 같은 글자), 이름은
    // 프로젝트 설정의 `PhysicsLayers` 가 준다 - 비트 i 가 그 목록의 i 번째 이름이다. 에디터는 이 타입 이름을 보고 이름을
    // 고르는 칸을 그린다. 정수와 저절로 오가므로 스크립트는 `collider->layer = 1u << 3;` 처럼 쓴다.
    struct PhysicsLayerMask
    {
        std::uint32_t bits = 0;

        constexpr PhysicsLayerMask() = default;
        constexpr PhysicsLayerMask(std::uint32_t value)
            : bits(value)
        {
        }

        constexpr operator std::uint32_t() const
        {
            return bits;
        }
    };

    // 잎사귀다. 정수 코덱을 그대로 쓴다(구조체의 첫 멤버가 그 정수다).
    template <>
    struct TypeDescriptorOf<PhysicsLayerMask>
    {
        static const TypeDescriptor& Get()
        {
            static const TypeDescriptor descriptor = [] {
                TypeDescriptor made;
                made.typeName = NameTable::Get().Intern("JBro.PhysicsLayerMask");
                made.size = static_cast<std::uint32_t>(sizeof(PhysicsLayerMask));
                made.alignment = static_cast<std::uint32_t>(alignof(PhysicsLayerMask));
                made.triviallyCopyable = true;
                made.codec = &GetScalarCodec<std::uint32_t>();
                return made;
            }();
            return descriptor;
        }
    };

    // 프로젝트가 담는 물리 레이어 수다. 비트 하나가 레이어 하나다.
    inline constexpr std::uint32_t PhysicsLayerCount = 32;
}

namespace JBro::System
{
    class Physics2DSystem;
}

namespace JBro::Component
{
    // 스크립트가 다음 고정 스텝에 가할 힘·충격량을 모은 것이다(D-227). 위치를 준 힘은 월드 원점에 대한 모멘트(cross(점, 힘))로 모아
    // 두고, 물리가 그 스텝의 질량 중심으로 토크를 푼다 - 컴포넌트는 질량 중심을 모른다.
    struct PendingForces2D
    {
        Vec2  forceAtCenter;
        Vec2  forceAtPoints;
        float forceMoment = 0.0f;
        float torque = 0.0f;
        Vec2  impulseAtCenter;
        Vec2  impulseAtPoints;
        float impulseMoment = 0.0f;
        float angularImpulse = 0.0f;
        // 잠든 몸을 깨운다(D-229).
        bool  wake = false;
    };

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
        JBRO_FIELD(float, angularDamping) = 0.0f;
        JBRO_FIELD(bool,  fixedRotation) = false;
        // 축 고정(D-227). 그 축으로는 중력·힘·접촉 어느 것으로도 움직이지 않는다.
        JBRO_FIELD(bool,  freezePositionX) = false;
        JBRO_FIELD(bool,  freezePositionY) = false;
        // 멈춰 있으면 잠들 수 있는가(D-229). 잠든 몸은 계산에서 빠지고, 닿거나 힘을 받으면 깬다.
        JBRO_FIELD(bool,  canSleep) = true;

        // 힘·토크는 다음 고정 스텝 한 번 동안 가해지고, 충격량은 그 스텝이 시작할 때 속도를 바꾼다(D-227). 월드 좌표다.
        // Dynamic 이 아니면 물리가 버린다.
        void AddForce(Vec2 force)
        {
            m_pending.forceAtCenter = { m_pending.forceAtCenter.x + force.x, m_pending.forceAtCenter.y + force.y };
        }
        void AddForceAtPosition(Vec2 force, Vec2 worldPoint)
        {
            m_pending.forceAtPoints = { m_pending.forceAtPoints.x + force.x, m_pending.forceAtPoints.y + force.y };
            m_pending.forceMoment += worldPoint.x * force.y - worldPoint.y * force.x;
        }
        void AddTorque(float torque)
        {
            m_pending.torque += torque;
        }
        void AddImpulse(Vec2 impulse)
        {
            m_pending.impulseAtCenter = { m_pending.impulseAtCenter.x + impulse.x, m_pending.impulseAtCenter.y + impulse.y };
        }
        void AddImpulseAtPosition(Vec2 impulse, Vec2 worldPoint)
        {
            m_pending.impulseAtPoints = { m_pending.impulseAtPoints.x + impulse.x, m_pending.impulseAtPoints.y + impulse.y };
            m_pending.impulseMoment += worldPoint.x * impulse.y - worldPoint.y * impulse.x;
        }
        void AddAngularImpulse(float impulse)
        {
            m_pending.angularImpulse += impulse;
        }
        // 다음 고정 스텝에 깨운다. 지난 고정 스텝이 끝났을 때 잠들어 있었는가(D-229).
        void WakeUp()
        {
            m_pending.wake = true;
        }
        bool IsSleeping() const
        {
            return m_sleeping;
        }
        // 물리 시스템이 고정 스텝마다 가져가고 비운다.
        PendingForces2D TakePendingForces()
        {
            const PendingForces2D taken = m_pending;
            m_pending = {};
            return taken;
        }

    private:
        friend class System::Physics2DSystem;

        // 저장하지 않고 인스펙터에도 없다 - 스크립트가 쌓고 물리가 가져가는 한 스텝짜리 값이다.
        PendingForces2D m_pending;
        // 물리 시스템이 고정 스텝마다 쓴다.
        bool            m_sleeping = false;
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
        // Chain 이 끝과 처음을 잇는가(D-229). Chain 은 points 를 이은 선분 모음이고 두께와 질량이 없으며 두 면 모두에서 부딪힌다 -
        // 오목 폴리곤을 조각으로 나눈 바닥과 달리 이음매에서 걸리지 않는다. 포인트가 없으면 `size.x` 폭의 가로 선분이다.
        JBRO_FIELD(bool, loop) = false;
        // 한 방향 발판(D-232). 오브젝트의 위(+y) 쪽에서 오는 것만 막는다 - 밑에서 뛰어오르거나 옆에서 들어오면 지나가고, 지나가는
        // 동안에는 닿은 것도 아니다(훅이 없다). 닿기 시작할 때 방향을 보고 떨어질 때까지 그대로다.
        JBRO_FIELD(bool, oneWay) = false;
        // 표면 성질과 충돌 거르기는 도형의 것이다(D-199 (4)). 두 도형의 마찰은 기하 평균, 반발은 큰 쪽으로 섞는다.
        JBRO_FIELD(float, friction, Range(0, 2)) = 0.6f;
        JBRO_FIELD(float, restitution, Range(0, 1)) = 0.0f;
        // 두 콜라이더는 (A.layer & B.mask) 와 (B.layer & A.mask) 가 모두 0 이 아니고, 프로젝트의 레이어 충돌 표가 두 레이어를
        // 떼어 두지 않았을 때만 만난다(D-232). 비트 i 의 이름은 프로젝트 설정의 물리 레이어 i 번째다.
        JBRO_FIELD(PhysicsLayerMask, layer) = 0x00000001u;
        JBRO_FIELD(PhysicsLayerMask, mask) = 0xFFFFFFFFu;
    };

    // **거리 조인트**(D-232, 기존 엔진 `DistanceJoint2D`). 이 오브젝트의 몸을 connectedObject 의 몸과 일정한 거리로 잇는다.
    // connectedObject 가 비었으면 connectedAnchor 는 월드의 점이다. 두 오브젝트 모두 물리 몸(Rigidbody2D 나 Collider2D)이 있어야
    // 이어진다. 앵커는 오브젝트 로컬이고 트랜스폼의 크기를 곱한다.
    class DistanceJoint2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::DistanceJoint2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(DistanceJoint2D)

        JBRO_FIELD(GameObjectHandle, connectedObject);
        JBRO_FIELD(Vec2, anchor);
        JBRO_FIELD(Vec2, connectedAnchor);
        // 참이면 조인트가 처음 이어지는 순간 두 앵커 사이의 거리를 distance 에 적는다.
        JBRO_FIELD(bool, autoDistance) = true;
        JBRO_FIELD(float, distance, Range(0, 1000)) = 1.0f;
        // 참이면 밧줄이다: distance 보다 멀어지지만 않게 하고 가까워지는 것은 막지 않는다.
        JBRO_FIELD(bool, maxDistanceOnly) = false;
        // 0 보다 크면 용수철이다(초당 떨림 수). 0 이면 단단하다. 밧줄에는 쓰지 않는다.
        JBRO_FIELD(float, frequency, Range(0, 30)) = 0.0f;
        JBRO_FIELD(float, dampingRatio, Range(0, 1)) = 0.0f;
        // 거짓이면 이은 두 오브젝트의 콜라이더가 서로 부딪히지 않는다.
        JBRO_FIELD(bool, collideConnected) = false;
    };

    // **경첩 조인트**(D-232, 기존 엔진 `HingeJoint2D`). 두 몸이 한 점을 함께 쓰고 그 둘레로 돈다. 각도는 도(°)이고, 한계는
    // 조인트가 처음 이어질 때 이 오브젝트가 상대에 대해 놓인 각도를 0 으로 잰다. 반시계가 양수다.
    class HingeJoint2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::HingeJoint2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(HingeJoint2D)

        JBRO_FIELD(GameObjectHandle, connectedObject);
        JBRO_FIELD(Vec2, anchor);
        JBRO_FIELD(Vec2, connectedAnchor);
        // 참이면 조인트가 처음 이어지는 순간 anchor 가 놓인 자리를 상대의 로컬(없으면 월드)로 connectedAnchor 에 적는다.
        JBRO_FIELD(bool, autoConnectedAnchor) = true;
        JBRO_FIELD(bool, useLimits) = false;
        JBRO_FIELD(float, lowerAngle, Range(-360, 360)) = -45.0f;
        JBRO_FIELD(float, upperAngle, Range(-360, 360)) = 45.0f;
        // 모터는 이 오브젝트를 상대에 대해 motorSpeed(도/초)로 돌린다. 그러려고 쓸 수 있는 가장 큰 토크가 maxMotorTorque 다.
        JBRO_FIELD(bool, useMotor) = false;
        JBRO_FIELD(float, motorSpeed) = 0.0f;
        JBRO_FIELD(float, maxMotorTorque, Range(0, 100000)) = 1000.0f;
        JBRO_FIELD(bool, collideConnected) = false;
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
