#pragma once

#include <JBro/Framework3D/Math3D.h>
#include <JBro/Reflection/Field.h>

// 3D 수학 타입의 리플렉션 설명자다. `Math3D.h` 와 따로 두는 이유는 그 헤더가
// 매 프레임 경로에 있기 때문이다 — 거기에 리플렉션 기계를 넣으면 벡터를 쓰는 모든
// 번역 단위가 함께 물고 간다. 2D 쪽 `Math2DReflection.h` 와 같은 이유, 같은 모양이다.

namespace JBro
{
    template <>
    struct TypeDescriptorOf<Vec3>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&Vec3::x>(),
                MakeFieldEntry<&Vec3::y>(),
                MakeFieldEntry<&Vec3::z>(),
            };
            static const StaticPropertyTable<3> fields { entries };
            static const TypeDescriptor descriptor =
                MakeVectorTypeDescriptor<Vec3>("JBro.Vec3", fields.Get());
            return descriptor;
        }
    };

    // 인스펙터가 네 성분을 그대로 내보이는 것은 쓰기 나쁘다 — 사람이 원하는 것은 보통
    // 오일러각이다. 그것은 표현 문제이므로 인스펙터가 정할 일이고, 저장되는 것은
    // 성분 넷이다. 오일러각으로 저장하면 짐벌락과 각도 규약이 파일 형식에 들어온다.
    template <>
    struct TypeDescriptorOf<Quaternion>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&Quaternion::x>(),
                MakeFieldEntry<&Quaternion::y>(),
                MakeFieldEntry<&Quaternion::z>(),
                MakeFieldEntry<&Quaternion::w>(),
            };
            static const StaticPropertyTable<4> fields { entries };
            static const TypeDescriptor descriptor =
                MakeVectorTypeDescriptor<Quaternion>("JBro.Quaternion", fields.Get());
            return descriptor;
        }
    };
}
