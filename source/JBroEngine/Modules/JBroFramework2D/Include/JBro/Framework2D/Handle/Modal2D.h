#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class Modal2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Modal2D` 다(D-302·D-357). 켜져 있는 동안 이 오브젝트 아래의 위젯만 입력을 받는다. 켜고 끄는 것은 오브젝트의 `SetActive` 다.
    class Modal2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Modal2D";
        }

        Bool GetBlockGame() const;
        void SetBlockGame(Bool value);

    private:
        Component::Modal2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Modal2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Modal2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Modal2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Modal2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Modal2D);
