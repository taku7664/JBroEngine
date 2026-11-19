#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Rigidbody3D`·`Collider3D`)로만 만진다(D-302).
// 정규화한 이름으로 써도 스크립트 대상 빌드에서는 여기서 컴파일이 실패한다.
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches the 3D physics components through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Reflection/Math3DReflection.h>
#include <JBro/Reflection/PhysicsLayerMaskReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

// 3D 물리 컴포넌트다(D-416, physics3d-plan §2.2). 필드 이름과 뜻은 2D 의 `Rigidbody2D`·`Collider2D` 를 따른다 - 축이 하나 늘어난 것만 다르다.
// 시뮬레이션은 `System::Physics3DSystem` 이 Jolt 커널(`JBroPhysics3D`)로 돌린다.
namespace JBro::System
{
    class Physics3DSystem;
}

namespace JBro::Component
{
    // 스크립트가 다음 고정 스텝에 가할 힘·충격량을 모은 것이다(D-420, 2D 의 `PendingForces2D` 와 같은 모양). 위치를 준 힘은 월드 원점에 대한
    // 모멘트(cross(점, 힘))로 모아 두고, 물리가 그 스텝의 질량 중심으로 토크를 푼다 - 컴포넌트는 질량 중심을 모른다.
    struct PendingForces3D
    {
        Vector3 forceAtCenter;
        Vector3 forceAtPoints;
        Vector3 forceMoment;
        Vector3 torque;
        Vector3 impulseAtCenter;
        Vector3 impulseAtPoints;
        Vector3 impulseMoment;
        Vector3 angularImpulse;
        Bool wake = false;
    };

