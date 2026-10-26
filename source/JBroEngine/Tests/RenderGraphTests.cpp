#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Graphics/RenderGraph.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/RHI/ConstantRing.h>
#include <JBro/Types/Array.h>
#include <JBro/VulkanRHI/VulkanRHI.h>

#include "SubresourceProbeSamplePS.generated.h"
#include "SubresourceProbeVS.generated.h"

// fxc 의 헤더는 `BYTE` 로 적는다. Windows.h 를 끌어들이지 않고 이름만 준다(SubresourceTests.cpp 와 같다).
namespace Sm5
{
    using BYTE = unsigned char;
#include "SubresourceProbeSamplePS_SM5.generated.h"
#include "SubresourceProbeVS_SM5.generated.h"
}

// Vulkan SDK 의 dxc 가 구운 SPIR-V 다(`Tests/Shaders/Compile.ps1`).
namespace Spv
{
#include "SubresourceProbeSamplePS_SPV.generated.h"
#include "SubresourceProbeVS_SPV.generated.h"
}

#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 후처리 계획 5 단계(postprocess-plan §3.6, D-339)를 잰다 - RHI 의 명시 배리어와 렌더 그래프.
//
// 배리어는 세 백엔드에서 거절 규칙을 재고, 세 백엔드 모두 복사한 사본을 읽어 그려 검증 레이어로 상태를 잰다. 그래프는 실제 장치의 풀과
// 기록만 하는 가짜 컨텍스트로 잰다 - 실행 순서·가려진 까닭·텍스처 나눠 쓰기·바람·배리어 목록은 그리지 않고도 보인다.
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

    // 탐침 셰이더의 정점 단계 b1 이다(SubresourceTests.cpp 의 `Placement`).
    struct Placement
    {
        Float4 placement{1.0f, 1.0f, 0.0f, 0.0f};
        Float4 depth{0.0f, 0.0f, 0.0f, 0.0f};
    };

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
    };

    JBro::TextureHandle CreateTarget(JBro::IRHIDevice& device, JBro::TextureUsage usage, JBro::UInt32 mipLevels = 1)
    {
        JBro::TextureDesc desc;
        desc.extent = {SurfaceSize, SurfaceSize};
        desc.mipLevels = mipLevels;
        desc.format = JBro::TextureFormat::BGRA8Unorm;
        desc.usage = usage;
        return device.CreateTexture(desc);
    }

    JBro::Bool ClearTarget(JBro::IRHICommandContext& commands, JBro::TextureHandle target, const JBro::ClearColor& color)
    {
        JBro::ColorAttachmentDesc attachment;
        attachment.texture = target;
        attachment.loadOperation = JBro::LoadOperation::Clear;
        attachment.clearColor = color;
        JBro::RenderPassDesc pass;
        pass.colorAttachments = {&attachment, 1};
        if (false == commands.BeginRenderPass(pass))
        {
            return false;
        }
        commands.EndRenderPass();
        return true;
    }

    // **배리어의 거절 규칙은 세 백엔드가 같다**(D-339). 하나라도 틀린 묶음은 통째로 거절하고, 렌더 패스 안과 백버퍼도 거절한다.
    template <typename TModule>
    void TestBarrierRefusesWhatTheTextureCannotBe()
    {
        Probe<TModule> probe;
        if (false == probe.Open("JBro barrier rules probe"))
        {
            std::cout << "  [skip] no device for this API; barrier rules not verified" << std::endl;
            return;
        }
        JBro::IRHIDevice& device = *probe.device;
        const JBro::TextureHandle sampled = CreateTarget(device, JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled, 3);
        const JBro::TextureHandle plain = CreateTarget(device, JBro::TextureUsage::RenderTarget);
        JBro::TextureDesc depthDesc;
        depthDesc.extent = {SurfaceSize, SurfaceSize};
        depthDesc.format = JBro::TextureFormat::D32Float;
        depthDesc.usage = JBro::TextureUsage::DepthStencil;
        const JBro::TextureHandle depth = device.CreateTexture(depthDesc);
        Check(sampled.IsValid() && plain.IsValid() && depth.IsValid(), "the barrier probe textures must be created");

        // 규칙 함수부터 - 백엔드가 이것으로 거절한다.
        JBro::TextureDesc described;
        described.mipLevels = 3;
        described.format = JBro::TextureFormat::BGRA8Unorm;
        described.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
        Check(JBro::IsTextureBarrierValid(described, {sampled, 2, JBro::TextureAccess::ShaderRead}), "a sampled texture's last mip may be read");
        Check(false == JBro::IsTextureBarrierValid(described, {sampled, 3, JBro::TextureAccess::ShaderRead}), "a mip past the chain is refused");
        Check(false == JBro::IsTextureBarrierValid(described, {sampled, JBro::AllTextureMips, JBro::TextureAccess::DepthWrite}),
            "a colour texture cannot be a depth target");
        Check(false == JBro::IsTextureBarrierValid(described, {sampled, JBro::AllTextureMips, JBro::TextureAccess::Storage}),
            "a texture made without storage cannot be written by a dispatch");
        Check(JBro::IsTextureBarrierValid(described, {sampled, JBro::AllTextureMips, JBro::TextureAccess::CopyDestination}),
            "any colour texture may take a copy, as `CopyTexture` accepts it");
        described.format = JBro::TextureFormat::D32Float;
        described.mipLevels = 1;
        Check(false == JBro::IsTextureBarrierValid(described, {sampled, JBro::AllTextureMips, JBro::TextureAccess::CopySource}),
            "depth is not copied");

        const JBro::BeginFrameResult begun = device.BeginFrame(probe.swapchain);
        Check(begun.status == JBro::FrameStatus::Ready, "the barrier frame must begin");
        JBro::IRHICommandContext& commands = *begun.frame.commands;
        const JBro::TextureBarrier good[] = {
            {sampled, JBro::AllTextureMips, JBro::TextureAccess::RenderTarget},
            {sampled, JBro::AllTextureMips, JBro::TextureAccess::ShaderRead},
            {depth, JBro::AllTextureMips, JBro::TextureAccess::DepthWrite},
            {sampled, 1, JBro::TextureAccess::CopyDestination},
            {sampled, 1, JBro::TextureAccess::ShaderRead},
        };
        Check(commands.Barrier({good, 5}), "a batch the textures can all take must record");
        Check(commands.Barrier({good, 0}), "an empty batch records nothing and succeeds");
        const JBro::TextureBarrier notSampled[] = {
            {sampled, JBro::AllTextureMips, JBro::TextureAccess::RenderTarget},
            {plain, JBro::AllTextureMips, JBro::TextureAccess::ShaderRead},
        };
        Check(false == commands.Barrier({notSampled, 2}), "one texture the shaders cannot read fails the whole batch");
        const JBro::TextureBarrier pastChain[] = {{sampled, 3, JBro::TextureAccess::ShaderRead}};
        Check(false == commands.Barrier({pastChain, 1}), "a mip the texture does not have is refused");
        const JBro::TextureBarrier backBuffer[] = {{begun.frame.backBuffer, JBro::AllTextureMips, JBro::TextureAccess::RenderTarget}};
        Check(false == commands.Barrier({backBuffer, 1}), "the back buffer moves with presentation and is refused");
        const JBro::TextureBarrier dead[] = {{JBro::TextureHandle{}, JBro::AllTextureMips, JBro::TextureAccess::ShaderRead}};
        Check(false == commands.Barrier({dead, 1}), "an empty handle is refused");

        Check(ClearTarget(commands, plain, {0.0f, 0.0f, 0.0f, 1.0f}), "a pass must open on the plain target");
        JBro::ColorAttachmentDesc attachment;
        attachment.texture = plain;
        attachment.loadOperation = JBro::LoadOperation::Load;
        JBro::RenderPassDesc pass;
        pass.colorAttachments = {&attachment, 1};
        Check(commands.BeginRenderPass(pass), "the plain target's pass must reopen");
        Check(false == commands.Barrier({good, 1}), "barriers are not recorded inside a render pass");
        commands.EndRenderPass();
        Check(device.EndFrame(begun.frame) == JBro::FrameStatus::Ready, "the barrier frame must end");

        probe.CheckValidationStayedQuiet("the barrier rules probe");
        device.DestroyTexture(depth);
        device.DestroyTexture(plain);
        device.DestroyTexture(sampled);
        probe.Close();
    }

    // 백엔드마다 읽는 바이트코드다 - D3D12 는 DXIL, D3D11 은 SM 5.0 DXBC, Vulkan 은 SPIR-V.
    template <std::size_t DxilSize, std::size_t DxbcSize, std::size_t SpirvSize>
    JBro::ShaderBytecode ProbeBytecode(JBro::GraphicsApi api, const unsigned char (&dxil)[DxilSize],
        const unsigned char (&dxbc)[DxbcSize], const unsigned char (&spirv)[SpirvSize])
    {
        if (api == JBro::GraphicsApi::D3D11)
        {
            return {dxbc, static_cast<JBro::UInt32>(DxbcSize)};
        }
        if (api == JBro::GraphicsApi::Vulkan)
        {
            return {spirv, static_cast<JBro::UInt32>(SpirvSize)};
        }
        return {dxil, static_cast<JBro::UInt32>(DxilSize)};
    }

    JBro::ShaderBytecode SampleVertexShader(JBro::GraphicsApi api)
    {
        return ProbeBytecode(api, JBroTestSubresourceProbeVS, Sm5::JBroTestSubresourceProbeVS_SM5, Spv::JBroTestSubresourceProbeVS_SPV);
    }

    JBro::ShaderBytecode SamplePixelShader(JBro::GraphicsApi api)
    {
        return ProbeBytecode(
            api, JBroTestSubresourceProbeSamplePS, Sm5::JBroTestSubresourceProbeSamplePS_SM5, Spv::JBroTestSubresourceProbeSamplePS_SPV);
    }

    // **복사한 사본을 다음 패스가 읽는다**(D-283 의 아래 그림 사본, D-339). `CopyTexture` 의 계약은 `Sampled` 로 만든 대상을 다음 패스가 곧바로
    // 읽는 것이고, 그래프는 그 앞에 사본을 읽기로 옮기는 배리어를 건다. 둘 다 검증 레이어가 조용해야 하고 사본의 색이 나와야 한다.
    // 세 백엔드에서 잰다(Vulkan 의 SPIR-V 탐침은 2026-10-26 에 구웠다).
    template <typename TModule>
    void TestACopyIsReadByTheNextPass(JBro::Bool withBarrier)
    {
        Probe<TModule> probe;
        if (false == probe.Open("JBro copy read probe"))
        {
            std::cout << "  [skip] no device for this API; copy reads not verified" << std::endl;
            return;
        }
        JBro::IRHIDevice& device = *probe.device;
        const JBro::GraphicsApi api = probe.rhi.GetApi();
        JBro::ConstantRing ring;
        Check(ring.Create(device, 4096), "the constant ring must be created");
        const JBro::TextureFormat colorFormat = JBro::TextureFormat::BGRA8Unorm;
        JBro::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = SampleVertexShader(api);
        pipelineDesc.pixelShader = SamplePixelShader(api);
        pipelineDesc.colorFormats = {&colorFormat, 1};
        pipelineDesc.cull = JBro::CullMode::None;
        pipelineDesc.bindings.constantBuffers = {2, JBro::ShaderStage::Vertex | JBro::ShaderStage::Pixel};
        pipelineDesc.bindings.sampledTextures = {1, JBro::ShaderStage::Pixel};
        pipelineDesc.bindings.samplers = {1, JBro::ShaderStage::Pixel};
        const JBro::GraphicsPipelineHandle pipeline = device.CreateGraphicsPipeline(pipelineDesc);
        JBro::SamplerDesc samplerDesc;
        samplerDesc.minFilter = JBro::FilterMode::Nearest;
        samplerDesc.magFilter = JBro::FilterMode::Nearest;
        const JBro::SamplerHandle sampler = device.CreateSampler(samplerDesc);
        const JBro::TextureHandle source = CreateTarget(device, JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled);
        const JBro::TextureHandle copy = CreateTarget(
            device, JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled | JBro::TextureUsage::CopyDestination);
        const JBro::TextureHandle output = CreateTarget(device, JBro::TextureUsage::RenderTarget);
        Check(pipeline.IsValid() && sampler.IsValid() && source.IsValid() && copy.IsValid() && output.IsValid(),
            "the copy read probe objects must be created");

        const JBro::BeginFrameResult begun = device.BeginFrame(probe.swapchain);
        Check(begun.status == JBro::FrameStatus::Ready, "the copy read frame must begin");
        JBro::IRHICommandContext& commands = *begun.frame.commands;
        ring.BeginFrame(begun.frame.slot);
        Check(ClearTarget(commands, source, {0.0f, 1.0f, 0.0f, 1.0f}), "the source must clear to green");
        if (withBarrier)
        {
            const JBro::TextureBarrier toCopy[] = {{copy, JBro::AllTextureMips, JBro::TextureAccess::CopyDestination}};
            Check(commands.Barrier({toCopy, 1}), "the copy destination must move ahead of the copy");
        }
        Check(commands.CopyTexture(source, copy), "the source must copy");
        if (withBarrier)
        {
            const JBro::TextureBarrier toRead[] = {{copy, JBro::AllTextureMips, JBro::TextureAccess::ShaderRead}};
            Check(commands.Barrier({toRead, 1}), "the copy must move to shader read before the pass that reads it");
        }
        JBro::ColorAttachmentDesc attachment;
        attachment.texture = output;
        attachment.loadOperation = JBro::LoadOperation::Clear;
        attachment.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        JBro::RenderPassDesc pass;
        pass.colorAttachments = {&attachment, 1};
        Check(commands.BeginRenderPass(pass), "the reading pass must begin");
        JBro::Viewport viewport;
        viewport.width = static_cast<JBro::Float>(SurfaceSize);
        viewport.height = static_cast<JBro::Float>(SurfaceSize);
        commands.SetViewport(viewport);
        commands.SetScissor({0, 0, static_cast<JBro::Int32>(SurfaceSize), static_cast<JBro::Int32>(SurfaceSize)});
        Check(commands.SetGraphicsPipeline(pipeline), "the sampling pipeline must bind");
        Check(commands.SetTexture(0, copy), "the copy must bind for reading");
        Check(commands.SetSampler(0, sampler), "the sampler must bind");
        const JBro::ConstantRange vertex = ring.Push(Placement{});
        const JBro::ConstantRange pixel = ring.Push(Float4{0.0f, 0.0f, 0.0f, 0.0f});
        Check(commands.SetConstantBuffer(1, vertex.buffer, vertex.offset, vertex.size)
                && commands.SetConstantBuffer(2, pixel.buffer, pixel.offset, pixel.size),
            "the probe constants must bind");
        Check(commands.DrawInstanced(3, 1, 0, 0), "the full screen triangle must draw");
        commands.EndRenderPass();
        Check(device.EndFrame(begun.frame) == JBro::FrameStatus::Ready, "the copy read frame must end");

        JBro::Array<std::byte> image;
        image.Resize(SurfaceSize * SurfaceSize * 4);
        JBro::TextureReadback result;
        Check(device.ReadTexture(output, image.Data(), image.Size(), result), "the output must read back");
        const auto* center = reinterpret_cast<const unsigned char*>(image.Data() + 32 * result.rowPitch.Get() + 32 * 4);
        Check(center[1] > 240 && center[0] < 16 && center[2] < 16, "the pass must read the copied green");
        probe.CheckValidationStayedQuiet(withBarrier ? "the copy read probe with barriers" : "the copy read probe without barriers");

        device.DestroyTexture(output);
        device.DestroyTexture(copy);
        device.DestroyTexture(source);
        device.DestroySampler(sampler);
        device.DestroyGraphicsPipeline(pipeline);
        ring.Destroy();
        probe.Close();
    }

    // ── 렌더 그래프 ─────────────────────────────────────────────────────────

    // 그래프가 RHI 에 무엇을 내는지만 적는 컨텍스트다. 그리지 않는다.
    class RecordingCommands final : public JBro::IRHICommandContext
    {
    public:
        static constexpr JBro::UInt32 Capacity = 64;
        enum class Event : std::uint8_t
        {
            BeginPass,
            EndPass,
            Barrier,
            Copy
        };
        Event events[Capacity] = {};
        JBro::TextureHandle firstColor[Capacity];
        JBro::Bool withDepth[Capacity] = {};
        JBro::UInt32 barrierCounts[Capacity] = {};
        JBro::UInt32 count = 0;

        void Clear()
        {
            count = 0;
        }

        JBro::Bool BeginRenderPass(const JBro::RenderPassDesc& desc) override
        {
            if (count < Capacity)
            {
                events[count.Get()] = Event::BeginPass;
                firstColor[count.Get()] = desc.colorAttachments.size != 0 ? desc.colorAttachments.data[0].texture : JBro::TextureHandle{};
                withDepth[count.Get()] = desc.depthStencilAttachment != nullptr;
                ++count;
            }
            return true;
        }
        void EndRenderPass() override
        {
            if (count < Capacity)
            {
                events[count.Get()] = Event::EndPass;
                ++count;
            }
        }
        JBro::Bool Barrier(JBro::JArrayView<JBro::TextureBarrier> barriers) override
        {
            if (count < Capacity)
            {
                events[count.Get()] = Event::Barrier;
                barrierCounts[count.Get()] = barriers.size;
                ++count;
            }
            return true;
        }
        JBro::Bool CopyTexture(JBro::TextureHandle, JBro::TextureHandle) override
        {
            if (count < Capacity)
            {
                events[count.Get()] = Event::Copy;
                ++count;
            }
            return true;
        }
        void SetViewport(const JBro::Viewport&) override
        {
        }
        void SetScissor(const JBro::ScissorRect&) override
        {
        }
        JBro::Bool SetGraphicsPipeline(JBro::GraphicsPipelineHandle) override
        {
            return true;
        }
        JBro::Bool SetVertexBuffer(JBro::UInt32, JBro::BufferHandle, JBro::UInt32, std::size_t) override
        {
            return true;
        }
        JBro::Bool SetIndexBuffer(JBro::BufferHandle, JBro::IndexFormat, std::size_t) override
        {
            return true;
        }
        JBro::Bool SetGraphicsConstants(JBro::JArrayView<std::byte>) override
        {
            return true;
        }
        JBro::Bool SetTexture(JBro::UInt32, JBro::TextureHandle) override
        {
            return true;
        }
        JBro::Bool SetTextureMip(JBro::UInt32, JBro::TextureHandle, JBro::UInt32) override
        {
            return true;
        }
        JBro::Bool SetSampler(JBro::UInt32, JBro::SamplerHandle) override
        {
            return true;
        }
        JBro::Bool SetConstantBuffer(JBro::UInt32, JBro::BufferHandle, std::size_t, std::size_t) override
        {
            return true;
        }
        JBro::Bool DrawIndexedInstanced(JBro::UInt32, JBro::UInt32, JBro::UInt32, JBro::Int32, JBro::UInt32) override
        {
            return true;
        }
        JBro::Bool DrawInstanced(JBro::UInt32, JBro::UInt32, JBro::UInt32, JBro::UInt32) override
        {
            return true;
        }
        JBro::Bool SetComputePipeline(JBro::ComputePipelineHandle) override
        {
            return true;
        }
        JBro::Bool SetStorageTexture(JBro::UInt32, JBro::TextureHandle, JBro::UInt32) override
        {
            return true;
        }
        JBro::Bool SetStorageBuffer(JBro::UInt32, JBro::BufferHandle) override
        {
            return true;
        }
        JBro::Bool Dispatch(JBro::UInt32, JBro::UInt32, JBro::UInt32) override
        {
            return true;
        }
    };

    // 실행 함수가 무엇을 보았는지 적는다. 자료는 그래프가 복사한 것이라 바깥의 기록 자리를 포인터로 든다.
    struct Seen
    {
        JBro::UInt32 order[16] = {};
        JBro::UInt32 count = 0;
        JBro::Bool hadInput = false;
        JBro::Bool hadDepth = false;
    };
    struct PassNote
    {
        Seen* seen = nullptr;
        JBro::UInt32 id = 0;
        JBro::RenderGraphTexture input;
        JBro::RenderGraphTexture depth;
    };

    JBro::Bool Note(JBro::RenderGraphContext& context, const void* data)
    {
        const PassNote& note = *static_cast<const PassNote*>(data);
        if (note.seen->count < 16)
        {
            note.seen->order[note.seen->count.Get()] = note.id;
            ++note.seen->count;
        }
        if (note.input.IsValid())
        {
            note.seen->hadInput = context.HasTexture(note.input);
        }
        if (note.depth.IsValid())
        {
            note.seen->hadDepth = context.GetTexture(note.depth).IsValid();
        }
        return true;
    }

    JBro::Bool CopyNote(JBro::RenderGraphContext& context, const void* data)
    {
        const PassNote& note = *static_cast<const PassNote*>(data);
        Note(context, data);
        return context.GetCommands().CopyTexture({}, {});
    }

    JBro::RenderGraphLimits SmallLimits()
    {
        JBro::RenderGraphLimits limits;
        limits.maxPasses = 16;
        limits.maxTextures = 16;
        limits.maxAccesses = 64;
        limits.dataBytes = 2048;
        return limits;
    }

    const JBro::TransientTextureDesc ColorDesc = {
        {SurfaceSize, SurfaceSize}, JBro::TextureFormat::RGBA16Float, JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled};

    JBro::RenderGraphPass AddNotedPass(JBro::RenderGraph& graph, JBro::RenderGraphPassKind kind, const char* name, Seen& seen, JBro::UInt32 id,
        JBro::RenderGraphTexture input = {}, JBro::RenderGraphTexture depth = {})
    {
        const JBro::RenderGraphPass pass = graph.AddPass(kind, name, kind == JBro::RenderGraphPassKind::Copy ? &CopyNote : &Note);
        PassNote note;
        note.seen = &seen;
        note.id = id;
        note.input = input;
        note.depth = depth;
        Check(graph.SetPassData(pass, note), "the pass note must fit in the frame arena");
        return pass;
    }

    // **처음 보는 모양은 그 프레임에 대신 그리고, 다음 프레임부터 본래 패스가 돈다**(D-288·D-339). 풀에 없던 텍스처는 바람으로 남고,
    // 그것을 쓰는 패스와 꼭 읽는 패스는 `MissingTexture`, 대신 그리기는 돈다. `Prepare` 뒤에는 그 반대다.
    void TestAMissingTextureRunsTheFallbackUntilItExists(JBro::IRHIDevice& device)
    {
        JBro::RenderTargetPool pool;
        pool.Initialize(device);
        JBro::RenderGraph graph;
        Check(graph.Initialize(SmallLimits()), "the graph must initialize");
        const JBro::TextureHandle output = CreateTarget(device, JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled);
        Check(output.IsValid(), "the imported output must be created");
        RecordingCommands commands;

        for (JBro::Int32 frame = 0; frame < 2; ++frame)
        {
            Seen seen;
            graph.Reset();
            const JBro::RenderGraphTexture target = graph.ImportTexture(output);
            const JBro::RenderGraphTexture layer = graph.CreateTexture(ColorDesc);
            const JBro::RenderGraphPass draw = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "Layer", seen, 1);
            Check(graph.SetColorAttachment(draw, layer, JBro::LoadOperation::Clear), "the layer attachment must declare");
            const JBro::RenderGraphPass direct = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "LayerDirect", seen, 2);
            Check(graph.SetColorAttachment(direct, target, JBro::LoadOperation::Load) && graph.RunIfMissing(direct, layer),
                "the fallback must declare");
            const JBro::RenderGraphPass composite = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "Composite", seen, 3);
            Check(graph.SetColorAttachment(composite, target, JBro::LoadOperation::Load) && graph.Read(composite, layer),
                "the composite must declare");
            Check(graph.Compile(pool), "the graph must compile");
            commands.Clear();
            Check(graph.Execute(commands), "the graph must execute");
            if (frame == 0)
            {
                Check(graph.GetPassSkip(draw) == JBro::RenderGraphSkip::MissingTexture, "without the texture its pass does not run");
                Check(graph.GetPassSkip(composite) == JBro::RenderGraphSkip::MissingTexture, "nor does the pass that must read it");
                Check(graph.GetPassSkip(direct) == JBro::RenderGraphSkip::None, "the fallback runs instead");
                Check(seen.count == 1 && seen.order[0] == 2, "only the fallback recorded");
                Check(pool.GetWantedCount(ColorDesc) == 1, "the missing shape is wanted for the next frame");
                pool.Prepare();
                Check(pool.GetTextureCount() == 1 && pool.GetWantedCount(ColorDesc) == 0, "preparing the pool makes what was wanted");
            }
            else
            {
                Check(graph.GetPassSkip(draw) == JBro::RenderGraphSkip::None && graph.GetPassSkip(composite) == JBro::RenderGraphSkip::None,
                    "with the texture the pass and its reader run");
                Check(graph.GetPassSkip(direct) == JBro::RenderGraphSkip::TexturePresent, "the fallback stands down");
                Check(seen.count == 2 && seen.order[0] == 1 && seen.order[1] == 3, "the passes ran in the order they were added");
                Check(graph.GetCompiledTexture(layer).IsValid(), "the graph names the pool's texture");
            }
        }
        device.DestroyTexture(output);
        pool.Shutdown();
    }

    // **수명이 겹치지 않는 같은 모양은 한 텍스처를 나눠 쓴다**(D-322 (1)). 겹치면 따로 들고, 풀에 없던 수만큼 바란다.
    void TestTransientsShareATextureWhenTheirLivesDoNotOverlap(JBro::IRHIDevice& device)
    {
        JBro::RenderTargetPool pool;
        pool.Initialize(device);
        JBro::RenderGraph graph;
        Check(graph.Initialize(SmallLimits()), "the graph must initialize");
        const JBro::TextureHandle output = CreateTarget(device, JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled);
        RecordingCommands commands;
        Seen seen;

        // 겹치는 둘: a 를 쓰고 b 를 쓴 뒤 둘 다 읽는다. 풀이 비었으니 두 개를 바란다.
        const auto buildOverlapping = [&](JBro::RenderGraphTexture& a, JBro::RenderGraphTexture& b) {
            graph.Reset();
            const JBro::RenderGraphTexture target = graph.ImportTexture(output);
            a = graph.CreateTexture(ColorDesc);
            b = graph.CreateTexture(ColorDesc);
            const JBro::RenderGraphPass writeA = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "A", seen, 1);
            Check(graph.SetColorAttachment(writeA, a, JBro::LoadOperation::Clear), "a must declare");
            const JBro::RenderGraphPass writeB = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "B", seen, 2);
            Check(graph.SetColorAttachment(writeB, b, JBro::LoadOperation::Clear), "b must declare");
            const JBro::RenderGraphPass both = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "Both", seen, 3);
            Check(graph.SetColorAttachment(both, target, JBro::LoadOperation::Load) && graph.Read(both, a) && graph.Read(both, b),
                "the reader of both must declare");
        };
        JBro::RenderGraphTexture a;
        JBro::RenderGraphTexture b;
        buildOverlapping(a, b);
        Check(graph.Compile(pool), "the overlapping graph must compile");
        Check(pool.GetWantedCount(ColorDesc) == 2, "two live at once means two textures are wanted");
        pool.Prepare();
        buildOverlapping(a, b);
        Check(graph.Compile(pool), "the overlapping graph must compile with the pool filled");
        Check(graph.GetCompiledTexture(a).IsValid() && graph.GetCompiledTexture(b).IsValid()
                && graph.GetCompiledTexture(a) != graph.GetCompiledTexture(b),
            "textures alive at the same time get their own");

        // 겹치지 않는 둘: a 를 쓰고 읽은 뒤 c 를 쓰고 읽는다.
        graph.Reset();
        const JBro::RenderGraphTexture target = graph.ImportTexture(output);
        const JBro::RenderGraphTexture first = graph.CreateTexture(ColorDesc);
        const JBro::RenderGraphTexture second = graph.CreateTexture(ColorDesc);
        const JBro::RenderGraphPass writeFirst = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "First", seen, 1);
        Check(graph.SetColorAttachment(writeFirst, first, JBro::LoadOperation::Clear), "first must declare");
        const JBro::RenderGraphPass readFirst = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "ReadFirst", seen, 2);
        Check(graph.SetColorAttachment(readFirst, target, JBro::LoadOperation::Load) && graph.Read(readFirst, first), "first's reader");
        const JBro::RenderGraphPass writeSecond = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "Second", seen, 3);
        Check(graph.SetColorAttachment(writeSecond, second, JBro::LoadOperation::Clear), "second must declare");
        const JBro::RenderGraphPass readSecond = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "ReadSecond", seen, 4);
        Check(graph.SetColorAttachment(readSecond, target, JBro::LoadOperation::Load) && graph.Read(readSecond, second), "second's reader");
        Check(graph.Compile(pool), "the sequential graph must compile");
        Check(graph.GetCompiledTexture(first).IsValid() && graph.GetCompiledTexture(first) == graph.GetCompiledTexture(second),
            "textures whose lives do not overlap share one");
        Check(pool.GetWantedCount(ColorDesc) == 0, "nothing more is wanted");
        Check(graph.Execute(commands), "the sequential graph must execute");

        device.DestroyTexture(output);
        pool.Shutdown();
    }

    // **결과에 닿지 않는 패스는 버리고, 앞이 돌지 않은 입력은 읽지 않는다**(D-322 (1)). 선택 읽기와 선택 깊이는 없어도 패스가 돈다.
    void TestPassesThatReachNothingAreDropped(JBro::IRHIDevice& device)
    {
        JBro::RenderTargetPool pool;
        pool.Initialize(device);
        Check(pool.Ensure(ColorDesc), "the colour shape must be made up front");
        JBro::RenderGraph graph;
        Check(graph.Initialize(SmallLimits()), "the graph must initialize");
        const JBro::TextureHandle output = CreateTarget(device, JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled);
        RecordingCommands commands;
        Seen seen;

        graph.Reset();
        const JBro::RenderGraphTexture target = graph.ImportTexture(output);
        Check(graph.ImportTexture(output).index == target.index, "importing the same texture again names the same graph texture");
        const JBro::RenderGraphTexture unread = graph.CreateTexture(ColorDesc);
        const JBro::TransientTextureDesc depthDesc = {{SurfaceSize, SurfaceSize}, JBro::TextureFormat::D32Float, JBro::TextureUsage::DepthStencil};
        const JBro::RenderGraphTexture depth = graph.CreateTexture(depthDesc);
        const JBro::RenderGraphTexture neverWritten = graph.CreateTexture(ColorDesc);

        const JBro::RenderGraphPass orphan = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "Orphan", seen, 1);
        Check(graph.SetColorAttachment(orphan, unread, JBro::LoadOperation::Clear), "the orphan must declare");
        const JBro::RenderGraphPass loads = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "LoadsNothing", seen, 2);
        Check(graph.SetColorAttachment(loads, neverWritten, JBro::LoadOperation::Load), "a pass loading an unwritten texture must declare");
        const JBro::RenderGraphPass main = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "Main", seen, 3, neverWritten, depth);
        Check(graph.SetColorAttachment(main, target, JBro::LoadOperation::Clear)
                && graph.SetDepthAttachment(main, depth, JBro::LoadOperation::Clear, JBro::StoreOperation::Discard, true)
                && graph.Read(main, neverWritten, true),
            "the main pass must declare an optional depth and an optional read");
        Check(false == graph.SetColorAttachment(orphan, target, JBro::LoadOperation::Load), "only the pass just added takes declarations");
        Check(graph.Compile(pool), "the graph must compile");
        Check(graph.GetPassSkip(orphan) == JBro::RenderGraphSkip::Unused, "a pass nobody reads is dropped");
        Check(graph.GetPassSkip(loads) == JBro::RenderGraphSkip::InputNotWritten, "loading what nothing wrote is not run");
        Check(graph.GetPassSkip(main) == JBro::RenderGraphSkip::None, "the pass writing the result runs");
        Check(pool.GetWantedCount(depthDesc) == 1, "the missing depth is wanted");
        Check(graph.Execute(commands), "the graph must execute");
        Check(seen.count == 1 && seen.order[0] == 3, "only the main pass recorded");
        Check(false == seen.hadInput && false == seen.hadDepth, "the optional read and depth were absent and the pass saw so");
        Check(commands.count == 2 && commands.events[0] == RecordingCommands::Event::BeginPass && false == commands.withDepth[0],
            "the main pass opened without its missing depth and took no barrier");

        pool.Prepare();
        Check(graph.Compile(pool), "the graph must compile again with the depth made");
        seen = {};
        commands.Clear();
        Check(graph.Execute(commands), "the graph must execute again");
        Check(seen.hadDepth, "the depth is attached once it exists");
        Check(commands.events[0] == RecordingCommands::Event::Barrier && commands.barrierCounts[0] == 1 && commands.withDepth[1],
            "the depth's barrier goes ahead of the pass that attaches it");

        device.DestroyTexture(output);
        pool.Shutdown();
    }

    // **복사 대상은 복사 앞에서, 읽는 패스 앞에서는 셰이더 읽기로 옮긴다**(D-339). 들인 텍스처는 옮기지 않는다 - 쓰임을 모른다.
    void TestBarriersFollowTheAccess(JBro::IRHIDevice& device)
    {
        JBro::RenderTargetPool pool;
        pool.Initialize(device);
        const JBro::TransientTextureDesc copyDesc = {{SurfaceSize, SurfaceSize}, JBro::TextureFormat::BGRA8Unorm,
            JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled | JBro::TextureUsage::CopyDestination};
        Check(pool.Ensure(copyDesc), "the copy shape must be made up front");
        JBro::RenderGraph graph;
        Check(graph.Initialize(SmallLimits()), "the graph must initialize");
        const JBro::TextureHandle output = CreateTarget(device, JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled);
        RecordingCommands commands;
        Seen seen;

        graph.Reset();
        const JBro::RenderGraphTexture target = graph.ImportTexture(output);
        const JBro::RenderGraphTexture backdrop = graph.CreateTexture(copyDesc);
        const JBro::RenderGraphPass copy = AddNotedPass(graph, JBro::RenderGraphPassKind::Copy, "Backdrop", seen, 1);
        Check(graph.ReadCopySource(copy, target) && graph.WriteCopyDestination(copy, backdrop), "the copy must declare");
        Check(false == graph.SetColorAttachment(copy, target, JBro::LoadOperation::Load), "a copy pass has no attachments");
        const JBro::RenderGraphPass blend = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "Composite", seen, 2, backdrop);
        Check(graph.SetColorAttachment(blend, target, JBro::LoadOperation::Load) && graph.Read(blend, backdrop, true), "the blend must declare");
        Check(graph.Compile(pool), "the graph must compile");

        const JBro::JArrayView<JBro::TextureBarrier> copyBarriers = graph.GetPassBarriers(copy);
        Check(copyBarriers.size == 1 && copyBarriers.data[0].access == JBro::TextureAccess::CopyDestination
                && copyBarriers.data[0].texture == graph.GetCompiledTexture(backdrop),
            "the copy pass moves only its graph texture, and to copy destination");
        const JBro::JArrayView<JBro::TextureBarrier> blendBarriers = graph.GetPassBarriers(blend);
        Check(blendBarriers.size == 1 && blendBarriers.data[0].access == JBro::TextureAccess::ShaderRead,
            "the pass reading the copy moves it to shader read first");
        Check(graph.Execute(commands), "the graph must execute");
        Check(commands.count == 5 && commands.events[0] == RecordingCommands::Event::Barrier && commands.events[1] == RecordingCommands::Event::Copy
                && commands.events[2] == RecordingCommands::Event::Barrier && commands.events[3] == RecordingCommands::Event::BeginPass,
            "each pass's barriers are recorded right ahead of it");
        Check(seen.hadInput, "the reading pass sees the copy as written");

        device.DestroyTexture(output);
        pool.Shutdown();
    }

    // **조건으로만 가리키는 텍스처도 풀에서 고르고, 없으면 바란다**(Codex 리뷰). 고르지 않으면 늘 없는 것이라 그 조건이 영영 바뀌지 않는다.
    void TestATextureNamedOnlyByAConditionIsMade(JBro::IRHIDevice& device)
    {
        JBro::RenderTargetPool pool;
        pool.Initialize(device);
        JBro::RenderGraph graph;
        Check(graph.Initialize(SmallLimits()), "the graph must initialize");
        const JBro::TextureHandle output = CreateTarget(device, JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled);
        Seen seen;
        for (JBro::Int32 frame = 0; frame < 2; ++frame)
        {
            graph.Reset();
            const JBro::RenderGraphTexture target = graph.ImportTexture(output);
            const JBro::RenderGraphTexture gate = graph.CreateTexture(ColorDesc);
            const JBro::RenderGraphPass gated = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "Gated", seen, 1);
            Check(graph.SetColorAttachment(gated, target, JBro::LoadOperation::Clear) && graph.RunIfAvailable(gated, gate),
                "the gated pass must declare");
            Check(graph.Compile(pool), "the gated graph must compile");
            if (frame == 0)
            {
                Check(graph.GetPassSkip(gated) == JBro::RenderGraphSkip::MissingTexture, "the gate is missing at first");
                Check(pool.GetWantedCount(ColorDesc) == 1, "a texture named only by a condition is wanted");
                pool.Prepare();
            }
            else
            {
                Check(graph.GetPassSkip(gated) == JBro::RenderGraphSkip::None, "once made the gate opens");
            }
        }
        device.DestroyTexture(output);
        pool.Shutdown();
    }

    // **가득 찬 풀이 한 모양을 하나 더 만들 때 그 모양을 내주지 않는다**(Codex 리뷰). 모든 칸이 같은 만큼 쉬었으면 첫 칸이 뽑히는데, 그것이 같은
    // 모양이면 내주고 다시 만들어 수가 그대로였다 - 다음 컴파일이 또 바라고 대신 그리기가 끝나지 않는다.
    void TestAFullPoolGrowsAShapeWithoutEvictingIt(JBro::IRHIDevice& device)
    {
        JBro::RenderTargetPool pool;
        pool.Initialize(device);
        for (JBro::UInt32 shape = 0; shape < JBro::RenderTargetPool::Capacity; ++shape)
        {
            JBro::TransientTextureDesc desc = ColorDesc;
            desc.extent.width = SurfaceSize + shape;
            Check(pool.Ensure(desc), "every shape of the full pool must be made");
        }
        Check(pool.GetTextureCount() == JBro::RenderTargetPool::Capacity, "the pool is full");
        JBro::RenderGraph graph;
        Check(graph.Initialize(SmallLimits()), "the graph must initialize");
        const JBro::TextureHandle output = CreateTarget(device, JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled);
        Seen seen;
        JBro::RenderGraphTexture a;
        JBro::RenderGraphTexture b;
        const auto build = [&]() {
            graph.Reset();
            const JBro::RenderGraphTexture target = graph.ImportTexture(output);
            a = graph.CreateTexture(ColorDesc);
            b = graph.CreateTexture(ColorDesc);
            const JBro::RenderGraphPass writeA = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "A", seen, 1);
            Check(graph.SetColorAttachment(writeA, a, JBro::LoadOperation::Clear), "a must declare");
            const JBro::RenderGraphPass writeB = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "B", seen, 2);
            Check(graph.SetColorAttachment(writeB, b, JBro::LoadOperation::Clear), "b must declare");
            const JBro::RenderGraphPass both = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "Both", seen, 3);
            Check(graph.SetColorAttachment(both, target, JBro::LoadOperation::Load) && graph.Read(both, a) && graph.Read(both, b),
                "the reader of both must declare");
        };
        build();
        Check(graph.Compile(pool), "the graph must compile on the full pool");
        Check(pool.GetWantedCount(ColorDesc) == 1, "one more of the first shape is wanted");
        pool.Prepare();
        build();
        Check(graph.Compile(pool), "the graph must compile after the pool grew the shape");
        Check(graph.GetCompiledTexture(a).IsValid() && graph.GetCompiledTexture(b).IsValid(),
            "the pool gave up another shape, not the one it was growing");
        device.DestroyTexture(output);
        pool.Shutdown();
    }

    // **상한을 넘는 선언은 거절되고 그 프레임의 컴파일이 실패한다.** 다음 `Reset` 뒤에는 다시 된다.
    void TestTheGraphRefusesPastItsLimits(JBro::IRHIDevice& device)
    {
        JBro::RenderTargetPool pool;
        pool.Initialize(device);
        JBro::RenderGraph graph;
        JBro::RenderGraphLimits limits = SmallLimits();
        limits.maxPasses = 2;
        Check(graph.Initialize(limits), "the tiny graph must initialize");
        Seen seen;
        graph.Reset();
        AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "One", seen, 1);
        AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "Two", seen, 2);
        Check(false == graph.AddPass(JBro::RenderGraphPassKind::Raster, "Three", &Note).IsValid(), "a pass past the limit is refused");
        Check(false == graph.Compile(pool), "a frame that overflowed does not compile");
        graph.Reset();
        AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "One", seen, 1);
        Check(graph.Compile(pool), "the next frame compiles again");
        pool.Shutdown();
    }

