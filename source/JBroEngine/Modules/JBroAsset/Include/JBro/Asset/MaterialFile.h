#pragma once

#include <JBro/Asset/MaterialShader.h>
#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Field.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace JBro
{
    // **이 파일에는 머티리얼 두 갈래가 함께 있다**(2026-11-19 main 통합). 둘 다 `.jmat` 이고 `Material` 에셋이며 글자로 가른다(`IsStandardMaterialText`).
    // - 셰이더 머티리얼(`MaterialDocument`, D-347): 사용자 `.hlsl` 셰이더 + 자리 + 적은 파라미터·텍스처. 스프라이트·후처리·메시가 쓴다.
    // - 표준 머티리얼(`MaterialDesc`, D-399): 빌트인 `Standard`(PBR)·`Unlit` 의 고정 칸. `.jmat` 의 `Material:` 블록과 모델의 하위 Material 이다.

    // 머티리얼 파라미터 값 하나다. 칸은 셰이더 반사의 모양과 상관없이 넷까지 든다 - 모양은 머티리얼을 푸는 쪽이 셰이더를 보고 맞춘다.
    struct MaterialParameterValue
    {
        String name;
        Float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        UInt32 count = 0;
    };

    struct MaterialTextureValue
    {
        String name;
        AssetId texture;
    };

    // **머티리얼 에셋**(`.jmat`, D-347, D-111 의 `{ Shader, 파라미터 블록, 텍스처 슬롯 }`). 본문은 YAML 이다:
    //
    //   Shader: <셰이더 에셋 아이디>
    //   Domain: Sprite            # PostProcess | Sprite
    //   Parameters:               # 셰이더의 cbuffer Material 변수 이름 - 적힌 것만, 나머지는 HLSL 초기값
    //     strength: 0.5
    //     tintColor:              # 벡터는 블록 시퀀스
    //       - 1
    //       - 0.5
    //       - 0
    //       - 1
    //   Textures:                 # 셰이더의 Texture2D 이름
    //     noise: <텍스처 에셋 아이디>
    //
    // 셰이더를 모르고 읽는다 - 이름과 숫자만 든다. 셰이더에 없는 이름은 푸는 쪽이 버린다(그래도 되쓴다).
    struct MaterialDocument
    {
        AssetId shader;
        MaterialDomain domain = MaterialDomain::PostProcess;
        Array<MaterialParameterValue> parameters;
        Array<MaterialTextureValue> textures;

        MaterialParameterValue* FindParameter(std::string_view name) noexcept;
        const MaterialParameterValue* FindParameter(std::string_view name) const noexcept;
        const MaterialTextureValue* FindTexture(std::string_view name) const noexcept;
        // 그 이름의 값을 쓴다(없으면 더한다). `count` 는 1..4 다.
        void SetParameter(std::string_view name, const Float* values, UInt32 count);
        void RemoveParameter(std::string_view name);
        // 빈 아이디면 그 텍스처를 뺀다.
        void SetTexture(std::string_view name, AssetId texture);
    };

    // 깨진 YAML·모르는 `Domain` 은 거짓이고 `error` 에 까닭을 적는다. 빈 글자는 빈 머티리얼(셰이더 없음)이다. 숫자가 아닌 값·다섯 칸 넘는
    // 벡터는 `warnings` 에 적고 버린다.
    Bool ParseMaterial(const char* text, std::size_t length, MaterialDocument& document, String* error = nullptr,
        Array<String>* warnings = nullptr);
    String WriteMaterial(const MaterialDocument& document);

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
        // 알파가 `alphaCutoff` 보다 작은 픽셀을 버린다(잎·철망).
        Cutout,
        // **반투명**(D-443). 알파로 섞고 깊이를 쓰지 않는다. 렌더러가 불투명을 다 그린 뒤 카메라에서 먼 것부터 그린다(오브젝트 자리로 잰다).
        Blend
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
        // **양면**(D-443, glTF 의 `doubleSided`). 뒷면을 버리지 않고, 뒤에서 보면 법선을 뒤집어 빛을 받는다.
        Bool doubleSided = false;
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
        { MaterialAlpha::Cutout, "Cutout" },
        { MaterialAlpha::Blend,  "Blend" });

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
                MakeFieldEntry<&MaterialDesc::doubleSided>(),
                MakeFieldEntry<&MaterialDesc::baseColorTextureId>(),
                MakeFieldEntry<&MaterialDesc::metallicRoughnessTextureId>(),
                MakeFieldEntry<&MaterialDesc::normalTextureId>(),
                MakeFieldEntry<&MaterialDesc::occlusionTextureId>(),
                MakeFieldEntry<&MaterialDesc::emissiveTextureId>(),
            };
            static const StaticPropertyTable<13> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<MaterialDesc>("JBro.MaterialDesc", fields.Get());
            return descriptor;
        }
    };

    // 글자가 표준 머티리얼(`Version`·`Material:` 블록, `ParseMaterialFile`)인가. 아니면 셰이더 머티리얼(`ParseMaterial`)로 읽는다.
    // 맨 앞 칸에서 시작하는 `Material:` 키가 있으면 참이다 - 셰이더 머티리얼의 키는 `Shader`·`Domain`·`Parameters`·`Textures` 뿐이다.
    Bool IsStandardMaterialText(const char* text, std::size_t length);
}
