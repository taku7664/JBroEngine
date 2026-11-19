#pragma once

#include <JBro/Asset/ShaderData.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace JBro
{
    // **머티리얼이 그리는 자리**(D-347, postprocess-plan §3.14). 셰이더(`.hlsl`)는 픽셀 단계 하나만 적고, 정점 단계와 입력은 엔진이 자리마다 준다.
    // 같은 `.hlsl` 을 자리마다 따로 굽는다 - 입력 구조가 다르기 때문이다(`MaterialPrelude` 의 `JBroPostProcessInput`·`JBroSpriteInput`).
    enum class MaterialDomain : std::uint8_t
    {
        // 화면 전체를 덮는 삼각형 하나다(후처리 효과 `Material`). t0 이 앞 효과의 결과(레이어 효과면 곱한 알파의 레이어)다.
        PostProcess,
        // 스프라이트 사각형이다(`SpriteRenderer2D.materialId`). t0 이 스프라이트의 텍스처, 입력에 틴트와 UV 가 온다.
        Sprite,
        // 3D 메시다(`MeshRenderer3D.materialId`, D-348). t0 은 없고, 입력에 월드 위치·법선·UV·틴트가 온다. 셰이더가 최종 색을 낸다 - 엔진의 빛은
        // `JBroMeshLight(normal, worldPosition)` 로 곱한다(뷰의 `Light3D`, 없으면 렌더러의 기본 빛 블록, D-407).
        Mesh,
    };
    inline constexpr UInt32 MaterialDomainCount = 3;

    JBRO_DEFINE_ENUM_TYPE(MaterialDomain, "JBro.MaterialDomain",
        { MaterialDomain::PostProcess, "PostProcess" },
        { MaterialDomain::Sprite,      "Sprite" },
        { MaterialDomain::Mesh,        "Mesh" });

    // 머티리얼 파라미터(셰이더의 `cbuffer Material : register(b1)` 의 변수 하나)의 모양이다. 스칼라와 벡터만 받는다 - 행렬·배열·구조체가 든
    // 셰이더는 굽기에서 거절한다.
    enum class MaterialParameterType : std::uint8_t
    {
        Float,
        Float2,
        Float3,
        Float4,
        Int,
        UInt,
        Bool,
    };

    // 칸 수(1..4)다. `Int`·`UInt`·`Bool` 은 하나다.
    UInt32 GetMaterialParameterComponents(MaterialParameterType type) noexcept;

    // 셰이더 반사에서 뽑은 파라미터 하나다. `offset` 은 상수 버퍼 안의 바이트 자리, `defaults` 는 HLSL 의 초기값(없으면 0)이다.
    struct MaterialParameterInfo
    {
        String name;
        MaterialParameterType type = MaterialParameterType::Float;
        UInt32 offset = 0;
        Float defaults[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        // 이름이 `color`·`Color` 로 끝나는 `Float3`·`Float4` 면 참이다 - 인스펙터가 색 칸으로 보인다.
        Bool color = false;
    };

    // 셰이더가 읽는 머티리얼 텍스처 하나다. `slot` 은 t 레지스터 번호(1..)다 - t0 은 자리의 입력이다.
    struct MaterialTextureInfo
    {
        String name;
        UInt32 slot = 1;
    };

    // **자리 하나로 구운 셰이더**(D-347). 픽셀 단계의 세 백엔드 바이트코드와 반사 결과다. 정점 단계는 엔진의 빌트인이다.
    struct CompiledMaterialShader
    {
        MaterialDomain domain = MaterialDomain::PostProcess;
        Array<std::byte> pixel[ShaderCodeFormatCount];
        // `cbuffer Material` 의 크기(16 의 배수)다. 없으면 0 이다.
        UInt32 parameterBytes = 0;
        Array<MaterialParameterInfo> parameters;
        Array<MaterialTextureInfo> textures;

        const MaterialParameterInfo* FindParameter(std::string_view name) const noexcept;
        const MaterialTextureInfo* FindTexture(std::string_view name) const noexcept;
    };

    // 머티리얼의 상한이다. 파라미터 블록은 한 번에 묶는 상수 버퍼 하나이고, 텍스처는 t1..t6 이다(t0 은 자리의 입력, t7 은 엔진의 라이트맵 -
    // 렌더러의 텍스처 자리가 세 백엔드 모두 8 이다, D-348).
    inline constexpr UInt32 MaxMaterialParameterBytes = 256;
    inline constexpr UInt32 MaxMaterialTextures = 6;
    // **스프라이트 자리의 라이트맵**(D-348). 엔진의 입구(`GetMaterialEntryPoint`)가 사용자 `PSMain` 의 색에 이 자리의 빛을 곱한다. 빛을 받지 않는
    // 스프라이트는 렌더러가 흰 텍스처를 묶는다 - 셰이더는 하나다.
    inline constexpr UInt32 MaterialLightMapSlot = 7;
    // 머티리얼 셰이더가 쓰는 샘플러 넷이다(s0 자리의 입력·s1 선형 자르기·s2 점 자르기·s3 선형 되풀이).
    inline constexpr UInt32 MaterialSamplerCount = 4;

    // **구운 셰이더 묶음**(`.hlsl` 하나, 자리마다 하나까지). 게임 패키지의 `CompiledShader` 블롭이 이것이다 - 게임은 굽지 않는다.
    struct CompiledShaderSet
    {
        Array<CompiledMaterialShader> variants;

        const CompiledMaterialShader* Find(MaterialDomain domain) const noexcept;
    };

    // 블롭으로 쓰고 읽는다(`JMSH` 머리, 판 1). 읽기는 깨진 블롭·상한을 넘는 값을 거절한다.
    void WriteCompiledShaderSet(const CompiledShaderSet& set, Array<std::byte>& out);
    Bool ReadCompiledShaderSet(const std::byte* data, std::size_t size, CompiledShaderSet& set);

    // **셰이더 굽는 곳**(D-347). 에디터·시험·게임 빌드가 구현(`JBroShaderCompiler`)을 넘긴다. 게임은 넘기지 않는다 - 패키지에 구운 것만 싣는다.
    // 메인 스레드에서 부른다.
    class IShaderCompiler
    {
    public:
        virtual ~IShaderCompiler() = default;

        // `source` 를 `domain` 의 픽셀 셰이더로 굽고 반사한다. `sourceName` 은 오류의 파일 이름이다. 실패하면 거짓이고 `error` 에 컴파일러의
        // 말을 적는다. 어느 백엔드든 굽지 못하면 실패다 - 단, 그 백엔드의 컴파일러가 없으면(`HasFormat`) 그 칸만 비운다.
        virtual Bool Compile(std::string_view source, const char* sourceName, MaterialDomain domain, CompiledMaterialShader& out,
            String& error) = 0;
        // 그 백엔드로 구울 수 있는가 - SPIR-V 는 Vulkan SDK 의 컴파일러가 있어야 한다.
        virtual Bool HasFormat(ShaderCodeFormat format) const = 0;
    };

    // **머티리얼 셰이더의 머리글**(D-347). 굽는 쪽이 `#include "JBro/Material.hlsli"` 로 이 글자를 준다. 자리마다 입력 구조·t0·샘플러·
    // 엔진 값(b0)을 선언한다. 정점 단계의 출력과 같은 모양이어야 한다(`BuiltinSprite.hlsl`·`BuiltinPostProcess.hlsl`).
    const char* GetMaterialPrelude() noexcept;
    // 머리글의 include 이름이다.
    inline constexpr const char* MaterialPreludeName = "JBro/Material.hlsli";
    // 새 셰이더 파일의 처음 글자다. 에디터의 `새 셰이더` 가 쓴다.
    const char* GetMaterialShaderTemplate() noexcept;
    // **자리의 입구와 맺음말**(D-348). 사용자는 언제나 `PSMain` 을 적는다. 엔진이 그 뒤에 덧붙이는 맺음말이 있으면(스프라이트) 입구는 맺음말의 함수이고
    // 그것이 `PSMain` 을 부른다. 맺음말은 `#line` 으로 제 이름을 달아 사용자 파일의 줄 번호를 밀지 않는다. 없으면 빈 글자다.
    const char* GetMaterialEntryPoint(MaterialDomain domain) noexcept;
    const char* GetMaterialEpilogue(MaterialDomain domain) noexcept;
}
