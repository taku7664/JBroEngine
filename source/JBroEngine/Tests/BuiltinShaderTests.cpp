#include <JBro/Graphics/BuiltinShaders.generated.h>

#include <Windows.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

#pragma comment(lib, "d3dcompiler.lib")

// 빌트인 셰이더 표(D-329)가 셰이더 자신과 맞는지 잰다. 매니페스트(`Shaders/ShaderManifest.psd1`)는 사람이 적는 것이라 틀릴 수 있다 -
// 셰이더가 쓰는 자원을 자리표가 빠뜨리면 그리는 자리에서 조용히 빈 자리를 읽고, 자리표가 쓰지 않는 자리를 적으면 묶지 않은 그리기가
// 거절된다. 여기서는 SM 5.0 바이트코드를 `D3DReflect`(d3dcompiler_47, Windows 에 늘 있다)로 열어 양쪽을 대조한다.
namespace
{
    using Microsoft::WRL::ComPtr;

    // 어긋남을 모두 세고 끝에서 한 번에 실패한다 - 매니페스트를 고치는 사람이 한 번에 다 보게 한다.
    JBro::UInt32 g_mismatches = 0;

    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            ++g_mismatches;
        }
    }

    // 프로그램 하나에서 쓰인 자리를 모은다. 비트 n 이 슬롯 n 이고, 단계마다 따로 든다.
    struct UsedSlots
    {
        std::uint32_t constantBuffers[2] = {};   // [단계 V/P] b#
        std::uint32_t textures[2] = {};
        std::uint32_t samplers[2] = {};
        std::uint32_t pushConstantBytes[2] = {}; // b0 의 크기, 쓰지 않으면 0
    };

    void ReflectStage(const JBro::JArrayView<std::byte>& code, std::uint32_t stage, UsedSlots& used, const char* program)
    {
        ComPtr<ID3D11ShaderReflection> reflection;
        if (FAILED(D3DReflect(code.data, code.size, IID_PPV_ARGS(&reflection))))
        {
            std::cout << "  program " << program << std::endl;
            Check(false, "every built-in stage's DXBC must reflect");
        }
        D3D11_SHADER_DESC shaderDesc = {};
        reflection->GetDesc(&shaderDesc);
        for (UINT index = 0; index < shaderDesc.BoundResources; ++index)
        {
            D3D11_SHADER_INPUT_BIND_DESC bind = {};
            reflection->GetResourceBindingDesc(index, &bind);
            switch (bind.Type)
            {
            case D3D_SIT_CBUFFER:
                if (bind.BindPoint == 0)
                {
                    D3D11_SHADER_BUFFER_DESC bufferDesc = {};
                    reflection->GetConstantBufferByName(bind.Name)->GetDesc(&bufferDesc);
                    used.pushConstantBytes[stage] = bufferDesc.Size;
                }
                else
                {
                    used.constantBuffers[stage] |= 1u << bind.BindPoint;
                }
                break;
            case D3D_SIT_TEXTURE:
                used.textures[stage] |= 1u << bind.BindPoint;
                break;
            case D3D_SIT_SAMPLER:
                used.samplers[stage] |= 1u << bind.BindPoint;
                break;
            default:
                std::cout << "  program " << program << " binds resource type " << bind.Type << std::endl;
                Check(false, "a built-in graphics shader binds only constants, textures and samplers");
                break;
            }
        }
    }

    // 자리표 하나와 쓰인 자리를 대조한다. 첫 슬롯 `first`(b 는 1, t·s 는 0)부터 count 개가 선언이다.
    void CheckRange(const JBro::ShaderResourceRange& declared, const std::uint32_t used[2], std::uint32_t first,
        const char* program, const char* what)
    {
        const std::uint32_t declaredBits = declared.count == 0 ? 0u : ((1u << declared.count.Get()) - 1u) << first;
        const std::uint32_t usedAnywhere = used[0] | used[1];
        if ((usedAnywhere & ~declaredBits) != 0 || usedAnywhere != declaredBits)
        {
            std::cout << "  program " << program << " " << what << ": declared bits " << declaredBits << ", used bits "
                << usedAnywhere << std::endl;
        }
        Check((usedAnywhere & ~declaredBits) == 0, "a shader uses no slot its program leaves undeclared");
        Check(usedAnywhere == declaredBits, "a program declares no slot its shaders never read");
        // 읽는 단계가 자리표의 단계 안이어야 한다 - 밖이면 D3D12 의 루트 가시성·Vulkan 의 stageFlags 가 그 단계에 열지 않는다.
        const std::uint8_t stageBits[2] = {JBro::ShaderStageMask::Vertex, JBro::ShaderStageMask::Pixel};
        for (std::uint32_t stage = 0; stage < 2; ++stage)
        {
            if (used[stage] != 0 && (declared.stageMask & stageBits[stage]) == 0)
            {
                std::cout << "  program " << program << " " << what << " is read in stage " << stage << std::endl;
                Check(false, "every stage that reads a resource is in its range's stages");
            }
        }
    }

    void TestEveryProgramMatchesItsShaders()
    {
        const auto count = static_cast<std::uint32_t>(JBro::BuiltinShader::Count);
        Check(count == 13, "thirteen built-in programs come from the manifest");
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const JBro::ShaderData& shader = JBro::GetBuiltinShader(static_cast<JBro::BuiltinShader>(index));
            for (std::uint32_t format = 0; format < JBro::ShaderCodeFormatCount; ++format)
            {
                const auto codeFormat = static_cast<JBro::ShaderCodeFormat>(format);
                Check(shader.Code(JBro::ShaderProgramStage::Vertex, codeFormat).size != 0
                        && shader.Code(JBro::ShaderProgramStage::Pixel, codeFormat).size != 0,
                    "every built-in program has its vertex and pixel code for every backend");
            }
            Check(false == shader.HasStage(JBro::ShaderProgramStage::Compute), "built-in graphics programs have no compute stage");

            UsedSlots used;
            ReflectStage(shader.Code(JBro::ShaderProgramStage::Vertex, JBro::ShaderCodeFormat::Dxbc), 0, used, shader.name);
            ReflectStage(shader.Code(JBro::ShaderProgramStage::Pixel, JBro::ShaderCodeFormat::Dxbc), 1, used, shader.name);

            CheckRange(shader.constantBuffers, used.constantBuffers, 1, shader.name, "constant buffers");
            CheckRange(shader.sampledTextures, used.textures, 0, shader.name, "textures");
            CheckRange(shader.samplers, used.samplers, 0, shader.name, "samplers");
            Check(shader.storageTextures.count == 0 && shader.storageBuffers.count == 0, "graphics programs take no storage");

            // 푸시 상수(b0): 쓰는 단계마다 크기가 같아야 하고, 그 단계가 푸시 상수 단계 안이어야 한다.
            const std::uint8_t stageBits[2] = {JBro::ShaderStageMask::Vertex, JBro::ShaderStageMask::Pixel};
            std::uint32_t pushUsers = 0;
            for (std::uint32_t stage = 0; stage < 2; ++stage)
            {
                if (used.pushConstantBytes[stage] == 0)
                {
                    continue;
                }
                ++pushUsers;
                if (used.pushConstantBytes[stage] != shader.pushConstantBytes
                    || (shader.pushConstantStageMask & stageBits[stage]) == 0)
                {
                    std::cout << "  program " << shader.name << " push block " << used.pushConstantBytes[stage]
                        << " bytes in stage " << stage << ", manifest " << shader.pushConstantBytes << std::endl;
                }
                Check(used.pushConstantBytes[stage] == shader.pushConstantBytes,
                    "the push block a shader declares is the size its program says");
                Check((shader.pushConstantStageMask & stageBits[stage]) != 0, "a stage reading push constants is in its stages");
            }
            Check((pushUsers != 0) == (shader.pushConstantBytes != 0), "a program declares push constants exactly when a shader reads them");
        }
    }
}

JBro::Int32 RunBuiltinShaderTests()
{
    g_mismatches = 0;
    TestEveryProgramMatchesItsShaders();
    if (g_mismatches != 0)
    {
        throw std::runtime_error("the built-in shader table disagrees with its shaders");
    }
    std::cout << "Built-in shader tests passed.\n";
    return 0;
}
