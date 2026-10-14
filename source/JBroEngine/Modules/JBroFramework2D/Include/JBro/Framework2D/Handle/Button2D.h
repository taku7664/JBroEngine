#pragma once

#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class Button2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Button2D` 다(D-302). 데이터는 엔진의 `Component::Button2D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 누를 수 있는 사각형이다(D-237). 이번 프레임의 상태(`GetHovered`·`GetPressed`·`GetClicked`)는 시스템이 채우고 읽기만 한다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class Button2D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<Button2D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::Button2D";
        }

        Vector2 GetSize() const;
        void SetSize(Vector2 value);
        Vector2 GetOffset() const;
        void SetOffset(Vector2 value);
        Bool GetInteractable() const;
        void SetInteractable(Bool value);
        Bool GetTintSprite() const;
        void SetTintSprite(Bool value);
        Color GetNormalTint() const;
        void SetNormalTint(Color value);
        Color GetHoverTint() const;
        void SetHoverTint(Color value);
        Color GetPressedTint() const;
        void SetPressedTint(Color value);
        Color GetDisabledTint() const;
        void SetDisabledTint(Color value);
        Bool GetHovered() const;
        Bool GetPressed() const;

        // 버튼 위에서 눌렀다가 버튼 위에서 뗀 그 프레임에만 참이다.
        Bool GetClicked() const;

    private:
        Component::Button2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Button2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Button2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Button2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Button2D, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Button2D);
