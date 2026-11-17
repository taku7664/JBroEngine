#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/RHI/ConstantRing.h>
#include <JBro/Types/Array.h>
#include <JBro/VulkanRHI/VulkanRHI.h>

#include "ComputeProbeFillCS.generated.h"
#include "ComputeProbeHalveCS.generated.h"
#include "ComputeProbeReadValuesCS.generated.h"
#include "ComputeProbeWriteValuesCS.generated.h"
#include "SubresourceProbeSamplePS.generated.h"
#include "SubresourceProbeVS.generated.h"

// fxc 의 헤더는 `BYTE` 로 적는다. Windows.h 를 끌어들이지 않고 이름만 준다(Renderer.cpp 와 같다).
namespace Sm5
{
    using BYTE = unsigned char;
#include "ComputeProbeFillCS_SM5.generated.h"
#include "ComputeProbeHalveCS_SM5.generated.h"
#include "ComputeProbeReadValuesCS_SM5.generated.h"
#include "ComputeProbeWriteValuesCS_SM5.generated.h"
#include "SubresourceProbeSamplePS_SM5.generated.h"
#include "SubresourceProbeVS_SM5.generated.h"
}

// Vulkan 의 SPIR-V(Vulkan SDK 의 dxc, `Tests/Shaders/Compile.ps1`).
namespace Spv
{
#include "ComputeProbeFillCS_SPV.generated.h"
#include "ComputeProbeHalveCS_SPV.generated.h"
#include "ComputeProbeReadValuesCS_SPV.generated.h"
#include "ComputeProbeWriteValuesCS_SPV.generated.h"
#include "SubresourceProbeSamplePS_SPV.generated.h"
#include "SubresourceProbeVS_SPV.generated.h"
}

