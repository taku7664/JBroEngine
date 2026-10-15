#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/RHI/ConstantRing.h>
#include <JBro/Types/Array.h>
#include <JBro/VulkanRHI/VulkanRHI.h>

#include "ConstantProbePS.generated.h"
#include "ConstantProbeVS.generated.h"

namespace Sm5
{
    using BYTE = unsigned char;
#include "ConstantProbePS_SM5.generated.h"
#include "ConstantProbeVS_SM5.generated.h"
}

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 후처리의 중간 텍스처 포맷 다섯(postprocess-plan §2.1)에 그려 되읽는다. 1 보다 큰 값을 칠한다 - UNORM 으로 잘못 만들어졌다면
// 1 로 잘려 드러난다. Vulkan 은 SPIR-V 탐침이 없어 그리기는 건너뛰고 텍셀 크기만 본다(ConstantBufferTests.cpp 의 머리말).
namespace
{
    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    constexpr JBro::UInt32 SurfaceSize = 16;

    struct Float4
    {
        JBro::Float x = 0.0f;
        JBro::Float y = 0.0f;
        JBro::Float z = 0.0f;
        JBro::Float w = 0.0f;
    };

    // 반정밀도 부동소수 하나를 푼다. 시험 값은 모두 정규수다.
    JBro::Float HalfToFloat(std::uint16_t bits)
    {
        const JBro::Int32 exponent = (bits >> 10) & 0x1F;
        const JBro::Int32 mantissa = bits & 0x3FF;
        const JBro::Float sign = (bits & 0x8000) != 0 ? -1.0f : 1.0f;
        if (exponent == 0)
        {
            return sign * std::ldexp(static_cast<JBro::Float>(mantissa), -24);
        }
        return sign * std::ldexp(1.0f + mantissa / 1024.0f, exponent - 15);
    }

    // 부호 없는 작은 부동소수(R11G11B10 의 한 채널)를 푼다. 지수는 5 비트, 가수는 `mantissaBits` 비트다.
    JBro::Float SmallFloatToFloat(std::uint32_t bits, JBro::Int32 mantissaBits)
    {
        const JBro::Int32 exponent = static_cast<JBro::Int32>(bits >> mantissaBits) & 0x1F;
        const JBro::Int32 mantissa = static_cast<JBro::Int32>(bits & ((1u << mantissaBits) - 1u));
        const JBro::Float scale = static_cast<JBro::Float>(1 << mantissaBits);
        if (exponent == 0)
        {
            return std::ldexp(mantissa / scale, -14);
        }
        return std::ldexp(1.0f + mantissa / scale, exponent - 15);
    }

    // 텍셀 하나를 RGBA 로 푼다. 포맷에 없는 채널은 0 이다.
    Float4 DecodeTexel(JBro::TextureFormat format, const std::byte* texel)
    {
        Float4 value;
        switch (format)
        {
        case JBro::TextureFormat::R16Float:
        {
            std::uint16_t r = 0;
            std::memcpy(&r, texel, 2);
            value.x = HalfToFloat(r);
            break;
        }
        case JBro::TextureFormat::RG16Float:
        {
            std::uint16_t rg[2] = {};
            std::memcpy(rg, texel, 4);
            value.x = HalfToFloat(rg[0]);
            value.y = HalfToFloat(rg[1]);
            break;
        }
        case JBro::TextureFormat::R32Float:
            std::memcpy(&value.x, texel, 4);
            break;
        case JBro::TextureFormat::RGBA32Float:
            std::memcpy(&value, texel, 16);
            break;
        case JBro::TextureFormat::R11G11B10Float:
        {
            std::uint32_t packed = 0;
            std::memcpy(&packed, texel, 4);
            value.x = SmallFloatToFloat(packed & 0x7FFu, 6);
            value.y = SmallFloatToFloat((packed >> 11) & 0x7FFu, 6);
            value.z = SmallFloatToFloat((packed >> 22) & 0x3FFu, 5);
            break;
        }
        default:
            break;
        }
        return value;
    }

    struct FormatCase
    {
        JBro::TextureFormat format = JBro::TextureFormat::Unknown;
        const char* name = "";
        JBro::UInt32 texelSize = 0;
        // 칠한 색에서 이 포맷이 담는 채널만 남긴 기대값이다.
        Float4 expected;
    };

