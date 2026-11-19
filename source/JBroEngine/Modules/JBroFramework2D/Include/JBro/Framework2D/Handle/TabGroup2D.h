#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Int.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class TabGroup2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `TabGroup2D` 다(D-302·D-357). 같은 오브젝트의 토글 그룹이 고른 번호의 페이지(`GetContent` 의 자식)만 켠다.
    class TabGroup2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::TabGroup2D";
        }

        GameObject GetContent() const;
        void SetContent(GameObject value);
        Int32 GetPage() const;

    private:
        Component::TabGroup2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(TabGroup2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<TabGroup2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<TabGroup2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(TabGroup2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::TabGroup2D);
