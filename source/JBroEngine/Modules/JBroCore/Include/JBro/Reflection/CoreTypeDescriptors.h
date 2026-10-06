#pragma once

#include <JBro/Reflection/Field.h>
#include <JBro/Reflection/ScalarCodec.h>
#include <JBro/Types/Angle.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Uuid.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // Color 는 구조를 가진 타입이므로 필드로 말한다. 인스펙터는 네 칸을 얻고,
    // 저장 파일에는 채널마다 이름이 붙는다 — 나중에 채널 순서를 헷갈릴 자리가 없다.
    //
    // 설명서를 Color.h 가 아니라 여기 두는 이유는 **Color 가 핫 경로에 있기 때문이다.**
    // 그 헤더에 리플렉션 기계를 넣으면 색을 쓰는 모든 번역 단위가 함께 물고 간다.
    template <>
    struct TypeDescriptorOf<Color>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&Color::R>(Attribute::Range(0.0f, 1.0f)),
                MakeFieldEntry<&Color::G>(Attribute::Range(0.0f, 1.0f)),
                MakeFieldEntry<&Color::B>(Attribute::Range(0.0f, 1.0f)),
                MakeFieldEntry<&Color::A>(Attribute::Range(0.0f, 1.0f)),
            };
            static const StaticPropertyTable<4> fields { entries };
            static const TypeDescriptor descriptor =
                MakeVectorTypeDescriptor<Color>("JBro.Color", fields.Get());
            return descriptor;
        }
    };

    // 각도 둘이다(D-247). **필드로 쪼개지 않고 스칼라 하나로 말한다** - 안에 `float` 가 하나뿐이라
    // 쪼갤 곳이 없다. 코덱은 `float` 의 것을 그대로 쓴다: 표준 레이아웃이라 첫 멤버의 주소가 곧
    // 객체의 주소이고, 크기와 정렬도 `float` 와 같다는 것을 `Angle.h` 가 단언한다.
    //
    // 이름을 `float` 가 아니라 `JBro.Radian` 으로 주는 것이 이 설명서의 전부이자 목적이다 -
    // 저장 파일에는 전과 같은 숫자가 적히고, 인스펙터는 그 이름을 보고 도로 바꿔 보여 준다.
    // `MakeScalarTypeDescriptor` 가 이름을 인자로 받는 이유가 이것이다.
    template <>
    struct TypeDescriptorOf<Radian>
    {
        static const TypeDescriptor& Get()
        {
            static const TypeDescriptor descriptor =
                MakeScalarTypeDescriptor<float>("JBro.Radian");
            return descriptor;
        }
    };

    template <>
    struct TypeDescriptorOf<Degree>
    {
        static const TypeDescriptor& Get()
        {
            static const TypeDescriptor descriptor =
                MakeScalarTypeDescriptor<float>("JBro.Degree");
            return descriptor;
        }
    };

    // 128 비트 식별자다. 영속 아이디(`AssetId`)가 이 타입의 별칭이라 저장 파일에 적히는 쪽이다.
    //
    // **필드로 쪼개지 않는다.** 안에 정수가 둘 있지만 그것은 저장 방식이지 부분이 아니다 - 쪼개면
    // 저장 파일에 `spriteId:` 아래 `high:`·`low:` 가 생기고 인스펙터도 칸을 둘 그린다. 나눌 곳이 없는
    // 값이므로 코덱 하나로 32 자리 16 진수를 말한다.
    template <>
    struct TypeDescriptorOf<Uuid>
    {
        static const TypeDescriptor& Get()
        {
            static const TypeDescriptor descriptor = []
            {
                TypeDescriptor built;
                built.typeName = NameTable::Get().Intern("JBro.Uuid");
                built.size = static_cast<JBro::UInt32>(sizeof(Uuid));
                built.alignment = static_cast<JBro::UInt32>(alignof(Uuid));
                built.triviallyCopyable = true;
                built.codec = &GetUuidCodec();
                return built;
            }();
            return descriptor;
        }
    };
}
