#include <JBro/D3D11RHI/D3D11RHI.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Framework3DSystem/Math3DMatrix.h>
#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Types/Array.h>
#include <JBro/VulkanRHI/VulkanRHI.h>

#include "TexturedQuadPS.generated.h"
#include "TexturedQuadVS.generated.h"

namespace Spv
{
#include "TexturedQuadPS_SPV.generated.h"
#include "TexturedQuadVS_SPV.generated.h"
}

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

// 렌더러 벤치마크(D-110). 테스트가 아니라 잣대다 - `JBRO_BENCH=1` 로 실행할 때만 돈다. 세 백엔드에 같은 장면을
// 던져 프레임 벽시계 시간을 잰다(`Immediate` 제시, 1280x720 오프스크린 타깃). 숫자는 기계마다 다르므로 서로
// 비교할 때만 뜻이 있다: 백엔드 사이, 그리고 고치기 전과 뒤.
namespace
{
    constexpr std::uint32_t TargetWidth = 1280;
    constexpr std::uint32_t TargetHeight = 720;
    constexpr int WarmupFrames = 10;
    constexpr int MeasuredFrames = 120;

    struct Timing
    {
        double averageMilliseconds = 0.0;
        double maxMilliseconds = 0.0;
        // 프레임 안의 두 토막: 제출(BeginFrame 뒤 뷰 제출까지)과 기록·제시(EndFrame).
        double submitMilliseconds = 0.0;
        double endFrameMilliseconds = 0.0;
        bool valid = false;
    };

    template <typename TModule>
    struct Bench
    {
        JBro::WindowsPlatform platform;
        TModule rhi;
        JBro::Renderer renderer;
        JBro::WindowHandle window;
        JBro::TextureHandle target;
        bool ready = false;

        bool Open()
        {
            JBro::JMemoryContext memory;
            if (false == platform.Initialize(memory))
            {
                return false;
            }
            if (false == rhi.Initialize(memory))
            {
                platform.Shutdown();
                return false;
            }
            JBro::WindowDesc windowDesc;
            constexpr char title[] = "JBro bench";
            windowDesc.title = {title, sizeof(title) - 1};
            windowDesc.width = 256;
            windowDesc.height = 256;
            windowDesc.visible = false;
            window = platform.OpenPlatformWindow(windowDesc);
            JBro::RendererConfig config;
            config.api = rhi.GetApi();
            config.surface = platform.CreateSurface(window);
            config.surfaceExtent = {256, 256};
            config.presentMode = JBro::PresentMode::Immediate;
            config.maxSpriteSubmissions = 65536;
            config.maxMeshSubmissions = 16384;
            if (false == renderer.Initialize(rhi, config))
            {
                rhi.Shutdown();
                platform.ClosePlatformWindow(window);
                platform.Shutdown();
                return false;
            }
            JBro::TextureDesc targetDesc;
            targetDesc.extent = {TargetWidth, TargetHeight};
            targetDesc.format = JBro::TextureFormat::BGRA8Unorm;
            targetDesc.usage = JBro::TextureUsage::RenderTarget | JBro::TextureUsage::Sampled;
            target = renderer.GetDevice()->CreateTexture(targetDesc);
            ready = target.IsValid();
            return ready;
        }

        void Close()
        {
            if (false == ready)
            {
                return;
            }
            renderer.GetDevice()->WaitIdle();
            renderer.GetDevice()->DestroyTexture(target);
            renderer.Shutdown();
            rhi.Shutdown();
            platform.ClosePlatformWindow(window);
            platform.PumpEvents();
            platform.Shutdown();
            ready = false;
        }

        // `record` 가 한 프레임의 뷰를 제출한다. 프레임 여닫기와 시간 재기는 여기서.
        template <typename TRecord>
        Timing Measure(TRecord&& record)
        {
            Timing timing;
            double total = 0.0;
            double submit = 0.0;
            double end = 0.0;
            using Clock = std::chrono::steady_clock;
            auto millis = [](Clock::time_point from, Clock::time_point to) {
                return std::chrono::duration<double, std::milli>(to - from).count();
            };
            for (int frame = 0; frame < WarmupFrames + MeasuredFrames; ++frame)
            {
                const auto start = Clock::now();
                JBro::FrameTarget frameTarget;
                frameTarget.texture = target;
                frameTarget.extent = {TargetWidth, TargetHeight};
                if (renderer.BeginFrame(frameTarget) != JBro::FrameStatus::Ready || false == record())
                {
                    return timing;
                }
                const auto submitted = Clock::now();
                if (renderer.EndFrame() != JBro::FrameStatus::Ready)
                {
                    return timing;
                }
                const auto finished = Clock::now();
                if (frame >= WarmupFrames)
                {
                    const double milliseconds = millis(start, finished);
                    total += milliseconds;
                    submit += millis(start, submitted);
                    end += millis(submitted, finished);
                    timing.maxMilliseconds = milliseconds > timing.maxMilliseconds ? milliseconds : timing.maxMilliseconds;
                }
            }
            timing.averageMilliseconds = total / MeasuredFrames;
            timing.submitMilliseconds = submit / MeasuredFrames;
            timing.endFrameMilliseconds = end / MeasuredFrames;
            timing.valid = true;
            return timing;
        }
    };

