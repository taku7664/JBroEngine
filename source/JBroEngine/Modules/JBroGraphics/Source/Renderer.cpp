#include <JBro/Graphics/Renderer.h>

// 빌트인 셰이더는 매니페스트(Shaders/ShaderManifest.psd1) 한 곳에서 굽고, 바이트코드 셋과 자리표를 표 하나로 받는다(D-329).
#include <JBro/Graphics/BuiltinShaders.generated.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        // API 마다 읽는 바이트코드가 다르다. D3D12 는 DXIL, D3D11 은 DXBC, Vulkan 은 SPIR-V 다.
        std::uint8_t ToUnorm8(Float value) noexcept
        {
            const Float clamped = value < 0.0f ? Float(0.0f) : (value > 1.0f ? Float(1.0f) : value);
            return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
        }

        std::uint16_t ToUnorm16(Float value) noexcept
        {
            const Float clamped = value < 0.0f ? Float(0.0f) : (value > 1.0f ? Float(1.0f) : value);
            return static_cast<std::uint16_t>(clamped * 65535.0f + 0.5f);
        }

        static_assert(ShaderStageMask::Vertex == static_cast<std::uint8_t>(ShaderStage::Vertex)
                && ShaderStageMask::Pixel == static_cast<std::uint8_t>(ShaderStage::Pixel)
                && ShaderStageMask::Compute == static_cast<std::uint8_t>(ShaderStage::Compute),
            "the asset's stage bits are the RHI's");

        BindingRange ToBindingRange(const ShaderResourceRange& range)
        {
            return {range.count, static_cast<ShaderStage>(range.stageMask)};
        }

        // 빌트인 프로그램의 셰이더 둘·푸시 상수·자리표를 파이프라인 서술자에 옮긴다(D-329). 나머지(포맷·블렌드·정점 배치)는
        // 부르는 쪽이 정한다. 그 API 의 바이트코드가 없거나, 렌더러가 올리는 푸시 상수 크기가 매니페스트와 다르면 거짓이다 -
        // 매니페스트를 고치고 C++ 구조체를 잊은 것이 그리기 자리가 아니라 여기서 드러난다.
        Bool ApplyShader(GraphicsPipelineDesc& desc, BuiltinShader program, GraphicsApi api, std::size_t pushConstantBytes)
        {
            const ShaderData& shader = GetBuiltinShader(program);
            const ShaderCodeFormat format = api == GraphicsApi::D3D11 ? ShaderCodeFormat::Dxbc
                : api == GraphicsApi::Vulkan                       ? ShaderCodeFormat::SpirV
                                                                   : ShaderCodeFormat::Dxil;
            const JArrayView<std::byte>& vertex = shader.Code(ShaderProgramStage::Vertex, format);
            const JArrayView<std::byte>& pixel = shader.Code(ShaderProgramStage::Pixel, format);
            if (vertex.size == 0 || pixel.size == 0 || shader.pushConstantBytes != pushConstantBytes)
            {
                return false;
            }
            desc.vertexShader = {vertex.data, vertex.size};
            desc.pixelShader = {pixel.data, pixel.size};
            desc.pushConstantBytes = shader.pushConstantBytes;
            if (shader.pushConstantStageMask != 0)
            {
                desc.pushConstantStages = static_cast<ShaderStage>(shader.pushConstantStageMask);
            }
            desc.bindings.constantBuffers = ToBindingRange(shader.constantBuffers);
            desc.bindings.sampledTextures = ToBindingRange(shader.sampledTextures);
            desc.bindings.samplers = ToBindingRange(shader.samplers);
            desc.bindings.storageTextures = ToBindingRange(shader.storageTextures);
            desc.bindings.storageBuffers = ToBindingRange(shader.storageBuffers);
            return true;
        }

        Matrix4x4 Multiply(const Matrix4x4& left, const Matrix4x4& right)
        {
            Matrix4x4 result;
            for (UInt32 row = 0; row < 4; ++row)
            {
                for (UInt32 column = 0; column < 4; ++column)
                {
                    Float value = 0.0f;
                    for (UInt32 element = 0; element < 4; ++element)
                    {
                        value += left.values[row * 4 + element]
                            * right.values[element * 4 + column];
                    }
                    result.values[row * 4 + column] = value;
                }
            }
            return result;
        }
    }

    Renderer::~Renderer()
    {
        Shutdown();
    }

    Bool Renderer::Initialize(IRHIModule& rhi, const RendererConfig& config)
    {
        if (m_device != nullptr
            || rhi.GetApi() != config.api
            || config.surface.value == 0
            || config.surfaceExtent.width == 0
            || config.surfaceExtent.height == 0
            || config.surfaceExtent.width > static_cast<JBro::UInt32>((std::numeric_limits<std::int32_t>::max)())
            || config.surfaceExtent.height > static_cast<JBro::UInt32>((std::numeric_limits<std::int32_t>::max)())
            || config.backBufferCount < 2
            || config.maxFramesInFlight == 0
            || config.maxFramesInFlight >= config.backBufferCount
            || config.maxFramesInFlight > MaxFrameSlots
            || config.maxViews == 0)
        {
            return false;
        }

        try
        {
            m_views.Reserve(config.maxViews);
            m_sprites.Reserve(config.maxSpriteSubmissions);
            m_meshes.Reserve(config.maxMeshSubmissions);
            m_worldTexts.Reserve(config.maxWorldTextSubmissions);
            m_worldTextRuns.Reserve(config.maxWorldTextSubmissions);
            m_gpuWorldTextInstances.Resize(config.maxWorldTextSubmissions);
            m_gpuSpriteInstances.Reserve(config.maxSpriteSubmissions);
            m_gpuMeshInstances.Reserve(config.maxMeshSubmissions);
            m_meshRuns.Reserve(config.maxMeshSubmissions);
            // 묶음은 많아도 스프라이트 수를 넘지 않는다. 프레임 안에서 자라지 않게 여기서 잡는다.
            m_spriteRuns.Reserve(config.maxSpriteSubmissions);
            // 레이어 묶음도 프레임 안에서 자라지 않는다. 넘치면 묶지 않고 센다.
            m_layerGroups.Reserve(config.maxLayerGroups);
            // 라이트와 빛을 받는 구간도 프레임 안에서 자라지 않는다(D-291). 넘치는 라이트는 버리고 센다.
            m_lights.Reserve(config.maxLights2D);
            m_gpuLightInstances.Resize(config.maxLights2D);
            m_shadowedLights.Reserve(config.maxLights2D);
            m_shadowEdges.Reserve(config.maxShadowEdges2D);
            m_gpuShadowEdges.Resize(config.maxShadowEdges2D);
            m_litRanges.Reserve(config.maxLayerGroups);
            m_textureResources.Reserve(64);
            // 후처리 효과와 블록도 프레임 안에서 자라지 않는다(D-342). 넘치는 효과는 버리고 센다.
            m_postEntries.Reserve(config.maxPostProcessEntries);
            m_postBlocks.Resize(config.postProcessBlockBytes);
            m_gpuSpriteInstances.Resize(config.maxSpriteSubmissions);
            m_gpuTextInstances.Resize(config.maxSpriteSubmissions);
            m_gpuMeshInstances.Resize(config.maxMeshSubmissions);
            // 메시 슬롯 수만큼 필요하다. 등록할 때 함께 자라므로 프레임 안에서는 할당하지 않는다.
            m_meshHistogram.Reserve(64);
            m_meshResources.Reserve(64);
            // **렌더 그래프의 저장소도 상한만큼 여기서 잡는다**(D-339). 뷰마다 패스가 여덟을 넘지 않고(라이트맵·본 패스·통째 얹기의 넷·
            // 외곽선 둘), 레이어 묶음마다 다섯(그리기·대신 그리기·복사·얹기·뒤 구간), 그림자를 드리우는 라이트마다 셋이다.
            // 후처리는 뷰마다 장면 사본 둘과 넣어 준 톤맵 하나, 효과마다 패스와 중간 텍스처 하나다(D-342).
            RenderGraphLimits limits;
            limits.maxPasses = config.maxViews * 11 + config.maxLayerGroups * 5 + config.maxLights2D * 3 + config.maxPostProcessEntries + 16;
            limits.maxTextures =
                config.maxViews * 11 + config.maxLayerGroups * 2 + config.maxLights2D + config.maxPostProcessEntries + 8;
            limits.maxAccesses = limits.maxPasses * 4;
            limits.dataBytes = static_cast<std::size_t>(limits.maxPasses.Get()) * 128;
            if (false == m_graph.Initialize(limits))
            {
                return false;
            }
            m_graphViews.Reserve(config.maxViews);
        }
        catch (const std::bad_alloc&)
        {
            m_views = {};
            m_sprites = {};
            m_meshes = {};
            m_gpuSpriteInstances = {};
            return false;
        }

        RHIDeviceCreateInfo createInfo;
        createInfo.enableValidation = config.validation;

        IRHIDevice* device = rhi.CreateDevice(createInfo);
        if (device == nullptr)
        {
            m_views = {};
            m_sprites = {};
            m_meshes = {};
            m_gpuSpriteInstances = {};
            return false;
        }

        SwapchainDesc swapchainDesc;
        swapchainDesc.surface = config.surface;
        swapchainDesc.extent = config.surfaceExtent;
        swapchainDesc.format = config.backBufferFormat;
        swapchainDesc.presentMode = config.presentMode;
        swapchainDesc.bufferCount = config.backBufferCount;
        swapchainDesc.maxFramesInFlight = config.maxFramesInFlight;

        const SwapchainHandle swapchain = device->CreateSwapchain(swapchainDesc);
        if (false == swapchain.IsValid())
        {
            rhi.DestroyDevice(device);
            m_views = {};
            m_sprites = {};
            m_meshes = {};
            m_gpuSpriteInstances = {};
            return false;
        }

        m_config = config;
        m_rhi = &rhi;
        m_device = device;
        m_swapchain = swapchain;
        m_targetPool.Initialize(*device);
        if (false == CreateBuiltinSpriteResources() || false == CreateBuiltinMeshResources()
            || false == CreateBuiltinWorldTextResources() || false == CreateBuiltinLightResources()
            || false == CreateScenePipelines(config.backBufferFormat, m_scene[LdrScene]))
        {
            Shutdown();
            return false;
        }
        ResetSubmissionStorage();
        m_lastStats = {};
        return true;
    }

    void Renderer::Shutdown()
    {
        if (m_frameActive && m_device != nullptr)
        {
            m_device->AbortFrame(m_frame);
        }

        if (m_device != nullptr && m_rhi != nullptr)
        {
            m_device->WaitIdle();
            DestroyMeshResources();
            DestroyTextureResources();
            m_targetPool.Shutdown();
            if (m_captureTexture.IsValid())
            {
                m_device->DestroyTexture(m_captureTexture);
            }
            DestroyPostProcessResources();
            DestroyScenePipelines(m_scene[LdrScene]);
            DestroyBuiltinLightResources();
            DestroyBuiltinWorldTextResources();
            DestroyBuiltinMeshResources();
            DestroyBuiltinSpriteResources();
            if (m_swapchain.IsValid())
            {
                m_device->DestroySwapchain(m_swapchain);
            }
            m_rhi->DestroyDevice(m_device);
        }

        m_config = {};
        m_swapchain = {};
        m_frame = {};
        m_frameTarget = {};
        m_views = {};
        m_sprites = {};
        m_meshes = {};
        m_gpuSpriteInstances = {};
        m_gpuMeshInstances = {};
        m_worldTexts = {};
        m_gpuWorldTextInstances = {};
        m_worldTextRuns = {};
        m_gpuWorldTextCount = 0;
        m_gpuSpriteCount = 0;
        m_gpuMeshCount = 0;
        m_meshRuns = {};
        m_layerGroups = {};
        m_openLayerGroup = NoLayerGroup;
        m_lights = {};
        m_gpuLightInstances = {};
        m_shadowedLights = {};
        m_shadowEdges = {};
        m_gpuShadowEdges = {};
        m_litRanges = {};
        m_openLitRange = NoLayerGroup;
        m_graph.Shutdown();
        m_graphViews = {};
        m_graphSerial = 0;
        m_timingCount = 0;
        m_timingSerial = 0;
        m_timingRhiSerial = 0;
        m_timingEnabled = false;
        for (TimingFrame& frame : m_timingRing)
        {
            frame.rhiSerial = 0;
            frame.count = 0;
        }
        m_captureRequested = false;
        m_captureStatus = RenderGraphCaptureStatus::None;
        m_captureTexture = {};
        m_captureWanted = false;
        m_postEntries = {};
        m_postBlocks = {};
        m_postReady = false;
        m_postWanted = false;
        m_postFailed = false;
        m_meshHistogram = {};
        m_meshResources = {};
        m_currentStats = {};
        m_lastStats = {};
        m_lastPresentedBackBuffer = {};
        m_lastViewCamera = {};
        m_hasLastViewCamera = false;
        m_lastEditorViewCamera = {};
        m_hasLastEditorViewCamera = false;
        m_frameOverlay = nullptr;
        m_frameOverlayUser = nullptr;
        m_activeView = InvalidViewIndex;
        m_frameActive = false;
        m_device = nullptr;
        m_rhi = nullptr;
    }

    AssetHandle Renderer::RegisterMesh(JArrayView<MeshVertex> vertices, JArrayView<UInt32> indices)
    {
        if (m_device == nullptr || m_frameActive
            || vertices.data == nullptr || vertices.size == 0
            || indices.data == nullptr || indices.size == 0 || indices.size % 3 != 0)
        {
            return {};
        }
        // 색인이 정점 밖을 가리키면 GPU 가 쓰레기를 읽는다. 여기서 거절한다.
        for (UInt32 index = 0; index < indices.size; ++index)
        {
            if (indices.data[index] >= vertices.size)
            {
                return {};
            }
        }

        BufferDesc vertexDesc;
        vertexDesc.size = static_cast<std::size_t>(vertices.size) * sizeof(MeshVertex);
        vertexDesc.usage = BufferUsage::Vertex | BufferUsage::CopySource;
        vertexDesc.memory = MemoryType::Upload;
        BufferDesc indexDesc;
        indexDesc.size = static_cast<std::size_t>(indices.size) * sizeof(std::uint32_t);
        indexDesc.usage = BufferUsage::Index | BufferUsage::CopySource;
        indexDesc.memory = MemoryType::Upload;

        MeshResource resource;
        resource.vertexBuffer = m_device->CreateBuffer(vertexDesc);
        resource.indexBuffer = m_device->CreateBuffer(indexDesc);
        const Bool written = resource.vertexBuffer.IsValid() && resource.indexBuffer.IsValid()
            && m_device->WriteBuffer(resource.vertexBuffer, 0,
                {reinterpret_cast<const std::byte*>(vertices.data),
                    static_cast<JBro::UInt32>(vertexDesc.size)})
            && m_device->WriteBuffer(resource.indexBuffer, 0,
                {reinterpret_cast<const std::byte*>(indices.data),
                    static_cast<JBro::UInt32>(indexDesc.size)});
        if (false == written)
        {
            if (resource.vertexBuffer.IsValid())
            {
                m_device->DestroyBuffer(resource.vertexBuffer);
            }
            if (resource.indexBuffer.IsValid())
            {
                m_device->DestroyBuffer(resource.indexBuffer);
            }
            return {};
        }
        resource.indexCount = indices.size;
        resource.occupied = true;

        // 빈 자리를 다시 쓴다. generation 은 그 자리가 살아온 횟수라 옛 핸들을 가른다.
        for (std::size_t slot = 0; slot < m_meshResources.Size(); ++slot)
        {
            if (false == m_meshResources[slot].occupied)
            {
                resource.generation = m_meshResources[slot].generation + 1;
                m_meshResources[slot] = resource;
                return AssetHandle{static_cast<JBro::UInt32>(slot), resource.generation};
            }
        }
        m_meshResources.Add(resource);
        if (m_meshHistogram.Size() < m_meshResources.Size())
        {
            m_meshHistogram.Resize(m_meshResources.Size());
        }
        return AssetHandle{static_cast<JBro::UInt32>(m_meshResources.Size() - 1), resource.generation};
    }

    void Renderer::UnregisterMesh(AssetHandle mesh)
    {
        if (m_device == nullptr || m_frameActive || mesh.index >= m_meshResources.Size())
        {
            return;
        }
        MeshResource& resource = m_meshResources[mesh.index];
        if (false == resource.occupied || resource.generation != mesh.generation)
        {
            return;
        }
        // 지난 프레임이 아직 이 버퍼를 읽고 있을 수 있다. 디바이스가 은퇴 펜스로 미뤄 놓는다.
        m_device->DestroyBuffer(resource.vertexBuffer);
        m_device->DestroyBuffer(resource.indexBuffer);
        resource.vertexBuffer = {};
        resource.indexBuffer = {};
        resource.indexCount = 0;
        resource.occupied = false;
    }

    UInt32 Renderer::GetMeshCount() const
    {
        UInt32 count = 0;
        for (std::size_t slot = 0; slot < m_meshResources.Size(); ++slot)
        {
            if (m_meshResources[slot].occupied)
            {
                ++count;
            }
        }
        return count;
    }

    const Renderer::MeshResource* Renderer::FindMesh(AssetHandle mesh) const
    {
        if (mesh.index >= m_meshResources.Size())
        {
            return nullptr;
        }
        const MeshResource& resource = m_meshResources[mesh.index];
        if (false == resource.occupied || resource.generation != mesh.generation)
        {
            return nullptr;
        }
        return &resource;
    }

    AssetHandle Renderer::RegisterTexture(const Extent2D& extent, JArrayView<std::byte> rgba8)
    {
        const std::size_t expected = static_cast<std::size_t>(extent.width) * extent.height * 4u;
        if (m_device == nullptr || m_frameActive || extent.width == 0 || extent.height == 0
            || rgba8.data == nullptr || rgba8.size != expected)
        {
            return {};
        }
        TextureDesc desc;
        desc.extent = extent;
        desc.format = TextureFormat::RGBA8Unorm;
        desc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
        TextureResource resource;
        resource.texture = m_device->CreateTexture(desc);
        if (false == resource.texture.IsValid())
        {
            return {};
        }
        if (false == m_device->WriteTexture(resource.texture, 0, rgba8))
        {
            m_device->DestroyTexture(resource.texture);
            return {};
        }
        resource.extent = extent;
        resource.occupied = true;
        for (std::size_t slot = 0; slot < m_textureResources.Size(); ++slot)
        {
            if (false == m_textureResources[slot].occupied)
            {
                resource.generation = m_textureResources[slot].generation + 1;
                m_textureResources[slot] = resource;
                return AssetHandle{static_cast<JBro::UInt32>(slot), resource.generation};
            }
        }
        m_textureResources.Add(resource);
        return AssetHandle{static_cast<JBro::UInt32>(m_textureResources.Size() - 1), resource.generation};
    }

    AssetHandle Renderer::RegisterVolumeTexture(UInt32 width, UInt32 height, UInt32 depth, JArrayView<std::byte> rgba16f)
    {
        // 텍셀 하나가 반정밀 실수 넷(8 바이트)이다.
        const std::size_t expected = static_cast<std::size_t>(width.Get()) * height.Get() * depth.Get() * 8u;
        if (m_device == nullptr || m_frameActive || width == 0 || height == 0 || depth == 0
            || depth > m_device->GetCaps().maxTexture3DSize || width > m_device->GetCaps().maxTexture3DSize
            || height > m_device->GetCaps().maxTexture3DSize || rgba16f.data == nullptr || rgba16f.size != expected)
        {
            return {};
        }
        TextureDesc desc;
        desc.extent = {width, height};
        desc.depthOrLayers = depth;
        desc.dimension = TextureDimension::Texture3D;
        desc.format = TextureFormat::RGBA16Float;
        desc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
        TextureResource resource;
        resource.texture = m_device->CreateTexture(desc);
        if (false == resource.texture.IsValid())
        {
            return {};
        }
        if (false == m_device->WriteTexture(resource.texture, 0, rgba16f))
        {
            m_device->DestroyTexture(resource.texture);
            return {};
        }
        resource.extent = desc.extent;
        resource.depth = depth;
        resource.occupied = true;
        for (std::size_t slot = 0; slot < m_textureResources.Size(); ++slot)
        {
            if (false == m_textureResources[slot].occupied)
            {
                resource.generation = m_textureResources[slot].generation + 1;
                m_textureResources[slot] = resource;
                return AssetHandle{static_cast<JBro::UInt32>(slot), resource.generation};
            }
        }
        m_textureResources.Add(resource);
        return AssetHandle{static_cast<JBro::UInt32>(m_textureResources.Size() - 1), resource.generation};
    }

    Bool Renderer::GetRegisteredTextureSize(AssetHandle texture, UInt32& width, UInt32& height, UInt32& depth) const
    {
        const TextureResource* resource = FindTexture(texture);
        if (resource == nullptr)
        {
            return false;
        }
        width = resource->extent.width;
        height = resource->extent.height;
        depth = resource->depth;
        return true;
    }

    Bool Renderer::UpdateTexture(AssetHandle texture, JArrayView<std::byte> rgba8)
    {
        const TextureResource* resource = FindTexture(texture);
        if (resource == nullptr || m_frameActive || rgba8.data == nullptr || resource->depth != 1
            || rgba8.size != static_cast<std::size_t>(resource->extent.width) * resource->extent.height * 4u)
        {
            return false;
        }
        return m_device->WriteTexture(resource->texture, 0, rgba8);
    }

    Bool Renderer::UpdateTextureRegion(AssetHandle texture, UInt32 x, UInt32 y, UInt32 width,
        UInt32 height, JArrayView<std::byte> rgba8, UInt32 rowPitch)
    {
        const TextureResource* resource = FindTexture(texture);
        // 텍스처 밖·짧은 행 간격은 백엔드가 거절한다(RHI 계약, D-216) - 여기서 한 번 더 재지 않는다.
        if (resource == nullptr || m_frameActive || rgba8.data == nullptr || width == 0 || height == 0 || resource->depth != 1)
        {
            return false;
        }
        return m_device->WriteTextureRegion(resource->texture, 0, x, y, width, height, rgba8, rowPitch);
    }

    void Renderer::UnregisterTexture(AssetHandle texture)
    {
        if (m_device == nullptr || m_frameActive || texture.index >= m_textureResources.Size())
        {
            return;
        }
        TextureResource& resource = m_textureResources[texture.index];
        if (false == resource.occupied || resource.generation != texture.generation)
        {
            return;
        }
        // 지난 프레임이 아직 읽을 수 있다. 백엔드의 지연 파기가 그것을 든다(RHI 계약).
        m_device->DestroyTexture(resource.texture);
        resource.texture = {};
        resource.extent = {};
        resource.occupied = false;
    }

    UInt32 Renderer::GetTextureCount() const
    {
        UInt32 count = 0;
        for (std::size_t slot = 0; slot < m_textureResources.Size(); ++slot)
        {
            count += m_textureResources[slot].occupied ? 1u : 0u;
        }
        return count;
    }

    const Renderer::TextureResource* Renderer::FindTexture(AssetHandle texture) const
    {
        if (texture.generation == 0 || texture.index >= m_textureResources.Size())
        {
            return nullptr;
        }
        const TextureResource& resource = m_textureResources[texture.index];
        return resource.occupied && resource.generation == texture.generation ? &resource : nullptr;
    }

    void Renderer::DestroyTextureResources()
    {
        if (m_device == nullptr)
        {
            return;
        }
        for (std::size_t slot = 0; slot < m_textureResources.Size(); ++slot)
        {
            TextureResource& resource = m_textureResources[slot];
            if (resource.occupied)
            {
                m_device->DestroyTexture(resource.texture);
                resource = {};
            }
        }
        m_textureResources.Clear();
    }

    void Renderer::DestroyMeshResources()
    {
        if (m_device == nullptr)
        {
            return;
        }
        for (std::size_t slot = 0; slot < m_meshResources.Size(); ++slot)
        {
            MeshResource& resource = m_meshResources[slot];
            if (resource.occupied)
            {
                m_device->DestroyBuffer(resource.vertexBuffer);
                m_device->DestroyBuffer(resource.indexBuffer);
                resource = {};
            }
        }
        m_meshResources.Clear();
    }

    FrameStatus Renderer::BeginFrame(const FrameTarget& target)
    {
        if (m_device == nullptr || false == m_swapchain.IsValid() || m_frameActive)
        {
            return FrameStatus::InvalidState;
        }
        // 텍스처를 주면서 크기를 안 주면 뷰포트를 잴 기준이 없다. 백버퍼 크기로
        // 대신 재면 타깃 밖으로 나가는 뷰포트를 통과시키게 된다.
        if (target.texture.IsValid() && (target.extent.width == 0 || target.extent.height == 0))
        {
            return FrameStatus::InvalidState;
        }

        // **깊이 텍스처는 프레임을 열기 전에 이 크기로 확보한다.** 디바이스는 프레임 안에서 자원을
        // 만들지 않으므로, 메시가 있는지 알게 되는 `RecordViews` 에서는 늦다. 크기가 같으면 지난
        // 것을 그대로 쓴다 - 2D 프레임이 내는 값은 크기가 바뀔 때의 텍스처 하나뿐이다.
        // 프레임 타깃과 크기가 다른 뷰(편집 화면·레이어 썸네일)의 깊이는 지난 프레임이 바란 크기로 먼저 만든다.
        // 레이어·라이트맵·그림자 마스크 같은 중간 텍스처도 같은 까닭으로 여기서 만든다(D-279·D-339) - 지난 프레임의 그래프가 바란 모양들이다.
        m_targetPool.Prepare();
        const Extent2D depthExtent = target.texture.IsValid() ? target.extent : m_config.surfaceExtent;
        if (false == m_targetPool.Ensure({depthExtent, TextureFormat::D32Float, TextureUsage::DepthStencil}))
        {
            return FrameStatus::InvalidState;
        }
        // 미리보기 사본도 프레임 밖에서만 만든다(D-341). 지난 프레임이 바란 모양이다.
        if (m_captureWanted)
        {
            m_captureWanted = false;
            if (m_captureTexture.IsValid())
            {
                m_device->DestroyTexture(m_captureTexture);
                m_captureTexture = {};
            }
            TextureDesc desc;
            desc.extent = m_captureWant.extent;
            desc.format = m_captureWant.format;
            desc.usage = TextureUsage::RenderTarget | TextureUsage::Sampled | TextureUsage::CopyDestination;
            m_captureTexture = m_device->CreateTexture(desc);
            m_captureDesc = m_captureTexture.IsValid() ? m_captureWant : TransientTextureDesc{};
        }
        // 후처리의 파이프라인도 프레임 밖에서만 만든다(D-342). 지난 프레임에 처음 바랐다.
        if (m_postWanted && false == m_postReady && false == m_postFailed)
        {
            m_postWanted = false;
            if (false == CreatePostProcessResources())
            {
                DestroyPostProcessResources();
                m_postFailed = true;
            }
        }
        const BeginFrameResult result = m_device->BeginFrame(m_swapchain);
        if (result.status != FrameStatus::Ready)
        {
            return result.status;
        }

        if (result.frame.commands == nullptr || false == result.frame.backBuffer.IsValid())
        {
            m_device->AbortFrame(result.frame);
            return FrameStatus::InvalidState;
        }

        m_frame = result.frame;
        if (m_postReady)
        {
            m_postConstants.BeginFrame(m_frame.slot);
        }
        // 장치가 이 슬롯의 앞 프레임을 끝내며 받아 둔 GPU 시각을 패스별 시간으로 바꾼다(D-341).
        CollectGraphTiming();
        // 프레임마다 여기서 통째로 덮어쓴다. 그래서 프레임을 닫을 때 따로 비울 것이 없다.
        m_frameTarget = target;
        m_frameActive = true;
        ResetSubmissionStorage();
        return FrameStatus::Ready;
    }

    Bool Renderer::BeginView(const CameraParams& camera)
    {
        if (false == m_frameActive || m_activeView != InvalidViewIndex)
        {
            return false;
        }

        if (m_views.Size() >= m_config.maxViews)
        {
            ++m_currentStats.droppedViewCount;
            return false;
        }

        ViewPacket packet;
        packet.camera = camera;
        packet.spriteOffset = static_cast<JBro::UInt32>(m_sprites.Size());
        packet.meshOffset = static_cast<JBro::UInt32>(m_meshes.Size());
        packet.worldTextOffset = static_cast<JBro::UInt32>(m_worldTexts.Size());
        packet.layerGroupOffset = static_cast<JBro::UInt32>(m_layerGroups.Size());
        packet.lightOffset = static_cast<JBro::UInt32>(m_lights.Size());
        packet.litRangeOffset = static_cast<JBro::UInt32>(m_litRanges.Size());
        packet.shadowEdgeOffset = static_cast<JBro::UInt32>(m_shadowEdges.Size());
        // 후처리 효과를 프레임 저장소에 옮긴다(D-342). 부르는 쪽의 배열은 이 함수가 돌아온 뒤 버려도 된다 - 뷰 자체는 받는다.
        packet.camera.postProcess = {};
        packet.camera.layerPostProcess = {};
        CopyPostEntries(camera.postProcess, packet.postOffset, packet.postCount);
        CopyPostEntries(camera.layerPostProcess, packet.layerPostOffset, packet.layerPostCount);
        m_views.Add(packet);
        m_activeView = static_cast<JBro::UInt32>(m_views.Size() - 1);
        ++m_currentStats.viewCount;
        return true;
    }

    Bool Renderer::SubmitSprite(const SpriteSubmit& item)
    {
        return SubmitSprites({&item, 1});
    }

    Bool Renderer::SubmitSprites(JArrayView<SpriteSubmit> items)
    {
        if (false == m_frameActive || m_activeView == InvalidViewIndex)
        {
            return false;
        }

        if (items.size == 0)
        {
            return true;
        }

        const std::size_t available = m_config.maxSpriteSubmissions - m_sprites.Size();
        if (items.data == nullptr || items.size > available)
        {
            m_currentStats.droppedSpriteCount += items.size;
            return false;
        }

        m_sprites.Append(items.data, items.size);
        m_views[m_activeView].spriteCount += items.size;
        m_currentStats.spriteCount += items.size;
        return true;
    }

    Bool Renderer::SubmitMesh(const MeshSubmit& item)
    {
        return SubmitMeshes({&item, 1});
    }

    Bool Renderer::SubmitMeshes(JArrayView<MeshSubmit> items)
    {
        if (false == m_frameActive || m_activeView == InvalidViewIndex)
        {
            return false;
        }

        if (items.size == 0)
        {
            return true;
        }

        const std::size_t available = m_config.maxMeshSubmissions - m_meshes.Size();
        if (items.data == nullptr || items.size > available)
        {
            m_currentStats.droppedMeshCount += items.size;
            return false;
        }

        m_meshes.Append(items.data, items.size);
        m_views[m_activeView].meshCount += items.size;
        m_currentStats.meshCount += items.size;
        return true;
    }

    Bool Renderer::SubmitWorldText(const WorldTextSubmit& item)
    {
        return SubmitWorldTexts({&item, 1});
    }

    Bool Renderer::SubmitWorldTexts(JArrayView<WorldTextSubmit> items)
    {
        if (false == m_frameActive || m_activeView == InvalidViewIndex)
        {
            return false;
        }

        if (items.size == 0)
        {
            return true;
        }

        const std::size_t available = m_config.maxWorldTextSubmissions - m_worldTexts.Size();
        if (items.data == nullptr || items.size > available)
        {
            m_currentStats.droppedWorldTextCount += items.size;
            return false;
        }

        m_worldTexts.Append(items.data, items.size);
        m_views[m_activeView].worldTextCount += items.size;
        m_currentStats.worldTextCount += items.size;
        return true;
    }

    void Renderer::CopyPostEntries(const PostProcessStack& stack, UInt32& offset, UInt32& count)
    {
        offset = static_cast<JBro::UInt32>(m_postEntries.Size());
        count = 0;
        for (UInt32 at = 0; at < stack.entries.size; ++at)
        {
            const PostProcessEntry& entry = stack.entries.data[at.Get()];
            if (false == entry.enabled)
            {
                continue;
            }
            const PostProcessEffectType* type = PostProcessEffectRegistry::Get(entry.type);
            const std::size_t blockEnd = static_cast<std::size_t>(entry.blockOffset.Get()) + (type != nullptr ? type->blockSize.Get() : 0u);
            const std::size_t blockAt = (static_cast<std::size_t>(m_postBlockUsed.Get()) + 15u) & ~static_cast<std::size_t>(15u);
            if (type == nullptr || blockEnd > static_cast<std::size_t>(stack.blocks.size.Get()) || count >= MaxPostProcessPerView
                || m_postEntries.Size() >= m_postEntries.Capacity() || blockAt + type->blockSize.Get() > m_postBlocks.Size())
            {
                ++m_currentStats.droppedPostProcessCount;
                continue;
            }
            if (type->blockSize != 0)
            {
                std::memcpy(m_postBlocks.Data() + blockAt, stack.blocks.data + entry.blockOffset.Get(), type->blockSize.Get());
            }
            PostEntrySlot slot;
            slot.type = entry.type;
            slot.blockOffset = static_cast<JBro::UInt32>(blockAt);
            m_postEntries.Add(slot);
            m_postBlockUsed = static_cast<JBro::UInt32>(blockAt + type->blockSize.Get());
            ++count;
        }
    }

    Bool Renderer::BeginLayer(CompositeBlend blend, Float opacity, const PostProcessStack& post)
    {
        if (false == m_frameActive || m_activeView == InvalidViewIndex || m_openLayerGroup != NoLayerGroup)
        {
            return false;
        }
        if (m_layerGroups.Size() >= m_config.maxLayerGroups)
        {
            // 묶지 못하면 그대로 그린다. 그림이 틀리지만 스프라이트는 남는다.
            ++m_currentStats.droppedLayerCount;
            return false;
        }
        LayerGroup group;
        group.firstSprite = static_cast<JBro::UInt32>(m_sprites.Size());
        group.endSprite = group.firstSprite;
        group.blend = blend;
        // 0..1 로 자른다. 유한하지 않은 값(NaN)은 1 이다 - 비교가 둘 다 거짓이라 처음 값이 남는다.
        group.opacity = 1.0f;
        if (opacity < 0.0f)
        {
            group.opacity = 0.0f;
        }
        else if (opacity <= 1.0f)
        {
            group.opacity = opacity;
        }
        CopyPostEntries(post, group.postOffset, group.postCount);
        m_layerGroups.Add(group);
        m_openLayerGroup = static_cast<JBro::UInt32>(m_layerGroups.Size() - 1);
        ++m_views[m_activeView].layerGroupCount;
        return true;
    }

    Bool Renderer::EndLayer()
    {
        if (false == m_frameActive || m_openLayerGroup == NoLayerGroup)
        {
            return false;
        }
        m_layerGroups[m_openLayerGroup].endSprite = static_cast<JBro::UInt32>(m_sprites.Size());
        m_openLayerGroup = NoLayerGroup;
        return true;
    }

    Bool Renderer::SubmitLight2D(const Light2DSubmit& light)
    {
        return SubmitLights2D({&light, 1});
    }

    Bool Renderer::SubmitLights2D(JArrayView<Light2DSubmit> lights)
    {
        if (false == m_frameActive || m_activeView == InvalidViewIndex || (lights.size != 0 && lights.data == nullptr))
        {
            return false;
        }
        ViewPacket& view = m_views[m_activeView];
        Bool allTaken = true;
        for (std::size_t at = 0; at < lights.size; ++at)
        {
            const Light2DSubmit& light = lights.data[at];
            ++m_currentStats.light2DCount;
            if (light.kind == Light2DKind::Global)
            {
                // 환경광은 지우는 색이다. 셋을 더하기만 하면 되므로 담아 두지 않는다.
                for (Int32 channel = 0; channel < 3; ++channel)
                {
                    view.ambient[channel] += light.color[channel];
                }
                view.lighting = true;
                continue;
            }
            if (m_lights.Size() >= m_config.maxLights2D)
            {
                ++m_currentStats.droppedLight2DCount;
                allTaken = false;
                continue;
            }
            m_lights.Add(light);
            ++view.lightCount;
            view.lighting = true;
        }
        return allTaken;
    }

    Bool Renderer::SubmitShadowEdges2D(JArrayView<ShadowEdge2D> edges)
    {
        if (false == m_frameActive || m_activeView == InvalidViewIndex || (edges.size != 0 && edges.data == nullptr))
        {
            return false;
        }
        if (edges.size == 0)
        {
            return true;
        }
        if (m_shadowEdges.Size() + edges.size > m_config.maxShadowEdges2D)
        {
            m_currentStats.droppedShadowEdge2DCount += edges.size;
            return false;
        }
        m_shadowEdges.Append(edges.data, edges.size);
        m_views[m_activeView].shadowEdgeCount += edges.size;
        m_currentStats.shadowEdge2DCount += edges.size;
        return true;
    }

    Bool Renderer::SetSpriteLighting(Bool lit)
    {
        if (false == m_frameActive || m_activeView == InvalidViewIndex)
        {
            return false;
        }
        const Bool open = m_openLitRange != NoLayerGroup;
        if (lit == open)
        {
            return true;
        }
        if (false == lit)
        {
            m_litRanges[m_openLitRange].endSprite = static_cast<JBro::UInt32>(m_sprites.Size());
            m_openLitRange = NoLayerGroup;
            return true;
        }
        if (m_litRanges.Size() >= m_config.maxLayerGroups)
        {
            // 구간을 열지 못하면 빛 없이 그린다. 레이어 묶음과 같은 정책이다.
            ++m_currentStats.droppedLayerCount;
            return false;
        }
        LitRange range;
        range.firstSprite = static_cast<JBro::UInt32>(m_sprites.Size());
        range.endSprite = range.firstSprite;
        m_litRanges.Add(range);
        m_openLitRange = static_cast<JBro::UInt32>(m_litRanges.Size() - 1);
        ++m_views[m_activeView].litRangeCount;
        return true;
    }

    Bool Renderer::EndView()
    {
        if (false == m_frameActive || m_activeView == InvalidViewIndex)
        {
            return false;
        }
        if (m_openLayerGroup != NoLayerGroup)
        {
            EndLayer();
        }
        if (m_openLitRange != NoLayerGroup)
        {
            SetSpriteLighting(false);
        }

        m_activeView = InvalidViewIndex;
        return true;
    }

    FrameStatus Renderer::EndFrame()
    {
        if (m_device == nullptr || false == m_frameActive || m_activeView != InvalidViewIndex)
        {
            return FrameStatus::InvalidState;
        }

        Bool recorded = false;
        try
        {
            recorded = RecordViews();
        }
        catch (const std::bad_alloc&)
        {
            // 배열이 자라다 실패했다. 프레임을 연 채로 예외를 내보내면 다음 프레임부터 영원히 InvalidState 다.
            recorded = false;
        }
        if (false == recorded)
        {
            m_device->AbortFrame(m_frame);
            m_lastStats = m_currentStats;
            m_frame = {};
            m_frameActive = false;
            return FrameStatus::InvalidState;
        }

        // 게임을 그린 뒤, 프레임을 닫기 전. 에디터 UI 가 여기서 백버퍼에 얹힌다.
        if (m_frameOverlay != nullptr
            && false == m_frameOverlay(
                *m_frame.commands, m_frame.backBuffer, m_frame.slot, m_frameOverlayUser))
        {
            m_device->AbortFrame(m_frame);
            m_lastStats = m_currentStats;
            m_frame = {};
            m_frameActive = false;
            return FrameStatus::InvalidState;
        }

        const FrameStatus status = m_device->EndFrame(m_frame);
        m_lastStats = m_currentStats;
        // 그리지 않은 프레임의 카메라는 화면에 없다. 기즈모는 보이는 그림의 카메라를 써야 한다.
        //
        // **자기 타깃을 든 뷰는 화면 프레임과 따로 논다**(D-130·D-140). 그쪽은 에디터가
        // 스스로 카메라를 정해 자기 텍스처에 그린 화면이라, 부르는 쪽이 찾는 "게임이 보는
        // 카메라" 가 아니고, 기록 조건도 다르다 - 시뮬레이션 뷰가 닫혀 `recordViews` 가 꺼진
        // 프레임에도 편집 화면은 그려지므로(RecordViews 가 같은 규칙으로 통과시킨다),
        // 편집 카메라는 그 깃발과 무관하게 든다.
        Bool tookGameCamera = false;
        for (std::size_t index = 0; index < m_views.Size(); ++index)
        {
            if (m_views[index].camera.target.IsValid())
            {
                // **편집 뷰는 따로 든다**(D-140). 에디터가 같은 행렬을 한 번 더 세우면
                // 둘로 갈리므로, 그린 쪽이 쓴 것을 그대로 내준다.
                m_lastEditorViewCamera = m_views[index].camera;
                m_hasLastEditorViewCamera = true;
                continue;
            }
            // 게임 쪽은 **첫 뷰**다. 여럿이면 앞의 것이 화면을 대표한다.
            if (m_frameTarget.recordViews && false == tookGameCamera)
            {
                m_lastViewCamera = m_views[index].camera;
                m_hasLastViewCamera = true;
                tookGameCamera = true;
            }
        }
        m_lastPresentedBackBuffer = m_frame.backBuffer;
        m_frame = {};
        m_frameActive = false;
        return status;
    }

    Bool Renderer::SetFrameOverlay(FrameOverlay overlay, void* user)
    {
        if (m_frameActive)
        {
            return false;
        }
        m_frameOverlay = overlay;
        m_frameOverlayUser = user;
        return true;
    }

    Bool Renderer::HasFrameOverlay() const
    {
        return m_frameOverlay != nullptr;
    }

    Extent2D Renderer::GetFrameExtent() const
    {
        return m_frameTarget.texture.IsValid()
            ? m_frameTarget.extent
            : m_config.surfaceExtent;
    }

    TextureFormat Renderer::GetBackBufferFormat() const
    {
        return m_config.backBufferFormat;
    }

    IRHIDevice* Renderer::GetDevice() const
    {
        return m_device;
    }

    Bool Renderer::ReadBackBuffer(
        std::byte* destination,
        std::size_t destinationSize,
        TextureReadback& result)
    {
        result = {};
        if (m_device == nullptr || m_frameActive || false == m_lastPresentedBackBuffer.IsValid())
        {
            return false;
        }
        return m_device->ReadTexture(
            m_lastPresentedBackBuffer, destination, destinationSize, result);
    }

    void Renderer::AbortFrame()
    {
        if (m_frameActive && m_device != nullptr)
        {
            m_device->AbortFrame(m_frame);
            m_lastStats = m_currentStats;
            m_frame = {};
            m_frameActive = false;
            ResetSubmissionStorage();
        }
    }

    Bool Renderer::ResizeSurface(const Extent2D& extent)
    {
        if (m_device == nullptr
            || false == m_swapchain.IsValid()
            || m_frameActive
            || extent.width == 0
            || extent.height == 0
            || extent.width > static_cast<JBro::UInt32>((std::numeric_limits<std::int32_t>::max)())
            || extent.height > static_cast<JBro::UInt32>((std::numeric_limits<std::int32_t>::max)()))
        {
            return false;
        }

        if (false == m_device->ResizeSwapchain(m_swapchain, extent))
        {
            return false;
        }

        // 옛 백버퍼는 이제 없다. 되읽기는 다음 프레임이 제시한 것을 본다.
        m_lastPresentedBackBuffer = {};
        m_config.surfaceExtent = extent;
        return true;
    }

    RendererFrameStats Renderer::GetLastFrameStats() const
    {
        return m_lastStats;
    }

    Bool Renderer::GetLastViewCamera(CameraParams& camera) const
    {
        if (false == m_hasLastViewCamera)
        {
            return false;
        }
        camera = m_lastViewCamera;
        return true;
    }

    Bool Renderer::GetLastEditorViewCamera(CameraParams& camera) const
    {
        if (false == m_hasLastEditorViewCamera)
        {
            return false;
        }
        camera = m_lastEditorViewCamera;
        return true;
    }

    const RenderGraph* Renderer::GetLastRenderGraph() const
    {
        return m_graph.IsCompiled() ? &m_graph : nullptr;
    }

    UInt64 Renderer::GetLastRenderGraphSerial() const
    {
        return m_graphSerial;
    }

    UInt32 Renderer::GetLastRenderGraphViewCount() const
    {
        return static_cast<JBro::UInt32>(m_graphViews.Size());
    }

    Bool Renderer::GetLastRenderGraphView(UInt32 index, RendererViewDebugInfo& info) const
    {
        if (index >= m_graphViews.Size())
        {
            return false;
        }
        info = m_graphViews[index.Get()];
        return true;
    }

    const RenderTargetPool& Renderer::GetRenderTargetPool() const
    {
        return m_targetPool;
    }

    Bool Renderer::IsDeviceLost() const
    {
        return m_device != nullptr && m_device->GetStatus() == FrameStatus::DeviceLost;
    }

    Bool Renderer::IsPostProcessReady() const
    {
        return m_postReady;
    }

    Bool Renderer::IsInitialized() const
    {
        return m_device != nullptr;
    }

    Extent2D Renderer::GetSurfaceExtent() const
    {
        return m_config.surfaceExtent;
    }

    UInt32 Renderer::GetLight2DLimit() const
    {
        return m_device != nullptr ? m_config.maxLights2D : UInt32(0);
    }

    UInt32 Renderer::GetShadowEdge2DLimit() const
    {
        return m_device != nullptr ? m_config.maxShadowEdges2D : UInt32(0);
    }

    UInt32 Renderer::GetSpriteSubmissionLimit() const
    {
        return m_device != nullptr ? m_config.maxSpriteSubmissions : UInt32(0);
    }

    Bool Renderer::RecordViews()
    {
        if (m_frame.commands == nullptr)
        {
            return false;
        }
        // **타깃이 뷰를 원하지 않는 프레임이다**(D-63). 제출은 받았지만 기록하지 않는다 -
        // 시뮬레이션 뷰 패널이 보이지 않을 때 텍스처를 그대로 두는 길이다.
        //
        // **자기 타깃을 든 뷰는 여기 해당하지 않는다**(D-130). 이 값은 **프레임 타깃**에
        // 대한 것이고, 편집 화면은 자기 텍스처에 그린다 - 시뮬레이션 뷰를 닫아 두었다고
        // 편집 화면까지 멈추면 안 된다.
        Bool anyRecorded = false;
        for (std::size_t index = 0; index < m_views.Size(); ++index)
        {
            if (m_frameTarget.recordViews || m_views[index].camera.target.IsValid())
            {
                anyRecorded = true;
                break;
            }
        }
        if (false == anyRecorded)
        {
            m_currentStats.skippedViewCount += static_cast<JBro::UInt32>(m_views.Size());
            return true;
        }
        if (false == UploadSpriteInstances() || false == UploadMeshInstances() || false == UploadWorldTextInstances()
            || false == UploadLightInstances())
        {
            return false;
        }

        // 뷰가 갈 곳과 그 크기다. 타깃을 안 준 프레임은 백버퍼로 간다.
        const Bool frameToTexture = m_frameTarget.texture.IsValid();
        const TextureHandle frameTexture = frameToTexture ? m_frameTarget.texture : m_frame.backBuffer;
        const Extent2D frameExtent = GetFrameExtent();

        // **뷰를 그래프의 패스로 짓는다**(D-339). 패스는 기록할 순서대로 더하고, 중간 텍스처는 그래프가 만든다 - 컴파일이 풀에서 고르고,
        // 이 프레임에 없는 모양은 다음 프레임 전에 서도록 바라며, 그 텍스처가 있어야 하는 패스 대신 대신 그리기 패스를 돌린다.
        m_graph.Reset();
        const RenderGraphTexture frameTarget = frameToTexture ? m_graph.ImportTexture(frameTexture, "FrameTarget")
                                                              : m_graph.ImportBackBuffer(frameTexture, "BackBuffer");
        m_graphViews.Clear();

        // **지우는 것은 타깃마다 처음 한 번이다**(D-130). 뷰 번호로 정하면 둘째 타깃의
        // 첫 뷰가 덧그리기가 되어 지난 프레임이 비쳐 남는다.
        m_clearedTargetCount = 0;

        for (std::size_t index = 0; index < m_views.Size(); ++index)
        {
            if (false == AddViewPasses(static_cast<JBro::UInt32>(index), frameTarget, frameTexture, frameExtent))
            {
                return false;
            }
        }
        if (false == m_graph.Compile(m_targetPool))
        {
            return false;
        }
        ++m_graphSerial;
        m_graph.SetTimestampCapacity(m_timingEnabled ? m_device->GetCaps().maxTimestampsPerFrame : UInt32(0));
        PrepareCapture();
        if (false == m_graph.Execute(*m_frame.commands))
        {
            return false;
        }
        if (m_captureRequested && m_captureStatus == RenderGraphCaptureStatus::Captured)
        {
            m_captureStatus = m_graph.WasCaptured() ? RenderGraphCaptureStatus::Captured : RenderGraphCaptureStatus::NotFound;
            if (m_graph.WasCaptured())
            {
                m_captureSerial = m_graphSerial;
            }
        }
        RecordGraphTiming();
        return true;
    }

    void Renderer::PrepareCapture()
    {
        if (false == m_captureRequested)
        {
            return;
        }
        // 뷰·이름(포인터)·그 뷰에서의 차례로 패스를 찾는다.
        RenderGraphPass found;
        UInt32 seen = 0;
        for (UInt32 index = 0; index < m_graph.GetPassCount(); ++index)
        {
            const RenderGraphPass pass{index};
            if (m_graph.GetPassGroup(pass) == m_captureRequest.group && m_graph.GetPassName(pass) == m_captureRequest.passName)
            {
                if (seen == m_captureRequest.occurrence)
                {
                    found = pass;
                    break;
                }
                ++seen;
            }
        }
        if (false == found.IsValid() || m_captureRequest.access >= m_graph.GetPassAccessCount(found)
            || m_graph.GetPassSkip(found) != RenderGraphSkip::None)
        {
            m_captureStatus = RenderGraphCaptureStatus::NotFound;
            return;
        }
        const RenderGraphTexture texture = m_graph.GetPassAccess(found, m_captureRequest.access).texture;
        TransientTextureDesc desc = m_graph.GetTextureDesc(texture);
        if (m_graph.GetTextureKind(texture) != RenderGraphTextureKind::Transient)
        {
            // 들인 텍스처(뷰의 타깃·외곽선 마스크)는 모양을 모른다. 그 뷰의 크기와 백버퍼 포맷이다 - 렌더러가 그렇게 받는다.
            desc = {};
            desc.extent = m_captureRequest.group < m_graphViews.Size() ? m_graphViews[m_captureRequest.group.Get()].extent : Extent2D{};
            desc.format = m_config.backBufferFormat;
        }
        if (desc.format == TextureFormat::D32Float)
        {
            m_captureStatus = RenderGraphCaptureStatus::Unsupported;
            return;
        }
        if (desc.extent.width == 0 || desc.extent.height == 0)
        {
            m_captureStatus = RenderGraphCaptureStatus::NotFound;
            return;
        }
        const Bool sameShape = m_captureTexture.IsValid() && m_captureDesc.extent.width == desc.extent.width
            && m_captureDesc.extent.height == desc.extent.height && m_captureDesc.format == desc.format;
        if (false == sameShape)
        {
            // 사본은 프레임 밖에서만 만든다 - 다음 `BeginFrame` 이 만든다.
            m_captureWant = desc;
            m_captureWanted = true;
            m_captureStatus = RenderGraphCaptureStatus::Waiting;
            return;
        }
        m_captureStatus = m_graph.SetCapture(found, texture, m_captureTexture) ? RenderGraphCaptureStatus::Captured
                                                                            : RenderGraphCaptureStatus::NotFound;
    }

    void Renderer::RecordGraphTiming()
    {
        if (false == m_timingEnabled)
        {
            return;
        }
        TimingFrame& frame = m_timingRing[m_timingRingNext.Get()];
        m_timingRingNext = (m_timingRingNext + 1) % TimingRingSize;
        frame.rhiSerial = m_frame.serial;
        frame.graphSerial = m_graphSerial;
        frame.count = 0;
        for (UInt32 index = 0; index < m_graph.GetPassCount() && frame.count < TimedPassCapacity; ++index)
        {
            const RenderGraphPass pass{index};
            const UInt32 timestamp = m_graph.GetPassTimestamp(pass);
            if (timestamp == RenderGraphPass::Invalid)
            {
                continue;
            }
            TimedPass& timed = frame.passes[frame.count.Get()];
            timed.name = m_graph.GetPassName(pass);
            timed.group = m_graph.GetPassGroup(pass);
            timed.pass = index;
            timed.timestamp = timestamp;
            ++frame.count;
        }
    }

    void Renderer::CollectGraphTiming()
    {
        TimestampReadback readback;
        if (false == m_device->ReadFrameTimestamps(m_timestampScratch, MaxTimestampsPerFrame, readback)
            || readback.frameSerial == m_timingRhiSerial || readback.ticksPerSecond == 0)
        {
            return;
        }
        m_timingRhiSerial = readback.frameSerial;
        for (const TimingFrame& frame : m_timingRing)
        {
            if (frame.rhiSerial != readback.frameSerial || frame.count == 0)
            {
                continue;
            }
            const double toMilliseconds = 1000.0 / static_cast<double>(readback.ticksPerSecond.Get());
            UInt64 first = 0;
            UInt64 last = 0;
            m_timingCount = 0;
            for (UInt32 at = 0; at < frame.count; ++at)
            {
                const TimedPass& timed = frame.passes[at.Get()];
                if (timed.timestamp + 1 >= readback.count)
                {
                    continue;
                }
                const UInt64 begin = m_timestampScratch[timed.timestamp.Get()];
                const UInt64 end = m_timestampScratch[(timed.timestamp + 1).Get()];
                RenderGraphPassTiming& timing = m_timings[m_timingCount.Get()];
                timing.name = timed.name;
                timing.group = timed.group;
                timing.pass = timed.pass;
                timing.milliseconds = end > begin ? static_cast<Float>(static_cast<double>((end - begin).Get()) * toMilliseconds) : Float(0.0f);
                ++m_timingCount;
                first = first == 0 || begin < first ? begin : first;
                last = end > last ? end : last;
            }
            m_timingGpuMilliseconds = last > first ? static_cast<Float>(static_cast<double>((last - first).Get()) * toMilliseconds) : Float(0.0f);
            m_timingSerial = frame.graphSerial;
            return;
        }
    }

    void Renderer::SetRenderGraphTimingEnabled(Bool enabled)
    {
        m_timingEnabled = enabled;
    }

    Bool Renderer::IsRenderGraphTimingSupported() const
    {
        return m_device != nullptr && m_device->GetCaps().maxTimestampsPerFrame >= 2;
    }

    UInt64 Renderer::GetRenderGraphTimingSerial() const
    {
        return m_timingSerial;
    }

    UInt32 Renderer::GetRenderGraphTimingCount() const
    {
        return m_timingCount;
    }

    Bool Renderer::GetRenderGraphTiming(UInt32 index, RenderGraphPassTiming& timing) const
    {
        if (index >= m_timingCount)
        {
            return false;
        }
        timing = m_timings[index.Get()];
        return true;
    }

    Float Renderer::GetRenderGraphGpuMilliseconds() const
    {
        return m_timingGpuMilliseconds;
    }

    void Renderer::RequestRenderGraphCapture(const RenderGraphCaptureRequest& request)
    {
        // 다른 패스·텍스처를 고르면 지금 사본은 앞 요청의 것이다 - 새로 뜰 때까지 기다리는 중이다.
        const Bool changed = request.passName != m_captureRequest.passName || request.group != m_captureRequest.group
            || request.occurrence != m_captureRequest.occurrence || request.access != m_captureRequest.access;
        m_captureRequest = request;
        if (false == m_captureRequested || changed)
        {
            m_captureStatus = RenderGraphCaptureStatus::Waiting;
        }
        m_captureRequested = true;
    }

    void Renderer::ClearRenderGraphCapture()
    {
        m_captureRequested = false;
        m_captureStatus = RenderGraphCaptureStatus::None;
    }

    RenderGraphCaptureStatus Renderer::GetRenderGraphCaptureStatus() const
    {
        return m_captureStatus;
    }

    TextureHandle Renderer::GetRenderGraphCaptureTexture() const
    {
        return m_captureTexture;
    }

    Extent2D Renderer::GetRenderGraphCaptureExtent() const
    {
        return m_captureDesc.extent;
    }

    UInt64 Renderer::GetRenderGraphCaptureSerial() const
    {
        return m_captureSerial;
    }

    Bool Renderer::AddViewPasses(UInt32 viewIndex, RenderGraphTexture frameTarget, TextureHandle frameTexture, const Extent2D& frameExtent)
    {
        const ViewPacket& view = m_views[viewIndex.Get()];
        const Bool ownTarget = view.camera.target.IsValid();
        RendererViewDebugInfo debug;
        debug.target = ownTarget ? view.camera.target : frameTexture;
        debug.extent = ownTarget ? view.camera.targetExtent : frameExtent;
        debug.ownTarget = ownTarget;
        debug.hasMeshes = view.runCount != 0 || view.worldTextRunCount != 0;
        debug.lit = view.hasLitRun && view.lighting;
        debug.layerGroupCount = view.layerGroupCount;
        // 프레임 타깃으로 가는 뷰만 이 프레임의 opt-in 을 따른다(D-63·D-130).
        if (false == ownTarget && false == m_frameTarget.recordViews)
        {
            ++m_currentStats.skippedViewCount;
            m_graphViews.Add(debug);
            return true;
        }
        debug.recorded = true;
        m_graphViews.Add(debug);
        if (ownTarget && (view.camera.targetExtent.width == 0 || view.camera.targetExtent.height == 0))
        {
            // 크기를 모르면 뷰포트가 타깃 안에 있는지 잴 수 없다. 짐작하지 않는다.
            return false;
        }
        const TextureHandle targetTexture = ownTarget ? view.camera.target : frameTexture;
        const RenderGraphTexture target = ownTarget ? m_graph.ImportTexture(targetTexture, "ViewTarget") : frameTarget;
        const Extent2D extent = ownTarget ? view.camera.targetExtent : frameExtent;
        if (false == target.IsValid())
        {
            return false;
        }

        Bool alreadyCleared = false;
        for (std::size_t at = 0; at < m_clearedTargetCount; ++at)
        {
            if (m_clearedTargets[at] == targetTexture)
            {
                alreadyCleared = true;
                break;
            }
        }
        if (false == alreadyCleared && m_clearedTargetCount < MaxClearedTargets)
        {
            m_clearedTargets[m_clearedTargetCount++] = targetTexture;
        }

        Viewport viewport = view.camera.viewport;
        if (viewport.width <= 0.0f || viewport.height <= 0.0f)
        {
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<JBro::Float>(extent.width);
            viewport.height = static_cast<JBro::Float>(extent.height);
        }

        const Float right = viewport.x + viewport.width;
        const Float bottom = viewport.y + viewport.height;
        if (viewport.x < 0.0f
            || viewport.y < 0.0f
            || right > static_cast<JBro::Float>(extent.width)
            || bottom > static_cast<JBro::Float>(extent.height)
            || viewport.minDepth < 0.0f
            || viewport.maxDepth > 1.0f
            || viewport.minDepth > viewport.maxDepth)
        {
            return false;
        }

        // 소수 자리의 뷰포트를 정수 시저로 옮긴다. 안쪽으로 자르면 마지막 열이 잘린다 - 바깥으로 넉넉히 잡는다.
        const ScissorRect scissor = {
            static_cast<JBro::Int32>(std::floor(viewport.x)),
            static_cast<JBro::Int32>(std::floor(viewport.y)),
            static_cast<JBro::Int32>(std::ceil(right)),
            static_cast<JBro::Int32>(std::ceil(bottom))};

        // **메시나 월드 텍스트가 있는 뷰만 깊이를 단다**(framework3d-plan §2.4, D-222). 스프라이트만 있는 2D 프레임은 깊이 없는 패스다.
        // 뷰마다 지운다 - 카메라가 다르면 깊이도 다른 것이고, 3D 레이어는 레이어마다 뷰라 레이어마다 지운다(D-280).
        // **처음 보는 크기의 뷰는 그 프레임에 메시·월드 텍스트 없이 그린다**(D-288) - 깊이는 선택 첨부라 없으면 패스가 깊이 없이 돈다.
        // 프레임 타깃 크기의 깊이는 `BeginFrame` 이 풀에 미리 세우므로 게임 화면은 늘 깊이가 있다.
        const Bool depthWork = view.runCount != 0 || view.worldTextRunCount != 0;
        RenderGraphTexture depth;
        if (depthWork)
        {
            depth = m_graph.CreateTexture({extent, TextureFormat::D32Float, TextureUsage::DepthStencil}, "Depth");
        }

        // **라이트맵을 먼저 그린다**(D-291). 빛을 받는 스프라이트가 있고 라이트를 받은 뷰만이다. 타깃과 같은 크기의 RGBA16F 이고, 빛을 받는
        // 구간이 제 픽셀 자리에서 읽어 곱한다 - 따로 합성하지 않으므로 레이어 묶음 안에서도 같다. 처음 보는 크기는 이 프레임에 빛 없이
        // 그리고 다음 프레임 전에 선다. 깊이가 달린 뷰(3D)는 라이팅을 보지 않는다.
        RenderGraphTexture lightMap;
        if (view.hasLitRun && view.lighting && false == depthWork && m_light2DPipeline.IsValid())
        {
            lightMap = m_graph.CreateTexture(
                {extent, TextureFormat::RGBA16Float, TextureUsage::RenderTarget | TextureUsage::Sampled}, "LightMap");
            if (false == AddLightMapPasses(viewIndex, lightMap, extent, viewport, scissor))
            {
                return false;
            }
        }

        const ClearColor viewClear = {
            view.camera.clearColor[0], view.camera.clearColor[1], view.camera.clearColor[2], view.camera.clearColor[3]};
        const ClearColor transparent = {0.0f, 0.0f, 0.0f, 0.0f};
        const LoadOperation targetLoad = alreadyCleared ? LoadOperation::Load : LoadOperation::Clear;
        const Bool wantsComposite =
            view.camera.composite != CompositeBlend::Normal || view.camera.compositeOpacity < 1.0f || view.layerPostCount != 0;

        // **후처리를 거는 뷰는 장면을 제 텍스처(`SceneColor`)에 그린다**(D-342). 효과가 HDR 을 바라면 그 텍스처는 `RGBA16Float` 이고, 레이어 텍스처와
        // 아래 그림 사본도 같은 포맷이다. 그 타깃에 앞 뷰가 이미 그렸으면 그것을 장면에 옮겨 두고 이어 그린다. 다 그린 장면에 효과를 지점 차례로
        // 걸고, 마지막 효과가 타깃의 뷰포트 안에 쓴다. 파이프라인이나 텍스처가 아직 없으면 바라 두고 이 프레임은 후처리 없이 그린다.
        RenderGraphTexture drawTarget = target;
        TextureFormat sceneFormat = m_config.backBufferFormat;
        PostPlan postPlan;
        Bool postProcess = false;
        if (view.postCount != 0 && wantsComposite)
        {
            ++m_currentStats.unpostprocessedCompositeViewCount;
        }
        else if (view.postCount != 0 && m_postFailed)
        {
            // 후처리 파이프라인을 만들지 못한 장치다. 텍스처를 바라지 않고 그대로 그린다.
            ++m_currentStats.postProcessWaitingViewCount;
        }
        else if (view.postCount != 0 && PlanPostProcess(view, postPlan))
        {
            const TextureFormat plannedFormat = postPlan.hdr ? PostProcessHdrFormat : m_config.backBufferFormat;
            const TransientTextureDesc sceneDesc = {
                extent, plannedFormat, TextureUsage::RenderTarget | TextureUsage::Sampled | TextureUsage::CopyDestination};
            const TransientTextureDesc loadDesc = {
                extent, m_config.backBufferFormat, TextureUsage::RenderTarget | TextureUsage::Sampled | TextureUsage::CopyDestination};
            // 효과가 만들 텍스처를 센다(효과 사이의 중간 텍스처와 블룸의 밉 체인 같은 효과 안의 것).
            PostProcessBuilder::TextureCount counts[PostProcessBuilder::MaxCountedShapes];
            UInt32 countSize = 0;
            const Bool counted = CountPostProcessTextures(viewIndex, postPlan, plannedFormat, m_config.backBufferFormat, false, false, extent,
                viewport, scissor, counts, countSize);
            Bool ready = m_postReady && counted;
            if (false == m_postReady)
            {
                m_postWanted = true;
            }
            ready = m_targetPool.Request(sceneDesc, 1) && ready;
            if (alreadyCleared && postPlan.hdr)
            {
                ready = m_targetPool.Request(loadDesc, 1) && ready;
            }
            for (UInt32 at = 0; at < countSize; ++at)
            {
                ready = m_targetPool.Request(counts[at.Get()].desc, counts[at.Get()].count) && ready;
            }
            if (ready)
            {
                postProcess = true;
                sceneFormat = plannedFormat;
                drawTarget = m_graph.CreateTexture(sceneDesc, "SceneColor");
                if (alreadyCleared)
                {
                    // 앞 뷰가 그린 타깃을 장면으로 옮긴다. 포맷이 같으면 복사 하나이고, HDR 이면 같은 포맷의 사본을 떠서 옮겨 그린다.
                    CopyPassData copy;
                    copy.source = target;
                    copy.destination = postPlan.hdr ? m_graph.CreateTexture(loadDesc, "SceneLoad") : drawTarget;
                    const RenderGraphPass load = m_graph.AddPass(RenderGraphPassKind::Copy, "SceneLoad", &Renderer::ExecuteCopy, viewIndex);
                    if (false == m_graph.ReadCopySource(load, target) || false == m_graph.WriteCopyDestination(load, copy.destination)
                        || false == m_graph.SetPassData(load, copy))
                    {
                        return false;
                    }
                    if (postPlan.hdr)
                    {
                        PostProcessBuilder convert;
                        convert.m_renderer = this;
                        convert.m_view = viewIndex;
                        convert.m_input = copy.destination;
                        convert.m_output = drawTarget;
                        convert.m_inputFormat = m_config.backBufferFormat;
                        convert.m_outputFormat = sceneFormat;
                        convert.m_outputLoad = LoadOperation::Discard;
                        convert.m_extent = extent;
                        convert.m_viewport.width = static_cast<JBro::Float>(extent.width);
                        convert.m_viewport.height = static_cast<JBro::Float>(extent.height);
                        convert.m_scissor = {0, 0, static_cast<JBro::Int32>(extent.width), static_cast<JBro::Int32>(extent.height)};
                        if (false == convert.AddFullscreenPass("SceneConvert", PostProcessProgram::Copy, {}))
                        {
                            return false;
                        }
                    }
                }
            }
            else
            {
                ++m_currentStats.postProcessWaitingViewCount;
            }
        }
        // 레이어를 그려 둘 텍스처와 아래 그림의 사본은 그리는 곳에 얹거나 그리는 곳을 복사하는 자리라 그 포맷이다.
        const TransientTextureDesc layerDesc = {
            extent, sceneFormat, TextureUsage::RenderTarget | TextureUsage::Sampled | TextureUsage::CopyDestination};

        SpritePassData sprites;
        sprites.renderer = this;
        sprites.viewport = viewport;
        sprites.scissor = scissor;
        sprites.view = viewIndex;
        sprites.lightMap = lightMap;
        sprites.depth = depth;
        sprites.scene = SceneIndexOf(sceneFormat);
        const UInt32 runBegin = view.spriteRunOffset;
        const UInt32 runEnd = view.spriteRunOffset + view.spriteRunCount;

        // 스프라이트 구간 하나를 그리는 패스를 더한다. 라이트맵은 선택으로 읽고 깊이는 선택으로 단다.
        const auto addSpritePass = [&](const char* name, RenderGraphTexture output, LoadOperation load, const ClearColor& clear,
                                       const SpritePassData& data) -> RenderGraphPass {
            const RenderGraphPass pass = m_graph.AddPass(RenderGraphPassKind::Raster, name, &Renderer::ExecuteSprites, viewIndex);
            if (false == m_graph.SetColorAttachment(pass, output, load, clear)
                || (data.depth.IsValid()
                    && false == m_graph.SetDepthAttachment(pass, data.depth, LoadOperation::Clear, StoreOperation::Discard, true))
                || (data.lightMap.IsValid() && false == m_graph.Read(pass, data.lightMap, true))
                || false == m_graph.SetPassData(pass, data))
            {
                return {};
            }
            return pass;
        };

        // **뷰를 통째로 얹는 경우**(D-280, 3D 레이어). 그릴 곳이 타깃이 아니라 레이어 텍스처다. 이 뷰 안의 스프라이트 묶음은 보지 않는다.
        // 텍스처가 아직 없는 크기면 타깃에 그대로 그린다(대신 그리기). 후처리는 받지 않는다(위).
        if (wantsComposite)
        {
            const RenderGraphTexture viewLayer = m_graph.CreateTexture(layerDesc, "ViewLayer");
            // 아래 그림을 읽는 블렌드면 사본 자리도 같은 프레임에 바란다 - 그래야 둘째 프레임부터 제 식으로 얹힌다(D-283).
            const RenderGraphTexture backdrop = static_cast<JBro::UInt32>(view.camera.composite) >= FirstBackdropBlend
                ? m_graph.CreateTexture(layerDesc, "Backdrop")
                : RenderGraphTexture{};
            if (false == alreadyCleared)
            {
                // 이 타깃의 첫 뷰다. 레이어 텍스처에 그리더라도 타깃은 지워야 지난 프레임이 비치지 않는다.
                const RenderGraphPass clear =
                    m_graph.AddPass(RenderGraphPassKind::Raster, "ViewClear", &Renderer::ExecuteNothing, viewIndex);
                if (false == m_graph.SetColorAttachment(clear, target, LoadOperation::Clear, viewClear)
                    || false == m_graph.RunIfAvailable(clear, viewLayer))
                {
                    return false;
                }
            }
            SpritePassData whole = sprites;
            whole.firstRun = runBegin;
            whole.endRun = runEnd;
            whole.drawDepthWork = true;
            whole.countView = true;
            if (false == addSpritePass("ViewLayer", viewLayer, LoadOperation::Clear, transparent, whole).IsValid())
            {
                return false;
            }
            whole.uncomposited = true;
            const RenderGraphPass direct = addSpritePass("ViewDirect", target, targetLoad, viewClear, whole);
            // 레이어 효과(D-345)를 건 뒤 얹는다. 레이어 텍스처가 없는 프레임에는 효과 패스도 돌지 않고 대신 그리기만 돈다.
            RenderGraphTexture composited = viewLayer;
            if (false == direct.IsValid() || false == m_graph.RunIfMissing(direct, viewLayer)
                || false == AddLayerPostPasses(viewIndex, view.layerPostOffset, view.layerPostCount, sceneFormat, extent, composited)
                || false == AddCompositePasses(viewIndex, composited, backdrop, target, view.camera.composite, view.camera.compositeOpacity,
                    viewport, scissor, LdrScene))
            {
                return false;
            }
        }
        else if (depthWork)
        {
            // 깊이가 달린 뷰는 패스를 끊으면 깊이를 다시 실어야 해서 레이어 묶음을 보지 않고 그대로 그린다.
            SpritePassData whole = sprites;
            whole.firstRun = runBegin;
            whole.endRun = runEnd;
            whole.drawDepthWork = true;
            whole.countView = true;
            if (false == addSpritePass("View", drawTarget, targetLoad, viewClear, whole).IsValid())
            {
                return false;
            }
        }
        else
        {
            // **레이어 묶음은 제 텍스처에 그렸다 얹는다**(D-279). 묶음 밖의 구간은 타깃에 바로 그리고, 묶음마다 레이어 텍스처를 투명하게
            // 지워 그린 뒤 타깃에 얹는다. 첫 구간 패스는 비어 있어도 둔다 - 타깃을 지우는 자리다.
            UInt32 run = runBegin;
            UInt32 segmentEnd = run;
            while (segmentEnd < runEnd && m_spriteRuns[segmentEnd.Get()].layerGroup == NoLayerGroup)
            {
                ++segmentEnd;
            }
            SpritePassData segment = sprites;
            segment.firstRun = run;
            segment.endRun = segmentEnd;
            segment.countView = true;
            if (false == addSpritePass("View", drawTarget, targetLoad, viewClear, segment).IsValid())
            {
                return false;
            }
            run = segmentEnd;
            while (run < runEnd)
            {
                const UInt32 group = m_spriteRuns[run.Get()].layerGroup;
                UInt32 end = run;
                while (end < runEnd && m_spriteRuns[end.Get()].layerGroup == group)
                {
                    ++end;
                }
                SpritePassData part = sprites;
                part.firstRun = run;
                part.endRun = end;
                if (group == NoLayerGroup)
                {
                    if (false == addSpritePass("View", drawTarget, LoadOperation::Load, viewClear, part).IsValid())
                    {
                        return false;
                    }
                }
                else
                {
                    const LayerGroup& layerGroup = m_layerGroups[group.Get()];
                    const RenderGraphTexture layer = m_graph.CreateTexture(layerDesc, "Layer");
                    const RenderGraphTexture backdrop = static_cast<JBro::UInt32>(layerGroup.blend) >= FirstBackdropBlend
                        ? m_graph.CreateTexture(layerDesc, "Backdrop")
                        : RenderGraphTexture{};
                    if (false == addSpritePass("Layer", layer, LoadOperation::Clear, transparent, part).IsValid())
                    {
                        return false;
                    }
                    // 텍스처가 없는 묶음은 그대로 그린다. 그 크기의 텍스처는 다음 프레임을 열 때 생긴다.
                    part.uncomposited = true;
                    const RenderGraphPass direct = addSpritePass("LayerDirect", drawTarget, LoadOperation::Load, viewClear, part);
                    RenderGraphTexture composited = layer;
                    if (false == direct.IsValid() || false == m_graph.RunIfMissing(direct, layer)
                        || false == AddLayerPostPasses(viewIndex, layerGroup.postOffset, layerGroup.postCount, sceneFormat, extent, composited)
                        || false == AddCompositePasses(viewIndex, composited, backdrop, drawTarget, layerGroup.blend, layerGroup.opacity,
                            viewport, scissor, SceneIndexOf(sceneFormat)))
                    {
                        return false;
                    }
                }
                run = end;
            }
        }

        // 다 그린 장면에 효과를 건다. 마지막 효과가 타깃에 쓴다 - 처음 그리는 타깃이면 뷰포트 밖을 지우는 색으로 지운다.
        if (postProcess)
        {
            ++m_currentStats.postProcessedViewCount;
            if (false == AddPostProcessPasses(viewIndex, postPlan, drawTarget, target, targetLoad, viewClear, extent, viewport, scissor))
            {
                return false;
            }
        }

        // **이 뷰를 다 그린 뒤 마스크의 둘레를 덧그린다**(D-276). 마스크와 사본 자리는 에디터가 준 텍스처다. 후처리 뒤다.
        if (view.camera.outlineMask.IsValid() && view.camera.outlineScratch.IsValid() && view.camera.outlineWidth != 0)
        {
            const RenderGraphTexture mask = m_graph.ImportTexture(view.camera.outlineMask, "OutlineMask");
            const RenderGraphTexture scratch = m_graph.ImportTexture(view.camera.outlineScratch, "OutlineScratch");
            OutlinePassData outline;
            outline.renderer = this;
            for (Int32 channel = 0; channel < 4; ++channel)
            {
                outline.color[channel] = view.camera.outlineColor[channel];
            }
            outline.width = view.camera.outlineWidth;
            outline.extent = extent;
            // 첫 패스는 마스크를 가로로 키워 사본에 덮어쓰고, 둘째 패스는 사본을 세로로 키우며 마스크 밖의 둘레만 타깃에 칠한다.
            // 둘째 패스만 마스크를 t1 로도 읽는다. 두 셰이더 모두 `Load` 로만 읽어 샘플러를 묶지 않고, 첫째 프로그램은 텍스처 하나만
            // 선언한다(매니페스트, D-329 - 반사 시험이 찾았다).
            outline.first = mask;
            const RenderGraphPass grow = m_graph.AddPass(RenderGraphPassKind::Raster, "OutlineGrow", &Renderer::ExecuteOutline, viewIndex);
            if (false == m_graph.SetColorAttachment(grow, scratch, LoadOperation::Clear, transparent) || false == m_graph.Read(grow, mask)
                || false == m_graph.SetPassData(grow, outline))
            {
                return false;
            }
            outline.first = scratch;
            outline.second = mask;
            outline.composite = true;
            const RenderGraphPass edge = m_graph.AddPass(RenderGraphPassKind::Raster, "Outline", &Renderer::ExecuteOutline, viewIndex);
            if (false == m_graph.SetColorAttachment(edge, target, LoadOperation::Load) || false == m_graph.Read(edge, scratch)
                || false == m_graph.Read(edge, mask) || false == m_graph.SetPassData(edge, outline))
            {
                return false;
            }
        }
        return true;
    }

    Bool Renderer::AddCompositePasses(UInt32 viewIndex, RenderGraphTexture layer, RenderGraphTexture backdrop, RenderGraphTexture target,
        CompositeBlend blend, Float opacity, const Viewport& viewport, const ScissorRect& scissor, UInt32 scene)
    {
        // 아래 그림을 읽는 블렌드는 레이어를 얹기 직전의 타깃을 사본에 복사해 두고, 셰이더가 둘을 섞어 그 자리를 덮어쓴다(D-283).
        if (backdrop.IsValid())
        {
            CopyPassData copy;
            copy.source = target;
            copy.destination = backdrop;
            const RenderGraphPass pass = m_graph.AddPass(RenderGraphPassKind::Copy, "Backdrop", &Renderer::ExecuteCopy, viewIndex);
            if (false == m_graph.ReadCopySource(pass, target) || false == m_graph.WriteCopyDestination(pass, backdrop)
                || false == m_graph.RunIfAvailable(pass, layer) || false == m_graph.SetPassData(pass, copy))
            {
                return false;
            }
        }
        CompositePassData composite;
        composite.renderer = this;
        composite.viewport = viewport;
        composite.scissor = scissor;
        composite.layer = layer;
        composite.backdrop = backdrop;
        composite.blend = blend;
        composite.opacity = opacity;
        composite.scene = scene;
        const RenderGraphPass pass = m_graph.AddPass(RenderGraphPassKind::Raster, "Composite", &Renderer::ExecuteComposite, viewIndex);
        return m_graph.SetColorAttachment(pass, target, LoadOperation::Load) && m_graph.Read(pass, layer)
            && (false == backdrop.IsValid() || m_graph.Read(pass, backdrop, true)) && m_graph.SetPassData(pass, composite);
    }

    Bool Renderer::AddLightMapPasses(UInt32 viewIndex, RenderGraphTexture lightMap, const Extent2D& extent, const Viewport& viewport,
        const ScissorRect& scissor)
    {
        const ViewPacket& view = m_views[viewIndex.Get()];
        LightPassData light;
        light.renderer = this;
        light.viewport = viewport;
        light.scissor = scissor;
        light.view = viewIndex;
        // 환경광으로 지우고 그림자 없는 라이트를 한 번에 더한다. 알파는 읽지 않는다.
        const RenderGraphPass ambient = m_graph.AddPass(RenderGraphPassKind::Raster, "LightMap", &Renderer::ExecuteLightMap, viewIndex);
        if (false == m_graph.SetColorAttachment(ambient, lightMap, LoadOperation::Clear, {view.ambient[0], view.ambient[1], view.ambient[2], 0.0f})
            || false == m_graph.SetPassData(ambient, light))
        {
            return false;
        }
        // **그림자를 드리우는 라이트는 하나씩이다**(D-291 3 단계). 마스크를 지우고 그 라이트에서 이 뷰의 변을 밀어내 칠한 뒤, 마스크로 깎으며 그 라이트
        // 하나를 라이트맵에 더한다. 마스크는 라이트마다 그래프 텍스처이고 수명이 겹치지 않아 실제 텍스처 하나를 나눠 쓴다. 마스크가 아직 없는 크기는
        // 그 프레임에 그림자 없이 더한다(대신 그리기).
        const UInt32 shadowedBegin = view.lightOffset + view.plainLightCount;
        const UInt32 shadowedEnd = view.lightOffset + view.lightCount;
        for (std::size_t at = 0; at < m_shadowedLights.Size(); ++at)
        {
            const ShadowedLight& shadowed = m_shadowedLights[at];
            if (shadowed.instance < shadowedBegin || shadowed.instance >= shadowedEnd)
            {
                continue;
            }
            const RenderGraphTexture mask = m_graph.CreateTexture(
                {extent, TextureFormat::RGBA16Float, TextureUsage::RenderTarget | TextureUsage::Sampled}, "ShadowMask");
            light.shadowedLight = static_cast<JBro::UInt32>(at);
            light.mask = mask;
            const RenderGraphPass maskPass =
                m_graph.AddPass(RenderGraphPassKind::Raster, "ShadowMask", &Renderer::ExecuteShadowMask, viewIndex);
            if (false == m_graph.SetColorAttachment(maskPass, mask, LoadOperation::Clear, {0.0f, 0.0f, 0.0f, 0.0f})
                || false == m_graph.SetPassData(maskPass, light))
            {
                return false;
            }
            const RenderGraphPass shadowedPass =
                m_graph.AddPass(RenderGraphPassKind::Raster, "ShadowedLight", &Renderer::ExecuteShadowedLight, viewIndex);
            if (false == m_graph.SetColorAttachment(shadowedPass, lightMap, LoadOperation::Load) || false == m_graph.Read(shadowedPass, mask)
                || false == m_graph.SetPassData(shadowedPass, light))
            {
                return false;
            }
            const RenderGraphPass plainPass =
                m_graph.AddPass(RenderGraphPassKind::Raster, "Light", &Renderer::ExecuteUnshadowedLight, viewIndex);
            if (false == m_graph.SetColorAttachment(plainPass, lightMap, LoadOperation::Load) || false == m_graph.RunIfMissing(plainPass, mask)
                || false == m_graph.SetPassData(plainPass, light))
            {
                return false;
            }
        }
        return true;
    }

    Bool Renderer::DrawSpriteRuns(IRHICommandContext& commands, const ScenePipelines& pipelines, const ViewPacket& view, UInt32 firstRun,
        UInt32 endRun, TextureHandle lightMap, Bool overDepth)
    {
        if (firstRun >= endRun)
        {
            return true;
        }
        const Matrix4x4 viewProjection = Multiply(view.camera.projection, view.camera.view);
        const JArrayView<std::byte> constants = {
            reinterpret_cast<const std::byte*>(viewProjection.values), static_cast<JBro::UInt32>(sizeof(viewProjection.values))};
        // 스프라이트와 SDF 텍스트가 번갈아 오면 구간마다 파이프라인과 인스턴스 버퍼를 갈아 끼운다. 파이프라인을 바꾸면
        // 루트 상수와 텍스처 자리가 비므로 상수도 다시 넣는다.
        // 빛을 받는 구간은 라이트맵이 있을 때만 라이트맵을 곱하는 파이프라인이다(D-291). 라이트맵은 깊이가 없는 뷰에만 선다.
        const auto bindShading = [&](Bool sdf, Bool lit) {
            const GraphicsPipelineHandle pipeline = lit
                ? (sdf ? pipelines.litSdfText : pipelines.litSprite)
                : sdf
                ? (overDepth ? pipelines.sdfTextOverDepth : pipelines.sdfText)
                : (overDepth ? pipelines.spriteOverDepth : pipelines.sprite);
            return commands.SetGraphicsPipeline(pipeline)
                && commands.SetVertexBuffer(
                    1,
                    sdf ? m_textInstanceBuffers[m_frame.slot] : m_spriteInstanceBuffers[m_frame.slot],
                    sdf ? static_cast<JBro::UInt32>(sizeof(GpuTextInstance)) : static_cast<JBro::UInt32>(sizeof(GpuSpriteInstance)),
                    0)
                && commands.SetGraphicsConstants(constants);
        };
        const auto litOf = [&](const SpriteRun& run) {
            return run.lit && lightMap.IsValid();
        };
        Bool boundSdf = m_spriteRuns[firstRun.Get()].sdf;
        Bool boundLit = litOf(m_spriteRuns[firstRun.Get()]);
        if (false == bindShading(boundSdf, boundLit)
            || false == commands.SetVertexBuffer(0, m_spriteVertexBuffer, static_cast<JBro::UInt32>(sizeof(float) * 2), 0)
            || false == commands.SetIndexBuffer(m_spriteIndexBuffer, IndexFormat::UInt16, 0))
        {
            return false;
        }
        for (UInt32 runIndex = firstRun; runIndex < endRun; ++runIndex)
        {
            const SpriteRun& run = m_spriteRuns[runIndex.Get()];
            const Bool runLit = litOf(run);
            if (run.sdf != boundSdf || runLit != boundLit)
            {
                if (false == bindShading(run.sdf, runLit))
                {
                    return false;
                }
                boundSdf = run.sdf;
                boundLit = runLit;
            }
            if ((runLit && false == commands.SetTexture(1, lightMap))
                || false == commands.SetTexture(0, run.texture)
                || false == commands.SetSampler(0, run.sampler)
                || false == commands.DrawIndexedInstanced(6, run.instanceCount, 0, 0, run.firstInstance))
            {
                return false;
            }
        }
        return true;
    }

    Bool Renderer::ExecuteNothing(RenderGraphContext&, const void*)
    {
        // 지우기만 하는 패스다. 지우는 것은 그래프가 패스를 열 때 첨부의 `Clear` 로 한다.
        return true;
    }

    Bool Renderer::ExecuteSprites(RenderGraphContext& context, const void* data)
    {
        const SpritePassData& pass = *static_cast<const SpritePassData*>(data);
        Renderer& renderer = *pass.renderer;
        const ViewPacket& view = renderer.m_views[pass.view.Get()];
        IRHICommandContext& commands = context.GetCommands();
        commands.SetViewport(pass.viewport);
        commands.SetScissor(pass.scissor);

        const Bool depthWork = view.runCount != 0 || view.worldTextRunCount != 0;
        const Bool withDepth = context.GetTexture(pass.depth).IsValid();
        const TextureHandle lightMap = context.HasTexture(pass.lightMap) ? context.GetTexture(pass.lightMap) : TextureHandle{};
        if (pass.countView)
        {
            if (view.hasLitRun && view.lighting && false == lightMap.IsValid())
            {
                ++renderer.m_currentStats.viewsWithoutLightMapCount;
            }
            if (depthWork && false == withDepth)
            {
                ++renderer.m_currentStats.viewsWithoutDepthCount;
            }
            // 3D 뷰와 통째로 얹는 뷰의 묶음은 그대로 그렸다. 그렇다고 센다.
            const Bool wholeView =
                view.camera.composite != CompositeBlend::Normal || view.camera.compositeOpacity < 1.0f || view.layerPostCount != 0;
            if (depthWork || wholeView)
            {
                for (UInt32 at = 0; at < view.layerGroupCount; ++at)
                {
                    const LayerGroup& group = renderer.m_layerGroups[(view.layerGroupOffset + at).Get()];
                    if (group.endSprite > group.firstSprite)
                    {
                        ++renderer.m_currentStats.uncompositedLayerCount;
                    }
                }
            }
        }
        if (pass.uncomposited)
        {
            ++renderer.m_currentStats.uncompositedLayerCount;
        }

        const ScenePipelines& pipelines = renderer.m_scene[pass.scene.Get()];
        if (view.spriteCount != 0
            && false == renderer.DrawSpriteRuns(commands, pipelines, view, pass.firstRun, pass.endRun, lightMap, withDepth))
        {
            return false;
        }
        if (false == pass.drawDepthWork || false == withDepth)
        {
            return true;
        }

        const Matrix4x4 viewProjection = Multiply(view.camera.projection, view.camera.view);
        const JArrayView<std::byte> constants = {
            reinterpret_cast<const std::byte*>(viewProjection.values), static_cast<JBro::UInt32>(sizeof(viewProjection.values))};
        if (view.runCount != 0)
        {
            if (false == commands.SetGraphicsPipeline(pipelines.mesh)
                || false == commands.SetVertexBuffer(
                    1, renderer.m_meshInstanceBuffers[renderer.m_frame.slot], static_cast<JBro::UInt32>(sizeof(GpuMeshInstance)), 0)
                || false == commands.SetGraphicsConstants(constants))
            {
                return false;
            }
            // 메시 종류마다 한 드로우다(D-110). 업로드가 같은 메시를 이어 놓았다.
            for (UInt32 at = 0; at < view.runCount; ++at)
            {
                const MeshRun& run = renderer.m_meshRuns[(view.runOffset + at).Get()];
                const MeshResource* mesh = renderer.FindMesh(run.mesh);
                if (mesh == nullptr)
                {
                    // 업로드와 기록 사이에 사라질 길은 없지만, 있다면 업로드와 같은 정책이다: 그 묶음만 건너뛴다.
                    renderer.m_currentStats.droppedMeshCount += run.instanceCount;
                    continue;
                }
                if (false == commands.SetVertexBuffer(0, mesh->vertexBuffer, static_cast<JBro::UInt32>(sizeof(MeshVertex)), 0)
                    || false == commands.SetIndexBuffer(mesh->indexBuffer, IndexFormat::UInt32, 0)
                    || false == commands.DrawIndexedInstanced(mesh->indexCount, run.instanceCount, 0, 0, run.firstInstance))
                {
                    return false;
                }
            }
        }

        // 월드 텍스트는 메시 **뒤**다 - 메시가 쓴 깊이로 가려진다. 파이프라인은 깊이를 보되 쓰지 않는다.
        if (view.worldTextRunCount != 0)
        {
            if (false == commands.SetGraphicsPipeline(pipelines.worldText)
                || false == commands.SetVertexBuffer(0, renderer.m_spriteVertexBuffer, static_cast<JBro::UInt32>(sizeof(float) * 2), 0)
                || false == commands.SetVertexBuffer(1, renderer.m_worldTextInstanceBuffers[renderer.m_frame.slot],
                    static_cast<JBro::UInt32>(sizeof(GpuWorldTextInstance)), 0)
                || false == commands.SetIndexBuffer(renderer.m_spriteIndexBuffer, IndexFormat::UInt16, 0)
                || false == commands.SetGraphicsConstants(constants))
            {
                return false;
            }
            for (UInt32 runIndex = 0; runIndex < view.worldTextRunCount; ++runIndex)
            {
                const SpriteRun& run = renderer.m_worldTextRuns[(view.worldTextRunOffset + runIndex).Get()];
                if (false == commands.SetTexture(0, run.texture)
                    || false == commands.SetSampler(0, run.sampler)
                    || false == commands.DrawIndexedInstanced(6, run.instanceCount, 0, 0, run.firstInstance))
                {
                    return false;
                }
            }
        }
        return true;
    }

    Bool Renderer::ExecuteComposite(RenderGraphContext& context, const void* data)
    {
        // 화면을 덮는 사각형 하나다. 레이어 텍스처는 미리 곱한 색이라 `Layer*` 블렌드로 얹고, 불투명도는 색과 알파에 함께 곱한다.
        // 아래 그림을 읽는 블렌드는 앞의 복사 패스가 채운 사본과 섞어 그 자리를 덮어쓴다(D-283). 사본 자리가 아직 없으면 표준으로 얹는다.
        const CompositePassData& pass = *static_cast<const CompositePassData*>(data);
        Renderer& renderer = *pass.renderer;
        IRHICommandContext& commands = context.GetCommands();
        const UInt32 blend = static_cast<JBro::UInt32>(pass.blend);
        const ScenePipelines& pipelines = renderer.m_scene[pass.scene.Get()];
        GraphicsPipelineHandle pipeline = pipelines.layerComposite[(blend < FirstBackdropBlend ? blend : UInt32(0)).Get()];
        TextureHandle backdrop;
        if (blend >= FirstBackdropBlend && blend < CompositeBlendCount)
        {
            if (context.HasTexture(pass.backdrop))
            {
                backdrop = context.GetTexture(pass.backdrop);
                pipeline = pipelines.layerBackdrop;
            }
            else
            {
                // 복사할 자리가 다음 프레임에 선다. 이 프레임은 표준으로 얹는다.
                ++renderer.m_currentStats.uncompositedLayerCount;
            }
        }
        const Float constants[4] = {
            pass.opacity, backdrop.IsValid() ? static_cast<JBro::Float>(blend - FirstBackdropBlend) : Float(0.0f), 0.0f, 0.0f};
        commands.SetViewport(pass.viewport);
        commands.SetScissor(pass.scissor);
        if (false == commands.SetGraphicsPipeline(pipeline)
            || false == commands.SetGraphicsConstants(
                {reinterpret_cast<const std::byte*>(constants), static_cast<JBro::UInt32>(sizeof(constants))})
            || false == commands.SetVertexBuffer(0, renderer.m_spriteVertexBuffer, static_cast<JBro::UInt32>(sizeof(float) * 2), 0)
            || false == commands.SetIndexBuffer(renderer.m_spriteIndexBuffer, IndexFormat::UInt16, 0)
            || false == commands.SetTexture(0, context.GetTexture(pass.layer))
            || (backdrop.IsValid() && false == commands.SetTexture(1, backdrop))
            || false == commands.DrawIndexedInstanced(6, 1, 0, 0, 0))
        {
            return false;
        }
        ++renderer.m_currentStats.compositedLayerCount;
        return true;
    }

    Bool Renderer::ExecuteCopy(RenderGraphContext& context, const void* data)
    {
        const CopyPassData& pass = *static_cast<const CopyPassData*>(data);
        return context.GetCommands().CopyTexture(context.GetTexture(pass.source), context.GetTexture(pass.destination));
    }

    Bool Renderer::ExecuteOutline(RenderGraphContext& context, const void* data)
    {
        // 화면을 덮는 사각형 하나다(D-276). 단위 쿼드를 두 배로 펴서 쓴다 - 정점 버퍼를 따로 두지 않는다.
        const OutlinePassData& pass = *static_cast<const OutlinePassData*>(data);
        Renderer& renderer = *pass.renderer;
        IRHICommandContext& commands = context.GetCommands();
        struct OutlineConstants
        {
            Float color[4];
            Float params[4];
        };
        OutlineConstants constants = {};
        for (Int32 channel = 0; channel < 4; ++channel)
        {
            constants.color[channel] = pass.color[channel];
        }
        constants.params[0] = static_cast<JBro::Float>(pass.width);
        Viewport viewport;
        viewport.width = static_cast<JBro::Float>(pass.extent.width);
        viewport.height = static_cast<JBro::Float>(pass.extent.height);
        commands.SetViewport(viewport);
        commands.SetScissor({0, 0, static_cast<JBro::Int32>(pass.extent.width), static_cast<JBro::Int32>(pass.extent.height)});
        const TextureHandle second = pass.second.IsValid() ? context.GetTexture(pass.second) : TextureHandle{};
        return commands.SetGraphicsPipeline(pass.composite ? renderer.m_outlineCompositePipeline : renderer.m_outlineGrowPipeline)
            && commands.SetGraphicsConstants({reinterpret_cast<const std::byte*>(&constants), static_cast<JBro::UInt32>(sizeof(constants))})
            && commands.SetVertexBuffer(0, renderer.m_spriteVertexBuffer, static_cast<JBro::UInt32>(sizeof(float) * 2), 0)
            && commands.SetIndexBuffer(renderer.m_spriteIndexBuffer, IndexFormat::UInt16, 0)
            && commands.SetTexture(0, context.GetTexture(pass.first))
            && (false == second.IsValid() || commands.SetTexture(1, second))
            && commands.DrawIndexedInstanced(6, 1, 0, 0, 0);
    }

    Bool Renderer::CreateBuiltinSpriteResources()
    {
        if (m_device == nullptr
            || m_config.maxSpriteSubmissions == 0
            || m_config.maxSpriteSubmissions
                > (std::numeric_limits<std::uint32_t>::max)() / sizeof(GpuSpriteInstance))
        {
            return false;
        }

        constexpr Float vertices[] = {
            -0.5f, -0.5f,
            -0.5f, 0.5f,
            0.5f, 0.5f,
            0.5f, -0.5f};
        constexpr std::uint16_t indices[] = {0, 1, 2, 0, 2, 3};

        BufferDesc vertexBufferDesc;
        vertexBufferDesc.size = sizeof(vertices);
        vertexBufferDesc.usage = BufferUsage::Vertex | BufferUsage::CopySource;
        vertexBufferDesc.memory = MemoryType::Upload;
        m_spriteVertexBuffer = m_device->CreateBuffer(vertexBufferDesc);
        if (false == m_spriteVertexBuffer.IsValid()
            || false == m_device->WriteBuffer(
                m_spriteVertexBuffer,
                0,
                {reinterpret_cast<const std::byte*>(vertices), static_cast<JBro::UInt32>(sizeof(vertices))}))
        {
            return false;
        }

        BufferDesc indexBufferDesc;
        indexBufferDesc.size = sizeof(indices);
        indexBufferDesc.usage = BufferUsage::Index | BufferUsage::CopySource;
        indexBufferDesc.memory = MemoryType::Upload;
        m_spriteIndexBuffer = m_device->CreateBuffer(indexBufferDesc);
        if (false == m_spriteIndexBuffer.IsValid()
            || false == m_device->WriteBuffer(
                m_spriteIndexBuffer,
                0,
                {reinterpret_cast<const std::byte*>(indices), static_cast<JBro::UInt32>(sizeof(indices))}))
        {
            return false;
        }

        BufferDesc instanceBufferDesc;
        instanceBufferDesc.size = static_cast<std::size_t>(m_config.maxSpriteSubmissions)
            * sizeof(GpuSpriteInstance);
        instanceBufferDesc.usage = BufferUsage::Vertex | BufferUsage::CopySource;
        instanceBufferDesc.memory = MemoryType::Upload;
        for (UInt32 index = 0; index < m_config.maxFramesInFlight; ++index)
        {
            m_spriteInstanceBuffers[index] = m_device->CreateBuffer(instanceBufferDesc);
            if (false == m_spriteInstanceBuffers[index].IsValid())
            {
                return false;
            }
        }

        const VertexAttributeDesc vertexAttributes[] = {
            {0, 0, VertexFormat::Float2}};
        const TextureFormat colorFormats[] = {m_config.backBufferFormat};

        // 텍스처가 없는 스프라이트의 자리다. 흰색 하나를 샘플링하면 틴트가 그대로 나온다 - 파이프라인이 텍스처
        // 하나를 선언했으므로 빈 채로는 그릴 수 없다(D-61).
        {
            TextureDesc whiteDesc;
            whiteDesc.extent = {1, 1};
            whiteDesc.format = TextureFormat::RGBA8Unorm;
            whiteDesc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
            m_whiteTexture = m_device->CreateTexture(whiteDesc);
            const std::byte white[4] = {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
            if (false == m_whiteTexture.IsValid() || false == m_device->WriteTexture(m_whiteTexture, 0, {white, 4}))
            {
                return false;
            }
            SamplerDesc nearest;
            nearest.minFilter = FilterMode::Nearest;
            nearest.magFilter = FilterMode::Nearest;
            m_nearestSampler = m_device->CreateSampler(nearest);
            SamplerDesc linear;
            m_linearSampler = m_device->CreateSampler(linear);
            if (false == m_nearestSampler.IsValid() || false == m_linearSampler.IsValid())
            {
                return false;
            }
        }

        // SDF 텍스트(4 단계). 같은 단위 쿼드와 뷰 상수이고, 인스턴스는 외곽선 색과 문턱을 더 든다.
        if (m_config.maxSpriteSubmissions > (std::numeric_limits<std::uint32_t>::max)() / sizeof(GpuTextInstance))
        {
            return false;
        }
        BufferDesc textBufferDesc = instanceBufferDesc;
        textBufferDesc.size = static_cast<std::size_t>(m_config.maxSpriteSubmissions) * sizeof(GpuTextInstance);
        for (UInt32 index = 0; index < m_config.maxFramesInFlight; ++index)
        {
            m_textInstanceBuffers[index] = m_device->CreateBuffer(textBufferDesc);
            if (false == m_textInstanceBuffers[index].IsValid())
            {
                return false;
            }
        }
        // 선택 외곽선(D-276). 같은 단위 쿼드의 위치만 읽고, 픽셀 셰이더가 텍스처 둘을 `Load` 로 읽는다.
        const VertexBufferLayoutDesc outlineLayouts[] = {
            {static_cast<JBro::UInt32>(sizeof(float) * 2), VertexStepMode::Vertex, {vertexAttributes, 1}}};
        GraphicsPipelineDesc outlineDesc;
        if (false == ApplyShader(outlineDesc, BuiltinShader::OutlineGrow, m_config.api, sizeof(float) * 8))
        {
            return false;
        }
        outlineDesc.vertexBuffers = {outlineLayouts, 1};
        outlineDesc.colorFormats = {colorFormats, 1};
        outlineDesc.blend = BlendMode::Opaque;
        outlineDesc.cull = CullMode::None;
        outlineDesc.depthTest = false;
        outlineDesc.depthWrite = false;
        m_outlineGrowPipeline = m_device->CreateGraphicsPipeline(outlineDesc);
        if (false == ApplyShader(outlineDesc, BuiltinShader::OutlineComposite, m_config.api, sizeof(float) * 8))
        {
            return false;
        }
        outlineDesc.blend = BlendMode::Alpha;
        m_outlineCompositePipeline = m_device->CreateGraphicsPipeline(outlineDesc);
        return m_outlineGrowPipeline.IsValid() && m_outlineCompositePipeline.IsValid();
    }

    Bool Renderer::CreateBuiltinMeshResources()
    {
        if (m_device == nullptr
            || m_config.maxMeshSubmissions == 0
            || m_config.maxMeshSubmissions
                > (std::numeric_limits<std::uint32_t>::max)() / sizeof(GpuMeshInstance))
        {
            return false;
        }
        BufferDesc instanceBufferDesc;
        instanceBufferDesc.size = static_cast<std::size_t>(m_config.maxMeshSubmissions) * sizeof(GpuMeshInstance);
        instanceBufferDesc.usage = BufferUsage::Vertex | BufferUsage::CopySource;
        instanceBufferDesc.memory = MemoryType::Upload;
        for (UInt32 slot = 0; slot < m_config.maxFramesInFlight && slot < MaxFrameSlots; ++slot)
        {
            m_meshInstanceBuffers[slot] = m_device->CreateBuffer(instanceBufferDesc);
            if (false == m_meshInstanceBuffers[slot].IsValid())
            {
                return false;
            }
        }

        return true;
    }

    void Renderer::DestroyBuiltinMeshResources()
    {
        if (m_device == nullptr)
        {
            return;
        }
        for (BufferHandle& buffer : m_meshInstanceBuffers)
        {
            if (buffer.IsValid())
            {
                m_device->DestroyBuffer(buffer);
                buffer = {};
            }
        }
    }

    Bool Renderer::CreateBuiltinLightResources()
    {
        // 상한이 0 이면 라이팅을 만들지 않는다. 라이트는 다 버려지고 빛을 받는 구간은 그대로 그려진다.
        if (m_device == nullptr || m_config.maxLights2D == 0)
        {
            return m_device != nullptr;
        }
        if (m_config.maxLights2D > (std::numeric_limits<std::uint32_t>::max)() / sizeof(GpuLight2DInstance))
        {
            return false;
        }
        BufferDesc instanceBufferDesc;
        instanceBufferDesc.size = static_cast<std::size_t>(m_config.maxLights2D) * sizeof(GpuLight2DInstance);
        instanceBufferDesc.usage = BufferUsage::Vertex | BufferUsage::CopySource;
        instanceBufferDesc.memory = MemoryType::Upload;
        for (UInt32 slot = 0; slot < m_config.maxFramesInFlight && slot < MaxFrameSlots; ++slot)
        {
            m_lightInstanceBuffers[slot] = m_device->CreateBuffer(instanceBufferDesc);
            if (false == m_lightInstanceBuffers[slot].IsValid())
            {
                return false;
            }
        }
        const VertexAttributeDesc vertexAttributes[] = {{0, 0, VertexFormat::Float2}};
        const VertexAttributeDesc instanceAttributes[] = {
            {1, static_cast<JBro::UInt32>(offsetof(GpuLight2DInstance, shape)), VertexFormat::Float4},
            {2, static_cast<JBro::UInt32>(offsetof(GpuLight2DInstance, color)), VertexFormat::Float4},
            {3, static_cast<JBro::UInt32>(offsetof(GpuLight2DInstance, cone)), VertexFormat::Float4}};
        const VertexBufferLayoutDesc layouts[] = {
            {static_cast<JBro::UInt32>(sizeof(float) * 2), VertexStepMode::Vertex, {vertexAttributes, 1}},
            {static_cast<JBro::UInt32>(sizeof(GpuLight2DInstance)), VertexStepMode::Instance, {instanceAttributes, 3}}};
        // 라이트맵은 1 을 넘는 빛을 담는다. 라이트는 서로 더한다 - `LayerAdditive` 가 `One·One` 이다.
        const TextureFormat colorFormats[] = {TextureFormat::RGBA16Float};
        GraphicsPipelineDesc desc;
        if (false == ApplyShader(desc, BuiltinShader::Light2D, m_config.api, sizeof(Matrix4x4)))
        {
            return false;
        }
        desc.vertexBuffers = {layouts, 2};
        desc.colorFormats = {colorFormats, 1};
        desc.blend = BlendMode::LayerAdditive;
        desc.cull = CullMode::None;
        desc.depthTest = false;
        desc.depthWrite = false;
        m_light2DPipeline = m_device->CreateGraphicsPipeline(desc);
        if (false == m_light2DPipeline.IsValid())
        {
            return false;
        }
        // 그림자를 드리우는 라이트 하나를 마스크(t0)로 깎으며 더한다. 같은 정점과 인스턴스다.
        GraphicsPipelineDesc shadowedDesc = desc;
        if (false == ApplyShader(shadowedDesc, BuiltinShader::Light2DShadowed, m_config.api, sizeof(Matrix4x4)))
        {
            return false;
        }
        m_shadowedLight2DPipeline = m_device->CreateGraphicsPipeline(shadowedDesc);
        if (false == m_shadowedLight2DPipeline.IsValid())
        {
            return false;
        }
        if (m_config.maxShadowEdges2D == 0)
        {
            return true;
        }
        // 그림자 마스크(D-291 3 단계): 변 하나가 인스턴스 하나이고, 단위 쿼드가 그 변의 두 끝과 그 먼 사본을 고른다.
        if (m_config.maxShadowEdges2D > (std::numeric_limits<std::uint32_t>::max)() / sizeof(GpuShadowEdgeInstance))
        {
            return false;
        }
        BufferDesc edgeBufferDesc;
        edgeBufferDesc.size = static_cast<std::size_t>(m_config.maxShadowEdges2D) * sizeof(GpuShadowEdgeInstance);
        edgeBufferDesc.usage = BufferUsage::Vertex | BufferUsage::CopySource;
        edgeBufferDesc.memory = MemoryType::Upload;
        for (UInt32 slot = 0; slot < m_config.maxFramesInFlight && slot < MaxFrameSlots; ++slot)
        {
            m_shadowEdgeBuffers[slot] = m_device->CreateBuffer(edgeBufferDesc);
            if (false == m_shadowEdgeBuffers[slot].IsValid())
            {
                return false;
            }
        }
        const VertexAttributeDesc edgeAttributes[] = {
            {1, static_cast<JBro::UInt32>(offsetof(GpuShadowEdgeInstance, edge)), VertexFormat::Float4},
            {2, static_cast<JBro::UInt32>(offsetof(GpuShadowEdgeInstance, flags)), VertexFormat::Float4}};
        const VertexBufferLayoutDesc edgeLayouts[] = {
            {static_cast<JBro::UInt32>(sizeof(float) * 2), VertexStepMode::Vertex, {vertexAttributes, 1}},
            {static_cast<JBro::UInt32>(sizeof(GpuShadowEdgeInstance)), VertexStepMode::Instance, {edgeAttributes, 2}}};
        // 마스크는 가려진 몫(0~1)이다. 겹친 그림자는 `One·One` 으로 더하고 라이트 패스가 1 에서 자른다. 이웃한 변의 반그림자가 나눠 가진 몫을
        // 더해 1 이 되어야 하므로 8 비트가 아니라 16 비트 실수다 - 8 비트면 이음매로 빛이 1/255 씩 샌다.
        const TextureFormat maskFormats[] = {TextureFormat::RGBA16Float};
        GraphicsPipelineDesc maskDesc;
        if (false == ApplyShader(maskDesc, BuiltinShader::Shadow2D, m_config.api, sizeof(Matrix4x4) + sizeof(float) * 4))
        {
            return false;
        }
        maskDesc.vertexBuffers = {edgeLayouts, 2};
        maskDesc.colorFormats = {maskFormats, 1};
        maskDesc.blend = BlendMode::LayerAdditive;
        maskDesc.cull = CullMode::None;
        maskDesc.depthTest = false;
        maskDesc.depthWrite = false;
        m_shadowMaskPipeline = m_device->CreateGraphicsPipeline(maskDesc);
        return m_shadowMaskPipeline.IsValid();
    }

    void Renderer::DestroyBuiltinLightResources()
    {
        if (m_device == nullptr)
        {
            return;
        }
        for (GraphicsPipelineHandle* pipeline : {&m_light2DPipeline, &m_shadowedLight2DPipeline, &m_shadowMaskPipeline})
        {
            if (pipeline->IsValid())
            {
                m_device->DestroyGraphicsPipeline(*pipeline);
                *pipeline = {};
            }
        }
        for (BufferHandle& buffer : m_shadowEdgeBuffers)
        {
            if (buffer.IsValid())
            {
                m_device->DestroyBuffer(buffer);
                buffer = {};
            }
        }
        for (BufferHandle& buffer : m_lightInstanceBuffers)
        {
            if (buffer.IsValid())
            {
                m_device->DestroyBuffer(buffer);
                buffer = {};
            }
        }
    }

    Bool Renderer::UploadLightInstances()
    {
        // 라이트는 뷰마다 낸 차례로 이어져 있다(`ViewPacket::lightOffset`). 뷰 안에서 그림자 없는 라이트를 앞에, 그림자를 드리우는 라이트를 뒤에
        // 놓는다 - 앞쪽은 인스턴스 드로우 한 번이고 뒤쪽은 하나씩이다. 그림자 변이 없는 뷰의 라이트는 그림자가 없는 것과 같다.
        m_shadowedLights.Clear();
        const std::size_t count = m_lights.Size();
        if (count == 0)
        {
            return UploadShadowEdges();
        }
        if (m_gpuLightInstances.Size() < count || m_frame.slot >= MaxFrameSlots || false == m_lightInstanceBuffers[m_frame.slot].IsValid())
        {
            return false;
        }
        for (ViewPacket& view : m_views)
        {
            const UInt32 end = view.lightOffset + view.lightCount;
            const Bool edges = view.shadowEdgeCount != 0 && m_shadowMaskPipeline.IsValid();
            UInt32 next = view.lightOffset;
            for (Int32 pass = 0; pass < 2; ++pass)
            {
                for (UInt32 at = view.lightOffset; at < end; ++at)
                {
                    const Bool shadowed = edges && m_lights[at].castShadows;
                    if (shadowed != (pass == 1))
                    {
                        continue;
                    }
                    WriteLightInstance(m_lights[at], m_gpuLightInstances[next]);
                    if (shadowed)
                    {
                        ShadowedLight entry;
                        entry.instance = next;
                        entry.position[0] = m_lights[at].position[0];
                        entry.position[1] = m_lights[at].position[1];
                        entry.reach = m_gpuLightInstances[next].shape[2];
                        entry.softness = m_lights[at].shadowSoftness > 0.0f ? m_lights[at].shadowSoftness : Float(0.0f);
                        m_shadowedLights.Add(entry);
                    }
                    ++next;
                }
                if (pass == 0)
                {
                    view.plainLightCount = next - view.lightOffset;
                }
            }
        }
        return m_device->WriteBuffer(m_lightInstanceBuffers[m_frame.slot], 0,
            {reinterpret_cast<const std::byte*>(m_gpuLightInstances.Data()),
                static_cast<JBro::UInt32>(count * sizeof(GpuLight2DInstance))})
            && UploadShadowEdges();
    }

    Bool Renderer::UploadShadowEdges()
    {
        const std::size_t count = m_shadowEdges.Size();
        if (count == 0)
        {
            return true;
        }
        if (m_gpuShadowEdges.Size() < count || m_frame.slot >= MaxFrameSlots || false == m_shadowEdgeBuffers[m_frame.slot].IsValid())
        {
            return false;
        }
        for (std::size_t at = 0; at < count; ++at)
        {
            const ShadowEdge2D& edge = m_shadowEdges[at];
            GpuShadowEdgeInstance& instance = m_gpuShadowEdges[at];
            instance.edge[0] = edge.from[0];
            instance.edge[1] = edge.from[1];
            instance.edge[2] = edge.to[0];
            instance.edge[3] = edge.to[1];
            instance.flags[0] = edge.selfShadow ? 1.0f : 0.0f;
        }
        return m_device->WriteBuffer(m_shadowEdgeBuffers[m_frame.slot], 0,
            {reinterpret_cast<const std::byte*>(m_gpuShadowEdges.Data()),
                static_cast<JBro::UInt32>(count * sizeof(GpuShadowEdgeInstance))});
    }

    void Renderer::WriteLightInstance(const Light2DSubmit& light, GpuLight2DInstance& instance)
    {
        {
            const Float outer = light.outerRadius > 0.0f ? light.outerRadius : Float(0.0f);
            const Float inner = light.innerRadius < 0.0f ? Float(0.0f) : (light.innerRadius > outer ? outer : light.innerRadius);
            instance.shape[0] = light.position[0];
            instance.shape[1] = light.position[1];
            instance.shape[2] = outer;
            instance.shape[3] = inner;
            instance.color[0] = light.color[0];
            instance.color[1] = light.color[1];
            instance.color[2] = light.color[2];
            instance.color[3] = 0.0f;
            if (light.kind == Light2DKind::Spot)
            {
                // 각은 원뿔 전체다. 축에서 재는 반각으로 넘긴다. 안쪽이 바깥보다 넓으면 바깥에서 끊는다.
                const Float length = std::sqrt(light.direction[0] * light.direction[0] + light.direction[1] * light.direction[1]);
                const Float axisX = length > 0.00001f ? light.direction[0] / length : Float(1.0f);
                const Float axisY = length > 0.00001f ? light.direction[1] / length : Float(0.0f);
                constexpr Float Pi = 3.14159265f;
                const Float halfOuter = Float::Clamp(light.outerAngle.Get() * 0.5f, 0.0f, Pi);
                const Float halfInner = Float::Clamp(light.innerAngle.Get() * 0.5f, 0.0f, halfOuter);
                instance.cone[0] = axisX;
                instance.cone[1] = axisY;
                instance.cone[2] = halfInner;
                instance.cone[3] = halfOuter;
            }
            else
            {
                // 축에서 잰 각은 π 를 넘지 않는다. 이보다 넓은 원뿔은 모든 방향이다.
                instance.cone[0] = 1.0f;
                instance.cone[1] = 0.0f;
                instance.cone[2] = 4.0f;
                instance.cone[3] = 5.0f;
            }
        }
    }

    Bool Renderer::ExecuteLightMap(RenderGraphContext& context, const void* data)
    {
        // 환경광으로 지운 라이트맵에 그림자 없는 라이트를 한 번에 더한다.
        const LightPassData& pass = *static_cast<const LightPassData*>(data);
        Renderer& renderer = *pass.renderer;
        const ViewPacket& view = renderer.m_views[pass.view.Get()];
        IRHICommandContext& commands = context.GetCommands();
        commands.SetViewport(pass.viewport);
        commands.SetScissor(pass.scissor);
        ++renderer.m_currentStats.litViewCount;
        if (view.plainLightCount == 0)
        {
            return true;
        }
        const Matrix4x4 viewProjection = Multiply(view.camera.projection, view.camera.view);
        return commands.SetGraphicsPipeline(renderer.m_light2DPipeline)
            && commands.SetGraphicsConstants(
                {reinterpret_cast<const std::byte*>(viewProjection.values), static_cast<JBro::UInt32>(sizeof(viewProjection.values))})
            && commands.SetVertexBuffer(0, renderer.m_spriteVertexBuffer, static_cast<JBro::UInt32>(sizeof(float) * 2), 0)
            && commands.SetVertexBuffer(
                1, renderer.m_lightInstanceBuffers[renderer.m_frame.slot], static_cast<JBro::UInt32>(sizeof(GpuLight2DInstance)), 0)
            && commands.SetIndexBuffer(renderer.m_spriteIndexBuffer, IndexFormat::UInt16, 0)
            && commands.DrawIndexedInstanced(6, view.plainLightCount, 0, 0, view.lightOffset);
    }

    Bool Renderer::ExecuteShadowMask(RenderGraphContext& context, const void* data)
    {
        // 그 라이트에서 이 뷰의 변을 밀어내 마스크에 칠한다.
        const LightPassData& pass = *static_cast<const LightPassData*>(data);
        Renderer& renderer = *pass.renderer;
        const ViewPacket& view = renderer.m_views[pass.view.Get()];
        const ShadowedLight& light = renderer.m_shadowedLights[pass.shadowedLight.Get()];
        IRHICommandContext& commands = context.GetCommands();
        commands.SetViewport(pass.viewport);
        commands.SetScissor(pass.scissor);
        struct ShadowConstants
        {
            Float viewProjection[16];
            Float light[4];
        };
        const Matrix4x4 viewProjection = Multiply(view.camera.projection, view.camera.view);
        ShadowConstants constants = {};
        for (Int32 at = 0; at < 16; ++at)
        {
            constants.viewProjection[at] = viewProjection.values[at];
        }
        constants.light[0] = light.position[0];
        constants.light[1] = light.position[1];
        // 라이트가 닿는 곳보다 멀리 민다. 변의 끝이 라이트 안쪽에 있어도 바깥 반지름의 두 배면 넘는다.
        constants.light[2] = light.reach * 2.0f;
        constants.light[3] = light.softness;
        if (false == commands.SetGraphicsPipeline(renderer.m_shadowMaskPipeline)
            || false == commands.SetGraphicsConstants(
                {reinterpret_cast<const std::byte*>(&constants), static_cast<JBro::UInt32>(sizeof(constants))})
            || false == commands.SetVertexBuffer(0, renderer.m_spriteVertexBuffer, static_cast<JBro::UInt32>(sizeof(float) * 2), 0)
            || false == commands.SetVertexBuffer(
                1, renderer.m_shadowEdgeBuffers[renderer.m_frame.slot], static_cast<JBro::UInt32>(sizeof(GpuShadowEdgeInstance)), 0)
            || false == commands.SetIndexBuffer(renderer.m_spriteIndexBuffer, IndexFormat::UInt16, 0)
            || false == commands.DrawIndexedInstanced(6, view.shadowEdgeCount, 0, 0, view.shadowEdgeOffset))
        {
            return false;
        }
        ++renderer.m_currentStats.shadowedLight2DCount;
        return true;
    }

    Bool Renderer::ExecuteShadowedLight(RenderGraphContext& context, const void* data)
    {
        // 마스크로 깎으며 그 라이트 하나를 라이트맵에 더한다.
        const LightPassData& pass = *static_cast<const LightPassData*>(data);
        Renderer& renderer = *pass.renderer;
        const ViewPacket& view = renderer.m_views[pass.view.Get()];
        const ShadowedLight& light = renderer.m_shadowedLights[pass.shadowedLight.Get()];
        IRHICommandContext& commands = context.GetCommands();
        commands.SetViewport(pass.viewport);
        commands.SetScissor(pass.scissor);
        const Matrix4x4 viewProjection = Multiply(view.camera.projection, view.camera.view);
        return commands.SetGraphicsPipeline(renderer.m_shadowedLight2DPipeline)
            && commands.SetGraphicsConstants(
                {reinterpret_cast<const std::byte*>(viewProjection.values), static_cast<JBro::UInt32>(sizeof(viewProjection.values))})
            && commands.SetVertexBuffer(0, renderer.m_spriteVertexBuffer, static_cast<JBro::UInt32>(sizeof(float) * 2), 0)
            && commands.SetVertexBuffer(
                1, renderer.m_lightInstanceBuffers[renderer.m_frame.slot], static_cast<JBro::UInt32>(sizeof(GpuLight2DInstance)), 0)
            && commands.SetIndexBuffer(renderer.m_spriteIndexBuffer, IndexFormat::UInt16, 0)
            && commands.SetTexture(0, context.GetTexture(pass.mask))
            && commands.DrawIndexedInstanced(6, 1, 0, 0, light.instance);
    }

    Bool Renderer::ExecuteUnshadowedLight(RenderGraphContext& context, const void* data)
    {
        // 마스크가 아직 없는 크기다. 그 라이트를 그림자 없이 더한다.
        const LightPassData& pass = *static_cast<const LightPassData*>(data);
        Renderer& renderer = *pass.renderer;
        const ViewPacket& view = renderer.m_views[pass.view.Get()];
        const ShadowedLight& light = renderer.m_shadowedLights[pass.shadowedLight.Get()];
        IRHICommandContext& commands = context.GetCommands();
        commands.SetViewport(pass.viewport);
        commands.SetScissor(pass.scissor);
        ++renderer.m_currentStats.unshadowedLight2DCount;
        const Matrix4x4 viewProjection = Multiply(view.camera.projection, view.camera.view);
        return commands.SetGraphicsPipeline(renderer.m_light2DPipeline)
            && commands.SetGraphicsConstants(
                {reinterpret_cast<const std::byte*>(viewProjection.values), static_cast<JBro::UInt32>(sizeof(viewProjection.values))})
            && commands.SetVertexBuffer(0, renderer.m_spriteVertexBuffer, static_cast<JBro::UInt32>(sizeof(float) * 2), 0)
            && commands.SetVertexBuffer(
                1, renderer.m_lightInstanceBuffers[renderer.m_frame.slot], static_cast<JBro::UInt32>(sizeof(GpuLight2DInstance)), 0)
            && commands.SetIndexBuffer(renderer.m_spriteIndexBuffer, IndexFormat::UInt16, 0)
            && commands.DrawIndexedInstanced(6, 1, 0, 0, light.instance);
    }

    Bool Renderer::UploadMeshInstances()
    {
        // **뷰 안에서 같은 메시를 모은다**(D-110). 메시 슬롯 번호로 세고(계수 정렬) 그 자리에 흩뿌리므로
        // 제출 순서와 무관하게 O(n) 이고, 같은 메시 안에서는 제출 순서가 지켜진다. 메시 종류마다 드로우
        // 하나가 나간다 - 정육면체 16000 개는 드로우 하나다. 등록되지 않은 핸들은 여기서 걸러 센다.
        m_meshRuns.Clear();
        if (m_meshes.IsEmpty())
        {
            return true;
        }
        const std::size_t slotCount = m_meshResources.Size();
        if (m_meshHistogram.Size() < slotCount || m_gpuMeshInstances.Size() < m_meshes.Size())
        {
            return false;
        }
        UInt32 written = 0;
        for (std::size_t viewIndex = 0; viewIndex < m_views.Size(); ++viewIndex)
        {
            ViewPacket& view = m_views[viewIndex];
            view.runOffset = static_cast<JBro::UInt32>(m_meshRuns.Size());
            view.runCount = 0;
            if (view.meshCount == 0)
            {
                continue;
            }
            for (std::size_t slot = 0; slot < slotCount; ++slot)
            {
                m_meshHistogram[slot] = 0;
            }
            const UInt32 end = view.meshOffset + view.meshCount;
            for (UInt32 at = view.meshOffset; at < end; ++at)
            {
                const AssetHandle handle = m_meshes[at].mesh;
                if (FindMesh(handle) == nullptr)
                {
                    // 등록되지 않은 메시다. 프레임을 버리지 않고 그 항목만 건너뛴다 - 핸들이 아직 해석되지
                    // 않은 첫 프레임이 그렇다.
                    ++m_currentStats.droppedMeshCount;
                    continue;
                }
                ++m_meshHistogram[handle.index];
            }
            // 개수를 시작 자리로 바꾸고, 종류마다 드로우 구간을 하나 낸다.
            UInt32 cursor = written;
            for (std::size_t slot = 0; slot < slotCount; ++slot)
            {
                const UInt32 count = m_meshHistogram[slot];
                m_meshHistogram[slot] = cursor;
                if (count != 0)
                {
                    MeshRun run;
                    run.mesh = AssetHandle{static_cast<JBro::UInt32>(slot), m_meshResources[slot].generation};
                    run.firstInstance = cursor;
                    run.instanceCount = count;
                    m_meshRuns.Add(run);
                    ++view.runCount;
                    cursor += count;
                }
            }
            for (UInt32 at = view.meshOffset; at < end; ++at)
            {
                const MeshSubmit& mesh = m_meshes[at];
                if (FindMesh(mesh.mesh) == nullptr)
                {
                    continue;
                }
                GpuMeshInstance& instance = m_gpuMeshInstances[m_meshHistogram[mesh.mesh.index]++];
                instance.world = mesh.world;
                instance.tint[0] = mesh.tint[0];
                instance.tint[1] = mesh.tint[1];
                instance.tint[2] = mesh.tint[2];
                instance.tint[3] = mesh.tint[3];
            }
            written = cursor;
        }
        m_gpuMeshCount = written;
        if (written == 0)
        {
            return true;
        }
        if (m_frame.slot >= MaxFrameSlots || false == m_meshInstanceBuffers[m_frame.slot].IsValid())
        {
            return false;
        }
        const std::size_t byteSize = m_gpuMeshCount * sizeof(GpuMeshInstance);
        return m_device->WriteBuffer(
            m_meshInstanceBuffers[m_frame.slot],
            0,
            {reinterpret_cast<const std::byte*>(m_gpuMeshInstances.Data()),
                static_cast<JBro::UInt32>(byteSize)});
    }

    Bool Renderer::CreateBuiltinWorldTextResources()
    {
        // 상한 0 은 월드 텍스트를 쓰지 않는 렌더러다. 버퍼와 파이프라인을 만들지 않는다.
        if (m_config.maxWorldTextSubmissions == 0)
        {
            return true;
        }
        if (m_device == nullptr
            || m_config.maxWorldTextSubmissions > (std::numeric_limits<std::uint32_t>::max)() / sizeof(GpuWorldTextInstance))
        {
            return false;
        }
        BufferDesc instanceBufferDesc;
        instanceBufferDesc.size = static_cast<std::size_t>(m_config.maxWorldTextSubmissions) * sizeof(GpuWorldTextInstance);
        instanceBufferDesc.usage = BufferUsage::Vertex | BufferUsage::CopySource;
        instanceBufferDesc.memory = MemoryType::Upload;
        for (UInt32 slot = 0; slot < m_config.maxFramesInFlight && slot < MaxFrameSlots; ++slot)
        {
            m_worldTextInstanceBuffers[slot] = m_device->CreateBuffer(instanceBufferDesc);
            if (false == m_worldTextInstanceBuffers[slot].IsValid())
            {
                return false;
            }
        }
        return true;
    }

    void Renderer::DestroyBuiltinWorldTextResources()
    {
        if (m_device == nullptr)
        {
            return;
        }
        for (BufferHandle& buffer : m_worldTextInstanceBuffers)
        {
            if (buffer.IsValid())
            {
                m_device->DestroyBuffer(buffer);
                buffer = {};
            }
        }
    }

    Bool Renderer::UploadWorldTextInstances()
    {
        // 제출 번호가 인스턴스 번호다. 순서는 프레임워크가 정한 뒤→앞 그대로이고, 텍스처·샘플러가 같은 이웃만 묶는다.
        m_worldTextRuns.Clear();
        const std::size_t count = m_worldTexts.Size();
        m_gpuWorldTextCount = count;
        if (count == 0)
        {
            for (ViewPacket& view : m_views)
            {
                view.worldTextRunOffset = 0;
                view.worldTextRunCount = 0;
            }
            return true;
        }
        if (m_gpuWorldTextInstances.Size() < count || m_frame.slot >= MaxFrameSlots
            || false == m_worldTextInstanceBuffers[m_frame.slot].IsValid())
        {
            return false;
        }
        for (ViewPacket& view : m_views)
        {
            view.worldTextRunOffset = static_cast<JBro::UInt32>(m_worldTextRuns.Size());
            view.worldTextRunCount = 0;
            SpriteRun* last = nullptr;
            const UInt32 end = view.worldTextOffset + view.worldTextCount;
            for (UInt32 index = view.worldTextOffset; index < end; ++index)
            {
                const WorldTextSubmit& item = m_worldTexts[index];
                GpuWorldTextInstance& instance = m_gpuWorldTextInstances[index];
                instance.world = item.world;
                for (Int32 channel = 0; channel < 4; ++channel)
                {
                    instance.fill[channel] = ToUnorm8(item.tint[channel]);
                    instance.uvRect[channel] = ToUnorm16(item.uvRect[channel]);
                    instance.outline[channel] = item.outlineColor[channel];
                }
                instance.params[0] = item.outlineEdge;
                instance.params[1] = item.sdf ? 65535 : 0;

                TextureHandle texture = m_whiteTexture;
                if (item.texture.generation != 0)
                {
                    const TextureResource* resource = FindTexture(item.texture);
                    if (resource != nullptr)
                    {
                        texture = resource->texture;
                    }
                    else
                    {
                        ++m_currentStats.staleTextureSpriteCount;
                    }
                }
                const SamplerHandle sampler = item.filter == SpriteFilter::Linear ? m_linearSampler : m_nearestSampler;
                if (last != nullptr && last->texture == texture && last->sampler == sampler)
                {
                    ++last->instanceCount;
                    continue;
                }
                SpriteRun run;
                run.texture = texture;
                run.sampler = sampler;
                run.firstInstance = index;
                run.instanceCount = 1;
                last = &m_worldTextRuns.Add(run);
                ++view.worldTextRunCount;
            }
        }
        return m_device->WriteBuffer(
            m_worldTextInstanceBuffers[m_frame.slot],
            0,
            {reinterpret_cast<const std::byte*>(m_gpuWorldTextInstances.Data()),
                static_cast<JBro::UInt32>(count * sizeof(GpuWorldTextInstance))});
    }

    void Renderer::DestroyBuiltinSpriteResources()
    {
        if (m_device == nullptr)
        {
            return;
        }

        for (GraphicsPipelineHandle* pipeline : {&m_outlineGrowPipeline, &m_outlineCompositePipeline})
        {
            if (pipeline->IsValid())
            {
                m_device->DestroyGraphicsPipeline(*pipeline);
                *pipeline = {};
            }
        }
        for (BufferHandle& buffer : m_textInstanceBuffers)
        {
            if (buffer.IsValid())
            {
                m_device->DestroyBuffer(buffer);
                buffer = {};
            }
        }
        if (m_whiteTexture.IsValid())
        {
            m_device->DestroyTexture(m_whiteTexture);
            m_whiteTexture = {};
        }
        if (m_nearestSampler.IsValid())
        {
            m_device->DestroySampler(m_nearestSampler);
            m_nearestSampler = {};
        }
        if (m_linearSampler.IsValid())
        {
            m_device->DestroySampler(m_linearSampler);
            m_linearSampler = {};
        }
        for (BufferHandle& buffer : m_spriteInstanceBuffers)
        {
            if (buffer.IsValid())
            {
                m_device->DestroyBuffer(buffer);
                buffer = {};
            }
        }
        if (m_spriteIndexBuffer.IsValid())
        {
            m_device->DestroyBuffer(m_spriteIndexBuffer);
            m_spriteIndexBuffer = {};
        }
        if (m_spriteVertexBuffer.IsValid())
        {
            m_device->DestroyBuffer(m_spriteVertexBuffer);
            m_spriteVertexBuffer = {};
        }
    }

    Bool Renderer::UploadSpriteInstances()
    {
        // 한 번에 크기를 잡고 자리에 바로 쓴다. 항목마다 `Add` 를 부르면 60000 개에서 0.3ms 가 그 호출에
        // 들어갔다(D-110 실측) - 인스턴스 자료 자체를 옮기는 memcpy 의 세 배다.
        const std::size_t count = m_sprites.Size();
        if (m_gpuSpriteInstances.Size() < count)
        {
            return false;
        }
        m_gpuSpriteCount = count;
        if (count == 0)
        {
            return true;
        }
        const SpriteSubmit* source = m_sprites.Data();
        GpuSpriteInstance* destination = m_gpuSpriteInstances.Data();
        GpuTextInstance* textDestination = m_gpuTextInstances.Data();
        m_gpuTextFirst = count;
        m_gpuTextEnd = 0;

        // **한 번만 지나간다.** 인스턴스를 옮기는 같은 걸음에서 텍스처·샘플러가 같은 이웃을 묶어 드로우 하나로 낸다(D-113).
        // 60000 개를 두 번 지나가면 패킷 배열(개당 80B, 4.8MB)을 한 번 더 흘리는 값이 0.3ms 였다. 순서는 바꾸지 않는다 -
        // 정렬은 프레임워크의 일이다. 빈 핸들은 흰색이고, 죽은 핸들도 흰색으로 그리되 센다.
        // 묶음 비교는 텍스처·샘플러 핸들을 64 비트 둘로 접어 한다.
        m_spriteRuns.Clear();
        const UInt64 whiteKey = (static_cast<JBro::UInt64>(m_whiteTexture.index) << 32) | m_whiteTexture.generation;
        const UInt64 nearestKey =
            (static_cast<JBro::UInt64>(m_nearestSampler.index) << 32) | m_nearestSampler.generation;
        const UInt64 linearKey =
            (static_cast<JBro::UInt64>(m_linearSampler.index) << 32) | m_linearSampler.generation;
        for (std::size_t viewIndex = 0; viewIndex < m_views.Size(); ++viewIndex)
        {
            ViewPacket& view = m_views[viewIndex];
            view.spriteRunOffset = static_cast<JBro::UInt32>(m_spriteRuns.Size());
            view.spriteRunCount = 0;
            view.hasLitRun = false;
            UInt64 lastTextureKey = 0;
            UInt64 lastSamplerKey = 0;
            SpriteRun* last = nullptr;
            // 묶음은 스프라이트 번호 순이다. 지나간 묶음을 넘기며 이 스프라이트가 든 묶음을 찾는다(D-279).
            UInt32 groupCursor = view.layerGroupOffset;
            const UInt32 groupEnd = view.layerGroupOffset + view.layerGroupCount;
            // 빛을 받는 구간도 같은 걸음으로 찾는다(D-291).
            UInt32 litCursor = view.litRangeOffset;
            const UInt32 litEnd = view.litRangeOffset + view.litRangeCount;
            const UInt32 viewEnd = view.spriteOffset + view.spriteCount;
            for (UInt32 index = view.spriteOffset; index < viewEnd; ++index)
            {
                const SpriteSubmit& item = source[index];
                while (groupCursor < groupEnd && index >= m_layerGroups[groupCursor].endSprite)
                {
                    ++groupCursor;
                }
                const UInt32 layerGroup = groupCursor < groupEnd && index >= m_layerGroups[groupCursor].firstSprite
                    ? groupCursor : NoLayerGroup;
                while (litCursor < litEnd && index >= m_litRanges[litCursor].endSprite)
                {
                    ++litCursor;
                }
                const Bool lit = litCursor < litEnd && index >= m_litRanges[litCursor].firstSprite;
                const Bool sdf = item.shading == SpriteShading::SdfText;
                if (sdf)
                {
                    // SDF 텍스트는 제 버퍼의 같은 번호 칸에 쓴다. 올릴 구간을 넓혀 둔다.
                    GpuTextInstance& text = textDestination[index];
                    text.world = item.world;
                    for (Int32 channel = 0; channel < 4; ++channel)
                    {
                        text.fill[channel] = ToUnorm8(item.tint[channel]);
                        text.uvRect[channel] = ToUnorm16(item.uvRect[channel]);
                        text.outline[channel] = item.outlineColor[channel];
                    }
                    text.params[0] = item.outlineEdge;
                    m_gpuTextFirst = index < m_gpuTextFirst ? static_cast<std::size_t>(index) : m_gpuTextFirst;
                    m_gpuTextEnd = index + 1 > m_gpuTextEnd ? static_cast<std::size_t>(index + 1) : m_gpuTextEnd;
                }
                else
                {
                    GpuSpriteInstance& instance = destination[index];
                    instance.world = item.world;
                    // 0..1 로 잘라 정규화 정수로 접는다(D-114). 반올림해야 0.5 가 128 로 가서 되읽기가 0.502 다.
                    instance.tint[0] = ToUnorm8(item.tint[0]);
                    instance.tint[1] = ToUnorm8(item.tint[1]);
                    instance.tint[2] = ToUnorm8(item.tint[2]);
                    instance.tint[3] = ToUnorm8(item.tint[3]);
                    instance.uvRect[0] = ToUnorm16(item.uvRect[0]);
                    instance.uvRect[1] = ToUnorm16(item.uvRect[1]);
                    instance.uvRect[2] = ToUnorm16(item.uvRect[2]);
                    instance.uvRect[3] = ToUnorm16(item.uvRect[3]);
                }

                TextureHandle texture = m_whiteTexture;
                UInt64 textureKey = whiteKey;
                if (item.texture.generation != 0)
                {
                    const TextureResource* resource = FindTexture(item.texture);
                    if (resource != nullptr)
                    {
                        texture = resource->texture;
                        textureKey = (static_cast<JBro::UInt64>(texture.index) << 32) | texture.generation;
                    }
                    else
                    {
                        ++m_currentStats.staleTextureSpriteCount;
                    }
                }
                const Bool linear = item.filter == SpriteFilter::Linear;
                const UInt64 samplerKey = linear ? linearKey : nearestKey;
                if (last != nullptr && lastTextureKey == textureKey && lastSamplerKey == samplerKey && last->sdf == sdf
                    && last->layerGroup == layerGroup && last->lit == lit)
                {
                    ++last->instanceCount;
                    continue;
                }
                SpriteRun run;
                run.texture = texture;
                run.sampler = linear ? m_linearSampler : m_nearestSampler;
                run.firstInstance = index;
                run.instanceCount = 1;
                run.sdf = sdf;
                run.layerGroup = layerGroup;
                run.lit = lit;
                view.hasLitRun = view.hasLitRun || lit;
                last = &m_spriteRuns.Add(run);
                lastTextureKey = textureKey;
                lastSamplerKey = samplerKey;
                ++view.spriteRunCount;
            }
        }
        if (m_frame.slot >= MaxFrameSlots
            || false == m_spriteInstanceBuffers[m_frame.slot].IsValid())
        {
            return false;
        }

        const std::size_t byteSize = m_gpuSpriteCount * sizeof(GpuSpriteInstance);
        if (false == m_device->WriteBuffer(
                m_spriteInstanceBuffers[m_frame.slot],
                0,
                {reinterpret_cast<const std::byte*>(m_gpuSpriteInstances.Data()),
                    static_cast<JBro::UInt32>(byteSize)}))
        {
            return false;
        }
        if (m_gpuTextEnd <= m_gpuTextFirst)
        {
            return true;
        }
        // 텍스트 칸은 이번 프레임에 쓴 번호 구간만 올린다. 스프라이트만 있는 프레임은 한 바이트도 올리지 않는다.
        const std::size_t textOffset = m_gpuTextFirst * sizeof(GpuTextInstance);
        const std::size_t textBytes = (m_gpuTextEnd - m_gpuTextFirst) * sizeof(GpuTextInstance);
        return m_textInstanceBuffers[m_frame.slot].IsValid()
            && m_device->WriteBuffer(
                m_textInstanceBuffers[m_frame.slot],
                textOffset,
                {reinterpret_cast<const std::byte*>(m_gpuTextInstances.Data() + m_gpuTextFirst),
                    static_cast<JBro::UInt32>(textBytes)});
    }

    Bool Renderer::CreateScenePipelines(TextureFormat format, ScenePipelines& pipelines)
    {
        // 장면에 그리는 것은 모두 여기서 그 포맷으로 만든다(D-342). 버퍼·텍스처는 `CreateBuiltin*Resources` 가 한 번 만든다.
        const TextureFormat colorFormats[] = {format};
        const VertexAttributeDesc quadAttributes[] = {{0, 0, VertexFormat::Float2}};
        // 오프셋을 손으로 적지 않는다. 구조체와 정점 속성이 따로 놀 수 있는 틈을 없앤다.
        constexpr std::size_t TransformOffset = offsetof(GpuSpriteInstance, world);
        const VertexAttributeDesc spriteAttributes[] = {
            {1, static_cast<JBro::UInt32>(TransformOffset + offsetof(SpriteTransform2D, linear)), VertexFormat::Float4},
            {2, static_cast<JBro::UInt32>(TransformOffset + offsetof(SpriteTransform2D, translation)), VertexFormat::Float3},
            {3, static_cast<JBro::UInt32>(offsetof(GpuSpriteInstance, tint)), VertexFormat::UByte4Norm},
            {4, static_cast<JBro::UInt32>(offsetof(GpuSpriteInstance, uvRect)), VertexFormat::UShort4Norm}};
        const VertexBufferLayoutDesc spriteLayouts[] = {
            {static_cast<JBro::UInt32>(sizeof(float) * 2), VertexStepMode::Vertex, {quadAttributes, 1}},
            {static_cast<JBro::UInt32>(sizeof(GpuSpriteInstance)), VertexStepMode::Instance, {spriteAttributes, 4}}};

        GraphicsPipelineDesc spriteDesc;
        if (false == ApplyShader(spriteDesc, BuiltinShader::Sprite, m_config.api, sizeof(Matrix4x4)))
        {
            return false;
        }
        spriteDesc.vertexBuffers = {spriteLayouts, 2};
        spriteDesc.colorFormats = {colorFormats, 1};
        spriteDesc.blend = BlendMode::Alpha;
        spriteDesc.cull = CullMode::None;
        pipelines.sprite = m_device->CreateGraphicsPipeline(spriteDesc);
        // 라이트맵(t1)을 곱하는 쌍둥이다(D-291). 라이트맵은 깊이가 없는 뷰에만 서므로 깊이 판은 없다.
        if (m_config.maxLights2D != 0)
        {
            GraphicsPipelineDesc litDesc = spriteDesc;
            if (false == ApplyShader(litDesc, BuiltinShader::SpriteLit, m_config.api, sizeof(Matrix4x4)))
            {
                return false;
            }
            pipelines.litSprite = m_device->CreateGraphicsPipeline(litDesc);
            if (false == pipelines.litSprite.IsValid())
            {
                return false;
            }
        }
        // 깊이가 달린 패스용 쌍둥이. 포맷은 맞추고 깊이는 끈다 - 스프라이트는 제출 순서로 겹친다.
        spriteDesc.depthFormat = TextureFormat::D32Float;
        spriteDesc.depthTest = false;
        spriteDesc.depthWrite = false;
        pipelines.spriteOverDepth = m_device->CreateGraphicsPipeline(spriteDesc);
        if (false == pipelines.sprite.IsValid() || false == pipelines.spriteOverDepth.IsValid())
        {
            return false;
        }

        // SDF 텍스트(text-plan 4 단계). 같은 단위 쿼드와 뷰 상수이고, 인스턴스는 외곽선 색과 문턱을 더 든다.
        const VertexAttributeDesc textAttributes[] = {
            {1, static_cast<JBro::UInt32>(offsetof(GpuTextInstance, world) + offsetof(SpriteTransform2D, linear)),
                VertexFormat::Float4},
            {2, static_cast<JBro::UInt32>(offsetof(GpuTextInstance, world) + offsetof(SpriteTransform2D, translation)),
                VertexFormat::Float3},
            {3, static_cast<JBro::UInt32>(offsetof(GpuTextInstance, fill)), VertexFormat::UByte4Norm},
            {4, static_cast<JBro::UInt32>(offsetof(GpuTextInstance, uvRect)), VertexFormat::UShort4Norm},
            {5, static_cast<JBro::UInt32>(offsetof(GpuTextInstance, outline)), VertexFormat::UByte4Norm},
            {6, static_cast<JBro::UInt32>(offsetof(GpuTextInstance, params)), VertexFormat::UShort4Norm}};
        const VertexBufferLayoutDesc textLayouts[] = {
            {static_cast<JBro::UInt32>(sizeof(float) * 2), VertexStepMode::Vertex, {quadAttributes, 1}},
            {static_cast<JBro::UInt32>(sizeof(GpuTextInstance)), VertexStepMode::Instance, {textAttributes, 6}}};
        GraphicsPipelineDesc textDesc = spriteDesc;
        if (false == ApplyShader(textDesc, BuiltinShader::SdfText, m_config.api, sizeof(Matrix4x4)))
        {
            return false;
        }
        textDesc.vertexBuffers = {textLayouts, 2};
        textDesc.depthFormat = TextureFormat::Unknown;
        pipelines.sdfText = m_device->CreateGraphicsPipeline(textDesc);
        if (m_config.maxLights2D != 0)
        {
            GraphicsPipelineDesc litDesc = textDesc;
            if (false == ApplyShader(litDesc, BuiltinShader::SdfTextLit, m_config.api, sizeof(Matrix4x4)))
            {
                return false;
            }
            pipelines.litSdfText = m_device->CreateGraphicsPipeline(litDesc);
            if (false == pipelines.litSdfText.IsValid())
            {
                return false;
            }
        }
        textDesc.depthFormat = TextureFormat::D32Float;
        pipelines.sdfTextOverDepth = m_device->CreateGraphicsPipeline(textDesc);
        if (false == pipelines.sdfText.IsValid() || false == pipelines.sdfTextOverDepth.IsValid())
        {
            return false;
        }

        // 레이어 합성(D-279). 화면을 덮는 사각형이고, 픽셀 셰이더가 레이어 텍스처를 `Load` 로 읽어 불투명도를 곱한다.
        // 블렌드만 다른 넷이다 - 미리 곱한 색을 얹는 `Layer*` 계수다.
        const VertexBufferLayoutDesc quadLayouts[] = {
            {static_cast<JBro::UInt32>(sizeof(float) * 2), VertexStepMode::Vertex, {quadAttributes, 1}}};
        GraphicsPipelineDesc compositeDesc;
        if (false == ApplyShader(compositeDesc, BuiltinShader::LayerComposite, m_config.api, sizeof(float) * 4))
        {
            return false;
        }
        compositeDesc.vertexBuffers = {quadLayouts, 1};
        compositeDesc.colorFormats = {colorFormats, 1};
        compositeDesc.cull = CullMode::None;
        compositeDesc.depthTest = false;
        compositeDesc.depthWrite = false;
        const BlendMode compositeBlends[4] = {
            BlendMode::LayerNormal, BlendMode::LayerAdditive, BlendMode::LayerMultiply, BlendMode::LayerScreen};
        for (std::size_t at = 0; at < 4; ++at)
        {
            compositeDesc.blend = compositeBlends[at];
            pipelines.layerComposite[at] = m_device->CreateGraphicsPipeline(compositeDesc);
            if (false == pipelines.layerComposite[at].IsValid())
            {
                return false;
            }
        }
        // 아래 그림을 읽는 블렌드(D-283). 레이어(t0)와 아래 그림의 사본(t1)을 읽어 섞은 색으로 덮어쓴다.
        GraphicsPipelineDesc backdropDesc = compositeDesc;
        if (false == ApplyShader(backdropDesc, BuiltinShader::LayerBackdrop, m_config.api, sizeof(float) * 4))
        {
            return false;
        }
        backdropDesc.blend = BlendMode::Opaque;
        pipelines.layerBackdrop = m_device->CreateGraphicsPipeline(backdropDesc);
        if (false == pipelines.layerBackdrop.IsValid())
        {
            return false;
        }

        // 메시(D-110).
        const VertexAttributeDesc meshAttributes[] = {
            {0, static_cast<JBro::UInt32>(offsetof(MeshVertex, position)), VertexFormat::Float3},
            {1, static_cast<JBro::UInt32>(offsetof(MeshVertex, normal)), VertexFormat::Float3}};
        // 월드 행렬은 행 넷으로 쪼개 넘긴다. 정점 포맷에 4x4 가 없다.
        const VertexAttributeDesc meshInstanceAttributes[] = {
            {2, static_cast<JBro::UInt32>(offsetof(GpuMeshInstance, world)) + 0, VertexFormat::Float4},
            {3, static_cast<JBro::UInt32>(offsetof(GpuMeshInstance, world)) + 16, VertexFormat::Float4},
            {4, static_cast<JBro::UInt32>(offsetof(GpuMeshInstance, world)) + 32, VertexFormat::Float4},
            {5, static_cast<JBro::UInt32>(offsetof(GpuMeshInstance, world)) + 48, VertexFormat::Float4},
            {6, static_cast<JBro::UInt32>(offsetof(GpuMeshInstance, tint)), VertexFormat::Float4}};
        const VertexBufferLayoutDesc meshLayouts[] = {
            {static_cast<JBro::UInt32>(sizeof(MeshVertex)), VertexStepMode::Vertex, {meshAttributes, 2}},
            {static_cast<JBro::UInt32>(sizeof(GpuMeshInstance)), VertexStepMode::Instance, {meshInstanceAttributes, 5}}};
        GraphicsPipelineDesc meshDesc;
        if (false == ApplyShader(meshDesc, BuiltinShader::Mesh, m_config.api, sizeof(Matrix4x4)))
        {
            return false;
        }
        meshDesc.vertexBuffers = {meshLayouts, 2};
        meshDesc.colorFormats = {colorFormats, 1};
        meshDesc.depthFormat = TextureFormat::D32Float;
        meshDesc.blend = BlendMode::Opaque;
        meshDesc.cull = CullMode::Back;
        pipelines.mesh = m_device->CreateGraphicsPipeline(meshDesc);
        if (false == pipelines.mesh.IsValid())
        {
            return false;
        }

        // 월드 텍스트(D-222). 상한 0 은 월드 텍스트를 쓰지 않는 렌더러다.
        if (m_config.maxWorldTextSubmissions == 0)
        {
            return true;
        }
        const VertexAttributeDesc worldTextAttributes[] = {
            {1, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, world)) + 0, VertexFormat::Float4},
            {2, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, world)) + 16, VertexFormat::Float4},
            {3, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, world)) + 32, VertexFormat::Float4},
            {4, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, world)) + 48, VertexFormat::Float4},
            {5, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, fill)), VertexFormat::UByte4Norm},
            {6, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, uvRect)), VertexFormat::UShort4Norm},
            {7, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, outline)), VertexFormat::UByte4Norm},
            {8, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, params)), VertexFormat::UShort4Norm}};
        const VertexBufferLayoutDesc worldTextLayouts[] = {
            {static_cast<JBro::UInt32>(sizeof(float) * 2), VertexStepMode::Vertex, {quadAttributes, 1}},
            {static_cast<JBro::UInt32>(sizeof(GpuWorldTextInstance)), VertexStepMode::Instance, {worldTextAttributes, 8}}};
        GraphicsPipelineDesc worldTextDesc;
        if (false == ApplyShader(worldTextDesc, BuiltinShader::WorldText, m_config.api, sizeof(Matrix4x4)))
        {
            return false;
        }
        worldTextDesc.vertexBuffers = {worldTextLayouts, 2};
        worldTextDesc.colorFormats = {colorFormats, 1};
        worldTextDesc.depthFormat = TextureFormat::D32Float;
        // 메시에 가려지되 글자끼리는 가리지 않는다. 판은 양면이다 - 뒤에서 보면 거울 글자다(빌보드는 늘 앞을 본다).
        worldTextDesc.depthTest = true;
        worldTextDesc.depthWrite = false;
        worldTextDesc.blend = BlendMode::Alpha;
        worldTextDesc.cull = CullMode::None;
        pipelines.worldText = m_device->CreateGraphicsPipeline(worldTextDesc);
        return pipelines.worldText.IsValid();
    }

    void Renderer::DestroyScenePipelines(ScenePipelines& pipelines)
    {
        if (m_device == nullptr)
        {
            return;
        }
        for (GraphicsPipelineHandle* pipeline :
            {&pipelines.sprite, &pipelines.spriteOverDepth, &pipelines.sdfText, &pipelines.sdfTextOverDepth, &pipelines.litSprite,
                &pipelines.litSdfText, &pipelines.worldText, &pipelines.mesh, &pipelines.layerBackdrop, &pipelines.layerComposite[0],
                &pipelines.layerComposite[1], &pipelines.layerComposite[2], &pipelines.layerComposite[3]})
        {
            if (pipeline->IsValid())
            {
                m_device->DestroyGraphicsPipeline(*pipeline);
                *pipeline = {};
            }
        }
    }

    Bool Renderer::CreatePostProcessResources()
    {
        // 효과의 상수는 패스마다 float4 몇 개다. 한 프레임에 효과가 상한만큼 와도 남는 크기다.
        if (m_device == nullptr || false == m_postConstants.Create(*m_device, 64 * 1024)
            || false == CreateScenePipelines(PostProcessHdrFormat, m_scene[HdrScene]))
        {
            return false;
        }
        const BuiltinShader programs[PostProcessProgramCount] = {BuiltinShader::PostCopy, BuiltinShader::PostExposure,
            BuiltinShader::PostTonemap, BuiltinShader::PostBloomPrefilter, BuiltinShader::PostBloomDownsample, BuiltinShader::PostBloomUpsample,
            BuiltinShader::PostBloomComposite, BuiltinShader::PostColorAdjust, BuiltinShader::PostColorLookup, BuiltinShader::PostLayerBlur,
            BuiltinShader::PostLayerOutlineDistance, BuiltinShader::PostLayerOutlineComposite, BuiltinShader::PostLayerShadowBlur,
            BuiltinShader::PostLayerShadowComposite};
        for (UInt32 scene = 0; scene < SceneFormatCount; ++scene)
        {
            const TextureFormat colorFormats[] = {scene == HdrScene ? PostProcessHdrFormat : m_config.backBufferFormat};
            for (UInt32 program = 0; program < PostProcessProgramCount; ++program)
            {
                // 정점 버퍼가 없다 - 정점 번호로 화면을 덮는 삼각형 하나를 낸다. 효과는 뷰포트 안을 덮어쓴다.
                GraphicsPipelineDesc desc;
                if (false == ApplyShader(desc, programs[program.Get()], m_config.api, 0))
                {
                    return false;
                }
                desc.colorFormats = {colorFormats, 1};
                desc.blend = BlendMode::Opaque;
                desc.cull = CullMode::None;
                desc.depthTest = false;
                desc.depthWrite = false;
                m_postPipelines[scene.Get()][program.Get()] = m_device->CreateGraphicsPipeline(desc);
                if (false == m_postPipelines[scene.Get()][program.Get()].IsValid())
                {
                    return false;
                }
            }
        }
        m_postReady = true;
        return true;
    }

    void Renderer::DestroyPostProcessResources()
    {
        m_postReady = false;
        if (m_device == nullptr)
        {
            return;
        }
        for (auto& scene : m_postPipelines)
        {
            for (GraphicsPipelineHandle& pipeline : scene)
            {
                if (pipeline.IsValid())
                {
                    m_device->DestroyGraphicsPipeline(pipeline);
                    pipeline = {};
                }
            }
        }
        DestroyScenePipelines(m_scene[HdrScene]);
        m_postConstants.Destroy();
    }

    Bool Renderer::PlanPostProcess(const ViewPacket& view, PostPlan& plan)
    {
        plan.count = 0;
        plan.hdr = false;
        Bool tonemap = false;
        // 지점 차례로, 같은 지점 안에서는 낸 차례로 늘어놓는다.
        for (UInt32 stage = 0; stage < PostProcessStageCount; ++stage)
        {
            for (UInt32 at = 0; at < view.postCount; ++at)
            {
                const PostEntrySlot& slot = m_postEntries[(view.postOffset + at).Get()];
                const PostProcessEffectType* type = PostProcessEffectRegistry::Get(slot.type);
                if (type == nullptr || static_cast<JBro::UInt32>(type->stage) != stage)
                {
                    continue;
                }
                // 레이어마다의 효과(9 단계)와 화면 레이어 뒤의 효과는 아직 받을 자리가 없다.
                if (type->stage == PostProcessStage::AfterLayer || type->stage == PostProcessStage::AfterScreenLayers)
                {
                    ++m_currentStats.droppedPostProcessCount;
                    continue;
                }
                if (type->stage == PostProcessStage::Tonemap)
                {
                    if (tonemap)
                    {
                        ++m_currentStats.droppedPostProcessCount;
                        continue;
                    }
                    tonemap = true;
                }
                PostPlanStep& step = plan.steps[plan.count.Get()];
                step.type = type;
                step.block = m_postBlocks.Data() + slot.blockOffset.Get();
                ++plan.count;
                if (HasNeed(type->needs, PostProcessNeeds::Hdr))
                {
                    plan.hdr = true;
                }
            }
        }
        if (plan.count == 0)
        {
            return false;
        }
        // HDR 인데 톤맵이 없으면 `None` 으로 내린다. 출력 포맷으로 가는 효과 앞에 끼운다.
        if (plan.hdr && false == tonemap)
        {
            const PostProcessEffectType* type = PostProcessEffectRegistry::Get(PostProcessEffects::Tonemap);
            UInt32 insertAt = plan.count;
            for (UInt32 at = 0; at < plan.count; ++at)
            {
                if (static_cast<JBro::UInt32>(plan.steps[at.Get()].type->stage) > static_cast<JBro::UInt32>(PostProcessStage::Tonemap))
                {
                    insertAt = at;
                    break;
                }
            }
            for (UInt32 at = plan.count; at > insertAt; --at)
            {
                plan.steps[at.Get()] = plan.steps[(at - 1).Get()];
            }
            plan.steps[insertAt.Get()].type = type;
            plan.steps[insertAt.Get()].block = type->defaults;
            ++plan.count;
        }
        // 톤맵 앞의 효과는 HDR 장면에, 톤맵과 그 뒤는 출력 포맷에 쓴다.
        for (UInt32 at = 0; at < plan.count; ++at)
        {
            PostPlanStep& step = plan.steps[at.Get()];
            const Bool beforeTonemap =
                static_cast<JBro::UInt32>(step.type->stage) < static_cast<JBro::UInt32>(PostProcessStage::Tonemap);
            step.output = plan.hdr && beforeTonemap ? PostProcessHdrFormat : m_config.backBufferFormat;
        }
        return true;
    }

    Bool Renderer::AddPostProcessPasses(UInt32 viewIndex, const PostPlan& plan, RenderGraphTexture scene, RenderGraphTexture output,
        LoadOperation outputLoad, const ClearColor& outputClear, const Extent2D& extent, const Viewport& viewport,
        const ScissorRect& scissor)
    {
        RenderGraphTexture input = scene;
        TextureFormat inputFormat = plan.hdr ? PostProcessHdrFormat : m_config.backBufferFormat;
        for (UInt32 at = 0; at < plan.count; ++at)
        {
            const PostPlanStep& step = plan.steps[at.Get()];
            const Bool last = at + 1 == plan.count;
            PostProcessBuilder builder;
            builder.m_renderer = this;
            builder.m_view = viewIndex;
            builder.m_input = input;
            builder.m_inputFormat = inputFormat;
            builder.m_extent = extent;
            builder.m_viewport = viewport;
            builder.m_scissor = scissor;
            if (last)
            {
                // 마지막 효과는 타깃에 쓴다. 톤맵과 그 뒤의 효과라 늘 출력 포맷이다.
                builder.m_output = output;
                builder.m_outputFormat = m_config.backBufferFormat;
                builder.m_outputLoad = outputLoad;
                builder.m_outputClear = outputClear;
            }
            else
            {
                builder.m_output =
                    m_graph.CreateTexture({extent, step.output, TextureUsage::RenderTarget | TextureUsage::Sampled}, "PostEffect");
                builder.m_outputFormat = step.output;
                builder.m_outputLoad = LoadOperation::Discard;
            }
            if (false == step.type->addPasses(builder, step.block))
            {
                return false;
            }
            input = builder.m_output;
            inputFormat = builder.m_outputFormat;
        }
        return true;
    }

    Bool Renderer::CountPostProcessTextures(UInt32 viewIndex, const PostPlan& plan, TextureFormat inputFormat, TextureFormat finalFormat,
        Bool finalIsTexture, Bool premultiplied, const Extent2D& extent, const Viewport& viewport, const ScissorRect& scissor,
        PostProcessBuilder::TextureCount* counts, UInt32& countSize)
    {
        countSize = 0;
        Bool overflow = false;
        const auto add = [&](const TransientTextureDesc& desc, UInt32 count) {
            for (UInt32 at = 0; at < countSize; ++at)
            {
                PostProcessBuilder::TextureCount& entry = counts[at.Get()];
                if (entry.desc.extent.width == desc.extent.width && entry.desc.extent.height == desc.extent.height
                    && entry.desc.format == desc.format && entry.desc.usage == desc.usage)
                {
                    entry.count += count;
                    return;
                }
            }
            if (countSize >= PostProcessBuilder::MaxCountedShapes)
            {
                overflow = true;
                return;
            }
            counts[countSize.Get()] = {desc, count};
            ++countSize;
        };
        // 효과 사이의 중간 텍스처는 앞뒤 둘이 번갈아 쓴다(앞의 출력이 뒤의 입력이다). 포맷마다 많아야 둘이다. 레이어는 마지막 효과도
        // 텍스처에 쓴다(`finalIsTexture`).
        UInt32 hdrSteps = 0;
        UInt32 ldrSteps = 0;
        const UInt32 textureSteps = finalIsTexture ? plan.count : (plan.count != 0 ? plan.count - 1 : UInt32(0));
        for (UInt32 at = 0; at < textureSteps; ++at)
        {
            if (plan.steps[at.Get()].output == PostProcessHdrFormat)
            {
                ++hdrSteps;
            }
            else
            {
                ++ldrSteps;
            }
        }
        if (hdrSteps != 0)
        {
            add({extent, PostProcessHdrFormat, TextureUsage::RenderTarget | TextureUsage::Sampled}, hdrSteps < 2 ? hdrSteps : UInt32(2));
        }
        if (ldrSteps != 0)
        {
            add({extent, m_config.backBufferFormat, TextureUsage::RenderTarget | TextureUsage::Sampled}, ldrSteps < 2 ? ldrSteps : UInt32(2));
        }
        // 효과 안의 텍스처는 패스 함수를 셈으로만 돌려 얻는다 - 그래프에는 아무것도 더하지 않는다.
        for (UInt32 at = 0; at < plan.count; ++at)
        {
            const PostPlanStep& step = plan.steps[at.Get()];
            const TextureFormat outputFormat = at + 1 == plan.count ? finalFormat : step.output;
            PostProcessBuilder counter;
            counter.m_renderer = this;
            counter.m_view = viewIndex;
            counter.m_input = RenderGraphTexture{0};
            counter.m_output = RenderGraphTexture{0};
            counter.m_inputFormat = inputFormat;
            counter.m_outputFormat = outputFormat;
            counter.m_extent = extent;
            counter.m_viewport = viewport;
            counter.m_scissor = scissor;
            counter.m_premultiplied = premultiplied;
            counter.m_counting = true;
            PostProcessBuilder::TextureCount local[PostProcessBuilder::MaxCountedShapes];
            UInt32 localSize = 0;
            counter.m_counts = local;
            counter.m_countSize = &localSize;
            if (false == step.type->addPasses(counter, step.block) || counter.m_countOverflow)
            {
                return false;
            }
            for (UInt32 shape = 0; shape < localSize; ++shape)
            {
                add(local[shape.Get()].desc, local[shape.Get()].count);
            }
            inputFormat = outputFormat;
        }
        return false == overflow;
    }

    Bool Renderer::PlanLayerPostProcess(UInt32 offset, UInt32 count, TextureFormat format, PostPlan& plan)
    {
        plan.count = 0;
        plan.hdr = format == PostProcessHdrFormat;
        for (UInt32 at = 0; at < count; ++at)
        {
            const PostEntrySlot& slot = m_postEntries[(offset + at).Get()];
            const PostProcessEffectType* type = PostProcessEffectRegistry::Get(slot.type);
            if (type == nullptr || false == type->onLayer)
            {
                ++m_currentStats.droppedPostProcessCount;
                continue;
            }
            PostPlanStep& step = plan.steps[plan.count.Get()];
            step.type = type;
            step.block = m_postBlocks.Data() + slot.blockOffset.Get();
            step.output = format;
            ++plan.count;
        }
        return plan.count != 0;
    }

    Bool Renderer::AddLayerPostPasses(UInt32 viewIndex, UInt32 offset, UInt32 count, TextureFormat format, const Extent2D& extent,
        RenderGraphTexture& layer)
    {
        PostPlan plan;
        if (count == 0 || false == PlanLayerPostProcess(offset, count, format, plan))
        {
            return true;
        }
        if (m_postFailed)
        {
            ++m_currentStats.layerPostProcessWaitingCount;
            return true;
        }
        // 효과는 레이어 텍스처 전체를 덮는다. 뷰포트 밖은 투명하게 지워 둔 자리라 그림이 바뀌지 않고, 다음 효과가 둘레를 읽어도 빈 값을 본다.
        Viewport whole;
        whole.width = static_cast<JBro::Float>(extent.width);
        whole.height = static_cast<JBro::Float>(extent.height);
        const ScissorRect wholeScissor = {0, 0, static_cast<JBro::Int32>(extent.width), static_cast<JBro::Int32>(extent.height)};
        PostProcessBuilder::TextureCount counts[PostProcessBuilder::MaxCountedShapes];
        UInt32 countSize = 0;
        const Bool counted =
            CountPostProcessTextures(viewIndex, plan, format, format, true, true, extent, whole, wholeScissor, counts, countSize);
        Bool ready = m_postReady && counted;
        if (false == m_postReady)
        {
            m_postWanted = true;
        }
        for (UInt32 at = 0; at < countSize; ++at)
        {
            ready = m_targetPool.Request(counts[at.Get()].desc, counts[at.Get()].count) && ready;
        }
        if (false == ready)
        {
            ++m_currentStats.layerPostProcessWaitingCount;
            return true;
        }
        RenderGraphTexture input = layer;
        for (UInt32 at = 0; at < plan.count; ++at)
        {
            PostProcessBuilder builder;
            builder.m_renderer = this;
            builder.m_view = viewIndex;
            builder.m_input = input;
            builder.m_inputFormat = format;
            builder.m_output = m_graph.CreateTexture({extent, format, TextureUsage::RenderTarget | TextureUsage::Sampled}, "LayerEffect");
            builder.m_outputFormat = format;
            builder.m_outputLoad = LoadOperation::Discard;
            builder.m_extent = extent;
            builder.m_viewport = whole;
            builder.m_scissor = wholeScissor;
            builder.m_premultiplied = true;
            if (false == plan.steps[at.Get()].type->addPasses(builder, plan.steps[at.Get()].block))
            {
                return false;
            }
            input = builder.m_output;
        }
        layer = input;
        ++m_currentStats.layerPostProcessedCount;
        return true;
    }

    Bool Renderer::AddPostProcessPass(PostProcessBuilder& builder, const PostProcessPassDesc& desc)
    {
        if (desc.constants.size > PostProcessBuilder::MaxConstantBytes || static_cast<JBro::UInt32>(desc.program) >= PostProcessProgramCount
            || desc.inputCount > PostProcessPassDesc::MaxInputs || desc.name == nullptr)
        {
            return false;
        }
        PostPassData data;
        data.renderer = this;
        data.program = desc.program;
        data.external = desc.externalTexture;
        if (desc.inputCount == 0)
        {
            data.inputs[0] = builder.m_input;
            data.inputCount = 1;
        }
        else
        {
            for (UInt32 at = 0; at < desc.inputCount; ++at)
            {
                data.inputs[at.Get()] = desc.inputs[at.Get()];
            }
            data.inputCount = desc.inputCount;
        }
        // 텍스처를 주면 그 전체를, 아니면 뷰의 타깃(또는 중간 텍스처)의 뷰포트 안을 쓴다.
        const Bool whole = desc.output.IsValid();
        const RenderGraphTexture output = whole ? desc.output : builder.m_output;
        const TextureFormat format = whole ? desc.outputFormat : builder.m_outputFormat;
        if (whole)
        {
            data.viewport.width = static_cast<JBro::Float>(desc.outputExtent.width);
            data.viewport.height = static_cast<JBro::Float>(desc.outputExtent.height);
            data.scissor = {0, 0, static_cast<JBro::Int32>(desc.outputExtent.width), static_cast<JBro::Int32>(desc.outputExtent.height)};
        }
        else
        {
            data.viewport = builder.m_viewport;
            data.scissor = builder.m_scissor;
        }
        data.scene = SceneIndexOf(format);
        data.constantBytes = desc.constants.size;
        if (desc.constants.size != 0)
        {
            std::memcpy(data.constants, desc.constants.data, desc.constants.size.Get());
        }
        for (UInt32 at = 0; at < data.inputCount; ++at)
        {
            if (false == data.inputs[at.Get()].IsValid())
            {
                return false;
            }
        }
        if (false == output.IsValid())
        {
            return false;
        }
        const RenderGraphPass pass = m_graph.AddPass(RenderGraphPassKind::Raster, desc.name, &Renderer::ExecutePostProcess, builder.m_view);
        if (false == m_graph.SetColorAttachment(pass, output, whole ? LoadOperation::Discard : builder.m_outputLoad, builder.m_outputClear))
        {
            return false;
        }
        for (UInt32 at = 0; at < data.inputCount; ++at)
        {
            if (false == m_graph.Read(pass, data.inputs[at.Get()]))
            {
                return false;
            }
        }
        return m_graph.SetPassData(pass, data);
    }

    Bool Renderer::ExecutePostProcess(RenderGraphContext& context, const void* data)
    {
        // 화면을 덮는 삼각형 하나다(D-342). 그래프 텍스처를 t0 부터, 그래프 밖의 텍스처(LUT)를 그다음 자리에 묶고, 상수는 b1 이다.
        // 선형 샘플러는 입력을 샘플링하는 프로그램만 묶는다(블룸·LUT, D-343).
        static constexpr Bool Samples[PostProcessProgramCount] = {
            false, false, false, true, true, true, true, false, true, false, false, false, false, false};
        const PostPassData& pass = *static_cast<const PostPassData*>(data);
        Renderer& renderer = *pass.renderer;
        IRHICommandContext& commands = context.GetCommands();
        const std::size_t program = static_cast<std::size_t>(pass.program);
        commands.SetViewport(pass.viewport);
        commands.SetScissor(pass.scissor);
        if (false == commands.SetGraphicsPipeline(renderer.m_postPipelines[pass.scene.Get()][program]))
        {
            return false;
        }
        if (pass.constantBytes != 0)
        {
            const ConstantRange range = renderer.m_postConstants.Push(JArrayView<std::byte>{pass.constants, pass.constantBytes});
            if (false == range.IsValid() || false == commands.SetConstantBuffer(1, range.buffer, range.offset, range.size))
            {
                return false;
            }
        }
        for (UInt32 at = 0; at < pass.inputCount; ++at)
        {
            if (false == commands.SetTexture(at, context.GetTexture(pass.inputs[at.Get()])))
            {
                return false;
            }
        }
        if (pass.external.generation != 0)
        {
            const TextureResource* external = renderer.FindTexture(pass.external);
            if (external == nullptr || false == commands.SetTexture(pass.inputCount, external->texture))
            {
                return false;
            }
        }
        if (Samples[program] && false == commands.SetSampler(0, renderer.m_linearSampler))
        {
            return false;
        }
        return commands.DrawInstanced(3, 1, 0, 0);
    }

    void Renderer::ResetSubmissionStorage()
    {
        m_views.Clear();
        m_sprites.Clear();
        m_meshes.Clear();
        m_meshRuns.Clear();
        m_spriteRuns.Clear();
        m_layerGroups.Clear();
        m_openLayerGroup = NoLayerGroup;
        m_lights.Clear();
        m_shadowedLights.Clear();
        m_shadowEdges.Clear();
        m_litRanges.Clear();
        m_openLitRange = NoLayerGroup;
        m_worldTexts.Clear();
        m_worldTextRuns.Clear();
        m_postEntries.Clear();
        m_postBlockUsed = 0;
        m_currentStats = {};
        m_activeView = InvalidViewIndex;
    }
}
