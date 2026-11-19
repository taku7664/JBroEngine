#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class VirtualKeyboard2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `VirtualKeyboard2D` 다(D-302·D-358). 아래의 버튼이 글자 칸에 글자를 넣는다.
    class VirtualKeyboard2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::VirtualKeyboard2D";
        }

        GameObject GetTarget() const;
        void SetTarget(GameObject value);
        GameObject GetBackspace() const;
        void SetBackspace(GameObject value);
        GameObject GetEnter() const;
        void SetEnter(GameObject value);
        GameObject GetSpace() const;
        void SetSpace(GameObject value);

    private:
        Component::VirtualKeyboard2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(VirtualKeyboard2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<VirtualKeyboard2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<VirtualKeyboard2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(VirtualKeyboard2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::VirtualKeyboard2D);
