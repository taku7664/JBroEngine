#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class ToggleGroup2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `ToggleGroup2D` 다(D-302·D-355). 이 오브젝트와 자손의 토글 가운데 하나만 켜진다. `GetValue` 는 켜진 토글의 번호(계층 차례, -1 은 없음)다.
    class ToggleGroup2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::ToggleGroup2D";
        }

        Bool GetAllowOff() const;
        void SetAllowOff(Bool value);
        Int32 GetValue() const;

    private:
        Component::ToggleGroup2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(ToggleGroup2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<ToggleGroup2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<ToggleGroup2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(ToggleGroup2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::ToggleGroup2D);
