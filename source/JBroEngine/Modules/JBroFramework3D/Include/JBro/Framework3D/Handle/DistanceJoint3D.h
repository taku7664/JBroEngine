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
    class DistanceJoint3D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `DistanceJoint3D` 다(D-302). 데이터는 엔진의 `Component::DistanceJoint3D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 거리 조인트다(D-426). `ConnectedObject` 가 비었으면 `ConnectedAnchor` 는 월드의 점이다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class DistanceJoint3D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<DistanceJoint3D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::DistanceJoint3D";
        }

        GameObject GetConnectedObject() const;
        void SetConnectedObject(GameObject value);
        Vector3 GetAnchor() const;
        void SetAnchor(Vector3 value);
        Vector3 GetConnectedAnchor() const;
        void SetConnectedAnchor(Vector3 value);
        Bool GetAutoDistance() const;
        void SetAutoDistance(Bool value);
        Float GetDistance() const;
        void SetDistance(Float value);
        Bool GetMaxDistanceOnly() const;
        void SetMaxDistanceOnly(Bool value);
        Float GetFrequency() const;
        void SetFrequency(Float value);
        Float GetDampingRatio() const;
        void SetDampingRatio(Float value);
        Bool GetCollideConnected() const;
        void SetCollideConnected(Bool value);
        // 끊는 힘·돌림힘(D-437). 기본은 무한이다.
        Float GetBreakForce() const;
        void SetBreakForce(Float value);
        Float GetBreakTorque() const;
        void SetBreakTorque(Float value);

    private:
        Component::DistanceJoint3D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(DistanceJoint3D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<DistanceJoint3D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<DistanceJoint3D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(DistanceJoint3D, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::DistanceJoint3D);
