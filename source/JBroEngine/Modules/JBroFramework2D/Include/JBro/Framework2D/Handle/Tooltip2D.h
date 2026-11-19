#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class Tooltip2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Tooltip2D` 다(D-302·D-357). 포인터가 머물면 `GetTarget` 오브젝트를 켠다. `GetShown` 은 시스템이 채우고 읽기만 한다.
    class Tooltip2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Tooltip2D";
        }

        Vector2 GetSize() const;
        void SetSize(Vector2 value);
        Vector2 GetOffset() const;
        void SetOffset(Vector2 value);
        Float GetDelay() const;
        void SetDelay(Float value);
        // 보이는 동안 대상을 포인터 자리 + 오프셋에 둔다(D-360).
        Bool GetFollow() const;
        void SetFollow(Bool value);
        Vector2 GetFollowOffset() const;
        void SetFollowOffset(Vector2 value);
        GameObject GetTarget() const;
        void SetTarget(GameObject value);
        Bool GetShown() const;

    private:
        Component::Tooltip2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Tooltip2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Tooltip2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Tooltip2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Tooltip2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Tooltip2D);
