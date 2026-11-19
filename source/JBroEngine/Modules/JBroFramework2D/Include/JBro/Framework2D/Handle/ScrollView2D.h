#pragma once

#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Math2D.h>

#include <type_traits>

namespace JBro::Component
{
    class ScrollView2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `ScrollView2D` 다(D-302·D-351). 끌어서 내용을 움직이는 창이다. 뷰포트는 같은 오브젝트의 `Mask2D` 이고 `GetContent` 가 움직이는 자식이다.
    // 이번 프레임의 상태(`GetDragging`·`GetVelocity`)는 시스템이 채우고 읽기만 한다.
    class ScrollView2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::ScrollView2D";
        }

        GameObject GetContent() const;
        void SetContent(GameObject value);
        // 스크롤바(D-355) - `Slider2D` 가 붙은 오브젝트다.
        GameObject GetHorizontalBar() const;
        void SetHorizontalBar(GameObject value);
        GameObject GetVerticalBar() const;
        void SetVerticalBar(GameObject value);
        Vector2 GetContentSize() const;
        void SetContentSize(Vector2 value);
        Bool GetHorizontal() const;
        void SetHorizontal(Bool value);
        Bool GetVertical() const;
        void SetVertical(Bool value);
        Float GetScrollSensitivity() const;
        void SetScrollSensitivity(Float value);
        Float GetDecelerationRate() const;
        void SetDecelerationRate(Float value);
        Bool GetDragging() const;
        Vector2 GetVelocity() const;

    private:
        Component::ScrollView2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(ScrollView2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<ScrollView2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<ScrollView2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(ScrollView2D, list)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::ScrollView2D);