#if defined(_MSC_VER) && defined(_DEBUG)
    JBro::Int32 graphAllocations = 0;
    int CountGraphAllocations(int operation, void*, std::size_t, int, long, const unsigned char*, int)
    {
        if (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC)
        {
            ++graphAllocations;
        }
        return 1;
    }
#endif

    // **프레임마다 짓고 컴파일하고 실행해도 힙을 쓰지 않는다**(D-322 (1)) - 저장소는 `Initialize` 가 잡았다.
    void TestAFrameOfTheGraphDoesNotAllocate(JBro::IRHIDevice& device)
    {
        JBro::RenderTargetPool pool;
        pool.Initialize(device);
        Check(pool.Ensure(ColorDesc), "the colour shape must be made up front");
        JBro::RenderGraph graph;
        Check(graph.Initialize(SmallLimits()), "the graph must initialize");
        const JBro::TextureHandle output = CreateTarget(device, JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled);
        RecordingCommands commands;
        Seen seen;
#if defined(_MSC_VER) && defined(_DEBUG)
        graphAllocations = 0;
        const _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountGraphAllocations);
#endif
        for (JBro::Int32 frame = 0; frame < 3; ++frame)
        {
            graph.Reset();
            const JBro::RenderGraphTexture target = graph.ImportTexture(output);
            const JBro::RenderGraphTexture layer = graph.CreateTexture(ColorDesc);
            const JBro::RenderGraphPass draw = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "Layer", seen, 1);
            graph.SetColorAttachment(draw, layer, JBro::LoadOperation::Clear);
            const JBro::RenderGraphPass composite = AddNotedPass(graph, JBro::RenderGraphPassKind::Raster, "Composite", seen, 2);
            graph.SetColorAttachment(composite, target, JBro::LoadOperation::Load);
            graph.Read(composite, layer);
            graph.Compile(pool);
            commands.Clear();
            graph.Execute(commands);
        }
