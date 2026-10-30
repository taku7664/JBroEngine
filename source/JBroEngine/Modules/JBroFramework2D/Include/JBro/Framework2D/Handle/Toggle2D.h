#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Framework2D/Handle/SpriteRenderer2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Math2D.h>

#include <type_traits>

namespace JBro::Component
{
    class Toggle2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Toggle2D` 다(D-302·D-351). 데이터는 엔진의 `Component::Toggle2D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 켜고 끄는 단추다. `GetIsOn`·`SetIsOn` 을 스크립트가 써도 되고, 그러면 표시 스프라이트가 따라온다.
    // 이번 프레임의 상태(`GetHovered`·`GetPressed`·`GetChanged`)는 시스템이 채우고 읽기만 한다.
    class Toggle2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Toggle2D";
        }

        Vector2 GetSize() const;
        void SetSize(Vector2 value);
        Vector2 GetOffset() const;
        void SetOffset(Vector2 value);
        Bool GetInteractable() const;
        void SetInteractable(Bool value);
        // 전이(D-355) - 버튼과 같다. 상태 스프라이트의 에셋은 이번 실행의 자리(`AssetHandle`)로 바꾼다.
        Component::ButtonTransition GetTransition() const;
        void SetTransition(Component::ButtonTransition value);
        SpriteRenderer2D GetTargetGraphic() const;
        void SetTargetGraphic(SpriteRenderer2D value);
        Color GetNormalTint() const;
        void SetNormalTint(Color value);
        Color GetHoverTint() const;
        void SetHoverTint(Color value);
        Color GetPressedTint() const;
        void SetPressedTint(Color value);
        Color GetDisabledTint() const;
        void SetDisabledTint(Color value);
        AssetHandle GetHoverSprite() const;
        void SetHoverSprite(AssetHandle value);
        AssetHandle GetPressedSprite() const;
        void SetPressedSprite(AssetHandle value);
        AssetHandle GetDisabledSprite() const;
        void SetDisabledSprite(AssetHandle value);
        Bool GetIsOn() const;
        void SetIsOn(Bool value);
        SpriteRenderer2D GetCheckmark() const;
        void SetCheckmark(SpriteRenderer2D value);
        Bool GetHovered() const;
        Bool GetFocused() const;
        Bool GetPressed() const;

        // 포인터가 뒤집은 그 프레임에만 참이다.
        Bool GetChanged() const;

    private:
        Component::Toggle2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Toggle2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Toggle2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Toggle2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Toggle2D, mute)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Toggle2D);
