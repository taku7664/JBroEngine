#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/RHI/ConstantRing.h>
#include <JBro/Types/Array.h>
#include <JBro/VulkanRHI/VulkanRHI.h>

#include "SubresourceProbeDepthPS.generated.h"
#include "SubresourceProbeSamplePS.generated.h"
#include "SubresourceProbeTintPS.generated.h"
#include "SubresourceProbeVS.generated.h"
#include "VolumeProbePS.generated.h"

// fxc 의 헤더는 `BYTE` 로 적는다. Windows.h 를 끌어들이지 않고 이름만 준다(Renderer.cpp 와 같다).
namespace Sm5
{
    using BYTE = unsigned char;
#include "SubresourceProbeDepthPS_SM5.generated.h"
#include "SubresourceProbeSamplePS_SM5.generated.h"
#include "SubresourceProbeTintPS_SM5.generated.h"
#include "SubresourceProbeVS_SM5.generated.h"
#include "VolumeProbePS_SM5.generated.h"
}

#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 후처리 계획 2 단계(postprocess-plan §3)의 RHI 를 잰다 - mip 하나에 그리기·mip 하나 읽기·mip 마다의 상태·깊이 읽기.
//
// D3D12 와 D3D11 은 그려서 잰다. **D3D12 의 검증 레이어가 이 시험의 반이다** - mip 마다 상태를 들지 않으면 그림은 맞게 나와도
// "mip 0 을 읽는 동안 mip 1 이 렌더 타깃" 같은 자리에서 검증 오류가 난다. Vulkan 은 SPIR-V 탐침이 없어 그리지 않고, 셰이더 없이
// 되는 것(만들기·거절)만 잰다(ConstantBufferTests.cpp 의 머리말).
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

    constexpr JBro::UInt32 SurfaceSize = 64;

    struct Float4
    {
        JBro::Float x = 0.0f;
        JBro::Float y = 0.0f;
        JBro::Float z = 0.0f;
        JBro::Float w = 0.0f;
    };

    // 정점 단계의 b1 이다. 셰이더의 `Placement` 와 같은 배치다.
    struct Placement
    {
        Float4 placement{1.0f, 1.0f, 0.0f, 0.0f};
        Float4 depth{0.0f, 0.0f, 0.0f, 0.0f};
    };

    // 화면의 왼쪽 절반만 덮는다(x ∈ [-1, 0]).
    constexpr Float4 LeftHalf{0.5f, 1.0f, -0.5f, 0.0f};
    // 오른쪽 절반(x ∈ [0, 1]).
    constexpr Float4 RightHalf{0.5f, 1.0f, 0.5f, 0.0f};

    JBro::Bool HasProbeShaders(JBro::GraphicsApi api)
    {
        return api == JBro::GraphicsApi::D3D12 || api == JBro::GraphicsApi::D3D11;
    }

    enum class ProbePixel
    {
        Tint,
        Sample,
        Depth,
        Volume
    };

    JBro::ShaderBytecode VertexShader(JBro::GraphicsApi api)
    {
        if (api == JBro::GraphicsApi::D3D11)
        {
            return {Sm5::JBroTestSubresourceProbeVS_SM5, static_cast<JBro::UInt32>(sizeof(Sm5::JBroTestSubresourceProbeVS_SM5))};
        }
        return {JBroTestSubresourceProbeVS, static_cast<JBro::UInt32>(sizeof(JBroTestSubresourceProbeVS))};
    }

    JBro::ShaderBytecode PixelShader(JBro::GraphicsApi api, ProbePixel pixel)
    {
        const JBro::Bool sm5 = api == JBro::GraphicsApi::D3D11;
        switch (pixel)
        {
        case ProbePixel::Tint:
            return sm5 ? JBro::ShaderBytecode{Sm5::JBroTestSubresourceProbeTintPS_SM5,
                             static_cast<JBro::UInt32>(sizeof(Sm5::JBroTestSubresourceProbeTintPS_SM5))}
                       : JBro::ShaderBytecode{JBroTestSubresourceProbeTintPS,
                             static_cast<JBro::UInt32>(sizeof(JBroTestSubresourceProbeTintPS))};
        case ProbePixel::Sample:
            return sm5 ? JBro::ShaderBytecode{Sm5::JBroTestSubresourceProbeSamplePS_SM5,
                             static_cast<JBro::UInt32>(sizeof(Sm5::JBroTestSubresourceProbeSamplePS_SM5))}
                       : JBro::ShaderBytecode{JBroTestSubresourceProbeSamplePS,
                             static_cast<JBro::UInt32>(sizeof(JBroTestSubresourceProbeSamplePS))};
        case ProbePixel::Depth:
            return sm5 ? JBro::ShaderBytecode{Sm5::JBroTestSubresourceProbeDepthPS_SM5,
                             static_cast<JBro::UInt32>(sizeof(Sm5::JBroTestSubresourceProbeDepthPS_SM5))}
                       : JBro::ShaderBytecode{JBroTestSubresourceProbeDepthPS,
                             static_cast<JBro::UInt32>(sizeof(JBroTestSubresourceProbeDepthPS))};
        case ProbePixel::Volume:
            return sm5 ? JBro::ShaderBytecode{Sm5::JBroTestVolumeProbePS_SM5,
                             static_cast<JBro::UInt32>(sizeof(Sm5::JBroTestVolumeProbePS_SM5))}
                       : JBro::ShaderBytecode{JBroTestVolumeProbePS, static_cast<JBro::UInt32>(sizeof(JBroTestVolumeProbePS))};
        }
        return {};
    }

    // 탐침 파이프라인 하나. 상수 버퍼 둘(b1 정점, b2 픽셀)은 늘 있고, 읽는 셰이더면 텍스처 하나와 샘플러 하나를 더한다.
    JBro::GraphicsPipelineHandle CreateProbePipeline(JBro::IRHIDevice& device, JBro::GraphicsApi api, ProbePixel pixel,
        const JBro::TextureFormat* colorFormat, JBro::TextureFormat depthFormat)
    {
        JBro::GraphicsPipelineDesc desc;
        desc.vertexShader = VertexShader(api);
        desc.pixelShader = PixelShader(api, pixel);
        desc.colorFormats = {colorFormat, 1};
        desc.depthFormat = depthFormat;
        desc.cull = JBro::CullMode::None;
        desc.bindings.constantBuffers = {2, JBro::ShaderStage::Vertex | JBro::ShaderStage::Pixel};
        if (pixel != ProbePixel::Tint)
        {
            desc.bindings.sampledTextures = {1, JBro::ShaderStage::Pixel};
        }
        if (pixel == ProbePixel::Sample || pixel == ProbePixel::Volume)
        {
            desc.bindings.samplers = {1, JBro::ShaderStage::Pixel};
        }
        return device.CreateGraphicsPipeline(desc);
    }

    template <typename TModule>
    struct Probe
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::IRHIDevice* device = nullptr;
        JBro::WindowHandle window;
        JBro::SwapchainHandle swapchain;
        JBro::SamplerHandle pointSampler;
        JBro::ConstantRing ring;
        JBro::Bool platformOpen = false;
        JBro::Bool rhiOpen = false;

        JBro::Bool Open(const char* title)
        {
            JBro::JMemoryContext memory;
            Check(platform.Initialize(memory), "the platform must initialize");
            platformOpen = true;
            if (false == rhi.Initialize(memory))
            {
                Close();
                return false;
            }
            rhiOpen = true;
            JBro::RHIDeviceCreateInfo createInfo;
            createInfo.enableValidation = true;
            device = rhi.CreateDevice(createInfo);
            if (device == nullptr)
            {
                Close();
                return false;
            }
            JBro::WindowDesc windowDesc;
            windowDesc.title = {title, static_cast<JBro::UInt32>(std::strlen(title))};
            windowDesc.width = SurfaceSize;
            windowDesc.height = SurfaceSize;
            windowDesc.visible = false;
            window = platform.OpenPlatformWindow(windowDesc);
            Check(window.value != 0, "the probe window must open");
            JBro::SwapchainDesc swapchainDesc;
            swapchainDesc.surface = platform.CreateSurface(window);
            swapchainDesc.extent = {SurfaceSize, SurfaceSize};
            swapchainDesc.presentMode = JBro::PresentMode::Immediate;
            swapchain = device->CreateSwapchain(swapchainDesc);
            Check(swapchain.IsValid(), "the probe swapchain must be created");
            JBro::SamplerDesc samplerDesc;
            samplerDesc.minFilter = JBro::FilterMode::Nearest;
            samplerDesc.magFilter = JBro::FilterMode::Nearest;
            pointSampler = device->CreateSampler(samplerDesc);
            Check(pointSampler.IsValid(), "the point sampler must be created");
            Check(ring.Create(*device, 4096), "the constant ring must be created");
            return true;
        }

        void CheckValidationStayedQuiet(const char* what) const
        {
            const JBro::UInt32 errors = device->GetValidationErrorCount();
            if (errors != 0)
            {
                std::cout << "  the debug layer reported " << errors << " error(s) during " << what << std::endl;
            }
            Check(errors == 0, "the debug layer must stay quiet");
        }

        void Close()
        {
            if (device != nullptr)
            {
                ring.Destroy();
                device->DestroySampler(pointSampler);
                device->DestroySwapchain(swapchain);
                rhi.DestroyDevice(device);
                device = nullptr;
            }
            if (window.value != 0)
            {
                platform.ClosePlatformWindow(window);
                window = {};
            }
            if (rhiOpen)
            {
                rhi.Shutdown();
                rhiOpen = false;
            }
            if (platformOpen)
            {
                platform.Shutdown();
                platformOpen = false;
            }
        }

        // 링에서 두 상수를 올려 b1·b2 에 묶는다.
        void BindConstants(JBro::IRHICommandContext& commands, const Placement& placement, const Float4& tint)
        {
            const JBro::ConstantRange vertex = ring.Push(placement);
            const JBro::ConstantRange pixel = ring.Push(tint);
            Check(vertex.IsValid() && pixel.IsValid(), "the constants must fit in the ring");
            Check(commands.SetConstantBuffer(1, vertex.buffer, vertex.offset, vertex.size), "b1 must bind");
            Check(commands.SetConstantBuffer(2, pixel.buffer, pixel.offset, pixel.size), "b2 must bind");
        }
    };

    // 색 첨부 하나(그 mip)에 패스를 연다. 뷰포트·시저는 그 mip 의 크기로 다시 건다 - 백엔드마다 기본값이 달라 시험이 정한다.
    JBro::Bool BeginPass(JBro::IRHICommandContext& commands, JBro::TextureHandle target, JBro::UInt32 mip,
        JBro::UInt32 size, const JBro::DepthStencilAttachmentDesc* depth)
    {
        JBro::ColorAttachmentDesc attachment;
        attachment.texture = target;
        attachment.mipLevel = mip;
        attachment.loadOperation = JBro::LoadOperation::Clear;
        attachment.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
        JBro::RenderPassDesc pass;
        pass.colorAttachments = {&attachment, 1};
        pass.depthStencilAttachment = depth;
        if (false == commands.BeginRenderPass(pass))
        {
            return false;
        }
        JBro::Viewport viewport;
        viewport.width = static_cast<JBro::Float>(size);
        viewport.height = static_cast<JBro::Float>(size);
        commands.SetViewport(viewport);
        commands.SetScissor({0, 0, static_cast<JBro::Int32>(size), static_cast<JBro::Int32>(size)});
        return true;
    }

    Float4 ReadBgra(const JBro::Array<std::byte>& image, JBro::UInt32 rowPitch, JBro::UInt32 x, JBro::UInt32 y)
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(image.Data() + static_cast<std::size_t>(y) * rowPitch + x * 4);
        return {bytes[2] / 255.0f, bytes[1] / 255.0f, bytes[0] / 255.0f, bytes[3] / 255.0f};
    }

    JBro::Bool IsColor(const Float4& actual, const Float4& expected)
    {
        const auto close = [](JBro::Float a, JBro::Float b) { return a - b < 0.02f && b - a < 0.02f; };
        return close(actual.x, expected.x) && close(actual.y, expected.y) && close(actual.z, expected.z);
    }

    // **mip 수와 깊이 읽기를 장치가 받는지 본다.** 그리지 않으므로 세 백엔드에서 돈다.
    template <typename TModule>
    void TestTexturesTakeAMipChainAndReadableDepth()
    {
        Probe<TModule> probe;
        if (false == probe.Open("JBro subresource caps probe"))
        {
            std::cout << "  [skip] no device for this API; mip chains not verified" << std::endl;
            return;
        }
        Check(probe.device->GetCaps().maxTextureMipLevels == JBro::MaxTextureMipLevels, "the caps name the mip limit");
        Check(JBro::GetMipExtent(64, 2) == 16 && JBro::GetMipExtent(3, 5) == 1, "mip extents halve and stop at one");
        Check(JBro::IsMipCountValid({64, 32}, 7) && false == JBro::IsMipCountValid({64, 32}, 8),
            "a 64 wide texture has seven mips and no eighth");
        Check(false == JBro::IsMipCountValid({1 << 20, 1}, 17), "no texture has more mips than the limit");

        JBro::TextureDesc chain;
        chain.extent = {SurfaceSize, SurfaceSize};
        chain.mipLevels = 7;
        chain.format = JBro::TextureFormat::RGBA16Float;
        chain.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
        const JBro::TextureHandle full = probe.device->CreateTexture(chain);
        Check(full.IsValid(), "a render target with its full mip chain must be created");
        chain.mipLevels = 8;
        Check(false == probe.device->CreateTexture(chain).IsValid(), "a mip past the chain's end is refused");

        JBro::TextureDesc depth;
        depth.extent = {SurfaceSize, SurfaceSize};
        depth.format = JBro::TextureFormat::D32Float;
        depth.usage = JBro::TextureUsage::DepthStencil | JBro::TextureUsage::Sampled;
        const JBro::TextureHandle readableDepth = probe.device->CreateTexture(depth);
        Check(readableDepth.IsValid(), "a depth texture the shaders read must be created");
        depth.mipLevels = 2;
        Check(false == probe.device->CreateTexture(depth).IsValid(), "a depth texture is drawn at mip 0 only");

        probe.device->DestroyTexture(readableDepth);
        probe.device->DestroyTexture(full);
        probe.CheckValidationStayedQuiet("the subresource caps probe");
        probe.Close();
    }

    // **mip 하나를 읽어 다음 mip 에 그린다.** 블룸의 내림 사슬이 이 모양이다.
    // mip 0 은 빨강으로 칠하고, mip 1 은 mip 0 을 읽어 그리고(빨강), mip 2 는 초록으로 칠한다. 그 뒤 화면의 왼쪽 절반은 mip 1 을,
    // 오른쪽 절반은 mip 2 를 읽어 그린다. 검증 레이어가 조용해야 한다 - mip 을 하나로 보는 상태 추적이면 둘째 패스에서 어긋난다.
    template <typename TModule>
    void TestOneMipIsDrawnFromAnother()
    {
        Probe<TModule> probe;
        if (false == probe.Open("JBro mip probe"))
        {
            std::cout << "  [skip] no device for this API; mip drawing not verified" << std::endl;
            return;
        }
        const JBro::GraphicsApi api = probe.rhi.GetApi();
        if (false == HasProbeShaders(api))
        {
            std::cout << "  [skip] no SPIR-V probe shaders yet; mip drawing not verified" << std::endl;
            probe.Close();
            return;
        }

        static const JBro::TextureFormat colorFormat = JBro::TextureFormat::BGRA8Unorm;
        const JBro::GraphicsPipelineHandle tint =
            CreateProbePipeline(*probe.device, api, ProbePixel::Tint, &colorFormat, JBro::TextureFormat::Unknown);
        const JBro::GraphicsPipelineHandle sample =
            CreateProbePipeline(*probe.device, api, ProbePixel::Sample, &colorFormat, JBro::TextureFormat::Unknown);
        Check(tint.IsValid() && sample.IsValid(), "the probe pipelines must be created");

        JBro::TextureDesc chainDesc;
        chainDesc.extent = {SurfaceSize, SurfaceSize};
        chainDesc.mipLevels = 3;
        chainDesc.format = colorFormat;
        chainDesc.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
        const JBro::TextureHandle chain = probe.device->CreateTexture(chainDesc);
        JBro::TextureDesc targetDesc = chainDesc;
        targetDesc.mipLevels = 1;
        const JBro::TextureHandle target = probe.device->CreateTexture(targetDesc);
        Check(chain.IsValid() && target.IsValid(), "the mip chain and the read-back target must be created");

        const Float4 red{1.0f, 0.0f, 0.0f, 1.0f};
        const Float4 green{0.0f, 1.0f, 0.0f, 1.0f};
        const JBro::BeginFrameResult begun = probe.device->BeginFrame(probe.swapchain);
        Check(begun.status == JBro::FrameStatus::Ready, "the probe frame must begin");
        JBro::IRHICommandContext& commands = *begun.frame.commands;
        probe.ring.BeginFrame(begun.frame.slot);

        // 없는 mip 에는 패스를 열지 않는다.
        Check(false == BeginPass(commands, chain, 3, 8, nullptr), "a pass on a mip the texture lacks is refused");

        Check(BeginPass(commands, chain, 0, SurfaceSize, nullptr), "the mip 0 pass must begin");
        Check(commands.SetGraphicsPipeline(tint), "the tint pipeline must bind");
        probe.BindConstants(commands, Placement{}, red);
        Check(commands.DrawInstanced(3, 1, 0, 0), "mip 0 must be painted");
        commands.EndRenderPass();

        Check(BeginPass(commands, chain, 1, SurfaceSize / 2, nullptr), "the mip 1 pass must begin");
        Check(commands.SetGraphicsPipeline(sample), "the sample pipeline must bind");
        Check(false == commands.SetTextureMip(0, chain, 1), "the mip this pass draws cannot be read");
        Check(false == commands.SetTexture(0, chain), "the whole texture cannot be read while one of its mips is drawn");
        Check(false == commands.SetTextureMip(0, chain, 3), "a mip the texture lacks is refused");
        Check(commands.SetTextureMip(0, chain, 0), "mip 0 must be readable while mip 1 is drawn");
        Check(commands.SetSampler(0, probe.pointSampler), "the sampler must bind");
        probe.BindConstants(commands, Placement{}, Float4{});
        Check(commands.DrawInstanced(3, 1, 0, 0), "mip 1 must be drawn from mip 0");
        commands.EndRenderPass();

        Check(BeginPass(commands, chain, 2, SurfaceSize / 4, nullptr), "the mip 2 pass must begin");
        Check(commands.SetGraphicsPipeline(tint), "the tint pipeline must bind again");
        probe.BindConstants(commands, Placement{}, green);
        Check(commands.DrawInstanced(3, 1, 0, 0), "mip 2 must be painted");
        commands.EndRenderPass();

        Check(BeginPass(commands, target, 0, SurfaceSize, nullptr), "the read-back pass must begin");
        Check(commands.SetGraphicsPipeline(sample), "the sample pipeline must bind for the read-back");
        Check(commands.SetSampler(0, probe.pointSampler), "the sampler must bind for the read-back");
        Check(commands.SetTextureMip(0, chain, 1), "mip 1 must bind");
        probe.BindConstants(commands, Placement{LeftHalf, {}}, Float4{});
        Check(commands.DrawInstanced(3, 1, 0, 0), "the left half must draw mip 1");
        Check(commands.SetTextureMip(0, chain, 2), "mip 2 must bind");
        probe.BindConstants(commands, Placement{RightHalf, {}}, Float4{});
        Check(commands.DrawInstanced(3, 1, 0, 0), "the right half must draw mip 2");
        commands.EndRenderPass();
        Check(probe.device->EndFrame(begun.frame) == JBro::FrameStatus::Ready, "the probe frame must end");

        JBro::Array<std::byte> image;
        image.Resize(SurfaceSize * SurfaceSize * 4);
        JBro::TextureReadback readback;
        Check(probe.device->ReadTexture(target, image.Data(), image.Size(), readback), "the target must read back");
        Check(IsColor(ReadBgra(image, readback.rowPitch, 16, 32), red), "mip 1 must hold what was read from mip 0");
        Check(IsColor(ReadBgra(image, readback.rowPitch, 48, 32), green), "mip 2 must hold its own paint");

        probe.CheckValidationStayedQuiet("the mip probe");
        probe.device->DestroyTexture(target);
        probe.device->DestroyTexture(chain);
        probe.device->DestroyGraphicsPipeline(sample);
        probe.device->DestroyGraphicsPipeline(tint);
        probe.Close();
    }

    // **그린 깊이를 다음 패스가 읽는다.** 왼쪽 절반에만 깊이 0.25 를 쓰고(오른쪽은 지운 값 1.0 그대로), 다음 패스가 그 깊이를
    // R32Float 타깃에 옮겨 되읽는다. 깊이를 그리는 패스 안에서는 그 깊이를 읽지 못한다.
    template <typename TModule>
    void TestDrawnDepthIsReadByTheNextPass()
    {
        Probe<TModule> probe;
        if (false == probe.Open("JBro depth probe"))
        {
            std::cout << "  [skip] no device for this API; depth reads not verified" << std::endl;
            return;
        }
        const JBro::GraphicsApi api = probe.rhi.GetApi();
        if (false == HasProbeShaders(api))
        {
            std::cout << "  [skip] no SPIR-V probe shaders yet; depth reads not verified" << std::endl;
            probe.Close();
            return;
        }

        static const JBro::TextureFormat colorFormat = JBro::TextureFormat::BGRA8Unorm;
        static const JBro::TextureFormat floatFormat = JBro::TextureFormat::R32Float;
        const JBro::GraphicsPipelineHandle depthWrite =
            CreateProbePipeline(*probe.device, api, ProbePixel::Tint, &colorFormat, JBro::TextureFormat::D32Float);
        const JBro::GraphicsPipelineHandle depthRead =
            CreateProbePipeline(*probe.device, api, ProbePixel::Depth, &floatFormat, JBro::TextureFormat::Unknown);
        // 같은 깊이 읽기 셰이더를 깊이 첨부가 있는 패스용으로도 만든다(포맷이 파이프라인에 굳는다).
        const JBro::GraphicsPipelineHandle depthReadOverDepth =
            CreateProbePipeline(*probe.device, api, ProbePixel::Depth, &floatFormat, JBro::TextureFormat::D32Float);
        Check(depthWrite.IsValid() && depthRead.IsValid() && depthReadOverDepth.IsValid(),
            "the depth probe pipelines must be created");

        JBro::TextureDesc colorDesc;
        colorDesc.extent = {SurfaceSize, SurfaceSize};
        colorDesc.format = colorFormat;
        colorDesc.usage = JBro::TextureUsage::RenderTarget;
        const JBro::TextureHandle color = probe.device->CreateTexture(colorDesc);
        JBro::TextureDesc depthDesc;
        depthDesc.extent = {SurfaceSize, SurfaceSize};
        depthDesc.format = JBro::TextureFormat::D32Float;
        depthDesc.usage = JBro::TextureUsage::DepthStencil | JBro::TextureUsage::Sampled;
        const JBro::TextureHandle depth = probe.device->CreateTexture(depthDesc);
        JBro::TextureDesc floatDesc;
        floatDesc.extent = {SurfaceSize, SurfaceSize};
        floatDesc.format = floatFormat;
        floatDesc.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
        const JBro::TextureHandle readout = probe.device->CreateTexture(floatDesc);
        Check(color.IsValid() && depth.IsValid() && readout.IsValid(), "the depth probe textures must be created");

        const JBro::BeginFrameResult begun = probe.device->BeginFrame(probe.swapchain);
        Check(begun.status == JBro::FrameStatus::Ready, "the probe frame must begin");
        JBro::IRHICommandContext& commands = *begun.frame.commands;
        probe.ring.BeginFrame(begun.frame.slot);

        JBro::DepthStencilAttachmentDesc depthAttachment;
        depthAttachment.texture = depth;
        depthAttachment.depthLoadOperation = JBro::LoadOperation::Clear;
        depthAttachment.clearDepth = 1.0f;
        Check(BeginPass(commands, color, 0, SurfaceSize, &depthAttachment), "the depth pass must begin");
        Check(commands.SetGraphicsPipeline(depthWrite), "the depth writing pipeline must bind");
        probe.BindConstants(commands, Placement{LeftHalf, {0.25f, 0.0f, 0.0f, 0.0f}}, Float4{1.0f, 1.0f, 1.0f, 1.0f});
        Check(commands.DrawInstanced(3, 1, 0, 0), "the left half must write depth");
        commands.EndRenderPass();

        // 깊이를 그리는 패스 안에서 그 깊이를 읽으려 하면 거절한다. 텍스처 자리가 있는 파이프라인이어야 거절의 까닭이 자리 없음이
        // 아니라 겹침이다. 깊이는 지우지 않고 이어 받는다 - 위에서 쓴 값을 아래 패스가 읽는다.
        JBro::DepthStencilAttachmentDesc keepDepth;
        keepDepth.texture = depth;
        keepDepth.depthLoadOperation = JBro::LoadOperation::Load;
        Check(BeginPass(commands, readout, 0, SurfaceSize, &keepDepth), "a pass drawing that depth must begin");
        Check(commands.SetGraphicsPipeline(depthReadOverDepth), "the depth reading pipeline must bind over depth");
        Check(false == commands.SetTexture(0, depth), "the depth this pass draws cannot be read in the same pass");
        commands.EndRenderPass();

        Check(BeginPass(commands, readout, 0, SurfaceSize, nullptr), "the depth read pass must begin");
        Check(commands.SetGraphicsPipeline(depthRead), "the depth reading pipeline must bind");
        Check(commands.SetTexture(0, depth), "the drawn depth must bind as a texture");
        probe.BindConstants(commands, Placement{}, Float4{});
        Check(commands.DrawInstanced(3, 1, 0, 0), "the depth must be copied out");
        commands.EndRenderPass();
        Check(probe.device->EndFrame(begun.frame) == JBro::FrameStatus::Ready, "the probe frame must end");

        JBro::Array<std::byte> image;
        image.Resize(SurfaceSize * SurfaceSize * 4);
        JBro::TextureReadback readback;
        Check(probe.device->ReadTexture(readout, image.Data(), image.Size(), readback), "the depth readout must read back");
        JBro::Float left = 0.0f;
        JBro::Float right = 0.0f;
        std::memcpy(&left, image.Data() + static_cast<std::size_t>(32) * readback.rowPitch + 16 * 4, 4);
        std::memcpy(&right, image.Data() + static_cast<std::size_t>(32) * readback.rowPitch + 48 * 4, 4);
        if (left != 0.25f || right != 1.0f)
        {
            std::cout << "  depth read " << left << " and " << right << std::endl;
        }
        Check(left == 0.25f, "the left half must read the depth that was drawn");
        Check(right == 1.0f, "the right half must read the depth the pass cleared to");

        probe.CheckValidationStayedQuiet("the depth probe");
        probe.device->DestroyTexture(readout);
        probe.device->DestroyTexture(depth);
        probe.device->DestroyTexture(color);
        probe.device->DestroyGraphicsPipeline(depthReadOverDepth);
        probe.device->DestroyGraphicsPipeline(depthRead);
        probe.device->DestroyGraphicsPipeline(depthWrite);
        probe.Close();
    }

    // 깊이 넷인 2×2×4 볼륨이다. 조각마다 한 색이다: 빨강·초록·파랑·하양.
    constexpr unsigned char VolumeTexels[] = {
        255, 0, 0, 255,      255, 0, 0, 255,      255, 0, 0, 255,      255, 0, 0, 255,
        0, 255, 0, 255,      0, 255, 0, 255,      0, 255, 0, 255,      0, 255, 0, 255,
        0, 0, 255, 255,      0, 0, 255, 255,      0, 0, 255, 255,      0, 0, 255, 255,
        255, 255, 255, 255,  255, 255, 255, 255,  255, 255, 255, 255,  255, 255, 255, 255,
    };

    // **3D 텍스처를 만들고 올리고, 셰이더가 깊이로 골라 읽는다.** 컬러 그레이딩 LUT 의 모양이다.
    // 만들기·올리기·거절은 셰이더가 없어도 되므로 세 백엔드에서 돈다. 그리기는 사분면마다 w 를 달리한다:
    //   왼쪽 위 w = 0.125 → 조각 0(빨강), 오른쪽 위 w = 0.625 → 조각 2(파랑),
    //   왼쪽 아래 w = 1.375 를 Repeat 로 → 0.375 → 조각 1(초록), 오른쪽 아래 같은 w 를 Clamp 로 → 마지막 조각(하양).
    // 아래 둘이 샘플러의 W 축을 잰다 - W 를 늘 Clamp 로 두던 예전 모양이면 왼쪽 아래도 하양이다.
    template <typename TModule>
    void TestAVolumeIsReadByDepth()
    {
        Probe<TModule> probe;
        if (false == probe.Open("JBro volume probe"))
        {
            std::cout << "  [skip] no device for this API; 3D textures not verified" << std::endl;
            return;
        }
        const JBro::GraphicsApi api = probe.rhi.GetApi();
        Check(probe.device->GetCaps().maxTexture3DSize >= 256, "a 3D texture may be at least 256 on a side");

        JBro::TextureDesc volumeDesc;
        volumeDesc.dimension = JBro::TextureDimension::Texture3D;
        volumeDesc.extent = {2, 2};
        volumeDesc.depthOrLayers = 4;
        volumeDesc.format = JBro::TextureFormat::RGBA8Unorm;
        volumeDesc.usage = JBro::TextureUsage::Sampled | JBro::TextureUsage::CopyDestination;
        const JBro::TextureHandle volume = probe.device->CreateTexture(volumeDesc);
        Check(volume.IsValid(), "a 3D texture must be created");
        const JBro::JArrayView<std::byte> texels{
            reinterpret_cast<const std::byte*>(VolumeTexels), static_cast<JBro::UInt32>(sizeof(VolumeTexels))};
        Check(false == probe.device->WriteTexture(volume, 0, {texels.data, static_cast<JBro::UInt32>(texels.size - 16)}),
            "a 3D upload short of one slice is refused");
        Check(probe.device->WriteTexture(volume, 0, texels), "every slice of the volume must upload at once");
        Check(false == probe.device->WriteTextureRegion(volume, 0, 0, 0, 1, 1, texels, 8),
            "a 3D texture takes no region upload");
        JBro::Array<std::byte> readback;
        readback.Resize(sizeof(VolumeTexels));
        JBro::TextureReadback readResult;
        Check(false == probe.device->ReadTexture(volume, readback.Data(), readback.Size(), readResult),
            "a 3D texture is not read back");

        // 3D 는 읽기 전용, mip 하나, 상한 안이다.
        JBro::TextureDesc drawnVolume = volumeDesc;
        drawnVolume.usage = JBro::TextureUsage::Sampled | JBro::TextureUsage::RenderTarget;
        Check(false == probe.device->CreateTexture(drawnVolume).IsValid(), "a 3D texture cannot be drawn into");
        JBro::TextureDesc mippedVolume = volumeDesc;
        mippedVolume.mipLevels = 2;
        Check(false == probe.device->CreateTexture(mippedVolume).IsValid(), "a 3D texture has one mip");
        JBro::TextureDesc deepVolume = volumeDesc;
        deepVolume.depthOrLayers = probe.device->GetCaps().maxTexture3DSize + 1;
        Check(false == probe.device->CreateTexture(deepVolume).IsValid(), "a 3D texture deeper than the caps is refused");
        JBro::TextureDesc layered;
        layered.extent = {2, 2};
        layered.depthOrLayers = 2;
        layered.format = JBro::TextureFormat::RGBA8Unorm;
        layered.usage = JBro::TextureUsage::Sampled;
        Check(false == probe.device->CreateTexture(layered).IsValid(), "a 2D texture array is not taken yet");

        if (false == HasProbeShaders(api))
        {
            std::cout << "  [skip] no SPIR-V probe shaders yet; 3D sampling not verified" << std::endl;
            probe.CheckValidationStayedQuiet("the volume probe");
            probe.device->DestroyTexture(volume);
            probe.Close();
            return;
        }

        static const JBro::TextureFormat colorFormat = JBro::TextureFormat::BGRA8Unorm;
        const JBro::GraphicsPipelineHandle pipeline =
            CreateProbePipeline(*probe.device, api, ProbePixel::Volume, &colorFormat, JBro::TextureFormat::Unknown);
        Check(pipeline.IsValid(), "the volume pipeline must be created");
        JBro::TextureDesc targetDesc;
        targetDesc.extent = {SurfaceSize, SurfaceSize};
        targetDesc.format = colorFormat;
        targetDesc.usage = JBro::TextureUsage::RenderTarget;
        const JBro::TextureHandle target = probe.device->CreateTexture(targetDesc);
        JBro::SamplerDesc repeatDesc;
        repeatDesc.minFilter = JBro::FilterMode::Nearest;
        repeatDesc.magFilter = JBro::FilterMode::Nearest;
        repeatDesc.addressW = JBro::AddressMode::Repeat;
        const JBro::SamplerHandle repeat = probe.device->CreateSampler(repeatDesc);
        Check(target.IsValid() && repeat.IsValid(), "the volume target and the repeating sampler must be created");

        const JBro::BeginFrameResult begun = probe.device->BeginFrame(probe.swapchain);
        Check(begun.status == JBro::FrameStatus::Ready, "the probe frame must begin");
        JBro::IRHICommandContext& commands = *begun.frame.commands;
        probe.ring.BeginFrame(begun.frame.slot);
        Check(BeginPass(commands, target, 0, SurfaceSize, nullptr), "the volume pass must begin");
        Check(commands.SetGraphicsPipeline(pipeline), "the volume pipeline must bind");
        Check(commands.SetTexture(0, volume), "the volume must bind");
        Check(commands.SetTextureMip(0, volume, 0), "a 3D texture's only mip binds like the whole texture");
        struct Quarter
        {
            Float4 placement;
            JBro::Float w = 0.0f;
            JBro::SamplerHandle sampler;
        };
        const Quarter quarters[] = {
            {{0.5f, 0.5f, -0.5f, 0.5f}, 0.125f, probe.pointSampler},
            {{0.5f, 0.5f, 0.5f, 0.5f}, 0.625f, probe.pointSampler},
            {{0.5f, 0.5f, -0.5f, -0.5f}, 1.375f, repeat},
            {{0.5f, 0.5f, 0.5f, -0.5f}, 1.375f, probe.pointSampler},
        };
        for (const Quarter& quarter : quarters)
        {
            Check(commands.SetSampler(0, quarter.sampler), "the quarter's sampler must bind");
            probe.BindConstants(commands, Placement{quarter.placement, {}}, Float4{quarter.w, 0.0f, 0.0f, 0.0f});
            Check(commands.DrawInstanced(3, 1, 0, 0), "each quarter must draw");
        }
        commands.EndRenderPass();
        Check(probe.device->EndFrame(begun.frame) == JBro::FrameStatus::Ready, "the probe frame must end");

        JBro::Array<std::byte> image;
        image.Resize(SurfaceSize * SurfaceSize * 4);
        JBro::TextureReadback result;
        Check(probe.device->ReadTexture(target, image.Data(), image.Size(), result), "the volume target must read back");
        Check(IsColor(ReadBgra(image, result.rowPitch, 16, 16), {1.0f, 0.0f, 0.0f, 1.0f}), "w 0.125 must read slice 0");
        Check(IsColor(ReadBgra(image, result.rowPitch, 48, 16), {0.0f, 0.0f, 1.0f, 1.0f}), "w 0.625 must read slice 2");
        Check(IsColor(ReadBgra(image, result.rowPitch, 16, 48), {0.0f, 1.0f, 0.0f, 1.0f}),
            "a repeating W must wrap 1.375 to slice 1");
        Check(IsColor(ReadBgra(image, result.rowPitch, 48, 48), {1.0f, 1.0f, 1.0f, 1.0f}),
            "a clamping W must hold 1.375 at the last slice");

        probe.CheckValidationStayedQuiet("the volume probe");
        probe.device->DestroySampler(repeat);
        probe.device->DestroyTexture(target);
        probe.device->DestroyGraphicsPipeline(pipeline);
        probe.device->DestroyTexture(volume);
        probe.Close();
    }

    template <typename TModule>
    void RunSubresourceTestsOn()
    {
        TestTexturesTakeAMipChainAndReadableDepth<TModule>();
        TestOneMipIsDrawnFromAnother<TModule>();
        TestDrawnDepthIsReadByTheNextPass<TModule>();
        TestAVolumeIsReadByDepth<TModule>();
    }
}

JBro::Int32 RunSubresourceTests()
{
    RunSubresourceTestsOn<JBro::D3D12RHIModule>();
    RunSubresourceTestsOn<JBro::D3D11RHIModule>();
    RunSubresourceTestsOn<JBro::VulkanRHIModule>();
    std::cout << "Subresource tests passed.\n";
    return 0;
}
