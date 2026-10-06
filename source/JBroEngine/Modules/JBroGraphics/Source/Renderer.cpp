#include <JBro/Graphics/Renderer.h>

#include "BuiltinLayerBackdropPS.generated.h"
#include "BuiltinLayerCompositePS.generated.h"
#include "BuiltinLight2DPS.generated.h"
#include "BuiltinLight2DVS.generated.h"
#include "BuiltinMeshPS.generated.h"
#include "BuiltinMeshVS.generated.h"
#include "BuiltinOutlineCompositePS.generated.h"
#include "BuiltinOutlineGrowPS.generated.h"
#include "BuiltinOutlineVS.generated.h"
#include "BuiltinSdfTextLitPS.generated.h"
#include "BuiltinSdfTextPS.generated.h"
#include "BuiltinSdfTextVS.generated.h"
#include "BuiltinSpriteLitPS.generated.h"
#include "BuiltinSpritePS.generated.h"
#include "BuiltinSpriteVS.generated.h"
#include "BuiltinWorldTextPS.generated.h"
#include "BuiltinWorldTextVS.generated.h"

// D3D11 은 DXIL 을 읽지 못해 같은 HLSL 을 SM 5.0 DXBC 로도 굽는다(D-107). fxc 의 헤더는 `BYTE` 를
// 쓰므로 그 이름을 이 네임스페이스 안에서만 준다 - windows.h 를 렌더러에 들이지 않는다.
namespace JBro::Sm5
{
    using BYTE = unsigned char;
#include "BuiltinLayerBackdropPS_SM5.generated.h"
#include "BuiltinLayerCompositePS_SM5.generated.h"
#include "BuiltinLight2DPS_SM5.generated.h"
#include "BuiltinLight2DVS_SM5.generated.h"
#include "BuiltinMeshPS_SM5.generated.h"
#include "BuiltinMeshVS_SM5.generated.h"
#include "BuiltinOutlineCompositePS_SM5.generated.h"
#include "BuiltinOutlineGrowPS_SM5.generated.h"
#include "BuiltinOutlineVS_SM5.generated.h"
#include "BuiltinSdfTextLitPS_SM5.generated.h"
#include "BuiltinSdfTextPS_SM5.generated.h"
#include "BuiltinSdfTextVS_SM5.generated.h"
#include "BuiltinSpriteLitPS_SM5.generated.h"
#include "BuiltinSpritePS_SM5.generated.h"
#include "BuiltinSpriteVS_SM5.generated.h"
#include "BuiltinWorldTextPS_SM5.generated.h"
#include "BuiltinWorldTextVS_SM5.generated.h"
}

// Vulkan 은 SPIR-V 를 읽는다(D-108). Vulkan SDK 의 dxc 가 같은 HLSL 을 `-spirv` 로 구운 것이다.
namespace JBro::Spv
{
#include "BuiltinLayerBackdropPS_SPV.generated.h"
#include "BuiltinLayerCompositePS_SPV.generated.h"
#include "BuiltinLight2DPS_SPV.generated.h"
#include "BuiltinLight2DVS_SPV.generated.h"
#include "BuiltinMeshPS_SPV.generated.h"
#include "BuiltinMeshVS_SPV.generated.h"
#include "BuiltinOutlineCompositePS_SPV.generated.h"
#include "BuiltinOutlineGrowPS_SPV.generated.h"
#include "BuiltinOutlineVS_SPV.generated.h"
#include "BuiltinSdfTextLitPS_SPV.generated.h"
#include "BuiltinSdfTextPS_SPV.generated.h"
#include "BuiltinSdfTextVS_SPV.generated.h"
#include "BuiltinSpriteLitPS_SPV.generated.h"
#include "BuiltinSpritePS_SPV.generated.h"
#include "BuiltinSpriteVS_SPV.generated.h"
#include "BuiltinWorldTextPS_SPV.generated.h"
#include "BuiltinWorldTextVS_SPV.generated.h"
}