    JBro::CameraParams OrthoCamera()
    {
        JBro::CameraParams camera;
        JBro::MakeOrthographicMatrix(1.0f, static_cast<float>(TargetWidth) / TargetHeight, -10.0f, 10.0f,
            camera.projection);
        camera.viewport.width = static_cast<float>(TargetWidth);
        camera.viewport.height = static_cast<float>(TargetHeight);
        return camera;
    }

    JBro::CameraParams PerspectiveCamera()
    {
        JBro::CameraParams camera;
        JBro::MakePerspectiveMatrix(60.0f * 3.14159265f / 180.0f, static_cast<float>(TargetWidth) / TargetHeight,
            0.1f, 500.0f, camera.projection);
        camera.view = JBro::MakeViewMatrix({0.0f, 0.0f, 60.0f}, {});
        camera.viewport.width = static_cast<float>(TargetWidth);
        camera.viewport.height = static_cast<float>(TargetHeight);
        return camera;
    }

    // 화면에 흩어진 작은 스프라이트 N 개. 겹치지 않아 채우기 비용은 작고, 제출·업로드·인스턴스 경로가 재진다.
    void BuildSprites(JBro::Array<JBro::SpriteSubmit>& sprites, std::uint32_t count)
    {
        sprites.Clear();
        const std::uint32_t columns = static_cast<std::uint32_t>(std::sqrt(static_cast<float>(count))) + 1;
        const float aspect = static_cast<float>(TargetWidth) / TargetHeight;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            JBro::SpriteSubmit sprite;
            const float cell = 2.0f / columns;
            sprite.world.linear[0] = cell * 0.8f * aspect;
            sprite.world.linear[3] = cell * 0.8f;
            sprite.world.translation[0] = (-1.0f + cell * (0.5f + index % columns)) * aspect;
            sprite.world.translation[1] = -1.0f + cell * (0.5f + index / columns);
            sprite.tint[0] = 0.2f + 0.8f * (index % 7) / 7.0f;
            sprite.tint[1] = 0.2f + 0.8f * (index % 5) / 5.0f;
            sprite.tint[2] = 0.5f;
            sprites.Add(sprite);
        }
    }

    // 격자에 놓인 정육면체 N 개. `meshes` 를 번갈아 써서 같은 메시가 이어지는 길이를 조절한다.
    void BuildCubes(JBro::Array<JBro::MeshSubmit>& cubes, std::uint32_t count, const JBro::AssetHandle* meshes,
        std::uint32_t meshCount)
    {
        cubes.Clear();
        const std::uint32_t columns = static_cast<std::uint32_t>(std::sqrt(static_cast<float>(count))) + 1;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            JBro::MeshSubmit cube;
            const float x = (static_cast<float>(index % columns) - columns * 0.5f) * 1.5f;
            const float y = (static_cast<float>(index / columns) - columns * 0.5f) * 1.5f;
            cube.world = JBro::MakeTransformMatrix3D({x, y, 0.0f},
                JBro::FromAxisAngle({0.3f, 1.0f, 0.2f}, 0.01f * index), {1.0f, 1.0f, 1.0f});
            cube.mesh = meshes[index % meshCount];
            cube.tint[0] = 1.0f;
            cube.tint[1] = 0.6f;
            cube.tint[2] = 0.3f;
            cubes.Add(cube);
        }
    }

    struct Vertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
    };

    // 텍스처 셰이더는 DXIL 과 SPIR-V 로만 구웠다. D3D11 은 SM 5.0 헤더가 없어 그 장면을 건너뛴다.
    template <typename TModule>
    constexpr bool HasTexturedShader()
    {
        return false == std::is_same_v<TModule, JBro::D3D11RHIModule>;
    }

    template <typename TModule>
    constexpr bool IsVulkan()
    {
        return std::is_same_v<TModule, JBro::VulkanRHIModule>;
    }

    void Print(const char* backend, const char* scene, const Timing& timing)
    {
        if (timing.valid)
        {
            std::printf("  %-7s %-46s avg %7.3f ms  (submit %6.3f, end %6.3f)  max %7.3f ms\n", backend, scene,
                timing.averageMilliseconds, timing.submitMilliseconds, timing.endFrameMilliseconds,
                timing.maxMilliseconds);
        }
        else
        {
            std::printf("  %-7s %-46s (failed)\n", backend, scene);
        }
    }

    template <typename TModule>
    void RunBackend(const char* name)
    {
        Bench<TModule> bench;
        if (false == bench.Open())
        {
            std::printf("  %-7s (no device)\n", name);
            return;
        }
        JBro::Renderer& renderer = bench.renderer;

        // 0. 빈 프레임 - 뷰 하나에 클리어만. 프레임 하나의 고정 비용(획득·제출·제시·펜스)이다.
        {
            const JBro::CameraParams camera = OrthoCamera();
            Print(name, "empty frame (clear only)", bench.Measure([&]() {
                return renderer.BeginView(camera) && renderer.EndView();
            }));
        }

        // 1. 스프라이트 20000 개, 뷰 하나.
        {
            JBro::Array<JBro::SpriteSubmit> sprites;
            BuildSprites(sprites, 60000);
            const JBro::CameraParams camera = OrthoCamera();
            Print(name, "sprites 60000", bench.Measure([&]() {
                if (false == renderer.BeginView(camera))
                {
                    return false;
                }
                constexpr std::uint32_t Batch = 64;
                for (std::uint32_t offset = 0; offset < sprites.Size(); offset += Batch)
                {
                    const std::uint32_t count = static_cast<std::uint32_t>(
                        (std::min)(static_cast<std::size_t>(Batch), sprites.Size() - offset));
                    if (false == renderer.SubmitSprites({sprites.Data() + offset, count}))
                    {
                        return false;
                    }
                }
                return renderer.EndView();
            }));
        }

        // 1b. 같은 60000 개에 텍스처 둘을 100 개마다 번갈아 - 묶음 600 개. 텍스처 바인딩과 묶기의 값이다(D-113).
        {
            const std::byte texels[16] = {
                std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255},
                std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
            const JBro::AssetHandle textures[2] = {
                renderer.RegisterTexture({2, 2}, {texels, 16}), renderer.RegisterTexture({2, 2}, {texels, 16})};
            JBro::Array<JBro::SpriteSubmit> sprites;
            BuildSprites(sprites, 60000);
            for (std::uint32_t index = 0; index < sprites.Size(); ++index)
            {
                sprites[index].texture = textures[(index / 100) % 2];
            }
            const JBro::CameraParams camera = OrthoCamera();
            Print(name, "sprites 60000 textured, 600 runs", bench.Measure([&]() {
                if (false == renderer.BeginView(camera))
                {
                    return false;
                }
                constexpr std::uint32_t Batch = 64;
                for (std::uint32_t offset = 0; offset < sprites.Size(); offset += Batch)
                {
                    const std::uint32_t count = static_cast<std::uint32_t>(
                        (std::min)(static_cast<std::size_t>(Batch), sprites.Size() - offset));
                    if (false == renderer.SubmitSprites({sprites.Data() + offset, count}))
                    {
                        return false;
                    }
                }
                return renderer.EndView();
            }));
            renderer.UnregisterTexture(textures[0]);
            renderer.UnregisterTexture(textures[1]);
        }

        // 2·3. 정육면체 4096 개 - 메시 하나 / 메시 넷을 번갈아.
        {
            JBro::MeshLibrary library;
            library.Initialize(&renderer);
            JBro::AssetHandle meshes[4];
            meshes[0] = library.Resolve(JBro::MeshLibrary::BuiltinCubeId());
            // 같은 정육면체를 세 번 더 등록해 "다른 메시" 를 흉내 낸다. 정점은 같고 핸들만 다르다.
            const JBro::MeshVertex vertices[3] = {
                {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}, {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
                {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}};
            const std::uint32_t indices[3] = {0, 2, 1};
            for (int extra = 1; extra < 4; ++extra)
            {
                meshes[extra] = renderer.RegisterMesh({vertices, 3}, {indices, 3});
            }
            const JBro::CameraParams camera = PerspectiveCamera();
            JBro::Array<JBro::MeshSubmit> cubes;
            auto record = [&]() {
                if (false == renderer.BeginView(camera))
                {
                    return false;
                }
                constexpr std::uint32_t Batch = 64;
                for (std::uint32_t offset = 0; offset < cubes.Size(); offset += Batch)
                {
                    const std::uint32_t count = static_cast<std::uint32_t>(
                        (std::min)(static_cast<std::size_t>(Batch), cubes.Size() - offset));
                    if (false == renderer.SubmitMeshes({cubes.Data() + offset, count}))
                    {
                        return false;
                    }
                }
                return renderer.EndView();
            };
            BuildCubes(cubes, 16000, meshes, 1);
            Print(name, "meshes 16000 (one mesh)", bench.Measure(record));
            BuildCubes(cubes, 16000, meshes, 4);
            Print(name, "meshes 16000 (four meshes interleaved)", bench.Measure(record));
            for (int extra = 1; extra < 4; ++extra)
            {
                renderer.UnregisterMesh(meshes[extra]);
            }
            library.Shutdown();
        }

        // 4. 텍스처를 번갈아 묶는 드로우 2000 개 - RHI 를 직접 부른다. 디스크립터 경로가 재진다.
        //    D3D11 은 이 셰이더의 DXBC 가 없어 건너뛴다.
        if (HasTexturedShader<TModule>())
        {
            JBro::IRHIDevice* device = renderer.GetDevice();
            JBro::TextureDesc textureDesc;
            textureDesc.extent = {2, 2};
            textureDesc.format = JBro::TextureFormat::RGBA8Unorm;
            textureDesc.usage = JBro::TextureUsage::Sampled | JBro::TextureUsage::CopyDestination;
            const unsigned char texelsA[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
            const unsigned char texelsB[16] = {0, 0, 0, 255, 40, 40, 40, 255, 80, 80, 80, 255, 120, 120, 120, 255};
            const JBro::TextureHandle textures[2] = {device->CreateTexture(textureDesc), device->CreateTexture(textureDesc)};
            device->WriteTexture(textures[0], 0, {reinterpret_cast<const std::byte*>(texelsA), 16});
            device->WriteTexture(textures[1], 0, {reinterpret_cast<const std::byte*>(texelsB), 16});
            JBro::SamplerDesc samplerDesc;
            samplerDesc.minFilter = JBro::FilterMode::Nearest;
            samplerDesc.magFilter = JBro::FilterMode::Nearest;
            const JBro::SamplerHandle sampler = device->CreateSampler(samplerDesc);
            // 화면 구석의 작은 사각형 하나. 채우기가 아니라 바인딩을 재는 것이다.
            const Vertex quad[4] = {{-1.0f, 1.0f, 0.0f, 0.0f}, {-0.9f, 1.0f, 1.0f, 0.0f}, {-0.9f, 0.9f, 1.0f, 1.0f},
                {-1.0f, 0.9f, 0.0f, 1.0f}};
            const std::uint16_t quadIndices[6] = {0, 1, 2, 0, 2, 3};
            JBro::BufferDesc vertexDesc;
            vertexDesc.size = sizeof(quad);
            vertexDesc.usage = JBro::BufferUsage::Vertex;
            vertexDesc.memory = JBro::MemoryType::Upload;
            const JBro::BufferHandle vertexBuffer = device->CreateBuffer(vertexDesc);
            device->WriteBuffer(vertexBuffer, 0, {reinterpret_cast<const std::byte*>(quad), sizeof(quad)});
            JBro::BufferDesc indexDesc;
            indexDesc.size = sizeof(quadIndices);
            indexDesc.usage = JBro::BufferUsage::Index;
            indexDesc.memory = JBro::MemoryType::Upload;
            const JBro::BufferHandle indexBuffer = device->CreateBuffer(indexDesc);
            device->WriteBuffer(indexBuffer, 0, {reinterpret_cast<const std::byte*>(quadIndices), sizeof(quadIndices)});
            const JBro::VertexAttributeDesc attributes[] = {{0, 0, JBro::VertexFormat::Float2}, {1, 8, JBro::VertexFormat::Float2}};
            const JBro::VertexBufferLayoutDesc layout = {sizeof(Vertex), JBro::VertexStepMode::Vertex, {attributes, 2}};
            const JBro::TextureFormat colorFormats[] = {JBro::TextureFormat::BGRA8Unorm};
            JBro::GraphicsPipelineDesc pipelineDesc;
            if (IsVulkan<TModule>())
            {
                pipelineDesc.vertexShader = {Spv::JBroTestTexturedQuadVS_SPV, sizeof(Spv::JBroTestTexturedQuadVS_SPV)};
                pipelineDesc.pixelShader = {Spv::JBroTestTexturedQuadPS_SPV, sizeof(Spv::JBroTestTexturedQuadPS_SPV)};
            }
            else
            {
                pipelineDesc.vertexShader = {JBroTestTexturedQuadVS, sizeof(JBroTestTexturedQuadVS)};
                pipelineDesc.pixelShader = {JBroTestTexturedQuadPS, sizeof(JBroTestTexturedQuadPS)};
            }
            pipelineDesc.vertexBuffers = {&layout, 1};
            pipelineDesc.colorFormats = {colorFormats, 1};
            pipelineDesc.cull = JBro::CullMode::None;
            pipelineDesc.sampledTextureCount = 1;
            pipelineDesc.samplerCount = 1;
            const JBro::GraphicsPipelineHandle pipeline = device->CreateGraphicsPipeline(pipelineDesc);

            // 오버레이로 그린다 - 렌더러의 뷰 기록 뒤, 프레임 안에서 컨텍스트를 받는 유일한 길이다.
            struct Overlay
            {
                JBro::GraphicsPipelineHandle pipeline;
                JBro::BufferHandle vertexBuffer;
                JBro::BufferHandle indexBuffer;
                JBro::TextureHandle textures[2];
                JBro::SamplerHandle sampler;
                JBro::TextureHandle target;
                bool alternate = false;
            } overlay = {pipeline, vertexBuffer, indexBuffer, {textures[0], textures[1]}, sampler, bench.target, false};
            renderer.SetFrameOverlay(
                [](JBro::IRHICommandContext& commands, JBro::TextureHandle, std::uint32_t, void* user) {
                    const Overlay& o = *static_cast<const Overlay*>(user);
                    JBro::ColorAttachmentDesc attachment;
                    attachment.texture = o.target;
                    attachment.loadOperation = JBro::LoadOperation::Clear;
                    JBro::RenderPassDesc pass;
                    pass.colorAttachments = {&attachment, 1};
                    if (false == commands.BeginRenderPass(pass) || false == commands.SetGraphicsPipeline(o.pipeline)
                        || false == commands.SetVertexBuffer(0, o.vertexBuffer, sizeof(Vertex), 0)
                        || false == commands.SetIndexBuffer(o.indexBuffer, JBro::IndexFormat::UInt16, 0))
                    {
                        return false;
                    }
                    JBro::Viewport viewport;
                    viewport.width = static_cast<float>(TargetWidth);
                    viewport.height = static_cast<float>(TargetHeight);
                    commands.SetViewport(viewport);
                    commands.SetScissor({0, 0, static_cast<std::int32_t>(TargetWidth), static_cast<std::int32_t>(TargetHeight)});
                    for (std::uint32_t draw = 0; draw < 1000; ++draw)
                    {
                        if (false == commands.SetTexture(0, o.textures[o.alternate ? draw & 1 : 0])
                            || false == commands.SetSampler(0, o.sampler)
                            || false == commands.DrawIndexedInstanced(6, 1, 0, 0, 0))
                        {
                            return false;
                        }
                    }
                    commands.EndRenderPass();
                    return true;
                },
                &overlay);
            Print(name, "textured draws 1000 (same texture)", bench.Measure([&]() { return true; }));
            overlay.alternate = true;
            Print(name, "textured draws 1000 (two textures alternating)", bench.Measure([&]() { return true; }));
            renderer.SetFrameOverlay(nullptr, nullptr);
            device->WaitIdle();
            device->DestroyGraphicsPipeline(pipeline);
            device->DestroyBuffer(indexBuffer);
            device->DestroyBuffer(vertexBuffer);
            device->DestroySampler(sampler);
            device->DestroyTexture(textures[0]);
            device->DestroyTexture(textures[1]);
        }
        bench.Close();
    }
}

int RunRendererBenchmark()
{
    std::printf("renderer benchmark: %dx%d offscreen target, %d frames after %d warm-up, immediate present\n",
        TargetWidth, TargetHeight, MeasuredFrames, WarmupFrames);
    RunBackend<JBro::D3D12RHIModule>("D3D12");
    RunBackend<JBro::D3D11RHIModule>("D3D11");
    RunBackend<JBro::VulkanRHIModule>("Vulkan");
    return 0;
}
