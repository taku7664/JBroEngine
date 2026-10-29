#pragma once

#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Framework2D/Handle/SpriteRenderer2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Math2D.h>

#include <type_traits>

namespace JBro::Component
{
    class Slider2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Slider2D` 다(D-302·D-351). 데이터는 엔진의 `Component::Slider2D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 끌어서 값을 고르는 막대다. `GetValue`·`SetValue` 는 0..1 이고 스크립트가 쓰면 손잡이·채우기가 따라온다.
    // 이번 프레임의 상태(`GetHovered`·`GetPressed`·`GetChanged`)는 시스템이 채우고 읽기만 한다.
    class Slider2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Slider2D";
        }

        Vector2 GetSize() const;
        void SetSize(Vector2 value);
        Vector2 GetOffset() const;
        void SetOffset(Vector2 value);
        Bool GetInteractable() const;
        void SetInteractable(Bool value);
        Component::SliderDirection GetDirection() const;
        void SetDirection(Component::SliderDirection value);
        Float GetValue() const;
        void SetValue(Float value);
        GameObject GetHandle() const;
        void SetHandle(GameObject value);
        SpriteRenderer2D GetFill() const;
        void SetFill(SpriteRenderer2D value);
        Bool GetHovered() const;
        Bool GetPressed() const;

        // 포인터가 값을 바꾼 그 프레임에만 참이다.
        Bool GetChanged() const;

    private:
        Component::Slider2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Slider2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Slider2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Slider2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Slider2D, volume)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Slider2D);