#include <cmath>
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

        ShaderBytecode PickShader(GraphicsApi api, const unsigned char* dxil, std::size_t dxilSize,
            const unsigned char* dxbc, std::size_t dxbcSize, const unsigned char* spirv, std::size_t spirvSize)
        {
            if (api == GraphicsApi::D3D11)
            {
                return {dxbc, static_cast<JBro::UInt32>(dxbcSize)};
            }
            if (api == GraphicsApi::Vulkan)
            {
                return {spirv, static_cast<JBro::UInt32>(spirvSize)};
            }
            return {dxil, static_cast<JBro::UInt32>(dxilSize)};
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
            m_litRanges.Reserve(config.maxLayerGroups);
            m_textureResources.Reserve(64);
            m_gpuSpriteInstances.Resize(config.maxSpriteSubmissions);
            m_gpuTextInstances.Resize(config.maxSpriteSubmissions);
            m_gpuMeshInstances.Resize(config.maxMeshSubmissions);
            // 메시 슬롯 수만큼 필요하다. 등록할 때 함께 자라므로 프레임 안에서는 할당하지 않는다.
            m_meshHistogram.Reserve(64);
            m_meshResources.Reserve(64);
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
        if (false == CreateBuiltinSpriteResources() || false == CreateBuiltinMeshResources()
            || false == CreateBuiltinWorldTextResources() || false == CreateBuiltinLightResources())
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
            DestroyDepthTargets();
            DestroyLayerTargets();
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
        m_litRanges = {};
        m_openLitRange = NoLayerGroup;
        m_layerTargetWantCount = 0;
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

    Bool Renderer::UpdateTexture(AssetHandle texture, JArrayView<std::byte> rgba8)
    {
        const TextureResource* resource = FindTexture(texture);
        if (resource == nullptr || m_frameActive || rgba8.data == nullptr
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
        if (resource == nullptr || m_frameActive || rgba8.data == nullptr || width == 0 || height == 0)
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

    Bool Renderer::AcquireDepthTarget(const Extent2D& extent, TextureHandle& depth)
    {
        for (DepthTarget& slot : m_depthTargets)
        {
            if (slot.texture.IsValid() && slot.extent.width == extent.width && slot.extent.height == extent.height)
            {
                slot.idleFrames = 0;
                depth = slot.texture;
                return true;
            }
        }
        // 빈 자리가 없으면 가장 오래 안 쓴 것을 내준다. 디바이스가 은퇴 펜스로 지난 프레임이 다 그린 뒤에 놓으므로 여기서
        // 기다리지 않는다 - 보통 프레임은 GPU 를 기다리지 않는다는 계약이 있다(렌더러 계약 테스트).
        DepthTarget* chosen = nullptr;
        for (DepthTarget& slot : m_depthTargets)
        {
            if (false == slot.texture.IsValid())
            {
                chosen = &slot;
                break;
            }
            if (chosen == nullptr || slot.idleFrames > chosen->idleFrames)
            {
                chosen = &slot;
            }
        }
        if (chosen->texture.IsValid())
        {
            m_device->DestroyTexture(chosen->texture);
            *chosen = {};
        }
        TextureDesc desc;
        desc.extent = extent;
        desc.format = TextureFormat::D32Float;
        desc.usage = TextureUsage::DepthStencil;
        chosen->texture = m_device->CreateTexture(desc);
        if (false == chosen->texture.IsValid())
        {
            return false;
        }
        chosen->extent = extent;
        depth = chosen->texture;
        return true;
    }

    TextureHandle Renderer::FindDepthTarget(const Extent2D& extent)
    {
        for (DepthTarget& slot : m_depthTargets)
        {
            if (slot.texture.IsValid() && slot.extent.width == extent.width && slot.extent.height == extent.height)
            {
                slot.idleFrames = 0;
                return slot.texture;
            }
        }
        for (std::size_t at = 0; at < m_depthTargetWantCount; ++at)
        {
            if (m_depthTargetWants[at].width == extent.width && m_depthTargetWants[at].height == extent.height)
            {
                return {};
            }
        }
        if (m_depthTargetWantCount < MaxDepthTargets)
        {
            m_depthTargetWants[m_depthTargetWantCount++] = extent;
        }
        return {};
    }

    void Renderer::PrepareDepthTargets()
    {
        for (DepthTarget& slot : m_depthTargets)
        {
            if (slot.texture.IsValid() && ++slot.idleFrames > DepthTargetIdleFrames)
            {
                m_device->DestroyTexture(slot.texture);
                slot = {};
            }
        }
        for (std::size_t at = 0; at < m_depthTargetWantCount; ++at)
        {
            TextureHandle depth;
            AcquireDepthTarget(m_depthTargetWants[at], depth);
        }
        m_depthTargetWantCount = 0;
    }

    void Renderer::DestroyDepthTargets()
    {
        if (m_device == nullptr)
        {
            return;
        }
        for (DepthTarget& target : m_depthTargets)
        {
            if (target.texture.IsValid())
            {
                m_device->DestroyTexture(target.texture);
            }
            target = {};
        }
        m_depthTargetWantCount = 0;
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
        PrepareDepthTargets();
        TextureHandle depth;
        const Extent2D depthExtent = target.texture.IsValid() ? target.extent : m_config.surfaceExtent;
        if (false == AcquireDepthTarget(depthExtent, depth))
        {
            return FrameStatus::InvalidState;
        }
        // 레이어 텍스처도 같은 까닭으로 여기서 만든다(D-279). 지난 프레임이 바란 크기들이다.
        PrepareLayerTargets();
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

    Bool Renderer::BeginLayer(CompositeBlend blend, Float opacity)
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
        // 카메라" 가 아니고, 기록 조건도 다르다 - 게임 뷰가 닫혀 `recordViews` 가 꺼진
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

    Bool Renderer::IsDeviceLost() const
    {
        return m_device != nullptr && m_device->GetStatus() == FrameStatus::DeviceLost;
    }

    Bool Renderer::IsInitialized() const
    {
        return m_device != nullptr;
    }

    Extent2D Renderer::GetSurfaceExtent() const
    {
        return m_config.surfaceExtent;
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
        // 게임 뷰 패널이 보이지 않을 때 텍스처를 그대로 두는 길이다.
        //
        // **자기 타깃을 든 뷰는 여기 해당하지 않는다**(D-130). 이 값은 **프레임 타깃**에
        // 대한 것이고, 편집 화면은 자기 텍스처에 그린다 - 게임 뷰를 닫아 두었다고
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
        const TextureHandle frameTexture =
            frameToTexture ? m_frameTarget.texture : m_frame.backBuffer;
        const Extent2D frameExtent = GetFrameExtent();

        // **지우는 것은 타깃마다 처음 한 번이다**(D-130). 뷰 번호로 정하면 둘째 타깃의
        // 첫 뷰가 덧그리기가 되어 지난 프레임이 비쳐 남는다.
        m_clearedTargetCount = 0;

        for (std::size_t index = 0; index < m_views.Size(); ++index)
        {
            const ViewPacket& view = m_views[index];
            const Bool ownTarget = view.camera.target.IsValid();
            // 프레임 타깃으로 가는 뷰만 이 프레임의 opt-in 을 따른다(D-63·D-130).
            if (false == ownTarget && false == m_frameTarget.recordViews)
            {
                ++m_currentStats.skippedViewCount;
                continue;
            }
            if (ownTarget
                && (view.camera.targetExtent.width == 0 || view.camera.targetExtent.height == 0))
            {
                // 크기를 모르면 뷰포트가 타깃 안에 있는지 잴 수 없다. 짐작하지 않는다.
                return false;
            }
            const TextureHandle target = ownTarget ? view.camera.target : frameTexture;
            const Extent2D extent = ownTarget ? view.camera.targetExtent : frameExtent;

            Bool alreadyCleared = false;
            for (std::size_t at = 0; at < m_clearedTargetCount; ++at)
            {
                if (m_clearedTargets[at] == target)
                {
                    alreadyCleared = true;
                    break;
                }
            }
            if (false == alreadyCleared && m_clearedTargetCount < MaxClearedTargets)
            {
                m_clearedTargets[m_clearedTargetCount++] = target;
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

            // **라이트맵을 먼저 그린다**(D-291). 빛을 받는 스프라이트가 있고 라이트를 받은 뷰만이다. 타깃과 같은 크기의 RGBA16F 이고, 빛을 받는
            // 구간이 제 픽셀 자리에서 읽어 곱한다 - 따로 합성하지 않으므로 레이어 묶음 안에서도 같다. 처음 보는 크기는 이 프레임에 빛 없이
            // 그리고 다음 프레임 전에 선다. 깊이가 달린 뷰(3D)는 라이팅을 보지 않는다.
            TextureHandle lightMap;
            if (view.hasLitRun && view.lighting)
            {
                const Bool depthWork = view.runCount != 0 || view.worldTextRunCount != 0;
                if (false == depthWork && m_light2DPipeline.IsValid())
                {
                    lightMap = FindLayerTarget(extent, LayerTargetRole::LightMap);
                }
                if (lightMap.IsValid())
                {
                    if (false == RecordLightMap(view, lightMap, viewport, scissor))
                    {
                        return false;
                    }
                    ++m_currentStats.litViewCount;
                }
                else
                {
                    ++m_currentStats.viewsWithoutLightMapCount;
                }
            }

            // **뷰를 통째로 얹는 경우**(D-280, 3D 레이어). 그릴 곳이 타깃이 아니라 레이어 텍스처다. 텍스처가 아직 없는 크기면 그대로 그린다.
            const Bool wantsComposite = view.camera.composite != CompositeBlend::Normal || view.camera.compositeOpacity < 1.0f;
            TextureHandle viewLayer;
            if (wantsComposite)
            {
                viewLayer = FindLayerTarget(extent);
                // 아래 그림을 읽는 블렌드면 사본 자리도 같은 프레임에 바란다 - 그래야 둘째 프레임부터 제 식으로 얹힌다(D-283).
                if (static_cast<JBro::UInt32>(view.camera.composite) >= FirstBackdropBlend)
                {
                    FindLayerTarget(extent, LayerTargetRole::Backdrop);
                }
                if (false == viewLayer.IsValid())
                {
                    ++m_currentStats.uncompositedLayerCount;
                }
            }
            const Bool composited = viewLayer.IsValid();
            if (composited && false == alreadyCleared)
            {
                // 이 타깃의 첫 뷰다. 레이어 텍스처에 그리더라도 타깃은 지워야 지난 프레임이 비치지 않는다.
                ColorAttachmentDesc clearOnly;
                clearOnly.texture = target;
                clearOnly.loadOperation = LoadOperation::Clear;
                clearOnly.storeOperation = StoreOperation::Store;
                clearOnly.clearColor = {
                    view.camera.clearColor[0], view.camera.clearColor[1], view.camera.clearColor[2], view.camera.clearColor[3]};
                RenderPassDesc clearPass;
                clearPass.colorAttachments = {&clearOnly, 1};
                if (false == m_frame.commands->BeginRenderPass(clearPass))
                {
                    return false;
                }
                m_frame.commands->EndRenderPass();
            }

            ColorAttachmentDesc colorAttachment;
            colorAttachment.texture = composited ? viewLayer : target;
            colorAttachment.loadOperation = alreadyCleared && false == composited
                ? LoadOperation::Load
                : LoadOperation::Clear;
            colorAttachment.storeOperation = StoreOperation::Store;
            colorAttachment.clearColor = {
                view.camera.clearColor[0],
                view.camera.clearColor[1],
                view.camera.clearColor[2],
                view.camera.clearColor[3]};
            if (composited)
            {
                colorAttachment.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
            }

            RenderPassDesc pass;
            pass.colorAttachments = {&colorAttachment, 1};
            // **메시나 월드 텍스트가 있는 뷰만 깊이를 단다**(framework3d-plan §2.4, D-222). 스프라이트만 있는 2D 프레임은
            // 전과 같은 패스다. 뷰마다 지운다 - 카메라가 다르면 깊이도 다른 것이고, 3D 레이어는 레이어마다 뷰라 레이어마다 지운다(D-280).
            // **처음 보는 크기의 뷰는 그 프레임에 메시·월드 텍스트 없이 그린다**(D-288) - 깊이 텍스처는 다음 프레임 전에 선다.
            // 프레임을 버리지 않는다. 프레임 타깃의 깊이는 `BeginFrame` 이 이미 만들었으므로 게임 화면은 늘 깊이가 있다.
            DepthStencilAttachmentDesc depthAttachment;
            if (view.runCount != 0 || view.worldTextRunCount != 0)
            {
                depthAttachment.texture = FindDepthTarget(extent);
                if (false == depthAttachment.texture.IsValid())
                {
                    ++m_currentStats.viewsWithoutDepthCount;
                }
            }
            const Bool withDepth = depthAttachment.texture.IsValid();
            if (withDepth)
            {
                depthAttachment.depthLoadOperation = LoadOperation::Clear;
                depthAttachment.depthStoreOperation = StoreOperation::Discard;
                depthAttachment.stencilLoadOperation = LoadOperation::Discard;
                depthAttachment.stencilStoreOperation = StoreOperation::Discard;
                pass.depthStencilAttachment = &depthAttachment;
            }
            if (false == m_frame.commands->BeginRenderPass(pass))
            {
                return false;
            }

            m_frame.commands->SetViewport(viewport);
            m_frame.commands->SetScissor(scissor);

            if (view.spriteCount != 0)
            {
                const Matrix4x4 viewProjection = Multiply(
                    view.camera.projection,
                    view.camera.view);
                const JArrayView<std::byte> constants = {
                    reinterpret_cast<const std::byte*>(viewProjection.values),
                    static_cast<JBro::UInt32>(sizeof(viewProjection.values))};
                // 스프라이트와 SDF 텍스트가 번갈아 오면 구간마다 파이프라인과 인스턴스 버퍼를 갈아 끼운다. 파이프라인을 바꾸면
                // 루트 상수와 텍스처 자리가 비므로 상수도 다시 넣는다. 스프라이트만 있는 뷰는 예전처럼 한 번만 묶는다.
                const Bool overDepth = withDepth;
                // 빛을 받는 구간은 라이트맵이 있을 때만 라이트맵을 곱하는 파이프라인이다(D-291). 라이트맵은 깊이가 없는 뷰에만 선다.
                const auto bindShading = [&](Bool sdf, Bool lit) {
                    const GraphicsPipelineHandle pipeline = lit
                        ? (sdf ? m_litSdfTextPipeline : m_litSpritePipeline)
                        : sdf
                        ? (overDepth ? m_sdfTextOverDepthPipeline : m_sdfTextPipeline)
                        : (overDepth ? m_spriteOverDepthPipeline : m_spritePipeline);
                    return m_frame.commands->SetGraphicsPipeline(pipeline)
                        && m_frame.commands->SetVertexBuffer(
                            1,
                            sdf ? m_textInstanceBuffers[m_frame.slot] : m_spriteInstanceBuffers[m_frame.slot],
                            sdf ? static_cast<JBro::UInt32>(sizeof(GpuTextInstance))
                                : static_cast<JBro::UInt32>(sizeof(GpuSpriteInstance)),
                            0)
                        && m_frame.commands->SetGraphicsConstants(constants);
                };
                const auto litOf = [&](const SpriteRun& run) {
                    return run.lit && lightMap.IsValid();
                };
                const Bool firstSdf = view.spriteRunCount != 0 && m_spriteRuns[view.spriteRunOffset].sdf;
                const Bool firstLit = view.spriteRunCount != 0 && litOf(m_spriteRuns[view.spriteRunOffset]);
                if (false == bindShading(firstSdf, firstLit)
                    || false == m_frame.commands->SetVertexBuffer(
                        0,
                        m_spriteVertexBuffer,
                        static_cast<JBro::UInt32>(sizeof(float) * 2),
                        0)
                    || false == m_frame.commands->SetIndexBuffer(
                        m_spriteIndexBuffer,
                        IndexFormat::UInt16,
                        0))
                {
                    return false;
                }
                Bool boundSdf = firstSdf;
                Bool boundLit = firstLit;
                // **레이어 묶음은 제 텍스처에 그렸다 얹는다**(D-279). 묶음에 들어가면 타깃의 패스를 닫고 레이어 텍스처를 투명하게 지운
                // 패스를 열고, 나오면 그 텍스처를 타깃에 얹는다. 패스를 바꾸면 묶어 둔 것이 풀리므로 다시 묶는다. 깊이가 달린 뷰는
                // 패스를 끊으면 깊이를 다시 실어야 해서 묶음을 보지 않는다.
                const auto bindAll = [&](Bool sdf, Bool lit) {
                    return bindShading(sdf, lit)
                        && m_frame.commands->SetVertexBuffer(0, m_spriteVertexBuffer, static_cast<JBro::UInt32>(sizeof(float) * 2), 0)
                        && m_frame.commands->SetIndexBuffer(m_spriteIndexBuffer, IndexFormat::UInt16, 0);
                };
                const auto openPass = [&](TextureHandle output, LoadOperation load) {
                    ColorAttachmentDesc color;
                    color.texture = output;
                    color.loadOperation = load;
                    color.storeOperation = StoreOperation::Store;
                    color.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
                    RenderPassDesc desc;
                    desc.colorAttachments = {&color, 1};
                    if (false == m_frame.commands->BeginRenderPass(desc))
                    {
                        return false;
                    }
                    m_frame.commands->SetViewport(viewport);
                    m_frame.commands->SetScissor(scissor);
                    return true;
                };
                UInt32 activeGroup = NoLayerGroup;
                // 텍스처가 없어 그대로 그리는 묶음이다. 묶음 하나가 구간 여럿이어도 한 번만 센다.
                UInt32 plainGroup = NoLayerGroup;
                TextureHandle layerTexture;
                Bool rebind = false;
                for (UInt32 runIndex = 0; runIndex < view.spriteRunCount; ++runIndex)
                {
                    const SpriteRun& run = m_spriteRuns[view.spriteRunOffset + runIndex];
                    const UInt32 group = withDepth || composited ? NoLayerGroup : run.layerGroup;
                    if (group != activeGroup)
                    {
                        if (activeGroup != NoLayerGroup)
                        {
                            m_frame.commands->EndRenderPass();
                            if (false == RecordLayerComposite(m_layerGroups[activeGroup], layerTexture, target, extent, viewport, scissor))
                            {
                                return false;
                            }
                            rebind = true;
                            activeGroup = NoLayerGroup;
                        }
                        if (group != NoLayerGroup && group != plainGroup)
                        {
                            layerTexture = FindLayerTarget(extent);
                            if (static_cast<JBro::UInt32>(m_layerGroups[group].blend) >= FirstBackdropBlend)
                            {
                                FindLayerTarget(extent, LayerTargetRole::Backdrop);
                            }
                            if (layerTexture.IsValid())
                            {
                                m_frame.commands->EndRenderPass();
                                if (false == openPass(layerTexture, LoadOperation::Clear))
                                {
                                    return false;
                                }
                                rebind = true;
                                activeGroup = group;
                            }
                            else
                            {
                                ++m_currentStats.uncompositedLayerCount;
                                plainGroup = group;
                            }
                        }
                    }
                    const Bool runLit = litOf(run);
                    if (rebind)
                    {
                        if (false == bindAll(run.sdf, runLit))
                        {
                            return false;
                        }
                        boundSdf = run.sdf;
                        boundLit = runLit;
                        rebind = false;
                    }
                    if (run.sdf != boundSdf || runLit != boundLit)
                    {
                        if (false == bindShading(run.sdf, runLit))
                        {
                            return false;
                        }
                        boundSdf = run.sdf;
                        boundLit = runLit;
                    }
                    if ((runLit && false == m_frame.commands->SetTexture(1, lightMap))
                        || false == m_frame.commands->SetTexture(0, run.texture)
                        || false == m_frame.commands->SetSampler(0, run.sampler)
                        || false == m_frame.commands->DrawIndexedInstanced(
                            6,
                            run.instanceCount,
                            0,
                            0,
                            run.firstInstance))
                    {
                        return false;
                    }
                }
                if (activeGroup != NoLayerGroup)
                {
                    m_frame.commands->EndRenderPass();
                    if (false == RecordLayerComposite(m_layerGroups[activeGroup], layerTexture, target, extent, viewport, scissor))
                    {
                        return false;
                    }
                }
                if (withDepth || composited)
                {
                    // 3D 뷰와 통째로 얹는 뷰의 묶음은 그대로 그렸다. 그렇다고 센다.
                    for (UInt32 at = 0; at < view.layerGroupCount; ++at)
                    {
                        const LayerGroup& group = m_layerGroups[view.layerGroupOffset + at];
                        if (group.endSprite > group.firstSprite)
                        {
                            ++m_currentStats.uncompositedLayerCount;
                        }
                    }
                }
            }

            if (withDepth && view.runCount != 0)
            {
                const Matrix4x4 viewProjection = Multiply(
                    view.camera.projection,
                    view.camera.view);
                const JArrayView<std::byte> constants = {
                    reinterpret_cast<const std::byte*>(viewProjection.values),
                    static_cast<JBro::UInt32>(sizeof(viewProjection.values))};
                if (false == m_frame.commands->SetGraphicsPipeline(m_meshPipeline)
                    || false == m_frame.commands->SetVertexBuffer(
                        1, m_meshInstanceBuffers[m_frame.slot], static_cast<JBro::UInt32>(sizeof(GpuMeshInstance)), 0)
                    || false == m_frame.commands->SetGraphicsConstants(constants))
                {
                    return false;
                }
                // 메시 종류마다 한 드로우다(D-110). 업로드가 같은 메시를 이어 놓았다.
                for (UInt32 at = 0; at < view.runCount; ++at)
                {
                    const MeshRun& run = m_meshRuns[view.runOffset + at];
                    const MeshResource* mesh = FindMesh(run.mesh);
                    if (mesh == nullptr)
                    {
                        // 업로드와 기록 사이에 사라질 길은 없지만, 있다면 업로드와 같은 정책이다: 그 묶음만 건너뛴다.
                        m_currentStats.droppedMeshCount += run.instanceCount;
                        continue;
                    }
                    if (false == m_frame.commands->SetVertexBuffer(
                            0, mesh->vertexBuffer, static_cast<JBro::UInt32>(sizeof(MeshVertex)), 0)
                        || false == m_frame.commands->SetIndexBuffer(
                            mesh->indexBuffer, IndexFormat::UInt32, 0)
                        || false == m_frame.commands->DrawIndexedInstanced(
                            mesh->indexCount, run.instanceCount, 0, 0, run.firstInstance))
                    {
                        return false;
                    }
                }
            }

            // 월드 텍스트는 메시 **뒤**다 - 메시가 쓴 깊이로 가려진다. 파이프라인은 깊이를 보되 쓰지 않는다.
            if (withDepth && view.worldTextRunCount != 0)
            {
                const Matrix4x4 viewProjection = Multiply(
                    view.camera.projection,
                    view.camera.view);
                const JArrayView<std::byte> constants = {
                    reinterpret_cast<const std::byte*>(viewProjection.values),
                    static_cast<JBro::UInt32>(sizeof(viewProjection.values))};
                if (false == m_frame.commands->SetGraphicsPipeline(m_worldTextPipeline)
                    || false == m_frame.commands->SetVertexBuffer(0, m_spriteVertexBuffer, static_cast<JBro::UInt32>(sizeof(float) * 2), 0)
                    || false == m_frame.commands->SetVertexBuffer(
                        1, m_worldTextInstanceBuffers[m_frame.slot], static_cast<JBro::UInt32>(sizeof(GpuWorldTextInstance)), 0)
                    || false == m_frame.commands->SetIndexBuffer(m_spriteIndexBuffer, IndexFormat::UInt16, 0)
                    || false == m_frame.commands->SetGraphicsConstants(constants))
                {
                    return false;
                }
                for (UInt32 runIndex = 0; runIndex < view.worldTextRunCount; ++runIndex)
                {
                    const SpriteRun& run = m_worldTextRuns[view.worldTextRunOffset + runIndex];
                    if (false == m_frame.commands->SetTexture(0, run.texture)
                        || false == m_frame.commands->SetSampler(0, run.sampler)
                        || false == m_frame.commands->DrawIndexedInstanced(6, run.instanceCount, 0, 0, run.firstInstance))
                    {
                        return false;
                    }
                }
            }

            m_frame.commands->EndRenderPass();

            if (composited)
            {
                LayerGroup whole;
                whole.blend = view.camera.composite;
                whole.opacity = view.camera.compositeOpacity;
                if (false == RecordLayerComposite(whole, viewLayer, target, extent, viewport, scissor))
                {
                    return false;
                }
                m_frame.commands->EndRenderPass();
            }

            if (view.camera.outlineMask.IsValid() && view.camera.outlineScratch.IsValid() && view.camera.outlineWidth != 0
                && false == RecordOutline(view.camera, target, extent))
            {
                return false;
            }
        }

        return true;
    }

    Bool Renderer::RecordOutline(const CameraParams& camera, TextureHandle target, const Extent2D& extent)
    {
        // 화면을 덮는 사각형 하나씩 두 번이다(D-276). 단위 쿼드를 두 배로 펴서 쓴다 - 정점 버퍼를 따로 두지 않는다.
        struct OutlineConstants
        {
            Float color[4];
            Float params[4];
        };
        OutlineConstants constants = {};
        for (Int32 channel = 0; channel < 4; ++channel)
        {
            constants.color[channel] = camera.outlineColor[channel];
        }
        constants.params[0] = static_cast<JBro::Float>(camera.outlineWidth);
        const JArrayView<std::byte> bytes = {reinterpret_cast<const std::byte*>(&constants), static_cast<JBro::UInt32>(sizeof(constants))};
        Viewport viewport;
        viewport.width = static_cast<JBro::Float>(extent.width);
        viewport.height = static_cast<JBro::Float>(extent.height);
        const ScissorRect scissor = {0, 0, static_cast<JBro::Int32>(extent.width), static_cast<JBro::Int32>(extent.height)};

        const auto pass = [&](TextureHandle output, LoadOperation load, GraphicsPipelineHandle pipeline,
                              TextureHandle first, TextureHandle second) -> Bool {
            ColorAttachmentDesc color;
            color.texture = output;
            color.loadOperation = load;
            color.storeOperation = StoreOperation::Store;
            color.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
            RenderPassDesc desc;
            desc.colorAttachments = {&color, 1};
            if (false == m_frame.commands->BeginRenderPass(desc))
            {
                return false;
            }
            m_frame.commands->SetViewport(viewport);
            m_frame.commands->SetScissor(scissor);
            const Bool drawn = m_frame.commands->SetGraphicsPipeline(pipeline)
                && m_frame.commands->SetGraphicsConstants(bytes)
                && m_frame.commands->SetVertexBuffer(0, m_spriteVertexBuffer, static_cast<JBro::UInt32>(sizeof(float) * 2), 0)
                && m_frame.commands->SetIndexBuffer(m_spriteIndexBuffer, IndexFormat::UInt16, 0)
                && m_frame.commands->SetTexture(0, first)
                && m_frame.commands->SetTexture(1, second)
                && m_frame.commands->SetSampler(0, m_nearestSampler)
                && m_frame.commands->DrawIndexedInstanced(6, 1, 0, 0, 0);
            m_frame.commands->EndRenderPass();
            return drawn;
        };
        // 둘째 패스는 마스크를 t1 로도 읽는다. 첫째 패스의 t1 자리에는 같은 마스크를 묶어 둔다 - 비워 둘 수 없다.
        return pass(camera.outlineScratch, LoadOperation::Clear, m_outlineGrowPipeline, camera.outlineMask, camera.outlineMask)
            && pass(target, LoadOperation::Load, m_outlineCompositePipeline, camera.outlineScratch, camera.outlineMask);
    }

    Bool Renderer::RecordLayerComposite(const LayerGroup& group, TextureHandle layer, TextureHandle target, const Extent2D& extent,
        const Viewport& viewport, const ScissorRect& scissor)
    {
        // 화면을 덮는 사각형 하나다. 레이어 텍스처는 미리 곱한 색이라 `Layer*` 블렌드로 얹고, 불투명도는 색과 알파에 함께 곱한다.
        // 아래 그림을 읽는 블렌드는 타깃을 복사해 두고, 셰이더가 둘을 섞어 그 자리를 덮어쓴다(D-283).
        const UInt32 blend = static_cast<JBro::UInt32>(group.blend);
        GraphicsPipelineHandle pipeline = m_layerCompositePipelines[blend < FirstBackdropBlend ? blend : UInt32(0)];
        TextureHandle backdrop;
        if (blend >= FirstBackdropBlend && blend < CompositeBlendCount)
        {
            backdrop = FindLayerTarget(extent, LayerTargetRole::Backdrop);
            if (backdrop.IsValid() && m_frame.commands->CopyTexture(target, backdrop))
            {
                pipeline = m_layerBackdropPipeline;
            }
            else
            {
                // 복사할 자리가 다음 프레임에 선다. 이 프레임은 표준으로 얹는다.
                backdrop = {};
                ++m_currentStats.uncompositedLayerCount;
            }
        }
        const Float constants[4] = {
            group.opacity, backdrop.IsValid() ? static_cast<JBro::Float>(blend - FirstBackdropBlend) : Float(0.0f), 0.0f, 0.0f};
        ColorAttachmentDesc color;
        color.texture = target;
        color.loadOperation = LoadOperation::Load;
        color.storeOperation = StoreOperation::Store;
        RenderPassDesc desc;
        desc.colorAttachments = {&color, 1};
        if (false == m_frame.commands->BeginRenderPass(desc))
        {
            return false;
        }
        m_frame.commands->SetViewport(viewport);
        m_frame.commands->SetScissor(scissor);
        if (false == m_frame.commands->SetGraphicsPipeline(pipeline)
            || false == m_frame.commands->SetGraphicsConstants(
                {reinterpret_cast<const std::byte*>(constants), static_cast<JBro::UInt32>(sizeof(constants))})
            || false == m_frame.commands->SetVertexBuffer(0, m_spriteVertexBuffer, static_cast<JBro::UInt32>(sizeof(float) * 2), 0)
            || false == m_frame.commands->SetIndexBuffer(m_spriteIndexBuffer, IndexFormat::UInt16, 0)
            || false == m_frame.commands->SetTexture(0, layer)
            || (backdrop.IsValid() && false == m_frame.commands->SetTexture(1, backdrop))
            || false == m_frame.commands->SetSampler(0, m_nearestSampler)
            || false == m_frame.commands->DrawIndexedInstanced(6, 1, 0, 0, 0))
        {
            return false;
        }
        ++m_currentStats.compositedLayerCount;
        return true;
    }

    TextureHandle Renderer::FindLayerTarget(const Extent2D& extent, LayerTargetRole role)
    {
        for (LayerTarget& slot : m_layerTargets)
        {
            if (slot.texture.IsValid() && slot.role == role && slot.extent.width == extent.width && slot.extent.height == extent.height)
            {
                slot.idleFrames = 0;
                return slot.texture;
            }
        }
        for (std::size_t at = 0; at < m_layerTargetWantCount; ++at)
        {
            const LayerTargetWant& want = m_layerTargetWants[at];
            if (want.role == role && want.extent.width == extent.width && want.extent.height == extent.height)
            {
                return {};
            }
        }
        if (m_layerTargetWantCount < MaxLayerTargets)
        {
            m_layerTargetWants[m_layerTargetWantCount++] = {extent, role};
        }
        return {};
    }

    void Renderer::PrepareLayerTargets()
    {
        if (m_device == nullptr)
        {
            return;
        }
        for (LayerTarget& slot : m_layerTargets)
        {
            if (false == slot.texture.IsValid())
            {
                continue;
            }
            ++slot.idleFrames;
            if (slot.idleFrames > LayerTargetIdleFrames)
            {
                // 디바이스가 은퇴 펜스로 지난 프레임이 다 그린 뒤에 놓는다(깊이 텍스처와 같다).
                m_device->DestroyTexture(slot.texture);
                slot = {};
            }
        }
        for (std::size_t want = 0; want < m_layerTargetWantCount; ++want)
        {
            // 빈 자리가 없으면 가장 오래 안 쓴 것을 내준다.
            LayerTarget* chosen = nullptr;
            for (LayerTarget& slot : m_layerTargets)
            {
                if (false == slot.texture.IsValid())
                {
                    chosen = &slot;
                    break;
                }
                if (chosen == nullptr || slot.idleFrames > chosen->idleFrames)
                {
                    chosen = &slot;
                }
            }
            if (chosen->texture.IsValid())
            {
                m_device->DestroyTexture(chosen->texture);
                *chosen = {};
            }
            TextureDesc desc;
            desc.extent = m_layerTargetWants[want].extent;
            const Bool lightMap = m_layerTargetWants[want].role == LayerTargetRole::LightMap;
            // 라이트맵은 1 을 넘는 빛을 담는다(D-291). 나머지는 타깃에 얹거나 타깃을 복사해 두는 자리라 백버퍼 포맷이다.
            desc.format = lightMap ? TextureFormat::RGBA16Float : m_config.backBufferFormat;
            desc.usage = lightMap ? TextureUsage::RenderTarget | TextureUsage::Sampled
                                  : TextureUsage::RenderTarget | TextureUsage::Sampled | TextureUsage::CopyDestination;
            chosen->texture = m_device->CreateTexture(desc);
            chosen->extent = chosen->texture.IsValid() ? desc.extent : Extent2D{};
            chosen->role = m_layerTargetWants[want].role;
            chosen->idleFrames = 0;
        }
        m_layerTargetWantCount = 0;
    }

    void Renderer::DestroyLayerTargets()
    {
        if (m_device == nullptr)
        {
            return;
        }
        for (LayerTarget& slot : m_layerTargets)
        {
            if (slot.texture.IsValid())
            {
                m_device->DestroyTexture(slot.texture);
            }
            slot = {};
        }
        m_layerTargetWantCount = 0;
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
        // 오프셋을 손으로 적지 않는다. 구조체와 정점 속성이 따로 놀 수 있는 틈을 없앨다.
        constexpr std::size_t TransformOffset = offsetof(GpuSpriteInstance, world);
        const VertexAttributeDesc instanceAttributes[] = {
            {1, static_cast<JBro::UInt32>(TransformOffset + offsetof(SpriteTransform2D, linear)),
                VertexFormat::Float4},
            {2, static_cast<JBro::UInt32>(TransformOffset + offsetof(SpriteTransform2D, translation)),
                VertexFormat::Float3},
            {3, static_cast<JBro::UInt32>(offsetof(GpuSpriteInstance, tint)),
                VertexFormat::UByte4Norm},
            {4, static_cast<JBro::UInt32>(offsetof(GpuSpriteInstance, uvRect)),
                VertexFormat::UShort4Norm}};
        const VertexBufferLayoutDesc vertexLayouts[] = {
            {static_cast<JBro::UInt32>(sizeof(float) * 2), VertexStepMode::Vertex, {vertexAttributes, 1}},
            {static_cast<JBro::UInt32>(sizeof(GpuSpriteInstance)), VertexStepMode::Instance, {instanceAttributes, 4}}};
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

        GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = PickShader(m_config.api, JBroBuiltinSpriteVS, sizeof(JBroBuiltinSpriteVS),
            Sm5::JBroBuiltinSpriteVS_SM5, sizeof(Sm5::JBroBuiltinSpriteVS_SM5),
            Spv::JBroBuiltinSpriteVS_SPV, sizeof(Spv::JBroBuiltinSpriteVS_SPV));
        pipelineDesc.pixelShader = PickShader(m_config.api, JBroBuiltinSpritePS, sizeof(JBroBuiltinSpritePS),
            Sm5::JBroBuiltinSpritePS_SM5, sizeof(Sm5::JBroBuiltinSpritePS_SM5),
            Spv::JBroBuiltinSpritePS_SPV, sizeof(Spv::JBroBuiltinSpritePS_SPV));
        pipelineDesc.vertexBuffers = {vertexLayouts, 2};
        pipelineDesc.colorFormats = {colorFormats, 1};
        pipelineDesc.blend = BlendMode::Alpha;
        pipelineDesc.cull = CullMode::None;
        pipelineDesc.pushConstantStages = ShaderStage::Vertex;
        pipelineDesc.pushConstantBytes = static_cast<JBro::UInt32>(sizeof(Matrix4x4));
        pipelineDesc.sampledTextureCount = 1;
        pipelineDesc.samplerCount = 1;
        m_spritePipeline = m_device->CreateGraphicsPipeline(pipelineDesc);
        // 라이트맵(t1)을 곱하는 쌍둥이다(D-291). 라이트맵은 깊이가 없는 뷰에만 서므로 깊이 판은 없다.
        if (m_config.maxLights2D != 0)
        {
            GraphicsPipelineDesc litDesc = pipelineDesc;
            litDesc.pixelShader = PickShader(m_config.api, JBroBuiltinSpriteLitPS, sizeof(JBroBuiltinSpriteLitPS),
                Sm5::JBroBuiltinSpriteLitPS_SM5, sizeof(Sm5::JBroBuiltinSpriteLitPS_SM5),
                Spv::JBroBuiltinSpriteLitPS_SPV, sizeof(Spv::JBroBuiltinSpriteLitPS_SPV));
            litDesc.sampledTextureCount = 2;
            m_litSpritePipeline = m_device->CreateGraphicsPipeline(litDesc);
            if (false == m_litSpritePipeline.IsValid())
            {
                return false;
            }
        }
        // 깊이가 달린 패스용 쌍둥이. 포맷은 맞추고 깊이는 끈다 - 스프라이트는 제출 순서로 겹친다.
        pipelineDesc.depthFormat = TextureFormat::D32Float;
        pipelineDesc.depthTest = false;
        pipelineDesc.depthWrite = false;
        m_spriteOverDepthPipeline = m_device->CreateGraphicsPipeline(pipelineDesc);
        if (false == m_spritePipeline.IsValid() || false == m_spriteOverDepthPipeline.IsValid())
        {
            return false;
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
            {static_cast<JBro::UInt32>(sizeof(float) * 2), VertexStepMode::Vertex, {vertexAttributes, 1}},
            {static_cast<JBro::UInt32>(sizeof(GpuTextInstance)), VertexStepMode::Instance, {textAttributes, 6}}};
        GraphicsPipelineDesc textDesc = pipelineDesc;
        textDesc.vertexShader = PickShader(m_config.api, JBroBuiltinSdfTextVS, sizeof(JBroBuiltinSdfTextVS),
            Sm5::JBroBuiltinSdfTextVS_SM5, sizeof(Sm5::JBroBuiltinSdfTextVS_SM5),
            Spv::JBroBuiltinSdfTextVS_SPV, sizeof(Spv::JBroBuiltinSdfTextVS_SPV));
        textDesc.pixelShader = PickShader(m_config.api, JBroBuiltinSdfTextPS, sizeof(JBroBuiltinSdfTextPS),
            Sm5::JBroBuiltinSdfTextPS_SM5, sizeof(Sm5::JBroBuiltinSdfTextPS_SM5),
            Spv::JBroBuiltinSdfTextPS_SPV, sizeof(Spv::JBroBuiltinSdfTextPS_SPV));
        textDesc.vertexBuffers = {textLayouts, 2};
        textDesc.depthFormat = TextureFormat::Unknown;
        m_sdfTextPipeline = m_device->CreateGraphicsPipeline(textDesc);
        if (m_config.maxLights2D != 0)
        {
            GraphicsPipelineDesc litDesc = textDesc;
            litDesc.pixelShader = PickShader(m_config.api, JBroBuiltinSdfTextLitPS, sizeof(JBroBuiltinSdfTextLitPS),
                Sm5::JBroBuiltinSdfTextLitPS_SM5, sizeof(Sm5::JBroBuiltinSdfTextLitPS_SM5),
                Spv::JBroBuiltinSdfTextLitPS_SPV, sizeof(Spv::JBroBuiltinSdfTextLitPS_SPV));
            litDesc.sampledTextureCount = 2;
            m_litSdfTextPipeline = m_device->CreateGraphicsPipeline(litDesc);
            if (false == m_litSdfTextPipeline.IsValid())
            {
                return false;
            }
        }
        textDesc.depthFormat = TextureFormat::D32Float;
        m_sdfTextOverDepthPipeline = m_device->CreateGraphicsPipeline(textDesc);
        if (false == m_sdfTextPipeline.IsValid() || false == m_sdfTextOverDepthPipeline.IsValid())
        {
            return false;
        }

        // 선택 외곽선(D-276). 같은 단위 쿼드의 위치만 읽고, 픽셀 셰이더가 텍스처 둘을 `Load` 로 읽는다.
        const VertexBufferLayoutDesc outlineLayouts[] = {
            {static_cast<JBro::UInt32>(sizeof(float) * 2), VertexStepMode::Vertex, {vertexAttributes, 1}}};
        GraphicsPipelineDesc outlineDesc;
        outlineDesc.vertexShader = PickShader(m_config.api, JBroBuiltinOutlineVS, sizeof(JBroBuiltinOutlineVS),
            Sm5::JBroBuiltinOutlineVS_SM5, sizeof(Sm5::JBroBuiltinOutlineVS_SM5),
            Spv::JBroBuiltinOutlineVS_SPV, sizeof(Spv::JBroBuiltinOutlineVS_SPV));
        outlineDesc.pixelShader = PickShader(m_config.api, JBroBuiltinOutlineGrowPS, sizeof(JBroBuiltinOutlineGrowPS),
            Sm5::JBroBuiltinOutlineGrowPS_SM5, sizeof(Sm5::JBroBuiltinOutlineGrowPS_SM5),
            Spv::JBroBuiltinOutlineGrowPS_SPV, sizeof(Spv::JBroBuiltinOutlineGrowPS_SPV));
        outlineDesc.vertexBuffers = {outlineLayouts, 1};
        outlineDesc.colorFormats = {colorFormats, 1};
        outlineDesc.blend = BlendMode::Opaque;
        outlineDesc.cull = CullMode::None;
        outlineDesc.depthTest = false;
        outlineDesc.depthWrite = false;
        outlineDesc.pushConstantStages = ShaderStage::Pixel;
        outlineDesc.pushConstantBytes = static_cast<JBro::UInt32>(sizeof(float) * 8);
        outlineDesc.sampledTextureCount = 2;
        outlineDesc.samplerCount = 1;
        m_outlineGrowPipeline = m_device->CreateGraphicsPipeline(outlineDesc);
        outlineDesc.pixelShader = PickShader(m_config.api, JBroBuiltinOutlineCompositePS, sizeof(JBroBuiltinOutlineCompositePS),
            Sm5::JBroBuiltinOutlineCompositePS_SM5, sizeof(Sm5::JBroBuiltinOutlineCompositePS_SM5),
            Spv::JBroBuiltinOutlineCompositePS_SPV, sizeof(Spv::JBroBuiltinOutlineCompositePS_SPV));
        outlineDesc.blend = BlendMode::Alpha;
        m_outlineCompositePipeline = m_device->CreateGraphicsPipeline(outlineDesc);
        if (false == m_outlineGrowPipeline.IsValid() || false == m_outlineCompositePipeline.IsValid())
        {
            return false;
        }

        // 레이어 합성(D-279). 외곽선과 같은 정점 셰이더(화면을 덮는 사각형)이고, 픽셀 셰이더가 레이어 텍스처를 `Load` 로 읽어
        // 불투명도를 곱한다. 블렌드만 다른 넷이다 - 미리 곱한 색을 얹는 `Layer*` 계수다.
        GraphicsPipelineDesc compositeDesc = outlineDesc;
        compositeDesc.pixelShader = PickShader(m_config.api, JBroBuiltinLayerCompositePS, sizeof(JBroBuiltinLayerCompositePS),
            Sm5::JBroBuiltinLayerCompositePS_SM5, sizeof(Sm5::JBroBuiltinLayerCompositePS_SM5),
            Spv::JBroBuiltinLayerCompositePS_SPV, sizeof(Spv::JBroBuiltinLayerCompositePS_SPV));
        compositeDesc.pushConstantBytes = static_cast<JBro::UInt32>(sizeof(float) * 4);
        compositeDesc.sampledTextureCount = 1;
        const BlendMode compositeBlends[4] = {
            BlendMode::LayerNormal, BlendMode::LayerAdditive, BlendMode::LayerMultiply, BlendMode::LayerScreen};
        for (std::size_t at = 0; at < 4; ++at)
        {
            compositeDesc.blend = compositeBlends[at];
            m_layerCompositePipelines[at] = m_device->CreateGraphicsPipeline(compositeDesc);
            if (false == m_layerCompositePipelines[at].IsValid())
            {
                return false;
            }
        }
        // 아래 그림을 읽는 블렌드(D-283). 레이어(t0)와 아래 그림의 사본(t1)을 읽어 섞은 색으로 덮어쓴다.
        GraphicsPipelineDesc backdropDesc = compositeDesc;
        backdropDesc.pixelShader = PickShader(m_config.api, JBroBuiltinLayerBackdropPS, sizeof(JBroBuiltinLayerBackdropPS),
            Sm5::JBroBuiltinLayerBackdropPS_SM5, sizeof(Sm5::JBroBuiltinLayerBackdropPS_SM5),
            Spv::JBroBuiltinLayerBackdropPS_SPV, sizeof(Spv::JBroBuiltinLayerBackdropPS_SPV));
        backdropDesc.sampledTextureCount = 2;
        backdropDesc.blend = BlendMode::Opaque;
        m_layerBackdropPipeline = m_device->CreateGraphicsPipeline(backdropDesc);
        return m_layerBackdropPipeline.IsValid();
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

        const VertexAttributeDesc vertexAttributes[] = {
            {0, static_cast<JBro::UInt32>(offsetof(MeshVertex, position)), VertexFormat::Float3},
            {1, static_cast<JBro::UInt32>(offsetof(MeshVertex, normal)), VertexFormat::Float3}};
        // 월드 행렬은 행 넷으로 쪼개 넘긴다. 정점 포맷에 4x4 가 없다.
        const VertexAttributeDesc instanceAttributes[] = {
            {2, static_cast<JBro::UInt32>(offsetof(GpuMeshInstance, world)) + 0, VertexFormat::Float4},
            {3, static_cast<JBro::UInt32>(offsetof(GpuMeshInstance, world)) + 16, VertexFormat::Float4},
            {4, static_cast<JBro::UInt32>(offsetof(GpuMeshInstance, world)) + 32, VertexFormat::Float4},
            {5, static_cast<JBro::UInt32>(offsetof(GpuMeshInstance, world)) + 48, VertexFormat::Float4},
            {6, static_cast<JBro::UInt32>(offsetof(GpuMeshInstance, tint)), VertexFormat::Float4}};
        const VertexBufferLayoutDesc vertexLayouts[] = {
            {static_cast<JBro::UInt32>(sizeof(MeshVertex)), VertexStepMode::Vertex, {vertexAttributes, 2}},
            {static_cast<JBro::UInt32>(sizeof(GpuMeshInstance)), VertexStepMode::Instance, {instanceAttributes, 5}}};
        const TextureFormat colorFormats[] = {m_config.backBufferFormat};
        GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = PickShader(m_config.api, JBroBuiltinMeshVS, sizeof(JBroBuiltinMeshVS),
            Sm5::JBroBuiltinMeshVS_SM5, sizeof(Sm5::JBroBuiltinMeshVS_SM5),
            Spv::JBroBuiltinMeshVS_SPV, sizeof(Spv::JBroBuiltinMeshVS_SPV));
        pipelineDesc.pixelShader = PickShader(m_config.api, JBroBuiltinMeshPS, sizeof(JBroBuiltinMeshPS),
            Sm5::JBroBuiltinMeshPS_SM5, sizeof(Sm5::JBroBuiltinMeshPS_SM5),
            Spv::JBroBuiltinMeshPS_SPV, sizeof(Spv::JBroBuiltinMeshPS_SPV));
        pipelineDesc.vertexBuffers = {vertexLayouts, 2};
        pipelineDesc.colorFormats = {colorFormats, 1};
        pipelineDesc.depthFormat = TextureFormat::D32Float;
        pipelineDesc.blend = BlendMode::Opaque;
        pipelineDesc.cull = CullMode::Back;
        pipelineDesc.pushConstantStages = ShaderStage::Vertex;
        pipelineDesc.pushConstantBytes = static_cast<JBro::UInt32>(sizeof(Matrix4x4));
        m_meshPipeline = m_device->CreateGraphicsPipeline(pipelineDesc);
        return m_meshPipeline.IsValid();
    }

    void Renderer::DestroyBuiltinMeshResources()
    {
        if (m_device == nullptr)
        {
            return;
        }
        if (m_meshPipeline.IsValid())
        {
            m_device->DestroyGraphicsPipeline(m_meshPipeline);
            m_meshPipeline = {};
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
        desc.vertexShader = PickShader(m_config.api, JBroBuiltinLight2DVS, sizeof(JBroBuiltinLight2DVS),
            Sm5::JBroBuiltinLight2DVS_SM5, sizeof(Sm5::JBroBuiltinLight2DVS_SM5),
            Spv::JBroBuiltinLight2DVS_SPV, sizeof(Spv::JBroBuiltinLight2DVS_SPV));
        desc.pixelShader = PickShader(m_config.api, JBroBuiltinLight2DPS, sizeof(JBroBuiltinLight2DPS),
            Sm5::JBroBuiltinLight2DPS_SM5, sizeof(Sm5::JBroBuiltinLight2DPS_SM5),
            Spv::JBroBuiltinLight2DPS_SPV, sizeof(Spv::JBroBuiltinLight2DPS_SPV));
        desc.vertexBuffers = {layouts, 2};
        desc.colorFormats = {colorFormats, 1};
        desc.blend = BlendMode::LayerAdditive;
        desc.cull = CullMode::None;
        desc.depthTest = false;
        desc.depthWrite = false;
        desc.pushConstantStages = ShaderStage::Vertex;
        desc.pushConstantBytes = static_cast<JBro::UInt32>(sizeof(Matrix4x4));
        m_light2DPipeline = m_device->CreateGraphicsPipeline(desc);
        return m_light2DPipeline.IsValid();
    }

    void Renderer::DestroyBuiltinLightResources()
    {
        if (m_device == nullptr)
        {
            return;
        }
        if (m_light2DPipeline.IsValid())
        {
            m_device->DestroyGraphicsPipeline(m_light2DPipeline);
            m_light2DPipeline = {};
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
        // 라이트는 뷰마다 낸 차례로 이어져 있다(`ViewPacket::lightOffset`). 그대로 옮긴다.
        const std::size_t count = m_lights.Size();
        if (count == 0)
        {
            return true;
        }
        if (m_gpuLightInstances.Size() < count || m_frame.slot >= MaxFrameSlots || false == m_lightInstanceBuffers[m_frame.slot].IsValid())
        {
            return false;
        }
        for (std::size_t at = 0; at < count; ++at)
        {
            const Light2DSubmit& light = m_lights[at];
            GpuLight2DInstance& instance = m_gpuLightInstances[at];
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
        return m_device->WriteBuffer(m_lightInstanceBuffers[m_frame.slot], 0,
            {reinterpret_cast<const std::byte*>(m_gpuLightInstances.Data()),
                static_cast<JBro::UInt32>(count * sizeof(GpuLight2DInstance))});
    }

    Bool Renderer::RecordLightMap(const ViewPacket& view, TextureHandle lightMap, const Viewport& viewport, const ScissorRect& scissor)
    {
        // 환경광으로 지우고 라이트를 더한다. 알파는 읽지 않는다.
        ColorAttachmentDesc color;
        color.texture = lightMap;
        color.loadOperation = LoadOperation::Clear;
        color.storeOperation = StoreOperation::Store;
        color.clearColor = {view.ambient[0], view.ambient[1], view.ambient[2], 0.0f};
        RenderPassDesc desc;
        desc.colorAttachments = {&color, 1};
        if (false == m_frame.commands->BeginRenderPass(desc))
        {
            return false;
        }
        m_frame.commands->SetViewport(viewport);
        m_frame.commands->SetScissor(scissor);
        Bool drawn = true;
        if (view.lightCount != 0)
        {
            const Matrix4x4 viewProjection = Multiply(view.camera.projection, view.camera.view);
            drawn = m_frame.commands->SetGraphicsPipeline(m_light2DPipeline)
                && m_frame.commands->SetGraphicsConstants(
                    {reinterpret_cast<const std::byte*>(viewProjection.values), static_cast<JBro::UInt32>(sizeof(viewProjection.values))})
                && m_frame.commands->SetVertexBuffer(0, m_spriteVertexBuffer, static_cast<JBro::UInt32>(sizeof(float) * 2), 0)
                && m_frame.commands->SetVertexBuffer(1, m_lightInstanceBuffers[m_frame.slot], static_cast<JBro::UInt32>(sizeof(GpuLight2DInstance)), 0)
                && m_frame.commands->SetIndexBuffer(m_spriteIndexBuffer, IndexFormat::UInt16, 0)
                && m_frame.commands->DrawIndexedInstanced(6, view.lightCount, 0, 0, view.lightOffset);
        }
        m_frame.commands->EndRenderPass();
        return drawn;
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
        const VertexAttributeDesc vertexAttributes[] = {
            {0, 0, VertexFormat::Float2}};
        const VertexAttributeDesc instanceAttributes[] = {
            {1, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, world)) + 0, VertexFormat::Float4},
            {2, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, world)) + 16, VertexFormat::Float4},
            {3, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, world)) + 32, VertexFormat::Float4},
            {4, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, world)) + 48, VertexFormat::Float4},
            {5, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, fill)), VertexFormat::UByte4Norm},
            {6, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, uvRect)), VertexFormat::UShort4Norm},
            {7, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, outline)), VertexFormat::UByte4Norm},
            {8, static_cast<JBro::UInt32>(offsetof(GpuWorldTextInstance, params)), VertexFormat::UShort4Norm}};
        const VertexBufferLayoutDesc vertexLayouts[] = {
            {static_cast<JBro::UInt32>(sizeof(float) * 2), VertexStepMode::Vertex, {vertexAttributes, 1}},
            {static_cast<JBro::UInt32>(sizeof(GpuWorldTextInstance)), VertexStepMode::Instance, {instanceAttributes, 8}}};
        const TextureFormat colorFormats[] = {m_config.backBufferFormat};
        GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = PickShader(m_config.api, JBroBuiltinWorldTextVS, sizeof(JBroBuiltinWorldTextVS),
            Sm5::JBroBuiltinWorldTextVS_SM5, sizeof(Sm5::JBroBuiltinWorldTextVS_SM5),
            Spv::JBroBuiltinWorldTextVS_SPV, sizeof(Spv::JBroBuiltinWorldTextVS_SPV));
        pipelineDesc.pixelShader = PickShader(m_config.api, JBroBuiltinWorldTextPS, sizeof(JBroBuiltinWorldTextPS),
            Sm5::JBroBuiltinWorldTextPS_SM5, sizeof(Sm5::JBroBuiltinWorldTextPS_SM5),
            Spv::JBroBuiltinWorldTextPS_SPV, sizeof(Spv::JBroBuiltinWorldTextPS_SPV));
        pipelineDesc.vertexBuffers = {vertexLayouts, 2};
        pipelineDesc.colorFormats = {colorFormats, 1};
        pipelineDesc.depthFormat = TextureFormat::D32Float;
        // 메시에 가려지되 글자끼리는 가리지 않는다. 판은 양면이다 - 뒤에서 보면 거울 글자다(빌보드는 늘 앞을 본다).
        pipelineDesc.depthTest = true;
        pipelineDesc.depthWrite = false;
        pipelineDesc.blend = BlendMode::Alpha;
        pipelineDesc.cull = CullMode::None;
        pipelineDesc.pushConstantStages = ShaderStage::Vertex;
        pipelineDesc.pushConstantBytes = static_cast<JBro::UInt32>(sizeof(Matrix4x4));
        pipelineDesc.sampledTextureCount = 1;
        pipelineDesc.samplerCount = 1;
        m_worldTextPipeline = m_device->CreateGraphicsPipeline(pipelineDesc);
        return m_worldTextPipeline.IsValid();
    }

    void Renderer::DestroyBuiltinWorldTextResources()
    {
        if (m_device == nullptr)
        {
            return;
        }
        if (m_worldTextPipeline.IsValid())
        {
            m_device->DestroyGraphicsPipeline(m_worldTextPipeline);
            m_worldTextPipeline = {};
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

        if (m_spritePipeline.IsValid())
        {
            m_device->DestroyGraphicsPipeline(m_spritePipeline);
            m_spritePipeline = {};
        }
        if (m_spriteOverDepthPipeline.IsValid())
        {
            m_device->DestroyGraphicsPipeline(m_spriteOverDepthPipeline);
            m_spriteOverDepthPipeline = {};
        }
        for (GraphicsPipelineHandle* pipeline :
            {&m_sdfTextPipeline, &m_sdfTextOverDepthPipeline, &m_outlineGrowPipeline, &m_outlineCompositePipeline,
                &m_litSpritePipeline, &m_litSdfTextPipeline})
        {
            if (pipeline->IsValid())
            {
                m_device->DestroyGraphicsPipeline(*pipeline);
                *pipeline = {};
            }
        }
        for (GraphicsPipelineHandle& pipeline : m_layerCompositePipelines)
        {
            if (pipeline.IsValid())
            {
                m_device->DestroyGraphicsPipeline(pipeline);
                pipeline = {};
            }
        }
        if (m_layerBackdropPipeline.IsValid())
        {
            m_device->DestroyGraphicsPipeline(m_layerBackdropPipeline);
            m_layerBackdropPipeline = {};
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
        m_litRanges.Clear();
        m_openLitRange = NoLayerGroup;
        m_worldTexts.Clear();
        m_worldTextRuns.Clear();
        m_currentStats = {};
        m_activeView = InvalidViewIndex;
    }
}
