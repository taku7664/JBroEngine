#pragma once

#include <JBro/Framework2D/Math2D.h>
#include <JBro/Reflection/Field.h>

// 2D 수학 타입의 리플렉션 설명서다. `Math2D.h` 와 따로 두는 이유는 그 헤더가
// **매 프레임 경로에 있기 때문이다** — 거기에 리플렉션 기계를 넣으면 벡터를 쓰는 모든
// 번역 단위가 함께 물고 간다. 필드 이름은 여기서도 멤버 포인터에서 나온다.
//
// 이 타입들은 필드가 늘어나지 않는다. 늘어나는 타입이라면 선언 옆에 `JBRO_FIELD` 를 두어야
// 한다 — 표가 선언과 떨어져 있으면 필드를 더하고 여기를 안 고치는 날이 온다.

namespace JBro
{
    template <>
    struct TypeDescriptorOf<Vec2>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&Vec2::x>(),
                MakeFieldEntry<&Vec2::y>(),
            };
            static const StaticPropertyTable<2> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<Vec2>("JBro.Vec2", fields.Get());
            return descriptor;
        }
    };

    template <>
    struct TypeDescriptorOf<Rect>
    {
        static const TypeDescriptor& Get()
        {
            // min·max 가 Vec2 이므로 이 표는 한 단계 더 내려간다.
            // 소비자는 "필드가 있으면 내려간다" 하나만 알면 된다.
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&Rect::min>(),
                MakeFieldEntry<&Rect::max>(),
            };
            static const StaticPropertyTable<2> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<Rect>("JBro.Rect", fields.Get());
            return descriptor;
        }
    };

    template <>
    struct TypeDescriptorOf<Matrix3x2>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&Matrix3x2::m11>(),
                MakeFieldEntry<&Matrix3x2::m12>(),
                MakeFieldEntry<&Matrix3x2::m21>(),
                MakeFieldEntry<&Matrix3x2::m22>(),
                MakeFieldEntry<&Matrix3x2::m31>(),
                MakeFieldEntry<&Matrix3x2::m32>(),
            };
            static const StaticPropertyTable<6> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<Matrix3x2>("JBro.Matrix3x2", fields.Get());
            return descriptor;
        }
    };
}
