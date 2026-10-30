#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class CanvasGroup2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `CanvasGroup2D` 다(D-302·D-358). 아래의 그림을 한꺼번에 흐리게 하고 위젯을 한꺼번에 막는다.
    class CanvasGroup2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::CanvasGroup2D";
        }

        Float GetAlpha() const;
        void SetAlpha(Float value);
        Bool GetInteractable() const;
        void SetInteractable(Bool value);
        // 거짓이면 아래 위젯을 포인터가 지나간다(D-361). 포커스는 그대로다.
        Bool GetBlocksRaycasts() const;
        void SetBlocksRaycasts(Bool value);

    private:
        Component::CanvasGroup2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(CanvasGroup2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<CanvasGroup2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<CanvasGroup2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(CanvasGroup2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::CanvasGroup2D);
