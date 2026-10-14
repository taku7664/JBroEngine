#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`SpriteRenderer2D`)로만 만진다(D-302).
// 정규화한 이름으로 써도 스크립트 대상 빌드에서는 여기서 컴파일이 실패한다.
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::SpriteRenderer2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Runtime/Component.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Component
{
    class SpriteRenderer2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::SpriteRenderer2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(SpriteRenderer2D)

        // 에셋 참조는 저작 값과 런타임 캐시로 나뉜다. `Transform2D` 의 월드 캐시와 같은 형태다.
        //
        // `AssetId` 가 저장되는 쪽이다. `AssetHandle` 은 이번 실행에서의 자리라
        // 다음 실행에서는 다른 것을 가리킨다(`AssetTypes.h`) — 저장하면 뜻 없는 숫자가 된다.
        //
        // **핸들을 채우는 것은 아직 없다.** `AssetSystem::Load` 가 스텁이라
        // `AssetId` → `AssetHandle` 해석 패스를 붙일 데가 없다. 그것이 생기면
        // 씬 로드 뒤와 인스펙터 변경 시에 한 번씩 돌면 된다. 렌더 추출은 지금도 앞으로도
        // 핸들만 읽으므로 매 프레임 경로에 조회가 늘지 않는다.
        JBRO_FIELD(AssetId,     spriteId);
        JBRO_FIELD(AssetHandle, sprite,     NoSerialize() | ReadOnly() | Tooltip("spriteId 에서 해석된 값") | Hidden()); // 인스펙터에 없다(D-309)
        JBRO_FIELD(AssetId,     materialId);
        JBRO_FIELD(AssetHandle, material,   NoSerialize() | ReadOnly() | Tooltip("materialId 에서 해석된 값") | Hidden()); // 인스펙터에 없다(D-309)

        JBRO_FIELD(Color, tint) { 1.0f, 1.0f, 1.0f, 1.0f };
        // `Custom` 일 때만 `pivot`·`size` 가 쓰인다(D-117). 기본은 에셋이 정한다.
        JBRO_FIELD(SpriteSizeMode, sizeMode) = SpriteSizeMode::FromSprite;
        JBRO_FIELD(SpritePivotMode, pivotMode) = SpritePivotMode::FromSprite;
        JBRO_FIELD(Vector2,  pivot) { 0.5f, 0.5f };
        JBRO_FIELD(Vector2,  size)  { 1.0f, 1.0f };
        JBRO_FIELD(SpriteFlip,   flip)        = SpriteFlip::None;
        // 시트의 어느 칸인가(D-113). 슬라이싱이 없는 스프라이트는 언제나 0 이고, 넘치면 마지막 칸이다.
        JBRO_FIELD(UInt32, frameIndex) = 0;
        JBRO_FIELD(Int32, renderOrder) = 0;
        // 같은 레이어·같은 `renderOrder` 끼리의 그리는 차례다(D-296). 큰 것이 위다. 인스펙터에 줄이 없고 에디터의 "앞으로 가져오기" 메뉴가 고친다.
        JBRO_FIELD(Int32, drawSequence, Hidden()) = 0;
        JBRO_FIELD(Bool,         visible)     = true;
    };
}
