#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class Navigation2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Navigation2D` 다(D-302·D-358). 방향마다 포커스가 갈 위젯 오브젝트다.
    class Navigation2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Navigation2D";
        }

        GameObject GetUp() const;
        void SetUp(GameObject value);
        GameObject GetDown() const;
        void SetDown(GameObject value);
        GameObject GetLeft() const;
        void SetLeft(GameObject value);
        GameObject GetRight() const;
        void SetRight(GameObject value);

    private:
        Component::Navigation2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Navigation2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Navigation2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Navigation2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Navigation2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Navigation2D);