    // 모든 값이 각 포맷에서 정확히 표현된다(2 의 거듭제곱의 합). 반올림이 끼면 허용 오차가 무엇을 덮는지 알 수 없다.
    constexpr Float4 PaintedTint{2.0f, 0.5f, 0.25f, 1.0f};

    const FormatCase Cases[] = {
        {JBro::TextureFormat::R11G11B10Float, "R11G11B10Float", 4, {2.0f, 0.5f, 0.25f, 0.0f}},
        {JBro::TextureFormat::R16Float, "R16Float", 2, {2.0f, 0.0f, 0.0f, 0.0f}},
        {JBro::TextureFormat::R32Float, "R32Float", 4, {2.0f, 0.0f, 0.0f, 0.0f}},
        {JBro::TextureFormat::RG16Float, "RG16Float", 4, {2.0f, 0.5f, 0.0f, 0.0f}},
        {JBro::TextureFormat::RGBA32Float, "RGBA32Float", 16, {2.0f, 0.5f, 0.25f, 1.0f}},
    };

    template <typename TModule>
    void TestEachFormatHoldsWhatWasDrawn()
    {
        for (const FormatCase& testCase : Cases)
        {
            Check(JBro::GetTexelSize(testCase.format) == testCase.texelSize, "the texel size must match the format");
        }

        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        if (false == rhi.Initialize(memory))
        {
            platform.Shutdown();
            std::cout << "  [skip] no runtime for this API; render target formats not verified" << std::endl;
            return;
        }
        JBro::RHIDeviceCreateInfo createInfo;
        createInfo.enableValidation = true;
        JBro::IRHIDevice* device = rhi.CreateDevice(createInfo);
        if (device == nullptr)
        {
            rhi.Shutdown();
            platform.Shutdown();
            std::cout << "  [skip] no device for this API; render target formats not verified" << std::endl;
            return;
        }
        const JBro::GraphicsApi api = rhi.GetApi();
        if (api != JBro::GraphicsApi::D3D12 && api != JBro::GraphicsApi::D3D11)
        {
            rhi.DestroyDevice(device);
            rhi.Shutdown();
            platform.Shutdown();
            std::cout << "  [skip] no SPIR-V probe shaders yet; render target formats not drawn" << std::endl;
            return;
        }

        JBro::WindowDesc windowDesc;
        const char title[] = "JBro format probe";
        windowDesc.title = {title, static_cast<JBro::UInt32>(sizeof(title) - 1)};
        windowDesc.width = SurfaceSize;
        windowDesc.height = SurfaceSize;
        windowDesc.visible = false;
        const JBro::WindowHandle window = platform.OpenPlatformWindow(windowDesc);
        Check(window.value != 0, "the probe window must open");
        JBro::SwapchainDesc swapchainDesc;
        swapchainDesc.surface = platform.CreateSurface(window);
        swapchainDesc.extent = {SurfaceSize, SurfaceSize};
        swapchainDesc.presentMode = JBro::PresentMode::Immediate;
        const JBro::SwapchainHandle swapchain = device->CreateSwapchain(swapchainDesc);
        Check(swapchain.IsValid(), "the probe swapchain must be created");

        JBro::ConstantRing ring;
        Check(ring.Create(*device, 1024), "the constant ring must be created");

        for (const FormatCase& testCase : Cases)
        {
            JBro::TextureDesc targetDesc;
            targetDesc.extent = {SurfaceSize, SurfaceSize};
            targetDesc.format = testCase.format;
            targetDesc.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
            const JBro::TextureHandle target = device->CreateTexture(targetDesc);
            if (false == target.IsValid())
            {
                std::cout << "  format " << testCase.name << std::endl;
            }
            Check(target.IsValid(), "a render target of each post process format must be created");

            JBro::GraphicsPipelineDesc pipelineDesc;
            if (api == JBro::GraphicsApi::D3D11)
            {
                pipelineDesc.vertexShader = {Sm5::JBroTestConstantProbeVS_SM5,
                    static_cast<JBro::UInt32>(sizeof(Sm5::JBroTestConstantProbeVS_SM5))};
                pipelineDesc.pixelShader = {Sm5::JBroTestConstantProbePS_SM5,
                    static_cast<JBro::UInt32>(sizeof(Sm5::JBroTestConstantProbePS_SM5))};
            }
            else
            {
                pipelineDesc.vertexShader = {JBroTestConstantProbeVS, static_cast<JBro::UInt32>(sizeof(JBroTestConstantProbeVS))};
                pipelineDesc.pixelShader = {JBroTestConstantProbePS, static_cast<JBro::UInt32>(sizeof(JBroTestConstantProbePS))};
            }
            pipelineDesc.colorFormats = {&testCase.format, 1};
            pipelineDesc.cull = JBro::CullMode::None;
            pipelineDesc.bindings.constantBuffers = {2, JBro::ShaderStage::Vertex | JBro::ShaderStage::Pixel};
            const JBro::GraphicsPipelineHandle pipeline = device->CreateGraphicsPipeline(pipelineDesc);
            Check(pipeline.IsValid(), "a pipeline drawing into each post process format must be created");

            const JBro::BeginFrameResult begun = device->BeginFrame(swapchain);
            Check(begun.status == JBro::FrameStatus::Ready, "the probe frame must begin");
            JBro::IRHICommandContext& commands = *begun.frame.commands;
            ring.BeginFrame(begun.frame.slot);
            const JBro::ConstantRange placement = ring.Push(Float4{1.0f, 1.0f, 0.0f, 0.0f});
            const JBro::ConstantRange tint = ring.Push(PaintedTint);

            JBro::ColorAttachmentDesc attachment;
            attachment.texture = target;
            attachment.loadOperation = JBro::LoadOperation::Clear;
            attachment.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
            JBro::RenderPassDesc pass;
            pass.colorAttachments = {&attachment, 1};
            Check(commands.BeginRenderPass(pass), "the format pass must begin");
            JBro::Viewport viewport;
            viewport.width = static_cast<JBro::Float>(SurfaceSize);
            viewport.height = static_cast<JBro::Float>(SurfaceSize);
            commands.SetViewport(viewport);
            commands.SetScissor({0, 0, static_cast<JBro::Int32>(SurfaceSize), static_cast<JBro::Int32>(SurfaceSize)});
            Check(commands.SetGraphicsPipeline(pipeline), "the format pipeline must bind");
            Check(commands.SetConstantBuffer(1, placement.buffer, placement.offset, placement.size), "the placement must bind");
            Check(commands.SetConstantBuffer(2, tint.buffer, tint.offset, tint.size), "the tint must bind");
            Check(commands.DrawInstanced(3, 1, 0, 0), "the full screen triangle must draw");
            commands.EndRenderPass();
            Check(device->EndFrame(begun.frame) == JBro::FrameStatus::Ready, "the format frame must end");

            JBro::Array<std::byte> image;
            image.Resize(static_cast<std::size_t>(SurfaceSize) * SurfaceSize * testCase.texelSize);
            JBro::TextureReadback readback;
            Check(device->ReadTexture(target, image.Data(), image.Size(), readback), "each format must read back");
            Check(readback.format == testCase.format, "the readback names the format it read");
            const Float4 texel = DecodeTexel(testCase.format,
                image.Data() + static_cast<std::size_t>(SurfaceSize / 2) * readback.rowPitch
                    + static_cast<std::size_t>(SurfaceSize / 2) * testCase.texelSize);
            const JBro::Bool exact = texel.x == testCase.expected.x && texel.y == testCase.expected.y
                && texel.z == testCase.expected.z && texel.w == testCase.expected.w;
            if (false == exact)
            {
                std::cout << "  format " << testCase.name << " read " << texel.x << ", " << texel.y << ", " << texel.z
                    << ", " << texel.w << std::endl;
            }
            Check(exact, "each format must hold the drawn values, above one included");

            device->DestroyGraphicsPipeline(pipeline);
            device->DestroyTexture(target);
        }

        const JBro::UInt32 errors = device->GetValidationErrorCount();
        if (errors != 0)
        {
            std::cout << "  the debug layer reported " << errors << " error(s) during the format probe" << std::endl;
        }
        Check(errors == 0, "the debug layer must stay quiet");
        ring.Destroy();
        device->DestroySwapchain(swapchain);
        rhi.DestroyDevice(device);
        platform.ClosePlatformWindow(window);
        rhi.Shutdown();
        platform.Shutdown();
    }
}

JBro::Int32 RunRenderTargetFormatTests()
{
    TestEachFormatHoldsWhatWasDrawn<JBro::D3D12RHIModule>();
    TestEachFormatHoldsWhatWasDrawn<JBro::D3D11RHIModule>();
    TestEachFormatHoldsWhatWasDrawn<JBro::VulkanRHIModule>();
    std::cout << "Render target format tests passed.\n";
    return 0;
}
