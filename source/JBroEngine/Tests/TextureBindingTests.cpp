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

    // 텍스처 바인딩이 실제로 GPU 까지 닿는지는 픽셀을 되읽지 않고는 증명할 수 없다.
    // 디스크립터를 엉뚱한 자리에 복사해도, 샘플러를 안 묶어도, D3D12 는 대개
    // 아무 말도 하지 않고 색만 조용히 달라진다.
    //
    // 2x2 텍스처를 만들고 네 텍셀에 서로 다른 색을 넣는다. 화면을 가득 채운 사각형에
    // 그것을 입히고 네 모서리를 읽으면, 어느 텍셀이 어디로 갔는지까지 드러난다.
    void TestATextureReachesTheShader()
    {
        JBro::WindowsPlatform platform;
        JBro::D3D12RHIModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no D3D12 module; texture binding not verified" << std::endl;
            platform.Shutdown();
            return;
        }

        JBro::IRHIDevice* device = rhi.CreateDevice({});
        if (device == nullptr)
        {
            std::cout << "  [skip] no D3D12 device; texture binding not verified" << std::endl;
            rhi.Shutdown();
            platform.Shutdown();
            return;
        }

        constexpr std::uint32_t Size = 64;
        JBro::WindowDesc windowDesc;
        constexpr char title[] = "JBro texture probe";
        windowDesc.title = {title, sizeof(title) - 1};
        windowDesc.width = Size;
        windowDesc.height = Size;
        windowDesc.visible = false;
        const JBro::WindowHandle window = platform.OpenPlatformWindow(windowDesc);
        Check(window.value != 0, "the probe window must open");

        JBro::SwapchainDesc swapchainDesc;
        swapchainDesc.surface = platform.CreateSurface(window);
        swapchainDesc.extent = {Size, Size};
        swapchainDesc.presentMode = JBro::PresentMode::Immediate;
        const JBro::SwapchainHandle swapchain = device->CreateSwapchain(swapchainDesc);
        Check(swapchain.IsValid(), "the probe swapchain must be created");

        // ---- 텍스처 -------------------------------------------------------
        JBro::TextureDesc textureDesc;
        textureDesc.extent = {2, 2};
        textureDesc.format = JBro::TextureFormat::RGBA8Unorm;
        textureDesc.usage = JBro::TextureUsage::Sampled | JBro::TextureUsage::CopyDestination;
        const JBro::TextureHandle texture = device->CreateTexture(textureDesc);
        Check(texture.IsValid(), "a sampled texture must be created");

        // 좌상 빨강, 우상 초록, 좌하 파랑, 우하 하양. 넷이 모두 다르므로
        // 뒤집히거나 밀린 것도 드러난다.
        const unsigned char texels[] = {
            255, 0, 0, 255,    0, 255, 0, 255,
            0, 0, 255, 255,    255, 255, 255, 255
        };
        Check(device->WriteTexture(texture, 0,
            {reinterpret_cast<const std::byte*>(texels), sizeof(texels)}),
            "the texels must upload");

        // 밉 하나짜리만 올린다는 계약이다.
        Check(false == device->WriteTexture(texture, 1,
            {reinterpret_cast<const std::byte*>(texels), sizeof(texels)}),
            "a mip this engine does not upload must be refused");
        // 크기가 맞지 않는 것도 거절한다. 모자란 것을 받으면 나머지가 쓰레기가 된다.
        Check(false == device->WriteTexture(texture, 0,
            {reinterpret_cast<const std::byte*>(texels), sizeof(texels) - 4}),
            "a short upload must be refused rather than padded");

        JBro::SamplerDesc samplerDesc;
        // 텍셀을 그대로 집는다. 선형으로 섞으면 모서리 값이 흐려져 무엇이 어디 있는지
        // 읽을 수 없다.
        samplerDesc.minFilter = JBro::FilterMode::Nearest;
        samplerDesc.magFilter = JBro::FilterMode::Nearest;
        const JBro::SamplerHandle sampler = device->CreateSampler(samplerDesc);
        Check(sampler.IsValid(), "a sampler must be created");

        // ---- 기하 ---------------------------------------------------------
        const Vertex vertices[] = {
            {-1.0f,  1.0f, 0.0f, 0.0f},
            { 1.0f,  1.0f, 1.0f, 0.0f},
            { 1.0f, -1.0f, 1.0f, 1.0f},
            {-1.0f, -1.0f, 0.0f, 1.0f},
        };
        const std::uint16_t indices[] = {0, 1, 2, 0, 2, 3};

        JBro::BufferDesc vertexDesc;
        vertexDesc.size = sizeof(vertices);
        vertexDesc.usage = JBro::BufferUsage::Vertex;
        vertexDesc.memory = JBro::MemoryType::Upload;
        const JBro::BufferHandle vertexBuffer = device->CreateBuffer(vertexDesc);
        Check(vertexBuffer.IsValid(), "the vertex buffer must be created");
        Check(device->WriteBuffer(vertexBuffer, 0,
            {reinterpret_cast<const std::byte*>(vertices), sizeof(vertices)}),
            "the vertices must upload");

        JBro::BufferDesc indexDesc;
        indexDesc.size = sizeof(indices);
        indexDesc.usage = JBro::BufferUsage::Index;
        indexDesc.memory = JBro::MemoryType::Upload;
        const JBro::BufferHandle indexBuffer = device->CreateBuffer(indexDesc);
        Check(indexBuffer.IsValid(), "the index buffer must be created");
        Check(device->WriteBuffer(indexBuffer, 0,
            {reinterpret_cast<const std::byte*>(indices), sizeof(indices)}),
            "the indices must upload");

        // ---- 파이프라인 ---------------------------------------------------
        const JBro::VertexAttributeDesc attributes[] = {
            {0, 0, JBro::VertexFormat::Float2},
            {1, 8, JBro::VertexFormat::Float2},
        };
        JBro::VertexBufferLayoutDesc layout;
        layout.stride = sizeof(Vertex);
        layout.attributes = {attributes, 2};
        const JBro::TextureFormat colorFormats[] = {JBro::TextureFormat::BGRA8Unorm};

        JBro::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = {JBroTestTexturedQuadVS, sizeof(JBroTestTexturedQuadVS)};
        pipelineDesc.pixelShader = {JBroTestTexturedQuadPS, sizeof(JBroTestTexturedQuadPS)};
        pipelineDesc.vertexBuffers = {&layout, 1};
        pipelineDesc.colorFormats = {colorFormats, 1};
        pipelineDesc.cull = JBro::CullMode::None;
        pipelineDesc.sampledTextureCount = 1;
        pipelineDesc.samplerCount = 1;
        const JBro::GraphicsPipelineHandle pipeline =
            device->CreateGraphicsPipeline(pipelineDesc);
        Check(pipeline.IsValid(), "the textured pipeline must be created");

        // 선언한 수를 넘는 파이프라인은 거절한다. 루트 시그니처는 만들고 나면 못 바꾸므로
        // 여기서 막지 않으면 그리는 자리에서 조용히 잘린다.
        JBro::GraphicsPipelineDesc tooMany = pipelineDesc;
        tooMany.sampledTextureCount = 64;
        Check(false == device->CreateGraphicsPipeline(tooMany).IsValid(),
            "a pipeline asking for more textures than the backend binds must be refused");

        // ---- 그리기 -------------------------------------------------------
        const JBro::BeginFrameResult begun = device->BeginFrame(swapchain);
        Check(begun.status == JBro::FrameStatus::Ready, "the probe frame must begin");
        JBro::IRHICommandContext& commands = *begun.frame.commands;

        // 프레임 안에서는 올리지 않는다. 여기서 GPU 를 기다리면 프레임이 막힌다.
        Check(false == device->WriteTexture(texture, 0,
            {reinterpret_cast<const std::byte*>(texels), sizeof(texels)}),
            "uploading inside a frame must be refused");

        JBro::ColorAttachmentDesc attachment;
        attachment.texture = begun.frame.backBuffer;
        attachment.loadOperation = JBro::LoadOperation::Clear;
        attachment.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        JBro::RenderPassDesc pass;
        pass.colorAttachments = {&attachment, 1};
        Check(commands.BeginRenderPass(pass), "the probe render pass must begin");

        JBro::Viewport viewport;
        viewport.width = static_cast<float>(Size);
        viewport.height = static_cast<float>(Size);
        commands.SetViewport(viewport);
        commands.SetScissor({0, 0, static_cast<std::int32_t>(Size), static_cast<std::int32_t>(Size)});

        Check(commands.SetGraphicsPipeline(pipeline), "the pipeline must bind");

        // 파이프라인이 선언하지 않은 자리는 거절한다.
        Check(false == commands.SetTexture(1, texture),
            "a slot the pipeline never declared must be refused");
        Check(false == commands.SetSampler(1, sampler),
            "a sampler slot the pipeline never declared must be refused");

        Check(commands.SetTexture(0, texture), "the texture must bind");
        Check(commands.SetSampler(0, sampler), "the sampler must bind");
        Check(commands.SetVertexBuffer(0, vertexBuffer, sizeof(Vertex), 0),
            "the vertices must bind");
        Check(commands.SetIndexBuffer(indexBuffer, JBro::IndexFormat::UInt16, 0),
            "the indices must bind");
        Check(commands.DrawIndexedInstanced(6, 1, 0, 0, 0), "the quad must draw");

        commands.EndRenderPass();
        Check(device->EndFrame(begun.frame) == JBro::FrameStatus::Ready,
            "the probe frame must present");

        // ---- 되읽기 -------------------------------------------------------
        JBro::Array<std::byte> image;
        image.Resize(Size * Size * 4);
        JBro::TextureReadback readback;
        Check(device->ReadTexture(begun.frame.backBuffer, image.Data(), image.Size(), readback),
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

        device->DestroySampler(sampler);
        device->DestroyTexture(texture);
        device->DestroyBuffer(indexBuffer);
        device->DestroyBuffer(vertexBuffer);
        device->DestroyGraphicsPipeline(pipeline);
        device->DestroySwapchain(swapchain);
        rhi.DestroyDevice(device);
        platform.ClosePlatformWindow(window);
        rhi.Shutdown();
        platform.Shutdown();
    }

    void TestBindingRefusesWhatItCannotResolve()
    {
        JBro::WindowsPlatform platform;
        JBro::D3D12RHIModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no D3D12 module; refusals not verified" << std::endl;
            platform.Shutdown();
            return;
        }
        JBro::IRHIDevice* device = rhi.CreateDevice({});
        if (device == nullptr)
        {
            std::cout << "  [skip] no D3D12 device; refusals not verified" << std::endl;
            rhi.Shutdown();
            platform.Shutdown();
            return;
        }

        // Sampled 없이 만든 텍스처는 셰이더가 읽을 수 없다. 조용히 빈 것을 묶으면
        // 무엇을 읽는지 알 수 없게 된다.
        JBro::TextureDesc renderTargetOnly;
        renderTargetOnly.extent = {4, 4};
        renderTargetOnly.format = JBro::TextureFormat::RGBA8Unorm;
        renderTargetOnly.usage = JBro::TextureUsage::RenderTarget;
        const JBro::TextureHandle notSampled = device->CreateTexture(renderTargetOnly);
        Check(notSampled.IsValid(), "a render target must still be creatable");

        JBro::TextureDesc sampled;
        sampled.extent = {2, 2};
        sampled.format = JBro::TextureFormat::RGBA8Unorm;
        sampled.usage = JBro::TextureUsage::Sampled | JBro::TextureUsage::CopyDestination;
        const JBro::TextureHandle texture = device->CreateTexture(sampled);
        Check(texture.IsValid(), "a sampled texture must be creatable");

        const JBro::SamplerHandle sampler = device->CreateSampler({});
        Check(sampler.IsValid(), "a sampler must be creatable");

        // 파괴한 뒤의 핸들은 세대가 어긋나므로 되살아나지 않는다.
        const JBro::SamplerHandle stale = sampler;
        device->DestroySampler(sampler);
        const JBro::SamplerHandle second = device->CreateSampler({});
        Check(second.IsValid(), "the freed slot must be reusable");
        Check(false == (second == stale), "a reused slot must not answer to the old handle");

        device->DestroySampler(second);
        device->DestroyTexture(texture);
        device->DestroyTexture(notSampled);
        rhi.DestroyDevice(device);
        rhi.Shutdown();
        platform.Shutdown();
    }
}

int RunTextureBindingTests()
{
    TestATextureReachesTheShader();
    TestBindingRefusesWhatItCannotResolve();
    std::cout << "Texture binding tests passed.\n";
    return 0;
}
