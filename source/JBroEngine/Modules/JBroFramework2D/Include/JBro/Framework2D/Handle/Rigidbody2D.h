#pragma once

#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class Rigidbody2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Rigidbody2D` 다(D-302). 데이터는 엔진의 `Component::Rigidbody2D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 2D 물리 몸이다. 힘·토크는 다음 고정 스텝 한 번 동안 가해지고, 충격량은 그 스텝이 시작할 때 속도를 바꾼다(D-227). 월드 좌표다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class Rigidbody2D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<Rigidbody2D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::Rigidbody2D";
        }

        Component::BodyType2D GetBodyType() const;
        void SetBodyType(Component::BodyType2D value);
        Vector2 GetLinearVelocity() const;
        void SetLinearVelocity(Vector2 value);
        Float GetAngularVelocity() const;
        void SetAngularVelocity(Float value);
        Float GetMass() const;
        void SetMass(Float value);
        Float GetGravityScale() const;
        void SetGravityScale(Float value);
        Float GetLinearDamping() const;
        void SetLinearDamping(Float value);
        Float GetAngularDamping() const;
        void SetAngularDamping(Float value);
        Bool GetFixedRotation() const;
        void SetFixedRotation(Bool value);
        Bool GetFreezePositionX() const;
        void SetFreezePositionX(Bool value);
        Bool GetFreezePositionY() const;
        void SetFreezePositionY(Bool value);
        Bool GetCanSleep() const;
        void SetCanSleep(Bool value);

        // Dynamic 이 아니면 물리가 버린다.
        void AddForce(Vector2 force);
        void AddForceAtPosition(Vector2 force, Vector2 worldPoint);
        void AddTorque(Float torque);
        void AddImpulse(Vector2 impulse);
        void AddImpulseAtPosition(Vector2 impulse, Vector2 worldPoint);
        void AddAngularImpulse(Float impulse);

        // 다음 고정 스텝에 깨운다(D-229).
        void WakeUp();

        // 지난 고정 스텝이 끝났을 때 잠들어 있었는가.
        Bool IsSleeping() const;

    private:
        Component::Rigidbody2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Rigidbody2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Rigidbody2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Rigidbody2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Rigidbody2D, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Rigidbody2D);
