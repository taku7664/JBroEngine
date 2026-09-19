#include <JBro/Graphics/Renderer.h>

#include "BuiltinMeshPS.generated.h"
#include "BuiltinMeshVS.generated.h"
#include "BuiltinSpritePS.generated.h"
#include "BuiltinSpriteVS.generated.h"

// D3D11 은 DXIL 을 읽지 못해 같은 HLSL 을 SM 5.0 DXBC 로도 굽는다(D-107). fxc 의 헤더는 `BYTE` 를
// 쓰므로 그 이름을 이 네임스페이스 안에서만 준다 - windows.h 를 렌더러에 들이지 않는다.
namespace JBro::Sm5
{
    using BYTE = unsigned char;
#include "BuiltinMeshPS_SM5.generated.h"
#include "BuiltinMeshVS_SM5.generated.h"
#include "BuiltinSpritePS_SM5.generated.h"
#include "BuiltinSpriteVS_SM5.generated.h"
}

// Vulkan 은 SPIR-V 를 읽는다(D-108). Vulkan SDK 의 dxc 가 같은 HLSL 을 `-spirv` 로 구운 것이다.
namespace JBro::Spv
{
#include "BuiltinMeshPS_SPV.generated.h"
#include "BuiltinMeshVS_SPV.generated.h"
#include "BuiltinSpritePS_SPV.generated.h"
#include "BuiltinSpriteVS_SPV.generated.h"
}

#include <cmath>
#include <limits>
#include <new>

namespace JBro
{
    namespace
    {
        // API 마다 읽는 바이트코드가 다르다. D3D12 는 DXIL, D3D11 은 DXBC, Vulkan 은 SPIR-V 다.
        ShaderBytecode PickShader(GraphicsApi api, const unsigned char* dxil, std::size_t dxilSize,
            const unsigned char* dxbc, std::size_t dxbcSize, const unsigned char* spirv, std::size_t spirvSize)
        {
            if (api == GraphicsApi::D3D11)
            {
                return {dxbc, static_cast<std::uint32_t>(dxbcSize)};
            }
            if (api == GraphicsApi::Vulkan)
            {
                return {spirv, static_cast<std::uint32_t>(spirvSize)};
            }
            return {dxil, static_cast<std::uint32_t>(dxilSize)};
        }

        Matrix4x4 Multiply(const Matrix4x4& left, const Matrix4x4& right)
        {
            Matrix4x4 result;
            for (std::uint32_t row = 0; row < 4; ++row)
            {
                for (std::uint32_t column = 0; column < 4; ++column)
                {
                    float value = 0.0f;
                    for (std::uint32_t element = 0; element < 4; ++element)
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

    bool Renderer::Initialize(IRHIModule& rhi, const RendererConfig& config)
    {
        if (m_device != nullptr
            || rhi.GetApi() != config.api
            || config.surface.value == 0
            || config.surfaceExtent.width == 0
            || config.surfaceExtent.height == 0
            || config.surfaceExtent.width > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())
            || config.surfaceExtent.height > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())
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
            m_gpuSpriteInstances.Reserve(config.maxSpriteSubmissions);
            m_gpuMeshInstances.Reserve(config.maxMeshSubmissions);
            m_meshRuns.Reserve(config.maxMeshSubmissions);
            // 묶음은 많아도 스프라이트 수를 넘지 않는다. 프레임 안에서 자라지 않게 여기서 잡는다.
            m_spriteRuns.Reserve(config.maxSpriteSubmissions);
            m_textureResources.Reserve(64);
            m_gpuSpriteInstances.Resize(config.maxSpriteSubmissions);
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
        if (false == CreateBuiltinSpriteResources() || false == CreateBuiltinMeshResources())
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
        m_gpuSpriteCount = 0;
        m_gpuMeshCount = 0;
        m_meshRuns = {};
        m_meshHistogram = {};
        m_meshResources = {};
        m_currentStats = {};
        m_lastStats = {};
        m_lastPresentedBackBuffer = {};
        m_lastViewCamera = {};
        m_hasLastViewCamera = false;
        m_frameOverlay = nullptr;
        m_frameOverlayUser = nullptr;
        m_activeView = InvalidViewIndex;
        m_frameActive = false;
        m_device = nullptr;
        m_rhi = nullptr;
    }

    AssetHandle Renderer::RegisterMesh(JArrayView<MeshVertex> vertices, JArrayView<std::uint32_t> indices)
    {
        if (m_device == nullptr || m_frameActive
            || vertices.data == nullptr || vertices.size == 0
            || indices.data == nullptr || indices.size == 0 || indices.size % 3 != 0)
        {
            return {};
        }
        // 색인이 정점 밖을 가리키면 GPU 가 쓰레기를 읽는다. 여기서 거절한다.
        for (std::uint32_t index = 0; index < indices.size; ++index)
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
        const bool written = resource.vertexBuffer.IsValid() && resource.indexBuffer.IsValid()
            && m_device->WriteBuffer(resource.vertexBuffer, 0,
                {reinterpret_cast<const std::byte*>(vertices.data),
                    static_cast<std::uint32_t>(vertexDesc.size)})
            && m_device->WriteBuffer(resource.indexBuffer, 0,
                {reinterpret_cast<const std::byte*>(indices.data),
                    static_cast<std::uint32_t>(indexDesc.size)});
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
                return AssetHandle{static_cast<std::uint32_t>(slot), resource.generation};
            }
        }
        m_meshResources.Add(resource);
        if (m_meshHistogram.Size() < m_meshResources.Size())
        {
            m_meshHistogram.Resize(m_meshResources.Size());
        }
        return AssetHandle{static_cast<std::uint32_t>(m_meshResources.Size() - 1), resource.generation};
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

    std::uint32_t Renderer::GetMeshCount() const
    {
        std::uint32_t count = 0;
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
                return AssetHandle{static_cast<std::uint32_t>(slot), resource.generation};
            }
        }
        m_textureResources.Add(resource);
        return AssetHandle{static_cast<std::uint32_t>(m_textureResources.Size() - 1), resource.generation};
    }

