#pragma once

#include <JBro/Runtime/ComponentHandle.h>
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
    // 3D 물리 몸의 골격이다(D-53).
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class Rigidbody3D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<Rigidbody3D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::Rigidbody3D";
        }

        Vector3 GetVelocity() const;
        void SetVelocity(Vector3 value);
        Float GetMass() const;
        void SetMass(Float value);

    private:
        Component::Rigidbody3D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Rigidbody3D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Rigidbody3D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Rigidbody3D>, "a component handle must stay trivially copyable");
}