#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 후처리 계획 3 단계(postprocess-plan §3)의 RHI 를 잰다 - 컴퓨트 파이프라인·읽고 쓰는 텍스처·구조화 버퍼·`Dispatch` 의 자동 배리어.
//
// D3D12 와 D3D11 은 돌려서 잰다. **D3D12 의 검증 레이어가 배리어를 잰다** - 쓰기 상태로 옮기지 않거나 돈 뒤 읽기 상태로 되돌리지 않으면
// 그림이 맞아도 검증 오류다. Vulkan 도 돌려서 잰다(SPIR-V 탐침은 2026-11-17, D-348) -
// 레이아웃 전이와 메모리 배리어는 Vulkan 검증 레이어가 잰다.
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
    constexpr JBro::UInt32 ValueCount = 64;

    struct Float4
    {
        JBro::Float x = 0.0f;
        JBro::Float y = 0.0f;
        JBro::Float z = 0.0f;
        JBro::Float w = 0.0f;
    };

    // 컴퓨트 셰이더의 b1 이다(`Params`).
    struct Params
    {
        Float4 color;
        std::uint32_t count[4] = {};
    };

    // 정점 단계의 b1 이다(SubresourceProbe.hlsl 의 `Placement`).
    struct Placement
    {
        Float4 placement{1.0f, 1.0f, 0.0f, 0.0f};
        Float4 depth;
    };

    JBro::Bool HasProbeShaders(JBro::GraphicsApi api)
    {
        return api == JBro::GraphicsApi::D3D12 || api == JBro::GraphicsApi::D3D11 || api == JBro::GraphicsApi::Vulkan;
    }

    // 백엔드의 바이트코드를 고른다 - D3D12 는 DXIL, D3D11 은 SM 5.0 DXBC, Vulkan 은 SPIR-V.
    template <std::size_t DxilSize, std::size_t Sm5Size, std::size_t SpvSize>
    JBro::ShaderBytecode Pick(JBro::GraphicsApi api, const unsigned char (&dxil)[DxilSize], const unsigned char (&sm5)[Sm5Size],
        const unsigned char (&spv)[SpvSize])
    {
        if (api == JBro::GraphicsApi::D3D11)
        {
            return {sm5, static_cast<JBro::UInt32>(Sm5Size)};
        }
        if (api == JBro::GraphicsApi::Vulkan)
        {
            return {spv, static_cast<JBro::UInt32>(SpvSize)};
        }
        return {dxil, static_cast<JBro::UInt32>(DxilSize)};
    }

    enum class Kernel
    {
        Fill,
        WriteValues,
        ReadValues,
        Halve
    };

    JBro::ShaderBytecode KernelBytecode(JBro::GraphicsApi api, Kernel kernel)
    {
        switch (kernel)
        {
        case Kernel::Fill:
            return Pick(api, JBroTestComputeProbeFillCS, Sm5::JBroTestComputeProbeFillCS_SM5, Spv::JBroTestComputeProbeFillCS_SPV);
        case Kernel::WriteValues:
            return Pick(api, JBroTestComputeProbeWriteValuesCS, Sm5::JBroTestComputeProbeWriteValuesCS_SM5, Spv::JBroTestComputeProbeWriteValuesCS_SPV);
        case Kernel::ReadValues:
            return Pick(api, JBroTestComputeProbeReadValuesCS, Sm5::JBroTestComputeProbeReadValuesCS_SM5, Spv::JBroTestComputeProbeReadValuesCS_SPV);
        case Kernel::Halve:
            return Pick(api, JBroTestComputeProbeHalveCS, Sm5::JBroTestComputeProbeHalveCS_SM5, Spv::JBroTestComputeProbeHalveCS_SPV);
        }
        return {};
    }

    // 커널마다 읽는 것만 선언한다. 모두 `Compute` 단계다.
    JBro::ComputePipelineHandle CreateKernel(JBro::IRHIDevice& device, JBro::GraphicsApi api, Kernel kernel)
    {
        JBro::ComputePipelineDesc desc;
        desc.computeShader = KernelBytecode(api, kernel);
        const JBro::BindingRange one{1, JBro::ShaderStage::Compute};
        if (kernel != Kernel::ReadValues)
        {
            desc.bindings.constantBuffers = one;
        }
        if (kernel != Kernel::WriteValues)
        {
            desc.bindings.storageTextures = one;
        }
        if (kernel == Kernel::WriteValues || kernel == Kernel::ReadValues)
        {
            desc.bindings.storageBuffers = one;
        }
        if (kernel == Kernel::Halve)
        {
            desc.bindings.sampledTextures = one;
        }
        return device.CreateComputePipeline(desc);
    }

    template <typename TModule>
    struct Probe
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::IRHIDevice* device = nullptr;
        JBro::WindowHandle window;
        JBro::SwapchainHandle swapchain;
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

        // 링에 올려 b1 에 묶는다.
        template <typename T>
        void BindB1(JBro::IRHICommandContext& commands, const T& value)
        {
            const JBro::ConstantRange range = ring.Push(value);
            Check(range.IsValid(), "the constants must fit in the ring");
            Check(commands.SetConstantBuffer(1, range.buffer, range.offset, range.size), "b1 must bind");
        }
    };

    JBro::TextureHandle CreateStorageTexture(JBro::IRHIDevice& device, JBro::UInt32 size, JBro::UInt32 mips)
    {
        JBro::TextureDesc desc;
        desc.extent = {size, size};
        desc.mipLevels = mips;
        desc.format = JBro::TextureFormat::RGBA8Unorm;
        desc.usage = JBro::TextureUsage::Storage | JBro::TextureUsage::Sampled;
        return device.CreateTexture(desc);
    }

    Float4 ReadRgba(const JBro::Array<std::byte>& image, JBro::UInt32 rowPitch, JBro::UInt32 x, JBro::UInt32 y)
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(image.Data() + static_cast<std::size_t>(y) * rowPitch + x * 4);
        return {bytes[0] / 255.0f, bytes[1] / 255.0f, bytes[2] / 255.0f, bytes[3] / 255.0f};
    }

    JBro::Bool IsColor(const Float4& actual, const Float4& expected)
    {
        const auto close = [](JBro::Float a, JBro::Float b) { return a - b < 0.02f && b - a < 0.02f; };
        return close(actual.x, expected.x) && close(actual.y, expected.y) && close(actual.z, expected.z);
    }

    // **상한과 만들기 규칙을 본다.** 셰이더가 없어도 되므로 세 백엔드에서 돈다.
    template <typename TModule>
    void TestStorageResourcesFollowTheirRules()
    {
        Probe<TModule> probe;
        if (false == probe.Open("JBro storage rules probe"))
        {
            std::cout << "  [skip] no device for this API; storage rules not verified" << std::endl;
            return;
        }
        const JBro::DeviceCaps caps = probe.device->GetCaps();
        Check(caps.maxStorageTextures == 4 && caps.maxStorageBuffers == 4, "four storage textures (u0..u3) and four buffers (u4..u7)");

        JBro::BindingLayoutDesc layout;
        layout.storageTextures = {1, JBro::ShaderStage::Compute};
        Check(JBro::IsBindingLayoutValid(layout, caps, true), "a compute layout takes storage");
        // 단계를 그래픽의 것으로 맞춰도 그래픽 자리표는 storage 를 받지 않는다 - 거절의 까닭이 단계가 아니라 storage 여야 한다.
        layout.storageTextures = {1, JBro::ShaderStage::Pixel};
        Check(false == JBro::IsBindingLayoutValid(layout, caps, false), "a graphics layout takes no storage texture");
        layout.storageTextures = {};
        layout.storageBuffers = {1, JBro::ShaderStage::Pixel};
        Check(false == JBro::IsBindingLayoutValid(layout, caps, false), "a graphics layout takes no storage buffer");
        layout.storageBuffers = {};
        layout.storageTextures = {1, JBro::ShaderStage::Compute};
        layout.storageTextures = {1, JBro::ShaderStage::Pixel};
        Check(false == JBro::IsBindingLayoutValid(layout, caps, true), "a compute layout names only the compute stage");
        layout = {};
        layout.sampledTextures = {1, JBro::ShaderStage::Compute};
        Check(false == JBro::IsBindingLayoutValid(layout, caps, false), "a graphics layout cannot name the compute stage");

        JBro::BufferDesc bufferDesc;
        bufferDesc.size = ValueCount * 16;
        bufferDesc.usage = JBro::BufferUsage::Storage;
        bufferDesc.stride = 16;
        const JBro::BufferHandle values = probe.device->CreateBuffer(bufferDesc);
        Check(values.IsValid(), "a structured buffer must be created");
        JBro::BufferDesc noStride = bufferDesc;
        noStride.stride = 0;
        Check(false == probe.device->CreateBuffer(noStride).IsValid(), "a structured buffer names its element size");
        JBro::BufferDesc ragged = bufferDesc;
        ragged.size = bufferDesc.size + 4;
        Check(false == probe.device->CreateBuffer(ragged).IsValid(), "a structured buffer holds whole elements");
        JBro::BufferDesc uploaded = bufferDesc;
        uploaded.memory = JBro::MemoryType::Upload;
        Check(false == probe.device->CreateBuffer(uploaded).IsValid(), "a structured buffer lives in device memory");

        const JBro::TextureHandle storage = CreateStorageTexture(*probe.device, SurfaceSize, 2);
        Check(storage.IsValid(), "an RGBA8 storage texture with mips must be created");
        JBro::TextureDesc srgb;
        srgb.extent = {SurfaceSize, SurfaceSize};
        srgb.format = JBro::TextureFormat::RGBA8UnormSrgb;
        srgb.usage = JBro::TextureUsage::Storage;
        Check(false == probe.device->CreateTexture(srgb).IsValid(), "an sRGB texture is not written by compute");

        probe.device->DestroyTexture(storage);
        probe.device->DestroyBuffer(values);
        probe.CheckValidationStayedQuiet("the storage rules probe");
        probe.Close();
    }

    // **컴퓨트가 텍스처를 칠하고, 버퍼를 쓰고 읽고, 한 mip 을 읽어 다음 mip 에 쓴다.**
    //   1. CSFill 이 64×64 를 (1, 0.5, 0, 1) 로 칠한다 → 되읽어 잰다.
    //   2. CSWriteValues 가 버퍼 64 칸을 (i/63, 1 - i/63, 0.5, 1) 로 쓰고, CSReadValues 가 그 버퍼를 열마다 옮긴다 - 두 디스패치 사이의
    //      UAV 배리어가 없으면 둘째가 덜 쓴 버퍼를 읽는다.
    //   3. 2 mip 텍스처의 mip 0 을 CSFill 로 빨강, CSHalve 가 mip 0 을 읽어 (0.5, 1, 1, 1) 을 곱해 mip 1 에 쓴다. 그 mip 1 을 다음 렌더 패스가
    //      `SetTextureMip` 으로 읽는다 - 디스패치 뒤에 읽기 상태로 돌아가 있어야 한다.
    template <typename TModule>
    void TestComputeWritesWhatTheNextStepReads()
    {
        Probe<TModule> probe;
        if (false == probe.Open("JBro compute probe"))
        {
            std::cout << "  [skip] no device for this API; compute not verified" << std::endl;
            return;
        }
        const JBro::GraphicsApi api = probe.rhi.GetApi();
        if (false == HasProbeShaders(api))
        {
            std::cout << "  [skip] no SPIR-V probe shaders yet; compute not dispatched" << std::endl;
            probe.Close();
            return;
        }
        JBro::IRHIDevice& device = *probe.device;
        const JBro::ComputePipelineHandle fill = CreateKernel(device, api, Kernel::Fill);
        const JBro::ComputePipelineHandle writeValues = CreateKernel(device, api, Kernel::WriteValues);
        const JBro::ComputePipelineHandle readValues = CreateKernel(device, api, Kernel::ReadValues);
        const JBro::ComputePipelineHandle halve = CreateKernel(device, api, Kernel::Halve);
        Check(fill.IsValid() && writeValues.IsValid() && readValues.IsValid() && halve.IsValid(),
            "the four compute pipelines must be created");
        JBro::ComputePipelineDesc vertexStaged;
        vertexStaged.computeShader = KernelBytecode(api, Kernel::Fill);
        vertexStaged.bindings.storageTextures = {1, JBro::ShaderStage::Vertex};
        Check(false == device.CreateComputePipeline(vertexStaged).IsValid(), "a compute layout naming a graphics stage is refused");

        static const JBro::TextureFormat colorFormat = JBro::TextureFormat::BGRA8Unorm;
        JBro::GraphicsPipelineDesc sampleDesc;
        sampleDesc.vertexShader = Pick(api, JBroTestSubresourceProbeVS, Sm5::JBroTestSubresourceProbeVS_SM5, Spv::JBroTestSubresourceProbeVS_SPV);
        sampleDesc.pixelShader = Pick(api, JBroTestSubresourceProbeSamplePS, Sm5::JBroTestSubresourceProbeSamplePS_SM5, Spv::JBroTestSubresourceProbeSamplePS_SPV);
        sampleDesc.colorFormats = {&colorFormat, 1};
        sampleDesc.cull = JBro::CullMode::None;
        sampleDesc.bindings.constantBuffers = {2, JBro::ShaderStage::Vertex | JBro::ShaderStage::Pixel};
        sampleDesc.bindings.sampledTextures = {1, JBro::ShaderStage::Pixel};
        sampleDesc.bindings.samplers = {1, JBro::ShaderStage::Pixel};
        const JBro::GraphicsPipelineHandle sample = device.CreateGraphicsPipeline(sampleDesc);
        JBro::SamplerDesc pointDesc;
        pointDesc.minFilter = JBro::FilterMode::Nearest;
        pointDesc.magFilter = JBro::FilterMode::Nearest;
        const JBro::SamplerHandle point = device.CreateSampler(pointDesc);
        Check(sample.IsValid() && point.IsValid(), "the read-back pipeline and sampler must be created");

        const JBro::TextureHandle painted = CreateStorageTexture(device, SurfaceSize, 1);
        const JBro::TextureHandle columns = CreateStorageTexture(device, SurfaceSize, 1);
        const JBro::TextureHandle chain = CreateStorageTexture(device, SurfaceSize, 2);
        JBro::TextureDesc plainDesc;
        plainDesc.extent = {SurfaceSize, SurfaceSize};
        plainDesc.format = JBro::TextureFormat::RGBA8Unorm;
        plainDesc.usage = JBro::TextureUsage::Sampled | JBro::TextureUsage::RenderTarget;
        const JBro::TextureHandle notStorage = device.CreateTexture(plainDesc);
        JBro::TextureDesc targetDesc;
        targetDesc.extent = {SurfaceSize, SurfaceSize};
        targetDesc.format = colorFormat;
        targetDesc.usage = JBro::TextureUsage::RenderTarget;
        const JBro::TextureHandle target = device.CreateTexture(targetDesc);
        JBro::BufferDesc valuesDesc;
        valuesDesc.size = ValueCount * 16;
        valuesDesc.usage = JBro::BufferUsage::Storage;
        valuesDesc.stride = 16;
        const JBro::BufferHandle values = device.CreateBuffer(valuesDesc);
        Check(painted.IsValid() && columns.IsValid() && chain.IsValid() && notStorage.IsValid() && target.IsValid()
            && values.IsValid(), "the compute probe resources must be created");

        const JBro::BeginFrameResult begun = device.BeginFrame(probe.swapchain);
        Check(begun.status == JBro::FrameStatus::Ready, "the probe frame must begin");
        JBro::IRHICommandContext& commands = *begun.frame.commands;
        probe.ring.BeginFrame(begun.frame.slot);
        const JBro::UInt32 groups = SurfaceSize / 8;

        Check(false == commands.Dispatch(1, 1, 1), "nothing dispatches before a compute pipeline is set");

        // 1. 칠하기.
        Check(commands.SetComputePipeline(fill), "the fill pipeline must bind");
        Check(false == commands.Dispatch(groups, groups, 1), "a dispatch with its storage texture unbound is refused");
        Check(false == commands.SetStorageTexture(0, notStorage, 0), "a texture not made for storage is refused");
        Check(false == commands.SetStorageTexture(4, painted, 0), "u4 is a buffer slot");
        Check(false == commands.SetStorageTexture(0, painted, 1), "a mip the texture lacks is refused");
        Check(commands.SetStorageTexture(0, painted, 0), "the painted texture must bind");
        probe.BindB1(commands, Params{{1.0f, 0.5f, 0.0f, 1.0f}, {}});
        Check(false == commands.Dispatch(0, 1, 1), "an empty dispatch is refused");
        Check(commands.Dispatch(groups, groups, 1), "the fill must dispatch");

        // 2. 버퍼를 쓰고 읽는다.
        Check(commands.SetComputePipeline(writeValues), "the write-values pipeline must bind");
        Check(false == commands.SetStorageBuffer(0, values), "u0 is a texture slot");
        Check(commands.SetStorageBuffer(4, values), "the values buffer must bind at u4");
        probe.BindB1(commands, Params{{0.0f, 0.0f, 0.5f, 1.0f}, {ValueCount, 0, 0, 0}});
        Check(commands.Dispatch(1, 1, 1), "the values must be written");
        Check(commands.SetComputePipeline(readValues), "the read-values pipeline must bind");
        Check(commands.SetStorageBuffer(4, values), "the values buffer must bind again");
        Check(commands.SetStorageTexture(0, columns, 0), "the columns texture must bind");
        Check(commands.Dispatch(groups, groups, 1), "the values must be read into columns");

        // 3. mip 0 을 칠하고, 읽어서 mip 1 에 쓴다.
        Check(commands.SetComputePipeline(fill), "the fill pipeline must bind again");
        Check(commands.SetStorageTexture(0, chain, 0), "mip 0 of the chain must bind");
        probe.BindB1(commands, Params{{1.0f, 0.0f, 0.0f, 1.0f}, {}});
        Check(commands.Dispatch(groups, groups, 1), "mip 0 must be filled");
        Check(commands.SetComputePipeline(halve), "the halve pipeline must bind");
        Check(commands.SetTextureMip(0, chain, 0), "mip 0 must be readable by compute");
        Check(commands.SetStorageTexture(0, chain, 1), "mip 1 must bind for writing");
        probe.BindB1(commands, Params{{0.5f, 1.0f, 1.0f, 1.0f}, {}});
        Check(commands.Dispatch(groups / 2, groups / 2, 1), "mip 1 must be written from mip 0");

        // 렌더 패스 안에서는 컴퓨트를 걸지 않는다. 그 패스가 mip 1 을 읽는다.
        JBro::ColorAttachmentDesc attachment;
        attachment.texture = target;
        attachment.loadOperation = JBro::LoadOperation::Clear;
        JBro::RenderPassDesc pass;
        pass.colorAttachments = {&attachment, 1};
        Check(commands.BeginRenderPass(pass), "the read-back pass must begin");
        Check(false == commands.SetComputePipeline(fill), "a compute pipeline is refused inside a render pass");
        Check(false == commands.Dispatch(1, 1, 1), "a dispatch is refused inside a render pass");
        JBro::Viewport viewport;
        viewport.width = static_cast<JBro::Float>(SurfaceSize);
        viewport.height = static_cast<JBro::Float>(SurfaceSize);
        commands.SetViewport(viewport);
        commands.SetScissor({0, 0, static_cast<JBro::Int32>(SurfaceSize), static_cast<JBro::Int32>(SurfaceSize)});
        Check(commands.SetGraphicsPipeline(sample), "the sample pipeline must bind");
        Check(commands.SetTextureMip(0, chain, 1), "mip 1 written by compute must be readable in the next pass");
        Check(commands.SetSampler(0, point), "the sampler must bind");
        const JBro::ConstantRange placement = probe.ring.Push(Placement{});
        const JBro::ConstantRange unusedTint = probe.ring.Push(Float4{});
        Check(commands.SetConstantBuffer(1, placement.buffer, placement.offset, placement.size), "b1 must bind for drawing");
        Check(commands.SetConstantBuffer(2, unusedTint.buffer, unusedTint.offset, unusedTint.size), "b2 must bind for drawing");
        Check(commands.DrawInstanced(3, 1, 0, 0), "mip 1 must be drawn");
        commands.EndRenderPass();
        Check(device.EndFrame(begun.frame) == JBro::FrameStatus::Ready, "the probe frame must end");

        JBro::Array<std::byte> image;
        image.Resize(SurfaceSize * SurfaceSize * 4);
        JBro::TextureReadback readback;
        Check(device.ReadTexture(painted, image.Data(), image.Size(), readback), "the painted texture must read back");
        Check(IsColor(ReadRgba(image, readback.rowPitch, 5, 60), {1.0f, 0.5f, 0.0f, 1.0f}), "compute must paint every texel");
        Check(device.ReadTexture(columns, image.Data(), image.Size(), readback), "the columns texture must read back");
        Check(IsColor(ReadRgba(image, readback.rowPitch, 0, 10), {0.0f, 1.0f, 0.5f, 1.0f}), "column 0 must hold value 0");
        Check(IsColor(ReadRgba(image, readback.rowPitch, 63, 40), {1.0f, 0.0f, 0.5f, 1.0f}), "column 63 must hold value 63");
        Check(IsColor(ReadRgba(image, readback.rowPitch, 21, 30), {21.0f / 63.0f, 42.0f / 63.0f, 0.5f, 1.0f}),
            "column 21 must hold value 21");
        Check(device.ReadTexture(target, image.Data(), image.Size(), readback), "the drawn mip must read back");
        const auto* bgra = reinterpret_cast<const unsigned char*>(image.Data() + static_cast<std::size_t>(32) * readback.rowPitch + 32 * 4);
        Check(IsColor({bgra[2] / 255.0f, bgra[1] / 255.0f, bgra[0] / 255.0f, 1.0f}, {0.5f, 0.0f, 0.0f, 1.0f}),
            "mip 1 must hold half of mip 0's red");

        probe.CheckValidationStayedQuiet("the compute probe");
        device.DestroyBuffer(values);
        device.DestroyTexture(target);
        device.DestroyTexture(notStorage);
        device.DestroyTexture(chain);
        device.DestroyTexture(columns);
        device.DestroyTexture(painted);
        device.DestroySampler(point);
        device.DestroyGraphicsPipeline(sample);
        device.DestroyComputePipeline(halve);
        device.DestroyComputePipeline(readValues);
        device.DestroyComputePipeline(writeValues);
        device.DestroyComputePipeline(fill);
        probe.Close();
    }

    // **렌더 패스가 그린 텍스처를 바로 다음 디스패치가 읽는다**(블룸의 첫 내림). 쓰는 쪽은 `Sampled` 없는 읽고 쓰는 텍스처라 앞의
    // 디스패치 뒤로 쓰기 상태에 남는다 - 그래서 이 디스패치 앞에는 그 텍스처의 전이가 없고, 패스 끝의 "읽기 상태로" 전이 하나만이
    // 그림과 컴퓨트 사이를 잇는다. Vulkan 은 그 전이가 컴퓨트 단계도 기다려야 한다(D-348) - 동기화 검증
    // (`VK_LAYER_VALIDATE_SYNC=1`)이 그 자리의 읽기-쓰기 위험을 말한다.
    template <typename TModule>
    void TestComputeReadsWhatAPassDrew()
    {
        Probe<TModule> probe;
        if (false == probe.Open("JBro compute after pass probe"))
        {
            std::cout << "  [skip] no device for this API; compute after a pass not verified" << std::endl;
            return;
        }
        const JBro::GraphicsApi api = probe.rhi.GetApi();
        JBro::IRHIDevice& device = *probe.device;
        const JBro::ComputePipelineHandle fill = CreateKernel(device, api, Kernel::Fill);
        const JBro::ComputePipelineHandle halve = CreateKernel(device, api, Kernel::Halve);
        JBro::TextureDesc drawnDesc;
        drawnDesc.extent = {SurfaceSize, SurfaceSize};
        drawnDesc.format = JBro::TextureFormat::RGBA8Unorm;
        drawnDesc.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
        const JBro::TextureHandle drawn = device.CreateTexture(drawnDesc);
        JBro::TextureDesc halfDesc;
        halfDesc.extent = {SurfaceSize / 2, SurfaceSize / 2};
        halfDesc.format = JBro::TextureFormat::RGBA8Unorm;
        halfDesc.usage = JBro::TextureUsage::Storage;
        const JBro::TextureHandle half = device.CreateTexture(halfDesc);
        Check(fill.IsValid() && halve.IsValid() && drawn.IsValid() && half.IsValid(),
            "the compute-after-pass resources must be created");

        const JBro::BeginFrameResult begun = device.BeginFrame(probe.swapchain);
        Check(begun.status == JBro::FrameStatus::Ready, "the probe frame must begin");
        JBro::IRHICommandContext& commands = *begun.frame.commands;
        probe.ring.BeginFrame(begun.frame.slot);
        const JBro::UInt32 groups = SurfaceSize / 2 / 8;

        // 쓰는 텍스처를 먼저 쓰기 상태로 만든다(파랑). 읽기로 되돌리지 않는다.
        Check(commands.SetComputePipeline(fill), "the fill pipeline must bind");
        Check(commands.SetStorageTexture(0, half, 0), "the half texture must bind");
        probe.BindB1(commands, Params{{0.0f, 0.0f, 1.0f, 1.0f}, {}});
        Check(commands.Dispatch(groups, groups, 1), "the half texture must be filled");

        // 패스가 초록으로 지운다. 패스 끝에서 읽기 상태로 간다.
        JBro::ColorAttachmentDesc attachment;
        attachment.texture = drawn;
        attachment.loadOperation = JBro::LoadOperation::Clear;
        attachment.clearColor = {0.0f, 1.0f, 0.0f, 1.0f};
        JBro::RenderPassDesc pass;
        pass.colorAttachments = {&attachment, 1};
        Check(commands.BeginRenderPass(pass), "the clearing pass must begin");
        commands.EndRenderPass();

        // 그 그림을 읽어 반으로 줄인다.
        Check(commands.SetComputePipeline(halve), "the halve pipeline must bind");
        Check(commands.SetTextureMip(0, drawn, 0), "what the pass drew must be readable by compute");
        Check(commands.SetStorageTexture(0, half, 0), "the half texture must bind again");
        probe.BindB1(commands, Params{{1.0f, 1.0f, 1.0f, 1.0f}, {}});
        Check(commands.Dispatch(groups, groups, 1), "the drawn texture must be halved");
        Check(device.EndFrame(begun.frame) == JBro::FrameStatus::Ready, "the probe frame must end");

        JBro::Array<std::byte> image;
        image.Resize((SurfaceSize / 2) * (SurfaceSize / 2) * 4);
        JBro::TextureReadback readback;
        Check(device.ReadTexture(half, image.Data(), image.Size(), readback), "the half texture must read back");
        Check(IsColor(ReadRgba(image, readback.rowPitch, 3, 28), {0.0f, 1.0f, 0.0f, 1.0f}),
            "compute must read the green the pass drew");

        probe.CheckValidationStayedQuiet("the compute after pass probe");
        device.DestroyTexture(half);
        device.DestroyTexture(drawn);
        device.DestroyComputePipeline(halve);
        device.DestroyComputePipeline(fill);
        probe.Close();
    }

    template <typename TModule>
    void RunComputeTestsOn()
    {
        TestStorageResourcesFollowTheirRules<TModule>();
        TestComputeWritesWhatTheNextStepReads<TModule>();
        TestComputeReadsWhatAPassDrew<TModule>();
    }
}

JBro::Int32 RunComputeTests()
{
    RunComputeTestsOn<JBro::D3D12RHIModule>();
    RunComputeTestsOn<JBro::D3D11RHIModule>();
    RunComputeTestsOn<JBro::VulkanRHIModule>();
    std::cout << "Compute tests passed.\n";
    return 0;
}
