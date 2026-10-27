#pragma once

#include <JBro/Asset/MaterialShader.h>
#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

#include <cstddef>
#include <string_view>

namespace JBro
{
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
}
