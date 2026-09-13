#pragma once

#include <JBro/Reflection/Field.h>
#include <JBro/Types/Color.h>

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
                MakeFieldEntry<&Color::R>(Attribute::Range(0, 1)),
                MakeFieldEntry<&Color::G>(Attribute::Range(0, 1)),
                MakeFieldEntry<&Color::B>(Attribute::Range(0, 1)),
                MakeFieldEntry<&Color::A>(Attribute::Range(0, 1)),
            };
            static const StaticPropertyTable<4> fields { entries };
            static const TypeDescriptor descriptor =
                MakeVectorTypeDescriptor<Color>("JBro.Color", fields.Get());
            return descriptor;
        }
    };
}
