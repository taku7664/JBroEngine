#pragma once

#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>

#include <type_traits>

namespace JBro::Component
{
    class Layout2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Layout2D` 다(D-302·D-352). 자식을 칸에 늘어놓는다. 블록의 크기(`GetContentWidth`·`GetContentHeight`)는 시스템이 채우고 읽기만 한다.
    class Layout2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Layout2D";
        }

        Component::LayoutMode GetMode() const;
        void SetMode(Component::LayoutMode value);
        Vector2 GetCellSize() const;
        void SetCellSize(Vector2 value);
        Vector2 GetSpacing() const;
        void SetSpacing(Vector2 value);
        Vector2 GetPadding() const;
        void SetPadding(Vector2 value);
        Int32 GetColumns() const;
        void SetColumns(Int32 value);
        Vector2 GetPivot() const;
        void SetPivot(Vector2 value);
        Bool GetFitChildren() const;
        void SetFitChildren(Bool value);
        Float GetContentWidth() const;
        Float GetContentHeight() const;

    private:
        Component::Layout2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Layout2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Layout2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Layout2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Layout2D, list)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Layout2D);
