#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Field.h>

namespace JBro
{
    // `AssetId` 는 `Uuid` 의 별칭이라 설명서도 그것이다(`CoreTypeDescriptors.h` 의 `TypeDescriptorOf<Uuid>`).
    // 저장 파일에는 32 자리 16 진수 하나로 적힌다.

    JBRO_DEFINE_ENUM_TYPE(SpriteSliceType, "JBro.SpriteSliceType",
        { SpriteSliceType::None,      "None" },
        { SpriteSliceType::CellSize,  "CellSize" },
        { SpriteSliceType::CellCount, "CellCount" });

    // `.jmeta` 의 `Sprite.ImportOptions` 가 이 표로 읽히고 쓰인다. 필드 이름이 파일의 키다.
    template <>
    struct TypeDescriptorOf<SpriteImportOptions>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&SpriteImportOptions::sliceType>(),
                MakeFieldEntry<&SpriteImportOptions::rowCount>(),
                MakeFieldEntry<&SpriteImportOptions::columnCount>(),
                MakeFieldEntry<&SpriteImportOptions::cellWidth>(),
                MakeFieldEntry<&SpriteImportOptions::cellHeight>(),
                MakeFieldEntry<&SpriteImportOptions::marginX>(),
                MakeFieldEntry<&SpriteImportOptions::marginY>(),
                MakeFieldEntry<&SpriteImportOptions::gapX>(),
                MakeFieldEntry<&SpriteImportOptions::gapY>(),
                MakeFieldEntry<&SpriteImportOptions::pivotX>(Attribute::Range(0, 1)),
                MakeFieldEntry<&SpriteImportOptions::pivotY>(Attribute::Range(0, 1)),
                MakeFieldEntry<&SpriteImportOptions::pixelsPerUnit>(),
            };
            static const StaticPropertyTable<12> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<SpriteImportOptions>("JBro.SpriteImportOptions", fields.Get());
            return descriptor;
        }
    };

    JBRO_DEFINE_ENUM_TYPE(TextureFilter, "JBro.TextureFilter",
        { TextureFilter::Default, "Default" },
        { TextureFilter::Nearest, "Nearest" },
        { TextureFilter::Linear,  "Linear" });

    // `.jmeta` 의 `Texture.ImportOptions` 가 이 표로 읽히고 쓰인다.
    template <>
    struct TypeDescriptorOf<TextureImportOptions>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&TextureImportOptions::filter>(),
            };
            static const StaticPropertyTable<1> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<TextureImportOptions>("JBro.TextureImportOptions", fields.Get());
            return descriptor;
        }
    };

    JBRO_DEFINE_ENUM_TYPE(AudioImportMode, "JBro.AudioImportMode",
        { AudioImportMode::Decompressed, "Decompressed" },
        { AudioImportMode::Streaming,    "Streaming" },
        { AudioImportMode::StreamFromDisk, "StreamFromDisk" });

    // `.jmeta` 의 `Audio.ImportOptions` 가 이 표로 읽히고 쓰인다(D-197).
    template <>
    struct TypeDescriptorOf<AudioImportOptions>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&AudioImportOptions::mode>(),
            };
            static const StaticPropertyTable<1> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<AudioImportOptions>("JBro.AudioImportOptions", fields.Get());
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
