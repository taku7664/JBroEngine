#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class DropTarget2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `DropTarget2D` 다(D-302·D-357). 끌리는 것이 놓이는 자리다. 놓인 프레임에 `GetDropped` 가 그 오브젝트다. 상태는 읽기만 한다.
    class DropTarget2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::DropTarget2D";
        }

        Vector2 GetSize() const;
        void SetSize(Vector2 value);
        Vector2 GetOffset() const;
        void SetOffset(Vector2 value);
        Bool GetHovering() const;
        GameObject GetDropped() const;

    private:
        Component::DropTarget2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(DropTarget2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<DropTarget2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<DropTarget2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(DropTarget2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::DropTarget2D);
