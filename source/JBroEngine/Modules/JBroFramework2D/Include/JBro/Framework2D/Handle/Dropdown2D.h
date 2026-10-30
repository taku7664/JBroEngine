#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Framework2D/Handle/SpriteRenderer2D.h>
#include <JBro/Framework2D/Handle/Text2D.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class Dropdown2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Dropdown2D` 다(D-302·D-355). 누르면 목록(`GetList`, 자식 오브젝트)을 펴고 그 아래의 토글 가운데 하나를 고르면 `GetValue` 가 그 번호다.
    // `SetValue`·`SetOpen` 을 스크립트가 써도 된다 - 캡션·목록·항목 토글이 따라온다. 전이는 버튼과 같다. 이번 프레임의 상태(`GetHovered`·`GetPressed`·`GetChanged`)는 읽기만 한다.
    class Dropdown2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Dropdown2D";
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
        GameObject GetList() const;
        void SetList(GameObject value);
        Text2D GetCaption() const;
        void SetCaption(Text2D value);
        Int32 GetValue() const;
        void SetValue(Int32 value);
        Bool GetOpen() const;
        void SetOpen(Bool value);
        Bool GetHovered() const;
        Bool GetPressed() const;
        Bool GetFocused() const;
        Bool GetChanged() const;

    private:
        Component::Dropdown2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Dropdown2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Dropdown2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Dropdown2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Dropdown2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Dropdown2D);
