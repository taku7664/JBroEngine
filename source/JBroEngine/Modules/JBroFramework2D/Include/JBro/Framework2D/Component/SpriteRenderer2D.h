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

        // ⚠ **저장되지 않는다.** `AssetHandle` 은 이번 실행에서의 자리라 다음 실행에서는
        // 다른 것을 가리킨다(`AssetTypes.h`). 그대로 적으면 저장 파일에 뜻 없는 숫자가 들어간다.
        //
        // 씬이 스프라이트를 기억하려면 이 자리가 영속 식별자(`AssetId`)를 들거나,
        // 저장할 때 핸들을 식별자로 바꿔 주는 곳이 있어야 한다. **아직 둘 다 없다** —
        // 지금은 빠뜨리는 쪽을 고른다. 쓰레기를 적는 것보다 낫고, 빠진 것은 눈에 띈다.
        JBRO_FIELD(AssetHandle, sprite,   NoSerialize());
        JBRO_FIELD(AssetHandle, material, NoSerialize());

        JBRO_FIELD(Color, tint) { 1.0f, 1.0f, 1.0f, 1.0f };
        JBRO_FIELD(Vec2,  pivot) { 0.5f, 0.5f };
        JBRO_FIELD(Vec2,  size)  { 1.0f, 1.0f };
        JBRO_FIELD(SpriteFlip,   flip)        = SpriteFlip::None;
        JBRO_FIELD(std::int32_t, renderOrder) = 0;
        JBRO_FIELD(bool,         visible)     = true;
    };
}
