#pragma once

#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Angle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class HingeJoint2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `HingeJoint2D` 다(D-302). 데이터는 엔진의 `Component::HingeJoint2D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 경첩 조인트다(D-233). 각도는 도(°)이고 반시계가 양수다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class HingeJoint2D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<HingeJoint2D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::HingeJoint2D";
        }

        GameObject GetConnectedObject() const;
        void SetConnectedObject(GameObject value);
        Vector2 GetAnchor() const;
        void SetAnchor(Vector2 value);
        Vector2 GetConnectedAnchor() const;
        void SetConnectedAnchor(Vector2 value);
        Bool GetAutoConnectedAnchor() const;
        void SetAutoConnectedAnchor(Bool value);
        Bool GetUseLimits() const;
        void SetUseLimits(Bool value);
        Degree GetLowerAngle() const;
        void SetLowerAngle(Degree value);
        Degree GetUpperAngle() const;
        void SetUpperAngle(Degree value);
        Bool GetUseMotor() const;
        void SetUseMotor(Bool value);

        // 도/초다.
        Degree GetMotorSpeed() const;
        void SetMotorSpeed(Degree value);
        Float GetMaxMotorTorque() const;
        void SetMaxMotorTorque(Float value);
        Bool GetCollideConnected() const;
        void SetCollideConnected(Bool value);

    private:
        Component::HingeJoint2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(HingeJoint2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<HingeJoint2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<HingeJoint2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(HingeJoint2D, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::HingeJoint2D);
