#pragma once

// 2D 빌트인 컴포넌트의 값 타입이다(열거형·물리 레이어 마스크·충돌과 질의 결과). 컴포넌트 데이터 헤더와 스크립트가 함께 본다 -
// 데이터 헤더는 스크립트 대상에서 막혀 있으므로(D-302) 핸들의 인자·반환에 쓰이는 값은 여기에 있어야 한다.

#include <JBro/Core/Core.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/ScalarCodec.h>
#include <JBro/Reflection/TypeDescriptorOf.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/NameTable.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::Component
{
    enum class SpriteFlip : std::uint8_t { None, Horizontal, Vertical, Both };

    // 크기와 피벗을 어디서 가져오는가(D-117). `FromSprite` 는 풀린 칸의 픽셀 / 에셋 PPU 와 칸의 피벗이고,
    // `Custom` 은 컴포넌트의 `size`·`pivot` 이다. 스프라이트가 풀리지 않으면(없거나 로드 실패) 둘 다 저작 값이다.
    enum class SpriteSizeMode : std::uint8_t { FromSprite, Custom };
    // 피벗은 크기와 따로 고른다(D-117 의 "컴포넌트 피벗은 덮어쓰기"). 에셋 크기에 저작 피벗을 얹을 수 있다.
    enum class SpritePivotMode : std::uint8_t { FromSprite, Custom };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::SpriteFlip, "Component::SpriteFlip",
        { Component::SpriteFlip::None,       "None" },
        { Component::SpriteFlip::Horizontal, "Horizontal" },
        { Component::SpriteFlip::Vertical,   "Vertical" },
        { Component::SpriteFlip::Both,       "Both" });
    JBRO_DEFINE_ENUM_TYPE(Component::SpriteSizeMode, "Component::SpriteSizeMode",
        { Component::SpriteSizeMode::FromSprite, "FromSprite" },
        { Component::SpriteSizeMode::Custom,     "Custom" });
    JBRO_DEFINE_ENUM_TYPE(Component::SpritePivotMode, "Component::SpritePivotMode",
        { Component::SpritePivotMode::FromSprite, "FromSprite" },
        { Component::SpritePivotMode::Custom,     "Custom" });
}

namespace JBro::Component
{
    enum class CameraProjection2D { Orthographic, PixelPerfect };
}

namespace JBro
{
    // 저장 파일에는 이름이 적힌다. 숫자로 적으면 나중에 값을 가운데 끼워 넣는 순간
    // 예전 파일이 전부 한 칸씩 밀린다.
    JBRO_DEFINE_ENUM_TYPE(Component::CameraProjection2D, "Component::CameraProjection2D",
        { Component::CameraProjection2D::Orthographic, "Orthographic" },
        { Component::CameraProjection2D::PixelPerfect, "PixelPerfect" });
}

namespace JBro::Component
{
    enum class BodyType2D      : std::uint8_t { Static, Kinematic, Dynamic };
    enum class ColliderShape2D : std::uint8_t { Box, Circle, Capsule, Polygon, Chain };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::BodyType2D, "Component::BodyType2D",
        { Component::BodyType2D::Static,    "Static" },
        { Component::BodyType2D::Kinematic, "Kinematic" },
        { Component::BodyType2D::Dynamic,   "Dynamic" });

    JBRO_DEFINE_ENUM_TYPE(Component::ColliderShape2D, "Component::ColliderShape2D",
        { Component::ColliderShape2D::Box,     "Box" },
        { Component::ColliderShape2D::Circle,  "Circle" },
        { Component::ColliderShape2D::Capsule, "Capsule" },
        { Component::ColliderShape2D::Polygon, "Polygon" },
        { Component::ColliderShape2D::Chain,   "Chain" });
}

namespace JBro
{
    // 물리 레이어 비트 묶음이다(D-233). 메모리와 파일은 부호 없는 32 비트 정수 그대로이고(옛 파일과 같은 글자), 이름은
    // 프로젝트 설정의 `PhysicsLayers` 가 준다 - 비트 i 가 그 목록의 i 번째 이름이다. 에디터는 이 타입 이름을 보고 이름을
    // 고르는 칸을 그린다. 정수와 저절로 오가므로 스크립트는 `collider->layer = JBro::UInt32(1u << 3);` 처럼 쓴다.
    struct PhysicsLayerMask
    {
        UInt32 bits = 0;