    class Rigidbody3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Rigidbody3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Rigidbody3D)

        JBRO_FIELD(BodyType3D, bodyType) = BodyType3D::Dynamic;
        // 속도는 시뮬레이션이 매 프레임 다시 쓴다. 저장하면 씬을 열 때마다 물체가 저장된 순간의 속도로 튀어 나간다(2D 와 같다).
        JBRO_FIELD(JBro::Vector3, linearVelocity, NoSerialize());
        // 라디안/초, 월드 축 둘레.
        JBRO_FIELD(JBro::Vector3, angularVelocity, NoSerialize());
        JBRO_FIELD(Float, mass, Range(0.0f, 1000.0f)) = 1.0f;
        JBRO_FIELD(Float, gravityScale) = 1.0f;
        JBRO_FIELD(Float, linearDamping) = 0.0f;
        JBRO_FIELD(Float, angularDamping) = 0.0f;
        // 축 고정. 그 축으로는 중력·접촉 어느 것으로도 움직이거나 돌지 않는다. 2D 의 `fixedRotation` 은 회전 셋을 모두 고정한 것이다.
        JBRO_FIELD(Bool, freezePositionX) = false;
        JBRO_FIELD(Bool, freezePositionY) = false;
        JBRO_FIELD(Bool, freezePositionZ) = false;
        JBRO_FIELD(Bool, freezeRotationX) = false;
        JBRO_FIELD(Bool, freezeRotationY) = false;
        JBRO_FIELD(Bool, freezeRotationZ) = false;
        // 멈춰 있으면 잠들 수 있는가. 잠든 몸은 계산에서 빠지고, 닿으면 깬다.
        JBRO_FIELD(Bool, canSleep) = true;

        // 힘·토크는 다음 고정 스텝 한 번 동안 가해지고, 충격량은 그 스텝이 시작할 때 속도를 바꾼다(D-420, 2D 와 같다). 월드 좌표다.
        // Dynamic 이 아니면 물리가 버린다.
        void AddForce(Vector3 force)
        {
            m_pending.forceAtCenter = Add(m_pending.forceAtCenter, force);
        }
        void AddForceAtPosition(Vector3 force, Vector3 worldPoint)
        {
            m_pending.forceAtPoints = Add(m_pending.forceAtPoints, force);
            m_pending.forceMoment = Add(m_pending.forceMoment, Cross(worldPoint, force));
        }
        void AddTorque(Vector3 torque)
        {
            m_pending.torque = Add(m_pending.torque, torque);
        }
        void AddImpulse(Vector3 impulse)
        {
            m_pending.impulseAtCenter = Add(m_pending.impulseAtCenter, impulse);
        }
        void AddImpulseAtPosition(Vector3 impulse, Vector3 worldPoint)
        {
            m_pending.impulseAtPoints = Add(m_pending.impulseAtPoints, impulse);
            m_pending.impulseMoment = Add(m_pending.impulseMoment, Cross(worldPoint, impulse));
        }
        void AddAngularImpulse(Vector3 impulse)
        {
            m_pending.angularImpulse = Add(m_pending.angularImpulse, impulse);
        }
        // 다음 고정 스텝에 깨운다. 지난 고정 스텝이 끝났을 때 잠들어 있었는가.
        void WakeUp()
        {
            m_pending.wake = true;
        }
        Bool IsSleeping() const
        {
            return m_sleeping;
        }
        // 물리 시스템이 고정 스텝마다 가져가고 비운다.
        PendingForces3D TakePendingForces()
        {
            const PendingForces3D taken = m_pending;
            m_pending = {};
            return taken;
        }

    private:
        friend class System::Physics3DSystem;

        // 저장하지 않고 인스펙터에도 없다 - 스크립트가 쌓고 물리가 가져가는 한 스텝짜리 값이다.
        PendingForces3D m_pending;
        Bool m_sleeping = false;
    };

    // 한 오브젝트에 여럿 붙는다(2D 와 같다, D-416). 고체 콜라이더는 그 오브젝트의 바디 하나에 복합 도형으로 들고, 트리거는 따로 센서로 든다.
    // 크기는 트랜스폼의 월드 크기를 곱한다.
    class Collider3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Collider3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Collider3D)

        JBRO_FIELD(ColliderShape3D, shape) = ColliderShape3D::Box;
        // 오브젝트 원점에서 도형 중심까지(로컬, 크기를 곱하기 전).
        JBRO_FIELD(JBro::Vector3, offset);
        // 상자의 크기(전체 길이).
        JBRO_FIELD(JBro::Vector3, size) { 1.0f, 1.0f, 1.0f };
        // 구와 캡슐의 반지름.
        JBRO_FIELD(Float, radius) = 0.5f;
        // 캡슐의 전체 높이(로컬 Y, 양 끝의 반구를 넣어서). 반지름의 두 배보다 작으면 구다.
        JBRO_FIELD(Float, height) = 2.0f;
        // **메시 콜라이더**(D-418, 모양이 `Mesh` 일 때). 비면 같은 오브젝트의 `MeshRenderer3D` 가 그리는 메시를 쓴다(빈 `materialId` 가 메시의 재질을 쓰는 것과 같은 규칙).
        // `mesh` 는 해석 패스가 `meshId` 로 채우는 에셋 핸들이다(D-395).
        JBRO_FIELD(AssetId, meshId);
        JBRO_FIELD(AssetHandle, mesh, NoSerialize() | ReadOnly() | Tooltip("meshId 에서 해석된 값") | Hidden());
        // 참이면 메시를 감싼 볼록 껍질이다 - 어떤 몸에도 쓴다. 거짓이면 삼각형 메시이고 움직이지 않는 몸만 받는다. 움직이는 몸(켜진 동적·키네마틱
        // `Rigidbody3D`)이나 트리거에 거짓으로 붙이면 껍질로 바꾸고 경고를 한 번 남긴다(사용자 결정, 유니티의 MeshCollider 와 같은 칸).
        JBRO_FIELD(Bool, convex) = false;
        JBRO_FIELD(Bool, isTrigger) = false;
        JBRO_FIELD(Float, friction, Range(0.0f, 2.0f)) = 0.6f;
        JBRO_FIELD(Float, restitution, Range(0.0f, 1.0f)) = 0.0f;
        // 이 콜라이더가 속한 레이어와 만나는 레이어(2D 와 같다). Jolt 는 레이어를 바디마다 하나 두므로, 한 오브젝트의 고체 콜라이더 여럿은
        // 그중 첫 콜라이더의 값을 쓴다(physics3d-plan §4). 트리거는 저마다의 값이다.
        JBRO_FIELD(PhysicsLayerMask, layer) = 0x00000001u;
        JBRO_FIELD(PhysicsLayerMask, mask) = 0xFFFFFFFFu;
    };
}
