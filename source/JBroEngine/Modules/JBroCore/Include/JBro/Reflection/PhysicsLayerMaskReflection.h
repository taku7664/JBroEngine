#pragma once

#include <JBro/Reflection/ScalarCodec.h>
#include <JBro/Reflection/TypeDescriptorOf.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/PhysicsLayerMask.h>

#include <cstdint>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // 잎사귀다. 정수 코덱을 그대로 쓴다(구조체의 첫 멤버가 그 정수다). 타입 이름은 옮기기 전과 같다 - 에디터와 JBroScript 가 이 이름을 본다.
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
}
