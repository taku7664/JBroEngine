#include <JBro/Graphics/Renderer.h>

#include <new>
#include <limits>

namespace JBro
{
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
            || config.maxViews == 0)
        {
            return false;
        }

        try
        {
            m_views.Reserve(config.maxViews);
            m_sprites.Reserve(config.maxSpriteSubmissions);
            m_meshes.Reserve(config.maxMeshSubmissions);
        }
        catch (const std::bad_alloc&)
        {
            m_views = {};
            m_sprites = {};
            m_meshes = {};
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
            return false;
        }

        m_config = config;
        m_rhi = &rhi;
        m_device = device;
        m_swapchain = swapchain;
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
            if (m_swapchain.IsValid())
            {
                m_device->DestroySwapchain(m_swapchain);
            }
            m_rhi->DestroyDevice(m_device);
        }

        m_config = {};
        m_swapchain = {};
        m_frame = {};
        m_views = {};
        m_sprites = {};
        m_meshes = {};
        m_currentStats = {};
        m_lastStats = {};
        m_activeView = InvalidViewIndex;
        m_frameActive = false;
        m_device = nullptr;
        m_rhi = nullptr;
    }

    FrameStatus Renderer::BeginFrame()
    {
        if (m_device == nullptr || false == m_swapchain.IsValid() || m_frameActive)
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

        const FrameStatus status = m_device->EndFrame(m_frame);
        m_lastStats = m_currentStats;
        m_frame = {};
        m_frameActive = false;
        return status;
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

    bool Renderer::RecordViews()
    {
        if (m_frame.commands == nullptr)
        {
            return false;
        }

        for (std::size_t index = 0; index < m_views.Size(); ++index)
        {
            const ViewPacket& view = m_views[index];
            Viewport viewport = view.camera.viewport;
            if (viewport.width <= 0.0f || viewport.height <= 0.0f)
            {
                viewport.x = 0.0f;
                viewport.y = 0.0f;
                viewport.width = static_cast<float>(m_config.surfaceExtent.width);
                viewport.height = static_cast<float>(m_config.surfaceExtent.height);
            }

            const float right = viewport.x + viewport.width;
            const float bottom = viewport.y + viewport.height;
            if (viewport.x < 0.0f
                || viewport.y < 0.0f
                || right > static_cast<float>(m_config.surfaceExtent.width)
                || bottom > static_cast<float>(m_config.surfaceExtent.height)
                || viewport.minDepth < 0.0f
                || viewport.maxDepth > 1.0f
                || viewport.minDepth > viewport.maxDepth)
            {
                return false;
            }

            ColorAttachmentDesc colorAttachment;
            colorAttachment.texture = m_frame.backBuffer;
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
            m_frame.commands->EndRenderPass();
        }

        return true;
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
