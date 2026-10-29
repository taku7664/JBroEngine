#pragma once

#include <JBro/Framework2D/Handle/SpriteRenderer2D.h>
#include <JBro/Framework2D/Handle/Text2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>

#include <type_traits>

namespace JBro::Component
{
    class InputField2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `InputField2D` 다(D-302·D-352). 글자를 받는 칸이다. `GetText` 는 다음 쓰기까지 유효한 UTF-8 이고, `SetText` 는 글자를 통째로 바꾼다.
    // `SetFocused(true)` 로 포커스를 줄 수 있다(다음 프레임부터 키보드를 받는다). 이번 프레임의 상태(`GetChanged`·`GetSubmitted`)는 시스템이 채우고 읽기만 한다.
    class InputField2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::InputField2D";
        }

        Vector2 GetSize() const;
        void SetSize(Vector2 value);
        Vector2 GetOffset() const;
        void SetOffset(Vector2 value);
        Bool GetInteractable() const;
        void SetInteractable(Bool value);
        const char* GetText() const;
        void SetText(const char* utf8);
        Text2D GetDisplay() const;
        void SetDisplay(Text2D value);
        SpriteRenderer2D GetCaret() const;
        void SetCaret(SpriteRenderer2D value);
        Int32 GetMaxLength() const;
        void SetMaxLength(Int32 value);
        Float GetBlinkRate() const;
        void SetBlinkRate(Float value);
        Bool GetFocused() const;
        void SetFocused(Bool value);
        Bool GetChanged() const;
        Bool GetSubmitted() const;

    private:
        Component::InputField2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(InputField2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<InputField2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<InputField2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(InputField2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::InputField2D);
