#pragma once

#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Math2D.h>

#include <type_traits>

namespace JBro::Component
{
    class Mask2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Mask2D` 다(D-302·D-351). 자손을 사각형 안에서만 그린다. 사각형은 `offset` 가운데의 `size` 다.
    class Mask2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Mask2D";
        }

        Vector2 GetSize() const;
        void SetSize(Vector2 value);
        Vector2 GetOffset() const;
        void SetOffset(Vector2 value);

    private:
        Component::Mask2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Mask2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Mask2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Mask2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Mask2D, window)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Mask2D);
