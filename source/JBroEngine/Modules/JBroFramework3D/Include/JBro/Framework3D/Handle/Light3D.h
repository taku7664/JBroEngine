#pragma once

#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>

#include <type_traits>

namespace JBro::Component
{
    class Light3D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Light3D` 다(D-302, D-367). 데이터는 엔진의 `Component::Light3D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class Light3D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<Light3D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::Light3D";
        }

        Color GetColor() const;
        void SetColor(Color value);
        Float GetIntensity() const;
        void SetIntensity(Float value);
        Float GetRange() const;
        void SetRange(Float value);

    private:
        Component::Light3D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Light3D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Light3D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Light3D>, "a component handle must stay trivially copyable");
}
