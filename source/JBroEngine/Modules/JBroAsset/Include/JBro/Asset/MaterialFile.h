#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Field.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    // **재질 하나의 값**이다(D-399, material-plan 2 단계). 사용자의 `.jmat` 파일 내용이고, 모델의 하위 Material 도 같은 모양으로 읽힌다.
    // 자료 모델은 `{ 셰이더, 파라미터, 그림 칸 }`(D-33·D-111)이고 첫 판의 셰이더는 빌트인 둘이다. 그림 칸은 Texture 에셋의 아이디이고 비면 그 칸이 없다.
    // 스크립트가 볼 타입이 아니라 공개 값 타입(`AssetTypes.h`)이 아니라 여기에 둔다.

    enum class MaterialShader : std::uint8_t
    {
        // glTF 와 같은 metallic-roughness PBR. 빛은 지금 고정 방향광 하나다(조명 컴포넌트가 오면 바뀐다).
        Standard,
        // 빛을 받지 않는다. 기본색(×그림)과 발광만 낸다.
        Unlit
    };

    enum class MaterialAlpha : std::uint8_t
    {
        Opaque,
        // 알파가 `alphaCutoff` 보다 작은 픽셀을 버린다(잎·철망). 반투명은 정렬이 필요해 뒤다(D-399 (6)).
        Cutout
    };

    struct MaterialDesc
    {
        MaterialShader shader = MaterialShader::Standard;
        Color baseColor{1.0f, 1.0f, 1.0f, 1.0f};
        Float metallic = 0.0f;
        Float roughness = 1.0f;
        // 알파는 쓰지 않는다.
        Color emissive{0.0f, 0.0f, 0.0f, 1.0f};
        MaterialAlpha alpha = MaterialAlpha::Opaque;
        Float alphaCutoff = 0.5f;
        AssetId baseColorTextureId;
        // glTF 처럼 G 가 roughness, B 가 metallic 이다.
        AssetId metallicRoughnessTextureId;
        // 메시에 탄젠트가 있을 때만 쓴다(D-399 (5)).
        AssetId normalTextureId;
        // R 이 차폐다. MR 그림과 같은 그림인 경우가 흔하다.
        AssetId occlusionTextureId;
        AssetId emissiveTextureId;
    };

    // `.jmat` 을 읽는다. 블록이 없거나 모르는 키·틀린 값이면 실패다(메타의 옵션과 같은 규칙 - 조용히 버리면 값이 사라진다). 실패하면 `result` 는 그대로다.
    //
    //   Version: 1
    //   Material:
    //     shader: Standard
    //     baseColor:          # 색은 블록 목록이다(흐름 표기 `[ ]` 는 읽지 않는다)
    //       - 1
    //       - 1
    //       - 1
    //       - 1
    //     ...
    Bool ParseMaterialFile(const char* text, std::size_t length, MaterialDesc& result, String& error);
    // 쓴다. 값이 적히지 않으면 false 다.
    Bool FormatMaterialFile(const MaterialDesc& desc, String& text);

    JBRO_DEFINE_ENUM_TYPE(MaterialShader, "JBro.MaterialShader",
        { MaterialShader::Standard, "Standard" },
        { MaterialShader::Unlit,    "Unlit" });

    JBRO_DEFINE_ENUM_TYPE(MaterialAlpha, "JBro.MaterialAlpha",
        { MaterialAlpha::Opaque, "Opaque" },
        { MaterialAlpha::Cutout, "Cutout" });

    // `.jmat` 의 `Material` 블록이 이 표로 읽히고 쓰인다. 필드 이름이 파일의 키이고 인스펙터의 칸이다.
    template <>
    struct TypeDescriptorOf<MaterialDesc>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&MaterialDesc::shader>(),
                MakeFieldEntry<&MaterialDesc::baseColor>(),
                MakeFieldEntry<&MaterialDesc::metallic>(Attribute::Range(0.0f, 1.0f)),
                MakeFieldEntry<&MaterialDesc::roughness>(Attribute::Range(0.0f, 1.0f)),
                MakeFieldEntry<&MaterialDesc::emissive>(),
                MakeFieldEntry<&MaterialDesc::alpha>(),
                MakeFieldEntry<&MaterialDesc::alphaCutoff>(Attribute::Range(0.0f, 1.0f)),
                MakeFieldEntry<&MaterialDesc::baseColorTextureId>(),
                MakeFieldEntry<&MaterialDesc::metallicRoughnessTextureId>(),
                MakeFieldEntry<&MaterialDesc::normalTextureId>(),
                MakeFieldEntry<&MaterialDesc::occlusionTextureId>(),
                MakeFieldEntry<&MaterialDesc::emissiveTextureId>(),
            };
            static const StaticPropertyTable<12> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<MaterialDesc>("JBro.MaterialDesc", fields.Get());
            return descriptor;
        }
    };
}
