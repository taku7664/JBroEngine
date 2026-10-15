#pragma once

#include <JBro/Core/Core.h>

#include <cstddef>
#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // 셰이더 프로그램 하나의 자료다(D-111·D-329, postprocess-plan §2.4 의 Shader 에셋). 단계마다 백엔드별 바이트코드와,
    // 그 프로그램이 읽는 자원의 자리표·푸시 상수를 든다. 그래픽 쪽이 이것을 RHI 의 파이프라인 서술자로 옮긴다 - 에셋 모듈은
    // RHI 를 모르므로 RHI 타입을 쓰지 않는다.
    //
    // **뷰만 든다.** 바이트와 이름은 자료를 만든 쪽이 지킨다 - 빌트인은 실행 파일의 정적 배열(`BuiltinShaders.generated.cpp`)이다.
    // 파라미터 블록(b1.. 의 필드 배치)은 그것을 쓰는 첫 셰이더(효과, 6 단계)와 함께 더한다.

    // 바이트코드의 종류다. 백엔드마다 하나다.
    enum class ShaderCodeFormat : std::uint8_t
    {
        Dxil,   // D3D12
        Dxbc,   // D3D11(SM 5.0)
        SpirV   // Vulkan
    };
    inline constexpr UInt32 ShaderCodeFormatCount = 3;

    enum class ShaderProgramStage : std::uint8_t
    {
        Vertex,
        Pixel,
        Compute
    };
    inline constexpr UInt32 ShaderProgramStageCount = 3;

    // 자원을 읽는 단계의 비트다. RHI 의 `ShaderStage` 와 같은 값이다(그래픽 쪽이 `static_assert` 로 묶는다).
    namespace ShaderStageMask
    {
        inline constexpr std::uint8_t Vertex = 1u << 0u;
        inline constexpr std::uint8_t Pixel = 1u << 1u;
        inline constexpr std::uint8_t Compute = 1u << 2u;
    }

    // 한 종류의 자원 자리다. RHI 의 `BindingRange` 와 같은 뜻이다(슬롯 규칙은 `RHI.h` 의 `BindingLayoutDesc`).
    struct ShaderResourceRange
    {
        UInt32 count = 0;
        std::uint8_t stageMask = 0;
    };

    struct ShaderData
    {
        // 매니페스트의 이름이다(`Sprite`). 진단과 시험이 쓴다.
        const char* name = "";
        // [단계][포맷]. 그 단계가 없거나 그 포맷을 굽지 않았으면 비어 있다.
        JArrayView<std::byte> code[ShaderProgramStageCount][ShaderCodeFormatCount] = {};
        UInt32 pushConstantBytes = 0;
        std::uint8_t pushConstantStageMask = 0;
        ShaderResourceRange constantBuffers;
        ShaderResourceRange sampledTextures;
        ShaderResourceRange samplers;
        ShaderResourceRange storageTextures;
        ShaderResourceRange storageBuffers;

        const JArrayView<std::byte>& Code(ShaderProgramStage stage, ShaderCodeFormat format) const noexcept
        {
            return code[static_cast<std::uint8_t>(stage)][static_cast<std::uint8_t>(format)];
        }

        Bool HasStage(ShaderProgramStage stage) const noexcept
        {
            for (UInt32 format = 0; format < ShaderCodeFormatCount; ++format)
            {
                if (code[static_cast<std::uint8_t>(stage)][format.Get()].size != 0)
                {
                    return true;
                }
            }
            return false;
        }
    };
}
