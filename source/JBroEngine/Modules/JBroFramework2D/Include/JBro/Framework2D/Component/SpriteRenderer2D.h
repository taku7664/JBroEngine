#pragma once

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Framework2D/Math2DReflection.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Runtime/Component.h>

#include <cstdint>

namespace JBro::Component
{
    enum class SpriteFlip : std::uint8_t { None, Horizontal, Vertical, Both };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::SpriteFlip, "Component::SpriteFlip",
        { Component::SpriteFlip::None,       "None" },
        { Component::SpriteFlip::Horizontal, "Horizontal" },
        { Component::SpriteFlip::Vertical,   "Vertical" },
        { Component::SpriteFlip::Both,       "Both" });
}

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
        JBRO_FIELD(AssetHandle, sprite,     NoSerialize() | ReadOnly() | Tooltip("spriteId 에서 해석된 값"));
        JBRO_FIELD(AssetId,     materialId);
        JBRO_FIELD(AssetHandle, material,   NoSerialize() | ReadOnly() | Tooltip("materialId 에서 해석된 값"));

        JBRO_FIELD(Color, tint) { 1.0f, 1.0f, 1.0f, 1.0f };
        JBRO_FIELD(Vec2,  pivot) { 0.5f, 0.5f };
        JBRO_FIELD(Vec2,  size)  { 1.0f, 1.0f };
        JBRO_FIELD(SpriteFlip,   flip)        = SpriteFlip::None;
        JBRO_FIELD(std::int32_t, renderOrder) = 0;
        JBRO_FIELD(bool,         visible)     = true;
    };
}
