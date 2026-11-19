#pragma once

#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Math2D.h>

#include <type_traits>

namespace JBro::Component
{
    class ContentSizeFitter2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `ContentSizeFitter2D` 다(D-302·D-355). 같은 오브젝트의 내용 크기(레이아웃 블록·글자)에 스프라이트·마스크·위젯 사각형을 맞춘다.
    // 이번 프레임의 크기(`GetContentWidth`·`GetContentHeight`)는 시스템이 채우고 읽기만 한다.
    class ContentSizeFitter2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::ContentSizeFitter2D";
        }

        Bool GetFitWidth() const;
        void SetFitWidth(Bool value);
        Bool GetFitHeight() const;
        void SetFitHeight(Bool value);
        Vector2 GetPadding() const;
        void SetPadding(Vector2 value);
        Vector2 GetMinSize() const;
        void SetMinSize(Vector2 value);
        Float GetContentWidth() const;
        Float GetContentHeight() const;

    private:
        Component::ContentSizeFitter2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(ContentSizeFitter2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<ContentSizeFitter2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<ContentSizeFitter2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(ContentSizeFitter2D, fitter)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::ContentSizeFitter2D);
