#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class SpriteRenderer2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `SpriteRenderer2D` 다(D-302). 데이터는 엔진의 `Component::SpriteRenderer2D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 스프라이트를 그린다. 에셋은 이번 실행의 자리(`AssetHandle`)로 바꾼다 - 저장되는 id 는 읽기만 한다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class SpriteRenderer2D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<SpriteRenderer2D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::SpriteRenderer2D";
        }

        // 저장되는 스프라이트 id 다. 캔버스를 열 때 `GetSprite` 로 풀린다.
        AssetId GetSpriteId() const;

        // 그리는 스프라이트다. 바꾸면 다음 프레임부터 그린다. 저장되는 `SpriteId` 는 바뀌지 않는다.
        AssetHandle GetSprite() const;
        void SetSprite(AssetHandle value);
        AssetId GetMaterialId() const;
        AssetHandle GetMaterial() const;
        void SetMaterial(AssetHandle value);
        Color GetTint() const;
        void SetTint(Color value);
        Component::SpriteSizeMode GetSizeMode() const;
        void SetSizeMode(Component::SpriteSizeMode value);
        Component::SpritePivotMode GetPivotMode() const;
        void SetPivotMode(Component::SpritePivotMode value);

        // `PivotMode` 가 `Custom` 일 때만 쓰인다.
        Vector2 GetPivot() const;
        void SetPivot(Vector2 value);

        // `SizeMode` 가 `Custom` 일 때만 쓰인다.
        Vector2 GetSize() const;
        void SetSize(Vector2 value);
        Component::SpriteFlip GetFlip() const;
        void SetFlip(Component::SpriteFlip value);

        // 시트의 어느 칸인가(D-113). 넘치면 마지막 칸이다.
        UInt32 GetFrameIndex() const;
        void SetFrameIndex(UInt32 value);
        Int32 GetRenderOrder() const;
        void SetRenderOrder(Int32 value);
        Bool GetVisible() const;
        void SetVisible(Bool value);

    private:
        Component::SpriteRenderer2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(SpriteRenderer2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<SpriteRenderer2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<SpriteRenderer2D>, "a component handle must stay trivially copyable");
}
