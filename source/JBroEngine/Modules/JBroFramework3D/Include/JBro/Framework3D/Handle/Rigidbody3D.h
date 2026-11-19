#pragma once

#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class Rigidbody3D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Rigidbody3D` 다(D-302). 데이터는 엔진의 `Component::Rigidbody3D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class Rigidbody3D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<Rigidbody3D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::Rigidbody3D";
        }

        // 2D 핸들과 같은 이름이다(D-416, 전에는 `Get/SetVelocity`).
        Vector3 GetLinearVelocity() const;
        void SetLinearVelocity(Vector3 value);
        // 라디안/초, 월드 축 둘레.
        Vector3 GetAngularVelocity() const;
        void SetAngularVelocity(Vector3 value);
        Float GetMass() const;
        void SetMass(Float value);
        Component::BodyType3D GetBodyType() const;
        void SetBodyType(Component::BodyType3D value);
        Float GetGravityScale() const;
        void SetGravityScale(Float value);
        Float GetLinearDamping() const;
        void SetLinearDamping(Float value);
        Float GetAngularDamping() const;
        void SetAngularDamping(Float value);
        Bool GetFreezePositionX() const;
        void SetFreezePositionX(Bool value);
        Bool GetFreezePositionY() const;
        void SetFreezePositionY(Bool value);
        Bool GetFreezePositionZ() const;
        void SetFreezePositionZ(Bool value);
        Bool GetFreezeRotationX() const;
        void SetFreezeRotationX(Bool value);
        Bool GetFreezeRotationY() const;
        void SetFreezeRotationY(Bool value);
        Bool GetFreezeRotationZ() const;
        void SetFreezeRotationZ(Bool value);
        Bool GetCanSleep() const;
        void SetCanSleep(Bool value);
        // 힘·토크는 다음 고정 스텝 한 번 동안 가해지고, 충격량은 그 스텝이 시작할 때 속도를 바꾼다(D-420, 2D 와 같다). 월드 좌표다.
        // Dynamic 이 아니면 물리가 버린다.
        void AddForce(Vector3 force);
        void AddForceAtPosition(Vector3 force, Vector3 worldPoint);
        void AddTorque(Vector3 torque);
        void AddImpulse(Vector3 impulse);
        void AddImpulseAtPosition(Vector3 impulse, Vector3 worldPoint);
        void AddAngularImpulse(Vector3 impulse);
        // 다음 고정 스텝에 깨운다.
        void WakeUp();
        // 지난 고정 스텝이 끝났을 때 잠들어 있었는가.
        Bool IsSleeping() const;

    private:
        Component::Rigidbody3D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Rigidbody3D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Rigidbody3D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Rigidbody3D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Rigidbody3D, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Rigidbody3D);