        constexpr PhysicsLayerMask() = default;
        constexpr PhysicsLayerMask(UInt32 value)
            : bits(value)
        {
        }

        constexpr operator UInt32() const
        {
            return bits;
        }

        friend constexpr bool operator==(PhysicsLayerMask left, PhysicsLayerMask right)
        {
            return left.bits == right.bits;
        }
    };

    // 잎사귀다. 정수 코덱을 그대로 쓴다(구조체의 첫 멤버가 그 정수다).
    template <>
    struct TypeDescriptorOf<PhysicsLayerMask>
    {
        static const TypeDescriptor& Get()
        {
            static const TypeDescriptor descriptor = [] {
                TypeDescriptor made;
                made.typeName = NameTable::Get().Intern("JBro.PhysicsLayerMask");
                made.size = static_cast<JBro::UInt32>(sizeof(PhysicsLayerMask));
                made.alignment = static_cast<JBro::UInt32>(alignof(PhysicsLayerMask));
                made.triviallyCopyable = true;
                made.codec = &GetScalarCodec<std::uint32_t>();
                return made;
            }();
            return descriptor;
        }
    };

    // 프로젝트가 담는 물리 레이어 수다. 비트 하나가 레이어 하나다.
    inline constexpr UInt32 PhysicsLayerCount = 32;
}

namespace JBro::Component
{
    // 라이트의 종류다(D-291). 값의 차례는 파일과 렌더러의 `Light2DKind` 가 함께 쓰므로 뒤에만 더한다.
    enum class Light2DType : std::uint8_t
    {
        // 캔버스 전체에 같은 빛이다(환경광). 위치·반지름·각도를 보지 않는다. 여럿이면 더한다.
        Global,
        // 둘레로 퍼지는 빛이다.
        Point,
        // 오브젝트의 오른쪽(+x)을 향한 원뿔이다. 오브젝트를 돌리면 함께 돈다.
        Spot,
    };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::Light2DType, "Component::Light2DType",
        { Component::Light2DType::Global, "Global" },
        { Component::Light2DType::Point,  "Point" },
        { Component::Light2DType::Spot,   "Spot" });
}

namespace JBro::Component
{
    // 그림자의 모양을 어디서 가져오는가(D-291). 값의 차례는 파일이 쓰므로 뒤에만 더한다.
    enum class ShadowShape2D : std::uint8_t
    {
        // 같은 오브젝트의 `Collider2D` 다(상자·원·캡슐·폴리곤·체인). 콜라이더가 없으면 그림자가 없다.
        Collider,
        // 같은 오브젝트의 `SpriteRenderer2D` 가 그려지는 사각형이다. 투명한 픽셀의 모양은 따르지 않는다.
        Sprite,
    };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::ShadowShape2D, "Component::ShadowShape2D",
        { Component::ShadowShape2D::Collider, "Collider" },
        { Component::ShadowShape2D::Sprite,   "Sprite" });
}

namespace JBro
{
    // 충돌·트리거 훅이 받는 접촉이다(D-207). normal 은 받는 쪽에서 상대 쪽이다. 트리거와 끝 이벤트는 point·normal 이 0 이다.
    struct Collision2D
    {
        Handle::GameObject other;
        Component::BodyType2D bodyType = Component::BodyType2D::Dynamic;
        Vector2 point;
        Vector2 normal;
    };

    // 질의가 모든 레이어를 본다(콜라이더의 `layer` 비트와 AND 해서 0 이 아니면 대상이다).
    inline constexpr UInt32 AllPhysicsLayers = 0xFFFFFFFFu;

    // 반직선·스윕 질의의 결과다(physics-plan §4 의 6, 기존 엔진 `RaycastHit2D`). 스윕에서 "어디까지 갈 수 있는가" 가
    // 거리라서 접촉과 따로 둔다. normal 은 맞은 표면의 바깥(쏜 쪽을 향한다)이다. 출발부터 겹쳐 있으면 distance 0,
    // normal 은 쏜 방향의 반대, point 는 쏜 모양의 중심이다.
    struct RaycastHit2D
    {
        Handle::GameObject other;
        Component::BodyType2D bodyType = Component::BodyType2D::Static;
        Vector2 point;
        Vector2 normal;
        Float distance = 0.0f;
    };
}
