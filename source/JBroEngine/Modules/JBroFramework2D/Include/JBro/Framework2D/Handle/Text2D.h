#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/TextOptions.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class Text2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Text2D` 다(D-302). 데이터는 엔진의 `Component::Text2D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 월드 공간 2D 텍스트다. 글자는 `GetFramework2DServices().Text2D` 로 바꾸고 읽는다(D-51 - 글자는 컴포넌트에 없다). 크기와 상자는 글자 픽셀이다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class Text2D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<Text2D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::Text2D";
        }

        // 저장되는 폰트 id 다.
        AssetId GetFontId() const;

        // 쓰는 폰트다. 바꾸면 다음 프레임부터 그 폰트로 그린다. 저장되는 `FontId` 는 바뀌지 않는다.
        AssetHandle GetFont() const;
        void SetFont(AssetHandle value);
        Float GetFontSize() const;
        void SetFontSize(Float value);

        // 글자 픽셀이다. 0 이면 그 방향으로 제한이 없다.
        Vector2 GetBoxSize() const;
        void SetBoxSize(Vector2 value);
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
        Int32 GetRenderOrder() const;
        void SetRenderOrder(Int32 value);
        Bool GetVisible() const;
        void SetVisible(Bool value);
        Color GetOutlineColor() const;
        void SetOutlineColor(Color value);
        Float GetOutlineWidth() const;
        void SetOutlineWidth(Float value);
        Bool GetAutoSize() const;
        void SetAutoSize(Bool value);
        Float GetMinFontSize() const;
        void SetMinFontSize(Float value);
        Float GetMaxFontSize() const;
        void SetMaxFontSize(Float value);
        Bool GetPixelSnap() const;
        void SetPixelSnap(Bool value);
        Bool GetRichText() const;
        void SetRichText(Bool value);

    private:
        Component::Text2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Text2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Text2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Text2D>, "a component handle must stay trivially copyable");
}
