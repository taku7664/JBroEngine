#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class Draggable2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Draggable2D` 다(D-302·D-357). 끌어 놓는 것이다. 놓은 프레임에 `GetDroppedOn` 이 놓을 자리의 오브젝트다. 상태는 시스템이 채우고 읽기만 한다.
    class Draggable2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Draggable2D";
        }

        Vector2 GetSize() const;
        void SetSize(Vector2 value);
        Vector2 GetOffset() const;
        void SetOffset(Vector2 value);
        Bool GetInteractable() const;
        void SetInteractable(Bool value);
        Bool GetSnapBack() const;
        void SetSnapBack(Bool value);
        // 끌리는 동안 그림의 `renderOrder` 에 더하는 값이다(D-360). 0 이면 올리지 않는다.
        Int32 GetLiftOrder() const;
        void SetLiftOrder(Int32 value);
        Bool GetDragging() const;
        GameObject GetDroppedOn() const;

    private:
        Component::Draggable2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Draggable2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Draggable2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Draggable2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Draggable2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Draggable2D);
