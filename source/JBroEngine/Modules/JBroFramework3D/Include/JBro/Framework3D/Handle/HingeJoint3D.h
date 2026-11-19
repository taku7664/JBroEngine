#pragma once

#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Angle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class HingeJoint3D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `HingeJoint3D` 다(D-302). 데이터는 엔진의 `Component::HingeJoint3D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 경첩 조인트다(D-426). 각도는 도(°)이고, 축 둘레로 오른손으로 도는 쪽이 양수다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class HingeJoint3D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<HingeJoint3D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::HingeJoint3D";
        }

        GameObject GetConnectedObject() const;
        void SetConnectedObject(GameObject value);
        Vector3 GetAnchor() const;
        void SetAnchor(Vector3 value);
        Vector3 GetConnectedAnchor() const;
        void SetConnectedAnchor(Vector3 value);
        Bool GetAutoConnectedAnchor() const;
        void SetAutoConnectedAnchor(Bool value);
        Vector3 GetAxis() const;
        void SetAxis(Vector3 value);
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
        Component::HingeJoint3D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(HingeJoint3D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<HingeJoint3D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<HingeJoint3D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(HingeJoint3D, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::HingeJoint3D);