#if defined(_MSC_VER) && defined(_DEBUG)
        _CrtSetAllocHook(previous);
        Check(graphAllocations == 0, "building, compiling and executing a frame must not allocate");
#endif
        device.DestroyTexture(output);
        pool.Shutdown();
    }

    void RunGraphTests()
    {
        // 그래프는 백엔드를 모른다. 풀이 텍스처를 만들 장치 하나면 된다.
        Probe<JBro::D3D11RHIModule> probe;
        if (false == probe.Open("JBro render graph probe"))
        {
            std::cout << "  [skip] no D3D11 device; render graph not verified" << std::endl;
            return;
        }
        TestAMissingTextureRunsTheFallbackUntilItExists(*probe.device);
        TestTransientsShareATextureWhenTheirLivesDoNotOverlap(*probe.device);
        TestPassesThatReachNothingAreDropped(*probe.device);
        TestBarriersFollowTheAccess(*probe.device);
        TestATextureNamedOnlyByAConditionIsMade(*probe.device);
        TestAFullPoolGrowsAShapeWithoutEvictingIt(*probe.device);
        TestTheGraphRefusesPastItsLimits(*probe.device);
        TestAFrameOfTheGraphDoesNotAllocate(*probe.device);
        probe.Close();
    }

    template <typename TModule>
    void RunBarrierTestsOn()
    {
        TestBarrierRefusesWhatTheTextureCannotBe<TModule>();
        TestACopyIsReadByTheNextPass<TModule>(false);
        TestACopyIsReadByTheNextPass<TModule>(true);
    }
}

JBro::Int32 RunRenderGraphTests()
{
    RunBarrierTestsOn<JBro::D3D12RHIModule>();
    RunBarrierTestsOn<JBro::D3D11RHIModule>();
    RunBarrierTestsOn<JBro::VulkanRHIModule>();
    RunGraphTests();
    std::cout << "Render graph tests passed.\n";
    return 0;
}
