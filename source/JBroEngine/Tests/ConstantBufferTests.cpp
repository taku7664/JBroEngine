#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/RHI/ConstantRing.h>
#include <JBro/Types/Array.h>
#include <JBro/VulkanRHI/VulkanRHI.h>

#include "ConstantProbePS.generated.h"
#include "ConstantProbeVS.generated.h"

// fxc 의 헤더는 `BYTE` 로 적는다. Windows.h 를 끌어들이지 않고 이름만 준다(Renderer.cpp 와 같다).
namespace Sm5
{
    using BYTE = unsigned char;
#include "ConstantProbePS_SM5.generated.h"
#include "ConstantProbeVS_SM5.generated.h"
}

#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 후처리 계획 1 단계(postprocess-plan §3)의 RHI 를 잰다 - 바인딩 레이아웃·상수 버퍼·업로드 링·인덱스 없는 그리기·새 포맷.
//
// D3D12 와 D3D11 은 픽셀까지 잰다. **Vulkan 은 셰이더가 필요 없는 것만 잰다** - 이 기계에는 SPIR-V 를 굽는 Vulkan SDK 의
// dxc 가 없어 `ConstantProbe*_SPV.generated.h` 가 아직 없다(`Tests/Shaders/Compile.ps1 -SkipSpirv`, 2026-10-14). 굽고 나면
// 그리는 시험도 Vulkan 에서 돈다.
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

    JBro::Bool Near(JBro::Float a, JBro::Float b, JBro::Float tolerance)
    {
        return std::fabs(a - b) <= tolerance;
    }

    // 셰이더가 있는 백엔드다. Vulkan 은 SPIR-V 를 구운 뒤에 여기에 든다.
    JBro::Bool HasProbeShaders(JBro::GraphicsApi api)
    {
        return api == JBro::GraphicsApi::D3D12 || api == JBro::GraphicsApi::D3D11;
    }

    // 정점 버퍼가 없는 탐침 파이프라인이다. b1 은 정점 단계, b2 는 픽셀 단계가 읽지만 자리표는 둘 다에 연다 - 한 종류는
    // 단계 하나로 말하는 계약이라(`BindingRange`), 둘을 다 읽는 셰이더가 이 모양이다.
    JBro::GraphicsPipelineDesc MakeProbePipelineDesc(JBro::GraphicsApi api, const JBro::TextureFormat* colorFormat)
    {
        JBro::GraphicsPipelineDesc desc;
        if (api == JBro::GraphicsApi::D3D11)
        {
            desc.vertexShader = {Sm5::JBroTestConstantProbeVS_SM5, static_cast<JBro::UInt32>(sizeof(Sm5::JBroTestConstantProbeVS_SM5))};
            desc.pixelShader = {Sm5::JBroTestConstantProbePS_SM5, static_cast<JBro::UInt32>(sizeof(Sm5::JBroTestConstantProbePS_SM5))};
        }
        else
        {
            desc.vertexShader = {JBroTestConstantProbeVS, static_cast<JBro::UInt32>(sizeof(JBroTestConstantProbeVS))};
            desc.pixelShader = {JBroTestConstantProbePS, static_cast<JBro::UInt32>(sizeof(JBroTestConstantProbePS))};
        }
        desc.colorFormats = {colorFormat, 1};
        desc.cull = JBro::CullMode::None;
        desc.bindings.constantBuffers.count = 2;
        desc.bindings.constantBuffers.stages = JBro::ShaderStage::Vertex | JBro::ShaderStage::Pixel;
        return desc;
    }

    template <typename TModule>
    struct Probe
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::IRHIDevice* device = nullptr;
        JBro::WindowHandle window;
        JBro::SwapchainHandle swapchain;
        JBro::Bool platformOpen = false;
        JBro::Bool rhiOpen = false;

        // 이 기계에 그 API 가 없으면 거짓이다. 그 경우 시험은 건너뛴다.
        JBro::Bool Open(const char* title);
        void CheckValidationStayedQuiet(const char* what) const;
        void Close();
    };

    template <typename TModule>
    JBro::Bool Probe<TModule>::Open(const char* title)
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
        return true;
    }

    template <typename TModule>
    void Probe<TModule>::CheckValidationStayedQuiet(const char* what) const
    {
        const JBro::UInt32 errors = device->GetValidationErrorCount();
        if (errors != 0)
        {
            std::cout << "  the debug layer reported " << errors << " error(s) during " << what << std::endl;
        }
        Check(errors == 0, "the debug layer must stay quiet");
    }

    template <typename TModule>
    void Probe<TModule>::Close()
    {
        if (device != nullptr)
        {
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

    // 렌더 타깃 하나에 패스를 연다. 뷰포트·시저는 타깃 전체다.
    JBro::Bool BeginTargetPass(JBro::IRHICommandContext& commands, JBro::TextureHandle target, JBro::ClearColor clear)
    {
        JBro::ColorAttachmentDesc attachment;
        attachment.texture = target;
        attachment.loadOperation = JBro::LoadOperation::Clear;
        attachment.clearColor = clear;
        JBro::RenderPassDesc pass;
        pass.colorAttachments = {&attachment, 1};
        if (false == commands.BeginRenderPass(pass))
        {
            return false;
        }
        JBro::Viewport viewport;
        viewport.width = static_cast<JBro::Float>(SurfaceSize);
        viewport.height = static_cast<JBro::Float>(SurfaceSize);
        commands.SetViewport(viewport);
        commands.SetScissor({0, 0, static_cast<JBro::Int32>(SurfaceSize), static_cast<JBro::Int32>(SurfaceSize)});
        return true;
    }

    JBro::TextureHandle CreateTarget(JBro::IRHIDevice& device, JBro::TextureFormat format)
    {
        JBro::TextureDesc desc;
        desc.extent = {SurfaceSize, SurfaceSize};
        desc.format = format;
        desc.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
        return device.CreateTexture(desc);
    }

    // BGRA8 렌더 타깃의 한 픽셀이다.
    Float4 ReadBgra(const JBro::Array<std::byte>& image, JBro::UInt32 rowPitch, JBro::UInt32 x, JBro::UInt32 y)
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(image.Data() + static_cast<std::size_t>(y) * rowPitch + x * 4);
        return {bytes[2] / 255.0f, bytes[1] / 255.0f, bytes[0] / 255.0f, bytes[3] / 255.0f};
    }

    JBro::Bool NearColor(const Float4& actual, const Float4& expected)
    {
        return Near(actual.x, expected.x, 0.02f) && Near(actual.y, expected.y, 0.02f) && Near(actual.z, expected.z, 0.02f);
    }

    // **상한이 계약대로인지 본다.** 세 백엔드가 같은 바닥을 말해야 위 계층이 백엔드를 묻지 않고 자리표를 짠다.
    template <typename TModule>
    void TestTheCapsAreTheCommonFloor()
    {
        Probe<TModule> probe;
        if (false == probe.Open("JBro caps probe"))
        {
            std::cout << "  [skip] no device for this API; caps not verified" << std::endl;
            return;
        }
        const JBro::DeviceCaps caps = probe.device->GetCaps();
        Check(caps.maxConstantBuffers == 7, "seven constant buffer slots, b1..b7");
        Check(caps.maxSampledTextures == 8 && caps.maxSamplers == 4, "the texture and sampler slots stay as they were");
        Check(caps.maxPushConstantBytes == JBro::MaxPushConstantBytes && caps.maxPushConstantBytes == 128,
            "push constants are capped at 128 bytes on every backend");
        Check(caps.constantBufferAlignment == 256, "constant ranges are cut in 256 byte steps");
        Check(caps.maxConstantBufferBytes >= 16384 && caps.maxConstantBufferBytes % caps.constantBufferAlignment == 0,
            "a bound range may be at least 16 KiB and stays aligned");

        // 자리표 검사는 공용 함수 하나다. 넘는 수, 단계 없는 종류, 모르는 단계를 거절한다.
        JBro::BindingLayoutDesc layout;
        layout.constantBuffers = {7, JBro::ShaderStage::Pixel};
        Check(JBro::IsBindingLayoutValid(layout, caps), "a layout at the cap is valid");
        layout.constantBuffers.count = 8;
        Check(false == JBro::IsBindingLayoutValid(layout, caps), "a layout over the cap is refused");
        layout.constantBuffers = {1, static_cast<JBro::ShaderStage>(0)};
        Check(false == JBro::IsBindingLayoutValid(layout, caps), "a used range must name a stage");
        layout.constantBuffers = {0, static_cast<JBro::ShaderStage>(0)};
        Check(JBro::IsBindingLayoutValid(layout, caps), "an empty range needs no stage");
        layout.samplers = {1, static_cast<JBro::ShaderStage>(0x80)};
        Check(false == JBro::IsBindingLayoutValid(layout, caps), "an unknown stage is refused");
        probe.Close();
    }

    // **링이 정렬해서 쌓고, 차면 거절하고, 프레임마다 처음부터 쓴다.** 그리지 않으므로 셰이더 없는 Vulkan 에서도 돈다.
    template <typename TModule>
    void TestTheRingCutsAlignedRanges()
    {
        Probe<TModule> probe;
        if (false == probe.Open("JBro ring probe"))
        {
            std::cout << "  [skip] no device for this API; the constant ring not verified" << std::endl;
            return;
        }
        const JBro::DeviceCaps caps = probe.device->GetCaps();
        const std::size_t alignment = caps.constantBufferAlignment;

        JBro::ConstantRing ring;
        Check(ring.Create(*probe.device, alignment * 2), "a ring of two ranges must be created");
        Check(false == ring.Create(*probe.device, alignment), "a ring is created once");
        Check(ring.GetCapacity() == alignment * 2, "the capacity is the asked size, aligned");

        const Float4 value{1.0f, 2.0f, 3.0f, 4.0f};
        Check(false == ring.Push(value).IsValid(), "nothing is pushed before the frame opens its slot");

        ring.BeginFrame(0);
        const JBro::ConstantRange first = ring.Push(value);
        const JBro::ConstantRange second = ring.Push(value);
        const JBro::ConstantRange third = ring.Push(value);
        Check(first.IsValid() && first.offset == 0 && first.size == alignment, "the first range starts the slot");
        Check(second.IsValid() && second.offset == alignment && second.buffer == first.buffer,
            "the next range sits one alignment step further in the same buffer");
        Check(false == third.IsValid(), "a full ring refuses rather than wrapping onto ranges a draw still reads");
        Check(ring.GetUsedBytes() == alignment * 2, "the used bytes count the aligned tails");

        // 다시 연 슬롯은 처음부터다. 슬롯이 여럿인 백엔드는 다른 버퍼를 쓴다.
        ring.BeginFrame(1);
        const JBro::ConstantRange nextFrame = ring.Push(value);
        Check(nextFrame.IsValid() && nextFrame.offset == 0, "a new frame starts its slot from the beginning");
        if (probe.device->GetFramesInFlight() > 1)
        {
            Check(nextFrame.buffer != first.buffer, "each frame slot has its own buffer");
        }

        // 한 번에 묶을 수 있는 크기를 넘는 값은 받지 않는다.
        JBro::Array<std::byte> huge;
        huge.Resize(static_cast<std::size_t>(caps.maxConstantBufferBytes) + 4);
        Check(false == ring.Push({huge.Data(), static_cast<JBro::UInt32>(huge.Size())}).IsValid(),
            "a value larger than one bindable range is refused");

        ring.Destroy();
        Check(false == ring.IsCreated(), "a destroyed ring is empty");
        probe.CheckValidationStayedQuiet("the ring probe");
        probe.Close();
    }

    // **상수 버퍼가 두 단계에 닿고, 링의 구간마다 따로 읽힌다. 정점 버퍼 없이 그린다.**
    // 그림 하나: 첫 드로우는 화면을 빨강으로 덮고, 둘째 드로우는 정점 단계의 b1 으로 왼쪽 위 사분면에만 초록을 칠한다.
    // 둘째 드로우의 값이 첫 드로우의 자리를 덮었다면(링이 이미 쓴 구간을 다시 썼다면) 화면 전체가 초록이 된다.
    template <typename TModule>
    void TestConstantsReachBothStagesPerRange()
    {
        Probe<TModule> probe;
        if (false == probe.Open("JBro constant probe"))
        {
            std::cout << "  [skip] no device for this API; constant buffers not verified" << std::endl;
            return;
        }
        if (false == HasProbeShaders(probe.rhi.GetApi()))
        {
            std::cout << "  [skip] no SPIR-V probe shaders yet; constant buffer drawing not verified" << std::endl;
            probe.Close();
            return;
        }

        static const JBro::TextureFormat colorFormat = JBro::TextureFormat::BGRA8Unorm;
        const JBro::GraphicsPipelineHandle pipeline =
            probe.device->CreateGraphicsPipeline(MakeProbePipelineDesc(probe.rhi.GetApi(), &colorFormat));
        Check(pipeline.IsValid(), "a pipeline with no vertex buffer and two constant buffers must be created");

        // 푸시 상수 상한은 128 B 다. 셰이더가 쓰지 않아도 자리표에 적은 크기로 거르는지 본다.
        JBro::GraphicsPipelineDesc pushAtCap = MakeProbePipelineDesc(probe.rhi.GetApi(), &colorFormat);
        pushAtCap.pushConstantBytes = JBro::MaxPushConstantBytes;
        JBro::GraphicsPipelineDesc pushOverCap = pushAtCap;
        pushOverCap.pushConstantBytes = JBro::MaxPushConstantBytes + 4;
        const JBro::GraphicsPipelineHandle atCap = probe.device->CreateGraphicsPipeline(pushAtCap);
        Check(atCap.IsValid(), "128 bytes of push constants are accepted");
        Check(false == probe.device->CreateGraphicsPipeline(pushOverCap).IsValid(),
            "more than 128 bytes of push constants are refused");
        JBro::GraphicsPipelineDesc tooMany = MakeProbePipelineDesc(probe.rhi.GetApi(), &colorFormat);
        tooMany.bindings.constantBuffers.count = 8;
        Check(false == probe.device->CreateGraphicsPipeline(tooMany).IsValid(),
            "a pipeline asking for more constant buffers than the device binds is refused");

        const JBro::TextureHandle target = CreateTarget(*probe.device, colorFormat);
        Check(target.IsValid(), "the probe target must be created");

        JBro::BufferDesc vertexDesc;
        vertexDesc.size = 1024;
        vertexDesc.usage = JBro::BufferUsage::Vertex;
        vertexDesc.memory = JBro::MemoryType::Upload;
        const JBro::BufferHandle notConstant = probe.device->CreateBuffer(vertexDesc);
        Check(notConstant.IsValid(), "a vertex buffer must be created");

        JBro::ConstantRing ring;
        Check(ring.Create(*probe.device, 4096), "the constant ring must be created");

        const JBro::BeginFrameResult begun = probe.device->BeginFrame(probe.swapchain);
        Check(begun.status == JBro::FrameStatus::Ready, "the probe frame must begin");
        JBro::IRHICommandContext& commands = *begun.frame.commands;
        ring.BeginFrame(begun.frame.slot);

        const JBro::ConstantRange fullPlacement = ring.Push(Float4{1.0f, 1.0f, 0.0f, 0.0f});
        const JBro::ConstantRange red = ring.Push(Float4{1.0f, 0.0f, 0.0f, 1.0f});
        // 화면을 덮는 삼각형을 반으로 줄여 왼쪽 위로 옮긴다: x ∈ [-1, 0], y ∈ [0, 1](NDC 의 위가 화면의 위다).
        const JBro::ConstantRange quarterPlacement = ring.Push(Float4{0.5f, 0.5f, -0.5f, 0.5f});
        const JBro::ConstantRange green = ring.Push(Float4{0.0f, 1.0f, 0.0f, 1.0f});
        Check(fullPlacement.IsValid() && red.IsValid() && quarterPlacement.IsValid() && green.IsValid(),
            "four ranges must fit in the ring");

        Check(BeginTargetPass(commands, target, {0.0f, 0.0f, 0.0f, 1.0f}), "the probe pass must begin");
        Check(commands.SetGraphicsPipeline(pipeline), "the pipeline must bind");

        // 그리기 전에 거절해야 할 것들. 모두 구간 하나만 바꿔 다른 이유로 실패하지 않게 한다.
        Check(false == commands.DrawInstanced(3, 1, 0, 0), "a draw with declared constant buffers unbound is refused");
        Check(false == commands.SetConstantBuffer(0, red.buffer, red.offset, red.size),
            "b0 is the push constant register and cannot take a constant buffer");
        Check(false == commands.SetConstantBuffer(3, red.buffer, red.offset, red.size),
            "a slot the pipeline never declared is refused");
        Check(false == commands.SetConstantBuffer(2, red.buffer, red.offset + 16, red.size),
            "an offset off the alignment is refused");
        Check(false == commands.SetConstantBuffer(2, red.buffer, red.offset, 16),
            "a size off the alignment is refused");
        Check(false == commands.SetConstantBuffer(2, red.buffer, ring.GetCapacity(), red.size),
            "a range past the end of the buffer is refused");
        Check(false == commands.SetConstantBuffer(2, notConstant, 0, red.size),
            "a buffer not made for constants is refused");

        Check(commands.SetConstantBuffer(1, fullPlacement.buffer, fullPlacement.offset, fullPlacement.size),
            "the vertex stage range must bind");
        Check(false == commands.DrawInstanced(3, 1, 0, 0), "a draw with one of two declared slots bound is refused");
        Check(commands.SetConstantBuffer(2, red.buffer, red.offset, red.size), "the pixel stage range must bind");
        Check(false == commands.DrawInstanced(0, 1, 0, 0), "an empty draw is refused");
        Check(commands.DrawInstanced(3, 1, 0, 0), "the full screen triangle must draw");

        // 파이프라인을 다시 걸면 묶은 것은 버려진다 - 텍스처와 같은 계약이다.
        Check(commands.SetGraphicsPipeline(pipeline), "the pipeline must bind again");
        Check(false == commands.DrawInstanced(3, 1, 0, 0), "setting a pipeline drops the bound constant buffers");
        Check(commands.SetConstantBuffer(1, quarterPlacement.buffer, quarterPlacement.offset, quarterPlacement.size),
            "the second placement must bind");
        Check(commands.SetConstantBuffer(2, green.buffer, green.offset, green.size), "the second tint must bind");
        Check(commands.DrawInstanced(3, 1, 0, 0), "the quarter triangle must draw");
        commands.EndRenderPass();
        Check(probe.device->EndFrame(begun.frame) == JBro::FrameStatus::Ready, "the probe frame must end");

        JBro::Array<std::byte> image;
        image.Resize(SurfaceSize * SurfaceSize * 4);
        JBro::TextureReadback readback;
        Check(probe.device->ReadTexture(target, image.Data(), image.Size(), readback), "the target must read back");
        const Float4 redColor{1.0f, 0.0f, 0.0f, 1.0f};
        const Float4 greenColor{0.0f, 1.0f, 0.0f, 1.0f};
        Check(NearColor(ReadBgra(image, readback.rowPitch, 16, 16), greenColor),
            "the second draw's ranges must paint the top left quarter green");
        Check(NearColor(ReadBgra(image, readback.rowPitch, 48, 16), redColor),
            "the first draw must keep its own tint in the top right");
        Check(NearColor(ReadBgra(image, readback.rowPitch, 16, 48), redColor),
            "the first draw must keep its own tint in the bottom left");
        Check(NearColor(ReadBgra(image, readback.rowPitch, 48, 48), redColor),
            "the full screen triangle must cover the bottom right corner");

        probe.CheckValidationStayedQuiet("the constant probe");
        ring.Destroy();
        probe.device->DestroyBuffer(notConstant);
        probe.device->DestroyTexture(target);
        probe.device->DestroyGraphicsPipeline(atCap);
        probe.device->DestroyGraphicsPipeline(pipeline);
        probe.Close();
    }

    // **링이 프레임을 넘어 돈다.** 프레임마다 다른 색을 같은 링에 올려 그린다. 슬롯이 하나뿐인 D3D11 은 오프셋 0 쓰기에서
    // 버퍼를 갈아 끼우고(DISCARD), 슬롯이 여럿인 백엔드는 슬롯의 지난 프레임이 끝난 버퍼를 다시 쓴다. 마지막 색이 남아야 한다.
    template <typename TModule>
    void TestTheRingTurnsOverAcrossFrames()
    {
        Probe<TModule> probe;
        if (false == probe.Open("JBro ring frame probe"))
        {
            std::cout << "  [skip] no device for this API; the ring across frames not verified" << std::endl;
            return;
        }
        if (false == HasProbeShaders(probe.rhi.GetApi()))
        {
            std::cout << "  [skip] no SPIR-V probe shaders yet; the ring across frames not verified" << std::endl;
            probe.Close();
            return;
        }
        static const JBro::TextureFormat colorFormat = JBro::TextureFormat::BGRA8Unorm;
        const JBro::GraphicsPipelineHandle pipeline =
            probe.device->CreateGraphicsPipeline(MakeProbePipelineDesc(probe.rhi.GetApi(), &colorFormat));
        Check(pipeline.IsValid(), "the probe pipeline must be created");
        const JBro::TextureHandle target = CreateTarget(*probe.device, colorFormat);
        Check(target.IsValid(), "the probe target must be created");
        JBro::ConstantRing ring;
        Check(ring.Create(*probe.device, 1024), "the constant ring must be created");

        // 프레임 슬롯 수보다 넉넉히 돈다. 슬롯마다 두 바퀴 이상이다.
        constexpr JBro::UInt32 Frames = 8;
        const Float4 tints[Frames] = {
            {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f},
            {0.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.4f, 0.8f, 1.0f}};
        for (JBro::UInt32 frame = 0; frame < Frames; ++frame)
        {
            const JBro::BeginFrameResult begun = probe.device->BeginFrame(probe.swapchain);
            Check(begun.status == JBro::FrameStatus::Ready, "each frame must begin");
            JBro::IRHICommandContext& commands = *begun.frame.commands;
            ring.BeginFrame(begun.frame.slot);
            const JBro::ConstantRange placement = ring.Push(Float4{1.0f, 1.0f, 0.0f, 0.0f});
            const JBro::ConstantRange tint = ring.Push(tints[frame]);
            Check(placement.IsValid() && tint.IsValid() && placement.offset == 0,
                "each frame must push from the start of its slot");
            Check(BeginTargetPass(commands, target, {0.0f, 0.0f, 0.0f, 1.0f}), "each pass must begin");
            Check(commands.SetGraphicsPipeline(pipeline), "the pipeline must bind");
            Check(commands.SetConstantBuffer(1, placement.buffer, placement.offset, placement.size), "the placement must bind");
            Check(commands.SetConstantBuffer(2, tint.buffer, tint.offset, tint.size), "the tint must bind");
            Check(commands.DrawInstanced(3, 1, 0, 0), "each frame must draw");
            commands.EndRenderPass();
            Check(probe.device->EndFrame(begun.frame) == JBro::FrameStatus::Ready, "each frame must end");
        }

        JBro::Array<std::byte> image;
        image.Resize(SurfaceSize * SurfaceSize * 4);
        JBro::TextureReadback readback;
        Check(probe.device->ReadTexture(target, image.Data(), image.Size(), readback), "the target must read back");
        Check(NearColor(ReadBgra(image, readback.rowPitch, 32, 32), tints[Frames - 1]),
            "the last frame's tint must be what the ring handed to the last draw");

        probe.CheckValidationStayedQuiet("the ring frame probe");
        ring.Destroy();
        probe.device->DestroyTexture(target);
        probe.device->DestroyGraphicsPipeline(pipeline);
        probe.Close();
    }

    template <typename TModule>
    void RunConstantBufferTestsOn()
    {
        TestTheCapsAreTheCommonFloor<TModule>();
        TestTheRingCutsAlignedRanges<TModule>();
        TestConstantsReachBothStagesPerRange<TModule>();
        TestTheRingTurnsOverAcrossFrames<TModule>();
    }
}

JBro::Int32 RunConstantBufferTests()
{
    RunConstantBufferTestsOn<JBro::D3D12RHIModule>();
    RunConstantBufferTestsOn<JBro::D3D11RHIModule>();
    RunConstantBufferTestsOn<JBro::VulkanRHIModule>();
    std::cout << "Constant buffer tests passed.\n";
    return 0;
}