    bool Renderer::UpdateTexture(AssetHandle texture, JArrayView<std::byte> rgba8)
    {
        const TextureResource* resource = FindTexture(texture);
        if (resource == nullptr || m_frameActive || rgba8.data == nullptr
            || rgba8.size != static_cast<std::size_t>(resource->extent.width) * resource->extent.height * 4u)
        {
            return false;
        }
        return m_device->WriteTexture(resource->texture, 0, rgba8);
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

    std::uint32_t Renderer::GetTextureCount() const
    {
        std::uint32_t count = 0;
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

    bool Renderer::AcquireDepthTarget(const Extent2D& extent, bool forTexture, TextureHandle& depth)
    {
        DepthTarget& target = m_depthTargets[forTexture ? 1 : 0];
        if (target.texture.IsValid()
            && target.extent.width == extent.width && target.extent.height == extent.height)
        {
            depth = target.texture;
            return true;
        }
        if (target.texture.IsValid())
        {
            // 크기가 바뀌었다. 디바이스가 은퇴 펜스로 지난 프레임이 다 그린 뒤에 놓으므로 여기서
            // 기다리지 않는다 - 보통 프레임은 GPU 를 기다리지 않는다는 계약이 있다(렌더러 계약 테스트).
            m_device->DestroyTexture(target.texture);
            target = {};
        }
        TextureDesc desc;
        desc.extent = extent;
        desc.format = TextureFormat::D32Float;
        desc.usage = TextureUsage::DepthStencil;
        target.texture = m_device->CreateTexture(desc);
        if (false == target.texture.IsValid())
        {
            return false;
        }
        target.extent = extent;
        depth = target.texture;
        return true;
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
        TextureHandle depth;
        const Extent2D depthExtent = target.texture.IsValid() ? target.extent : m_config.surfaceExtent;
        if (false == AcquireDepthTarget(depthExtent, target.texture.IsValid(), depth))
        {
            return FrameStatus::InvalidState;
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
        // 프레임마다 여기서 통째로 덮어쓴다. 그래서 프레임을 닫을 때 따로 비울 것이 없다.
        m_frameTarget = target;
        m_frameActive = true;
        ResetSubmissionStorage();
        return FrameStatus::Ready;
    }

    bool Renderer::BeginView(const CameraParams& camera)
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
        packet.spriteOffset = static_cast<std::uint32_t>(m_sprites.Size());
        packet.meshOffset = static_cast<std::uint32_t>(m_meshes.Size());
        m_views.Add(packet);
        m_activeView = static_cast<std::uint32_t>(m_views.Size() - 1);
        ++m_currentStats.viewCount;
        return true;
    }

    bool Renderer::SubmitSprite(const SpriteSubmit& item)
    {
        return SubmitSprites({&item, 1});
    }

    bool Renderer::SubmitSprites(JArrayView<SpriteSubmit> items)
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

    bool Renderer::SubmitMesh(const MeshSubmit& item)
    {
        return SubmitMeshes({&item, 1});
    }

    bool Renderer::SubmitMeshes(JArrayView<MeshSubmit> items)
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

    bool Renderer::EndView()
    {
        if (false == m_frameActive || m_activeView == InvalidViewIndex)
        {
            return false;
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

        bool recorded = false;
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
        if (m_views.Size() != 0 && m_frameTarget.recordViews)
        {
            // 그리지 않은 프레임의 카메라는 화면에 없다. 기즈모는 보이는 그림의 카메라를 써야 한다.
            m_lastViewCamera = m_views[0].camera;
            m_hasLastViewCamera = true;
        }
        m_lastPresentedBackBuffer = m_frame.backBuffer;
        m_frame = {};
        m_frameActive = false;
        return status;
    }

    bool Renderer::SetFrameOverlay(FrameOverlay overlay, void* user)
    {
        if (m_frameActive)
        {
            return false;
        }
        m_frameOverlay = overlay;
        m_frameOverlayUser = user;
        return true;
    }

    bool Renderer::HasFrameOverlay() const
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

    bool Renderer::ReadBackBuffer(
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

    bool Renderer::ResizeSurface(const Extent2D& extent)
    {
        if (m_device == nullptr
            || false == m_swapchain.IsValid()
            || m_frameActive
            || extent.width == 0
            || extent.height == 0
            || extent.width > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())
            || extent.height > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)()))
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

    bool Renderer::GetLastViewCamera(CameraParams& camera) const
    {
        if (false == m_hasLastViewCamera)
        {
            return false;
        }
        camera = m_lastViewCamera;
        return true;
    }

    bool Renderer::IsDeviceLost() const
    {
        return m_device != nullptr && m_device->GetStatus() == FrameStatus::DeviceLost;
    }

    bool Renderer::IsInitialized() const
    {
        return m_device != nullptr;
    }

    Extent2D Renderer::GetSurfaceExtent() const
    {
        return m_config.surfaceExtent;
    }

    std::uint32_t Renderer::GetSpriteSubmissionLimit() const
    {
        return m_device != nullptr ? m_config.maxSpriteSubmissions : 0;
    }

    bool Renderer::RecordViews()
    {
        if (m_frame.commands == nullptr)
        {
            return false;
        }
        // **타깃이 뷰를 원하지 않는 프레임이다**(D-63). 제출은 받았지만 기록하지 않는다 -
        // 게임 뷰 패널이 보이지 않을 때 텍스처를 그대로 두는 길이다.
        if (false == m_frameTarget.recordViews)
        {
            m_currentStats.skippedViewCount += static_cast<std::uint32_t>(m_views.Size());
            return true;
        }
        if (false == UploadSpriteInstances() || false == UploadMeshInstances())
        {
            return false;
        }

        // 뷰가 갈 곳과 그 크기다. 타깃을 안 준 프레임은 백버퍼로 간다.
        const bool toTexture = m_frameTarget.texture.IsValid();
        const TextureHandle target = toTexture ? m_frameTarget.texture : m_frame.backBuffer;
        const Extent2D extent = GetFrameExtent();

        for (std::size_t index = 0; index < m_views.Size(); ++index)
        {
            const ViewPacket& view = m_views[index];
            Viewport viewport = view.camera.viewport;
            if (viewport.width <= 0.0f || viewport.height <= 0.0f)
            {
                viewport.x = 0.0f;
                viewport.y = 0.0f;
                viewport.width = static_cast<float>(extent.width);
                viewport.height = static_cast<float>(extent.height);
            }

            const float right = viewport.x + viewport.width;
            const float bottom = viewport.y + viewport.height;
            if (viewport.x < 0.0f
                || viewport.y < 0.0f
                || right > static_cast<float>(extent.width)
                || bottom > static_cast<float>(extent.height)
                || viewport.minDepth < 0.0f
                || viewport.maxDepth > 1.0f
                || viewport.minDepth > viewport.maxDepth)
            {
                return false;
            }

            ColorAttachmentDesc colorAttachment;
            colorAttachment.texture = target;
            colorAttachment.loadOperation = index == 0
                ? LoadOperation::Clear
                : LoadOperation::Load;
            colorAttachment.storeOperation = StoreOperation::Store;
            colorAttachment.clearColor = {
                view.camera.clearColor[0],
                view.camera.clearColor[1],
                view.camera.clearColor[2],
                view.camera.clearColor[3]};

            RenderPassDesc pass;
            pass.colorAttachments = {&colorAttachment, 1};
            // **메시가 있는 뷰만 깊이를 단다**(framework3d-plan §2.4). 스프라이트만 있는 2D 프레임은
            // 전과 같은 패스다. 뷰마다 지운다 - 카메라가 다르면 깊이도 다른 것이다.
            DepthStencilAttachmentDesc depthAttachment;
            if (view.runCount != 0)
            {
                if (false == AcquireDepthTarget(extent, toTexture, depthAttachment.texture))
                {
                    return false;
                }
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

            // 소수 자리의 뷰포트를 정수 시저로 옮긴다. 안쪽으로 자르면 마지막 열이 잘린다 - 바깥으로 넉넉히 잡는다.
            const ScissorRect scissor = {
                static_cast<std::int32_t>(std::floor(viewport.x)),
                static_cast<std::int32_t>(std::floor(viewport.y)),
                static_cast<std::int32_t>(std::ceil(right)),
                static_cast<std::int32_t>(std::ceil(bottom))};
            m_frame.commands->SetViewport(viewport);
            m_frame.commands->SetScissor(scissor);

            if (view.spriteCount != 0)
            {
                const Matrix4x4 viewProjection = Multiply(
                    view.camera.projection,
                    view.camera.view);
                const JArrayView<std::byte> constants = {
                    reinterpret_cast<const std::byte*>(viewProjection.values),
                    sizeof(viewProjection.values)};
                const GraphicsPipelineHandle spritePipeline =
                    view.runCount != 0 ? m_spriteOverDepthPipeline : m_spritePipeline;
                if (false == m_frame.commands->SetGraphicsPipeline(spritePipeline)
                    || false == m_frame.commands->SetVertexBuffer(
                        0,
                        m_spriteVertexBuffer,
                        sizeof(float) * 2,
                        0)
                    || false == m_frame.commands->SetVertexBuffer(
                        1,
                        m_spriteInstanceBuffers[m_frame.slot],
                        sizeof(GpuSpriteInstance),
                        0)
                    || false == m_frame.commands->SetIndexBuffer(
                        m_spriteIndexBuffer,
                        IndexFormat::UInt16,
                        0)
                    || false == m_frame.commands->SetGraphicsConstants(constants))
                {
                    return false;
                }
                for (std::uint32_t runIndex = 0; runIndex < view.spriteRunCount; ++runIndex)
                {
                    const SpriteRun& run = m_spriteRuns[view.spriteRunOffset + runIndex];
                    if (false == m_frame.commands->SetTexture(0, run.texture)
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
            }

            if (view.runCount != 0)
            {
                const Matrix4x4 viewProjection = Multiply(
                    view.camera.projection,
                    view.camera.view);
                const JArrayView<std::byte> constants = {
                    reinterpret_cast<const std::byte*>(viewProjection.values),
                    sizeof(viewProjection.values)};
                if (false == m_frame.commands->SetGraphicsPipeline(m_meshPipeline)
                    || false == m_frame.commands->SetVertexBuffer(
                        1, m_meshInstanceBuffers[m_frame.slot], sizeof(GpuMeshInstance), 0)
                    || false == m_frame.commands->SetGraphicsConstants(constants))
                {
                    return false;
                }
                // 메시 종류마다 한 드로우다(D-110). 업로드가 같은 메시를 이어 놓았다.
                for (std::uint32_t at = 0; at < view.runCount; ++at)
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
                            0, mesh->vertexBuffer, sizeof(MeshVertex), 0)
                        || false == m_frame.commands->SetIndexBuffer(
                            mesh->indexBuffer, IndexFormat::UInt32, 0)
                        || false == m_frame.commands->DrawIndexedInstanced(
                            mesh->indexCount, run.instanceCount, 0, 0, run.firstInstance))
                    {
                        return false;
                    }
                }
            }

            m_frame.commands->EndRenderPass();
        }

        return true;
    }

    bool Renderer::CreateBuiltinSpriteResources()
    {
        if (m_device == nullptr
            || m_config.maxSpriteSubmissions == 0
            || m_config.maxSpriteSubmissions
                > (std::numeric_limits<std::uint32_t>::max)() / sizeof(GpuSpriteInstance))
        {
            return false;
        }

        constexpr float vertices[] = {
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
                {reinterpret_cast<const std::byte*>(vertices), sizeof(vertices)}))
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
                {reinterpret_cast<const std::byte*>(indices), sizeof(indices)}))
        {
            return false;
        }

