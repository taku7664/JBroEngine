#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/TextOptions.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class Text3D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Text3D` 다(D-302). 데이터는 엔진의 `Component::Text3D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 3D 월드의 텍스트다(D-222). 글자는 `GetFramework3DServices().Text3D` 로 바꾸고 읽는다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class Text3D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<Text3D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::Text3D";
        }

        AssetId GetFontId() const;
        AssetHandle GetFont() const;
        void SetFont(AssetHandle value);
        Float GetFontSize() const;
        void SetFontSize(Float value);
        Float GetBoxWidth() const;
        void SetBoxWidth(Float value);
        Float GetBoxHeight() const;
        void SetBoxHeight(Float value);
        Component::TextOverflow GetOverflow() const;
        void SetOverflow(Component::TextOverflow value);
        Component::TextWrapMode GetWrapMode() const;
        void SetWrapMode(Component::TextWrapMode value);
        Component::TextAlignX GetAlignX() const;
        void SetAlignX(Component::TextAlignX value);
        Component::TextAlignY GetAlignY() const;
        void SetAlignY(Component::TextAlignY value);
        Float GetLineSpacing() const;
        void SetLineSpacing(Float value);
        Float GetLetterSpacing() const;
        void SetLetterSpacing(Float value);
        Color GetColor() const;
        void SetColor(Color value);
        Bool GetVisible() const;
        void SetVisible(Bool value);
        Component::TextFacing3D GetFacing() const;
        void SetFacing(Component::TextFacing3D value);
        Color GetOutlineColor() const;
        void SetOutlineColor(Color value);
        Float GetOutlineWidth() const;
        void SetOutlineWidth(Float value);
        Bool GetRichText() const;
        void SetRichText(Bool value);

    private:
        Component::Text3D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Text3D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Text3D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Text3D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Text3D, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Text3D);
