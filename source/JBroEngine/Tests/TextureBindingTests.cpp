#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/Array.h>

#include "TexturedQuadPS.generated.h"
#include "TexturedQuadVS.generated.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    struct Vertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
    };

    // 백버퍼는 BGRA8 이다.
    struct Pixel
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
    };

    Pixel ReadPixel(const JBro::Array<std::byte>& image, std::uint32_t rowPitch,
        std::uint32_t x, std::uint32_t y)
    {
        const std::size_t offset = static_cast<std::size_t>(y) * rowPitch
            + static_cast<std::size_t>(x) * 4;
        const auto* bytes = reinterpret_cast<const unsigned char*>(image.Data() + offset);
        Pixel pixel;
        pixel.b = bytes[0] / 255.0f;
        pixel.g = bytes[1] / 255.0f;
        pixel.r = bytes[2] / 255.0f;
        return pixel;
    }

    bool Near(float a, float b)
    {
        return std::fabs(a - b) < 0.02f;
    }

    constexpr std::uint32_t SurfaceSize = 64;

    // 좌상 빨강, 우상 초록, 좌하 파랑, 우하 하양. 넷이 모두 다르므로
    // 뒤집히거나 밀린 것도 드러난다.
    constexpr unsigned char ProbeTexels[] = {
        255, 0, 0, 255,    0, 255, 0, 255,
        0, 0, 255, 255,    255, 255, 255, 255
    };

    // 화면을 가득 채우는, 텍스처 입힌 사각형 하나를 그릴 만큼만 차린다.
    // 세 테스트가 같은 것을 필요로 하므로 한 자리에 모은다.
    struct Probe
    {
        JBro::WindowsPlatform platform;
        JBro::D3D12RHIModule rhi;
        JBro::IRHIDevice* device = nullptr;
        JBro::WindowHandle window;
        JBro::SwapchainHandle swapchain;
        JBro::TextureHandle texture;
        JBro::TextureHandle renderTargetOnly;
        JBro::SamplerHandle sampler;
        JBro::BufferHandle vertexBuffer;
        JBro::BufferHandle indexBuffer;
        JBro::GraphicsPipelineHandle pipeline;
        bool platformOpen = false;
        bool rhiOpen = false;

        // 이 기계에 D3D12 가 없으면 false 다. 그 경우 테스트는 건너뛴다.
        bool Open(const char* title);
        bool BeginPass(JBro::IRHICommandContext& commands, const JBro::BeginFrameResult& begun);
        void Close();
    };

    // 개수만 바꿔서 쓸 수 있도록 한 자리에서 만든다. 그러지 않으면 "너무 많다" 를
    // 시험한다면서 실은 다른 이유로 실패하는 서술자를 넘기게 된다.
    JBro::GraphicsPipelineDesc MakeTexturedPipelineDesc()
    {
        static const JBro::VertexAttributeDesc attributes[] = {
            {0, 0, JBro::VertexFormat::Float2},
            {1, 8, JBro::VertexFormat::Float2},
        };
        static const JBro::VertexBufferLayoutDesc layout = {
            sizeof(Vertex), JBro::VertexStepMode::Vertex, {attributes, 2}
        };
        static const JBro::TextureFormat colorFormats[] = {JBro::TextureFormat::BGRA8Unorm};

        JBro::GraphicsPipelineDesc desc;
        desc.vertexShader = {JBroTestTexturedQuadVS, sizeof(JBroTestTexturedQuadVS)};
        desc.pixelShader = {JBroTestTexturedQuadPS, sizeof(JBroTestTexturedQuadPS)};
        desc.vertexBuffers = {&layout, 1};
        desc.colorFormats = {colorFormats, 1};
        desc.cull = JBro::CullMode::None;
        desc.sampledTextureCount = 1;
        desc.samplerCount = 1;
        return desc;
    }

    bool Probe::Open(const char* title)
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
        device = rhi.CreateDevice({});
        if (device == nullptr)
        {
            Close();
            return false;
        }

        JBro::WindowDesc windowDesc;
        windowDesc.title = {title, static_cast<std::uint32_t>(std::strlen(title))};
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

        JBro::TextureDesc textureDesc;
        textureDesc.extent = {2, 2};
        textureDesc.format = JBro::TextureFormat::RGBA8Unorm;
        textureDesc.usage = JBro::TextureUsage::Sampled | JBro::TextureUsage::CopyDestination;
        texture = device->CreateTexture(textureDesc);
        Check(texture.IsValid(), "a sampled texture must be created");
        Check(device->WriteTexture(texture, 0,
            {reinterpret_cast<const std::byte*>(ProbeTexels), sizeof(ProbeTexels)}),
            "the texels must upload");

        // 셰이더가 읽을 수 없는 텍스처다. 묶으려 드는 쪽을 거절하는지 보는 데 쓴다.
        JBro::TextureDesc renderTargetDesc;
        renderTargetDesc.extent = {4, 4};
        renderTargetDesc.format = JBro::TextureFormat::RGBA8Unorm;
        renderTargetDesc.usage = JBro::TextureUsage::RenderTarget;
        renderTargetOnly = device->CreateTexture(renderTargetDesc);
        Check(renderTargetOnly.IsValid(), "a render target must still be creatable");

        JBro::SamplerDesc samplerDesc;
        // 텍셀을 그대로 집는다. 선형으로 섞으면 모서리 값이 흐려져 무엇이 어디 있는지
        // 읽을 수 없다.
        samplerDesc.minFilter = JBro::FilterMode::Nearest;
        samplerDesc.magFilter = JBro::FilterMode::Nearest;
        sampler = device->CreateSampler(samplerDesc);
        Check(sampler.IsValid(), "a sampler must be created");

        static const Vertex vertices[] = {
            {-1.0f,  1.0f, 0.0f, 0.0f},
            { 1.0f,  1.0f, 1.0f, 0.0f},
            { 1.0f, -1.0f, 1.0f, 1.0f},
            {-1.0f, -1.0f, 0.0f, 1.0f},
        };
        static const std::uint16_t indices[] = {0, 1, 2, 0, 2, 3};

        JBro::BufferDesc vertexDesc;
        vertexDesc.size = sizeof(vertices);
        vertexDesc.usage = JBro::BufferUsage::Vertex;
        vertexDesc.memory = JBro::MemoryType::Upload;
        vertexBuffer = device->CreateBuffer(vertexDesc);
        Check(vertexBuffer.IsValid(), "the vertex buffer must be created");
        Check(device->WriteBuffer(vertexBuffer, 0,
            {reinterpret_cast<const std::byte*>(vertices), sizeof(vertices)}),
            "the vertices must upload");

        JBro::BufferDesc indexDesc;
        indexDesc.size = sizeof(indices);
        indexDesc.usage = JBro::BufferUsage::Index;
        indexDesc.memory = JBro::MemoryType::Upload;
        indexBuffer = device->CreateBuffer(indexDesc);
        Check(indexBuffer.IsValid(), "the index buffer must be created");
        Check(device->WriteBuffer(indexBuffer, 0,
            {reinterpret_cast<const std::byte*>(indices), sizeof(indices)}),
            "the indices must upload");

        pipeline = device->CreateGraphicsPipeline(MakeTexturedPipelineDesc());
        Check(pipeline.IsValid(), "the textured pipeline must be created");
        return true;
    }

    bool Probe::BeginPass(
        JBro::IRHICommandContext& commands,
        const JBro::BeginFrameResult& begun)
    {
        JBro::ColorAttachmentDesc attachment;
        attachment.texture = begun.frame.backBuffer;
        attachment.loadOperation = JBro::LoadOperation::Clear;
        attachment.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        JBro::RenderPassDesc pass;
        pass.colorAttachments = {&attachment, 1};
        if (false == commands.BeginRenderPass(pass))
        {
            return false;
        }
        JBro::Viewport viewport;
        viewport.width = static_cast<float>(SurfaceSize);
        viewport.height = static_cast<float>(SurfaceSize);
        commands.SetViewport(viewport);
        commands.SetScissor({0, 0,
            static_cast<std::int32_t>(SurfaceSize),
            static_cast<std::int32_t>(SurfaceSize)});
        return true;
    }

    void Probe::Close()
    {
        if (device != nullptr)
        {
            device->DestroySampler(sampler);
            device->DestroyTexture(renderTargetOnly);
            device->DestroyTexture(texture);
            device->DestroyBuffer(indexBuffer);
            device->DestroyBuffer(vertexBuffer);
            device->DestroyGraphicsPipeline(pipeline);
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

    // 텍스처 바인딩이 실제로 GPU 까지 닿는지는 픽셀을 되읽지 않고는 증명할 수 없다.
    // 디스크립터를 엉뚱한 자리에 복사해도, 샘플러를 안 묶어도, D3D12 는 대개
    // 아무 말도 하지 않고 색만 조용히 달라진다.
    void TestATextureReachesTheShader()
    {
        Probe probe;
        if (false == probe.Open("JBro texture probe"))
        {
            std::cout << "  [skip] no D3D12 device; texture binding not verified" << std::endl;
            return;
        }

        // 밉 하나짜리만 올린다는 계약이다.
        Check(false == probe.device->WriteTexture(probe.texture, 1,
            {reinterpret_cast<const std::byte*>(ProbeTexels), sizeof(ProbeTexels)}),
            "a mip this engine does not upload must be refused");
        // 크기가 맞지 않는 것도 거절한다. 모자란 것을 받으면 나머지가 쓰레기가 된다.
        Check(false == probe.device->WriteTexture(probe.texture, 0,
            {reinterpret_cast<const std::byte*>(ProbeTexels), sizeof(ProbeTexels) - 4}),
            "a short upload must be refused rather than padded");

        // 선언한 수를 넘는 파이프라인은 거절한다. 루트 시그니처는 만들고 나면 못 바꾸므로
        // 여기서 막지 않으면 그리는 자리에서 조용히 잘린다.
        JBro::GraphicsPipelineDesc tooMany = MakeTexturedPipelineDesc();
        tooMany.sampledTextureCount = 64;
        // 같은 서술자에서 개수만 되돌리면 만들어져야 한다. 위의 거절이 개수 때문이지
        // 다른 무엇 때문이 아님을 그것이 말해 준다.
        JBro::GraphicsPipelineDesc justEnough = MakeTexturedPipelineDesc();
        Check(false == probe.device->CreateGraphicsPipeline(tooMany).IsValid(),
            "a pipeline asking for more textures than the backend binds must be refused");
        const JBro::GraphicsPipelineHandle spare =
            probe.device->CreateGraphicsPipeline(justEnough);
        Check(spare.IsValid(), "the same descriptor with a workable count must be created");
        probe.device->DestroyGraphicsPipeline(spare);

        const JBro::BeginFrameResult begun = probe.device->BeginFrame(probe.swapchain);
        Check(begun.status == JBro::FrameStatus::Ready, "the probe frame must begin");
        JBro::IRHICommandContext& commands = *begun.frame.commands;

        // 프레임 안에서는 올리지 않는다. 여기서 GPU 를 기다리면 프레임이 막힌다.
        Check(false == probe.device->WriteTexture(probe.texture, 0,
            {reinterpret_cast<const std::byte*>(ProbeTexels), sizeof(ProbeTexels)}),
            "uploading inside a frame must be refused");

        Check(probe.BeginPass(commands, begun), "the probe render pass must begin");
        Check(commands.SetGraphicsPipeline(probe.pipeline), "the pipeline must bind");

        // 파이프라인이 선언하지 않은 자리는 거절한다.
        Check(false == commands.SetTexture(1, probe.texture),
            "a slot the pipeline never declared must be refused");
        Check(false == commands.SetSampler(1, probe.sampler),
            "a sampler slot the pipeline never declared must be refused");

        Check(commands.SetTexture(0, probe.texture), "the texture must bind");
        Check(commands.SetSampler(0, probe.sampler), "the sampler must bind");
        Check(commands.SetVertexBuffer(0, probe.vertexBuffer, sizeof(Vertex), 0),
            "the vertices must bind");
        Check(commands.SetIndexBuffer(probe.indexBuffer, JBro::IndexFormat::UInt16, 0),
            "the indices must bind");
        Check(commands.DrawIndexedInstanced(6, 1, 0, 0, 0), "the quad must draw");

        commands.EndRenderPass();
        Check(probe.device->EndFrame(begun.frame) == JBro::FrameStatus::Ready,
            "the probe frame must present");

        JBro::Array<std::byte> image;
        image.Resize(SurfaceSize * SurfaceSize * 4);
        JBro::TextureReadback readback;
        Check(probe.device->ReadTexture(
                begun.frame.backBuffer, image.Data(), image.Size(), readback),
            "the back buffer must read back");

        // 화면을 가득 채운 사각형이므로 네 사분면이 네 텍셀이다.
        const Pixel topLeft     = ReadPixel(image, readback.rowPitch, 16, 16);
        const Pixel topRight    = ReadPixel(image, readback.rowPitch, 48, 16);
        const Pixel bottomLeft  = ReadPixel(image, readback.rowPitch, 16, 48);
        const Pixel bottomRight = ReadPixel(image, readback.rowPitch, 48, 48);

        Check(Near(topLeft.r, 1.0f) && Near(topLeft.g, 0.0f) && Near(topLeft.b, 0.0f),
            "the first texel must land in the top left");
        Check(Near(topRight.r, 0.0f) && Near(topRight.g, 1.0f) && Near(topRight.b, 0.0f),
            "the second texel must land in the top right");
        Check(Near(bottomLeft.r, 0.0f) && Near(bottomLeft.g, 0.0f) && Near(bottomLeft.b, 1.0f),
            "the third texel must land in the bottom left");
        Check(Near(bottomRight.r, 1.0f) && Near(bottomRight.g, 1.0f) && Near(bottomRight.b, 1.0f),
            "the fourth texel must land in the bottom right");

        probe.Close();
    }

    // 묶지 않은 자리로 그리면 셰이더가 남의 디스크립터를 읽는다. D3D12 는 그것을
    // 말해 주지 않고 화면만 조용히 달라진다.
    void TestDrawingNeedsEverySlotItDeclared()
    {
        Probe probe;
        if (false == probe.Open("JBro unbound probe"))
        {
            std::cout << "  [skip] no D3D12 device; unbound draws not verified" << std::endl;
            return;
        }

        const JBro::BeginFrameResult begun = probe.device->BeginFrame(probe.swapchain);
        Check(begun.status == JBro::FrameStatus::Ready, "the frame must begin");
        JBro::IRHICommandContext& commands = *begun.frame.commands;
        Check(probe.BeginPass(commands, begun), "the render pass must begin");
        Check(commands.SetGraphicsPipeline(probe.pipeline), "the pipeline must bind");
        Check(commands.SetVertexBuffer(0, probe.vertexBuffer, sizeof(Vertex), 0),
            "the vertices must bind");
        Check(commands.SetIndexBuffer(probe.indexBuffer, JBro::IndexFormat::UInt16, 0),
            "the indices must bind");

        // 텍스처만 묶고 샘플러는 두고 그린다.
        Check(commands.SetTexture(0, probe.texture), "the texture must bind");
        Check(false == commands.DrawIndexedInstanced(6, 1, 0, 0, 0),
            "a draw with a declared sampler slot left empty must be refused");

        // 반대도 마찬가지다. 파이프라인을 다시 걸면 묶어 둔 것이 버려진다.
        Check(commands.SetGraphicsPipeline(probe.pipeline), "the pipeline must bind again");
        Check(commands.SetSampler(0, probe.sampler), "the sampler must bind");
        Check(false == commands.DrawIndexedInstanced(6, 1, 0, 0, 0),
            "a draw with a declared texture slot left empty must be refused");

        // 셰이더가 읽을 수 없는 텍스처는 애초에 묶이지 않는다.
        Check(false == commands.SetTexture(0, probe.renderTargetOnly),
            "a texture that was not made sampled must not bind");

        // 둘 다 묶으면 그려진다. 위의 거절이 그리기 자체를 막은 것이 아님을 본다.
        Check(commands.SetTexture(0, probe.texture), "the texture must bind");
        Check(commands.DrawIndexedInstanced(6, 1, 0, 0, 0),
            "a draw with every declared slot bound must go through");

        commands.EndRenderPass();
        Check(probe.device->EndFrame(begun.frame) == JBro::FrameStatus::Ready,
            "the frame must present");
        probe.Close();
    }

    // 보이는 디스크립터 링은 프레임마다 처음으로 되돌아가야 한다. 되감지 않으면
    // 몇 프레임 뒤부터 그리기가 조용히 실패한다 — 화면이 멈춘 것처럼 보이고
    // 어디서 멈췄는지는 아무 데도 적히지 않는다.
    void TestTheDescriptorRingRewindsEachFrame()
    {
        Probe probe;
        if (false == probe.Open("JBro ring probe"))
        {
            std::cout << "  [skip] no D3D12 device; the descriptor ring not verified" << std::endl;
            return;
        }

        // 한 프레임이 쓸 수 있는 수는 백엔드의 사정이라 여기서 알 수 없다. 두 프레임을
        // 합쳐 한 프레임 몫보다 확실히 많이 그리는 것으로 충분하다 — 되감지 않으면
        // 둘째 프레임 도중에 자리가 떨어진다.
        constexpr std::uint32_t DrawsPerFrame = 400;
        for (std::uint32_t frame = 0; frame < 2; ++frame)
        {
            const JBro::BeginFrameResult begun = probe.device->BeginFrame(probe.swapchain);
            Check(begun.status == JBro::FrameStatus::Ready, "each frame must begin");
            JBro::IRHICommandContext& commands = *begun.frame.commands;
            Check(probe.BeginPass(commands, begun), "each render pass must begin");
            Check(commands.SetGraphicsPipeline(probe.pipeline), "the pipeline must bind");
            Check(commands.SetVertexBuffer(0, probe.vertexBuffer, sizeof(Vertex), 0),
                "the vertices must bind");
            Check(commands.SetIndexBuffer(probe.indexBuffer, JBro::IndexFormat::UInt16, 0),
                "the indices must bind");

            for (std::uint32_t draw = 0; draw < DrawsPerFrame; ++draw)
            {
                Check(commands.SetTexture(0, probe.texture), "the texture must bind");
                Check(commands.SetSampler(0, probe.sampler), "the sampler must bind");
                Check(commands.DrawIndexedInstanced(6, 1, 0, 0, 0),
                    "every draw must find room in this frame's descriptors");
            }

            commands.EndRenderPass();
            Check(probe.device->EndFrame(begun.frame) == JBro::FrameStatus::Ready,
                "each frame must present");
        }
        probe.Close();
    }

    void TestAFreedSamplerDoesNotComeBack()
    {
        Probe probe;
        if (false == probe.Open("JBro sampler probe"))
        {
            std::cout << "  [skip] no D3D12 device; sampler reuse not verified" << std::endl;
            return;
        }

        const JBro::SamplerHandle first = probe.device->CreateSampler({});
        Check(first.IsValid(), "a sampler must be creatable");
        const JBro::SamplerHandle stale = first;
        probe.device->DestroySampler(first);

        const JBro::SamplerHandle second = probe.device->CreateSampler({});
        Check(second.IsValid(), "the freed slot must be reusable");
        Check(false == (second == stale), "a reused slot must not answer to the old handle");
        probe.device->DestroySampler(second);
        probe.Close();
    }
}

int RunTextureBindingTests()
{
    TestATextureReachesTheShader();
    TestDrawingNeedsEverySlotItDeclared();
    TestTheDescriptorRingRewindsEachFrame();
    TestAFreedSamplerDoesNotComeBack();
    std::cout << "Texture binding tests passed.\n";
    return 0;
}
