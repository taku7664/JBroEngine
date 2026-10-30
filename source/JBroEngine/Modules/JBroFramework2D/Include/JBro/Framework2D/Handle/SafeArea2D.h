#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class SafeArea2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `SafeArea2D` 다(D-302·D-358). 화면 레이어 루트의 앵커를 안전 영역에서 잰다.
    class SafeArea2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::SafeArea2D";
        }

        Bool GetHorizontal() const;
        void SetHorizontal(Bool value);
        Bool GetVertical() const;
        void SetVertical(Bool value);

    private:
        Component::SafeArea2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(SafeArea2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<SafeArea2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<SafeArea2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(SafeArea2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::SafeArea2D);