        BufferDesc instanceBufferDesc;
        instanceBufferDesc.size = static_cast<std::size_t>(m_config.maxSpriteSubmissions)
            * sizeof(GpuSpriteInstance);
        instanceBufferDesc.usage = BufferUsage::Vertex | BufferUsage::CopySource;
        instanceBufferDesc.memory = MemoryType::Upload;
        for (std::uint32_t index = 0; index < m_config.maxFramesInFlight; ++index)
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
            {1, static_cast<std::uint32_t>(TransformOffset + offsetof(SpriteTransform2D, linear)),
                VertexFormat::Float4},
            {2, static_cast<std::uint32_t>(TransformOffset + offsetof(SpriteTransform2D, translation)),
                VertexFormat::Float3},
            {3, static_cast<std::uint32_t>(offsetof(GpuSpriteInstance, tint)),
                VertexFormat::Float4},
            {4, static_cast<std::uint32_t>(offsetof(GpuSpriteInstance, uvRect)),
                VertexFormat::Float4}};
        const VertexBufferLayoutDesc vertexLayouts[] = {
            {sizeof(float) * 2, VertexStepMode::Vertex, {vertexAttributes, 1}},
            {sizeof(GpuSpriteInstance), VertexStepMode::Instance, {instanceAttributes, 4}}};
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
        pipelineDesc.pushConstantBytes = sizeof(Matrix4x4);
        pipelineDesc.sampledTextureCount = 1;
        pipelineDesc.samplerCount = 1;
        m_spritePipeline = m_device->CreateGraphicsPipeline(pipelineDesc);
        // 깊이가 달린 패스용 쌍둥이. 포맷은 맞추고 깊이는 끈다 - 스프라이트는 제출 순서로 겹친다.
        pipelineDesc.depthFormat = TextureFormat::D32Float;
        pipelineDesc.depthTest = false;
        pipelineDesc.depthWrite = false;
        m_spriteOverDepthPipeline = m_device->CreateGraphicsPipeline(pipelineDesc);
        return m_spritePipeline.IsValid() && m_spriteOverDepthPipeline.IsValid();
    }

    bool Renderer::CreateBuiltinMeshResources()
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
        for (std::uint32_t slot = 0; slot < m_config.maxFramesInFlight && slot < MaxFrameSlots; ++slot)
        {
            m_meshInstanceBuffers[slot] = m_device->CreateBuffer(instanceBufferDesc);
            if (false == m_meshInstanceBuffers[slot].IsValid())
            {
                return false;
            }
        }

        const VertexAttributeDesc vertexAttributes[] = {
            {0, static_cast<std::uint32_t>(offsetof(MeshVertex, position)), VertexFormat::Float3},
            {1, static_cast<std::uint32_t>(offsetof(MeshVertex, normal)), VertexFormat::Float3}};
        // 월드 행렬은 행 넷으로 쪼개 넘긴다. 정점 포맷에 4x4 가 없다.
        const VertexAttributeDesc instanceAttributes[] = {
            {2, static_cast<std::uint32_t>(offsetof(GpuMeshInstance, world)) + 0, VertexFormat::Float4},
            {3, static_cast<std::uint32_t>(offsetof(GpuMeshInstance, world)) + 16, VertexFormat::Float4},
            {4, static_cast<std::uint32_t>(offsetof(GpuMeshInstance, world)) + 32, VertexFormat::Float4},
            {5, static_cast<std::uint32_t>(offsetof(GpuMeshInstance, world)) + 48, VertexFormat::Float4},
            {6, static_cast<std::uint32_t>(offsetof(GpuMeshInstance, tint)), VertexFormat::Float4}};
        const VertexBufferLayoutDesc vertexLayouts[] = {
            {sizeof(MeshVertex), VertexStepMode::Vertex, {vertexAttributes, 2}},
            {sizeof(GpuMeshInstance), VertexStepMode::Instance, {instanceAttributes, 5}}};
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
        pipelineDesc.pushConstantBytes = sizeof(Matrix4x4);
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

    bool Renderer::UploadMeshInstances()
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
        std::uint32_t written = 0;
        for (std::size_t viewIndex = 0; viewIndex < m_views.Size(); ++viewIndex)
        {
            ViewPacket& view = m_views[viewIndex];
            view.runOffset = static_cast<std::uint32_t>(m_meshRuns.Size());
            view.runCount = 0;
            if (view.meshCount == 0)
            {
                continue;
            }
            for (std::size_t slot = 0; slot < slotCount; ++slot)
            {
                m_meshHistogram[slot] = 0;
            }
            const std::uint32_t end = view.meshOffset + view.meshCount;
            for (std::uint32_t at = view.meshOffset; at < end; ++at)
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
            std::uint32_t cursor = written;
            for (std::size_t slot = 0; slot < slotCount; ++slot)
            {
                const std::uint32_t count = m_meshHistogram[slot];
                m_meshHistogram[slot] = cursor;
                if (count != 0)
                {
                    MeshRun run;
                    run.mesh = AssetHandle{static_cast<std::uint32_t>(slot), m_meshResources[slot].generation};
                    run.firstInstance = cursor;
                    run.instanceCount = count;
                    m_meshRuns.Add(run);
                    ++view.runCount;
                    cursor += count;
                }
            }
            for (std::uint32_t at = view.meshOffset; at < end; ++at)
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
                static_cast<std::uint32_t>(byteSize)});
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

    bool Renderer::UploadSpriteInstances()
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
        for (std::size_t index = 0; index < count; ++index)
        {
            destination[index].world = source[index].world;
            destination[index].tint[0] = source[index].tint[0];
            destination[index].tint[1] = source[index].tint[1];
            destination[index].tint[2] = source[index].tint[2];
            destination[index].tint[3] = source[index].tint[3];
            destination[index].uvRect[0] = source[index].uvRect[0];
            destination[index].uvRect[1] = source[index].uvRect[1];
            destination[index].uvRect[2] = source[index].uvRect[2];
            destination[index].uvRect[3] = source[index].uvRect[3];
        }

        // 텍스처·샘플러가 같은 이웃을 묶어 드로우 하나로 낸다(D-113). 순서는 바꾸지 않는다 - 정렬은 프레임워크가 끝냈다.
        // 빈 핸들은 흰색이고, 죽은 핸들도 흰색으로 그리되 센다.
        m_spriteRuns.Clear();
        for (std::size_t viewIndex = 0; viewIndex < m_views.Size(); ++viewIndex)
        {
            ViewPacket& view = m_views[viewIndex];
            view.spriteRunOffset = static_cast<std::uint32_t>(m_spriteRuns.Size());
            view.spriteRunCount = 0;
            const std::uint32_t end = view.spriteOffset + view.spriteCount;
            for (std::uint32_t index = view.spriteOffset; index < end; ++index)
            {
                TextureHandle texture = m_whiteTexture;
                if (source[index].texture.generation != 0)
                {
                    const TextureResource* resource = FindTexture(source[index].texture);
                    if (resource != nullptr)
                    {
                        texture = resource->texture;
                    }
                    else
                    {
                        ++m_currentStats.staleTextureSpriteCount;
                    }
                }
                const SamplerHandle sampler =
                    source[index].filter == SpriteFilter::Linear ? m_linearSampler : m_nearestSampler;
                if (view.spriteRunCount != 0)
                {
                    SpriteRun& last = m_spriteRuns.Last();
                    if (last.texture.index == texture.index && last.texture.generation == texture.generation
                        && last.sampler.index == sampler.index && last.sampler.generation == sampler.generation)
                    {
                        ++last.instanceCount;
                        continue;
                    }
                }
                SpriteRun run;
                run.texture = texture;
                run.sampler = sampler;
                run.firstInstance = index;
                run.instanceCount = 1;
                m_spriteRuns.Add(run);
                ++view.spriteRunCount;
            }
        }
        if (m_frame.slot >= MaxFrameSlots
            || false == m_spriteInstanceBuffers[m_frame.slot].IsValid())
        {
            return false;
        }

        const std::size_t byteSize = m_gpuSpriteCount * sizeof(GpuSpriteInstance);
        return m_device->WriteBuffer(
            m_spriteInstanceBuffers[m_frame.slot],
            0,
            {reinterpret_cast<const std::byte*>(m_gpuSpriteInstances.Data()),
                static_cast<std::uint32_t>(byteSize)});
    }

    void Renderer::ResetSubmissionStorage()
    {
        m_views.Clear();
        m_sprites.Clear();
        m_meshes.Clear();
        m_meshRuns.Clear();
        m_spriteRuns.Clear();
        m_currentStats = {};
        m_activeView = InvalidViewIndex;
    }
}
