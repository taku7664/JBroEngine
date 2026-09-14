#include <JBro/Graphics/Renderer.h>

#include "BuiltinSpritePS.generated.h"
#include "BuiltinSpriteVS.generated.h"

#include <limits>
#include <new>

namespace JBro
{
    namespace
    {
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
        if (false == CreateBuiltinSpriteResources())
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
        m_currentStats = {};
        m_lastStats = {};
        m_activeView = InvalidViewIndex;
        m_frameActive = false;
        m_device = nullptr;
        m_rhi = nullptr;
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

        if (false == RecordViews())
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
                *m_frame.commands, m_frame.backBuffer, m_frameOverlayUser))
        {
            m_device->AbortFrame(m_frame);
            m_lastStats = m_currentStats;
            m_frame = {};
            m_frameActive = false;
            return FrameStatus::InvalidState;
        }

        const FrameStatus status = m_device->EndFrame(m_frame);
        m_lastStats = m_currentStats;
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

        m_config.surfaceExtent = extent;
        return true;
    }

    RendererFrameStats Renderer::GetLastFrameStats() const
    {
        return m_lastStats;
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
        if (m_frame.commands == nullptr || false == UploadSpriteInstances())
        {
            return false;
        }

        // 뷰가 갈 곳과 그 크기다. 타깃을 안 준 프레임은 백버퍼로 간다.
        const bool toTexture = m_frameTarget.texture.IsValid();
        const TextureHandle target = toTexture ? m_frameTarget.texture : m_frame.backBuffer;
        const Extent2D extent = toTexture ? m_frameTarget.extent : m_config.surfaceExtent;

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
            if (false == m_frame.commands->BeginRenderPass(pass))
            {
                return false;
            }

            const ScissorRect scissor = {
                static_cast<std::int32_t>(viewport.x),
                static_cast<std::int32_t>(viewport.y),
                static_cast<std::int32_t>(right),
                static_cast<std::int32_t>(bottom)};
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
                if (false == m_frame.commands->SetGraphicsPipeline(m_spritePipeline)
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
                    || false == m_frame.commands->SetGraphicsConstants(constants)
                    || false == m_frame.commands->DrawIndexedInstanced(
                        6,
                        view.spriteCount,
                        0,
                        0,
                        view.spriteOffset))
                {
                    return false;
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
                VertexFormat::Float4}};
        const VertexBufferLayoutDesc vertexLayouts[] = {
            {sizeof(float) * 2, VertexStepMode::Vertex, {vertexAttributes, 1}},
            {sizeof(GpuSpriteInstance), VertexStepMode::Instance, {instanceAttributes, 3}}};
        const TextureFormat colorFormats[] = {m_config.backBufferFormat};

        GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = {JBroBuiltinSpriteVS, sizeof(JBroBuiltinSpriteVS)};
        pipelineDesc.pixelShader = {JBroBuiltinSpritePS, sizeof(JBroBuiltinSpritePS)};
        pipelineDesc.vertexBuffers = {vertexLayouts, 2};
        pipelineDesc.colorFormats = {colorFormats, 1};
        pipelineDesc.blend = BlendMode::Alpha;
        pipelineDesc.cull = CullMode::None;
        pipelineDesc.pushConstantStages = ShaderStage::Vertex;
        pipelineDesc.pushConstantBytes = sizeof(Matrix4x4);
        m_spritePipeline = m_device->CreateGraphicsPipeline(pipelineDesc);
        return m_spritePipeline.IsValid();
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
        m_gpuSpriteInstances.Clear();
        for (const SpriteSubmit& sprite : m_sprites)
        {
            GpuSpriteInstance instance;
            instance.world = sprite.world;
            instance.tint[0] = sprite.tint[0];
            instance.tint[1] = sprite.tint[1];
            instance.tint[2] = sprite.tint[2];
            instance.tint[3] = sprite.tint[3];
            m_gpuSpriteInstances.Add(instance);
        }

        if (m_gpuSpriteInstances.IsEmpty())
        {
            return true;
        }
        if (m_frame.slot >= MaxFrameSlots
            || false == m_spriteInstanceBuffers[m_frame.slot].IsValid())
        {
            return false;
        }

        const std::size_t byteSize = m_gpuSpriteInstances.Size() * sizeof(GpuSpriteInstance);
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
        m_currentStats = {};
        m_activeView = InvalidViewIndex;
    }
}
