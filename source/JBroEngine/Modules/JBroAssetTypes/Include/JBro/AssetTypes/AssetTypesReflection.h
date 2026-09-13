#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Reflection/Field.h>

namespace JBro
{
    // 영속 식별자다. 이것이 저장 파일에 적히는 쪽이다.
    //
    // **필드로 쪼개지 않는다.** 안에 정수가 하나 있지만 그것은 저장 방식이지 부분이 아니다 —
    // 쪼개면 저장 파일에 `spriteId:` 아래 `value:` 가 한 단 더 생기고, 인스펙터도 칸을
    // 하나 더 그린다. 나눌 곳이 없는 값이므로 코덱 하나로 말한다.
    template <>
    struct TypeDescriptorOf<AssetId>
    {
        // 감싼 정수와 레이아웃이 같아야 정수 코덱을 그대로 빌려 쓸 수 있다.
        static_assert(sizeof(AssetId) == sizeof(std::uint64_t),
            "an asset id must be exactly the number it wraps");
        static_assert(offsetof(AssetId, value) == 0,
            "an asset id must start at the number it wraps");

        static const TypeDescriptor& Get()
        {
            static const TypeDescriptor descriptor = []
            {
                TypeDescriptor built;
                built.typeName = NameTable::Get().Intern("JBro.AssetId");
                built.size = static_cast<std::uint32_t>(sizeof(AssetId));
                built.alignment = static_cast<std::uint32_t>(alignof(AssetId));
                built.triviallyCopyable = true;
                built.codec = &GetScalarCodec<std::uint64_t>();
                return built;
            }();
            return descriptor;
        }
    };

    // ⚠ **이것은 저장되는 값이 아니다.** `AssetTypes.h` 가 스스로 "이번 실행에서의 위치"
    // 라고 적어 둔 대로, index/generation 은 다음 실행에서 다른 것을 가리킨다.
    //
    // 설명서를 주는 것은 인스펙터가 "무엇이 붙어 있는지" 를 보여 줄 수 있게 하기 위해서이고,
    // 이것을 필드로 가지는 컴포넌트는 `NoSerialize()` 를 붙인다. 그러지 않으면 저장 파일에
    // 다음 실행에서 뜻이 없는 숫자가 들어간다.
    template <>
    struct TypeDescriptorOf<AssetHandle>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&AssetHandle::index>(Attribute::ReadOnly()),
                MakeFieldEntry<&AssetHandle::generation>(Attribute::ReadOnly()),
            };
            static const StaticPropertyTable<2> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<AssetHandle>("JBro.AssetHandle", fields.Get());
            return descriptor;
        }
    };
}
