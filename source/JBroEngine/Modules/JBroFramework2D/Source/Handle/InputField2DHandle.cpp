#include <JBro/Framework2D/Handle/InputField2D.h>

#include <JBro/Framework2D/Component/InputField2D.h>

#include <cstring>

namespace JBro::Handle
{
    Component::InputField2D* InputField2D::Resolve(const char* operation) const
    {
        return static_cast<Component::InputField2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_INPUTFIELD2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type InputField2D::Getter() const                                   \
    {                                                                   \
        const Component::InputField2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                       \
        {                                                               \
            return {};                                                  \
        }                                                               \
        return component->field;                                        \
    }                                                                   \
    void InputField2D::Setter(Type value)                               \
    {                                                                   \
        Component::InputField2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                       \
        {                                                               \
            return;                                                     \
        }                                                               \
        component->field = value;                                       \
    }

    JBRO_INPUTFIELD2D_ACCESSOR(Vector2, GetSize, SetSize, size)
    JBRO_INPUTFIELD2D_ACCESSOR(Vector2, GetOffset, SetOffset, offset)
    JBRO_INPUTFIELD2D_ACCESSOR(Bool, GetInteractable, SetInteractable, interactable)
    JBRO_INPUTFIELD2D_ACCESSOR(Text2D, GetDisplay, SetDisplay, display)
    JBRO_INPUTFIELD2D_ACCESSOR(SpriteRenderer2D, GetCaret, SetCaret, caret)
    JBRO_INPUTFIELD2D_ACCESSOR(Int32, GetMaxLength, SetMaxLength, maxLength)
    JBRO_INPUTFIELD2D_ACCESSOR(Float, GetBlinkRate, SetBlinkRate, blinkRate)
    JBRO_INPUTFIELD2D_ACCESSOR(Bool, GetFocused, SetFocused, focused)
    JBRO_INPUTFIELD2D_ACCESSOR(Int32, GetCaretIndex, SetCaretIndex, caretIndex)

#undef JBRO_INPUTFIELD2D_ACCESSOR

    const char* InputField2D::GetText() const
    {
        const Component::InputField2D* component = Resolve("GetText");
        if (component == nullptr)
        {
            return "";
        }
        // 저장소의 글자는 0 으로 끝난다. 다음 쓰기까지 유효하다.
        const ArrayView<const char> text = TextStore::Get().GetText(component->text);
        return text.Data() != nullptr ? text.Data() : "";
    }

    void InputField2D::SetText(const char* utf8)
    {
        Component::InputField2D* component = Resolve("SetText");
        if (component == nullptr)
        {
            return;
        }
        const char* safe = utf8 != nullptr ? utf8 : "";
        TextStore::Get().Assign(component->text, safe, std::strlen(safe));
    }

    Bool InputField2D::GetChanged() const
    {
        const Component::InputField2D* component = Resolve("GetChanged");
        return component != nullptr ? component->changed : Bool(false);
    }

    Bool InputField2D::GetSubmitted() const
    {
        const Component::InputField2D* component = Resolve("GetSubmitted");
        return component != nullptr ? component->submitted : Bool(false);
    }

    Int32 InputField2D::GetSelectionAnchor() const
    {
        const Component::InputField2D* component = Resolve("GetSelectionAnchor");
        return component != nullptr ? component->selectionAnchor : Int32(-1);
    }

    void InputField2D::SetSelection(Int32 anchor, Int32 caret)
    {
        Component::InputField2D* component = Resolve("SetSelection");
        if (component == nullptr)
        {
            return;
        }
        component->selectionAnchor = anchor < 0 ? Int32(-1) : anchor;
        component->caretIndex = caret < 0 ? Int32(0) : caret;
    }

    Bool InputField2D::IsComposing() const
    {
        const Component::InputField2D* component = Resolve("IsComposing");
        return component != nullptr ? component->composing : Bool(false);
    }
}
