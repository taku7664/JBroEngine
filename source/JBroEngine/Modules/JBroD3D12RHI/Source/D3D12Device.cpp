#include "D3D12Device.h"

namespace JBro::Internal
{
    namespace
    {
        DXGI_FORMAT ToNativeFormat(TextureFormat format)
        {
            switch (format)
            {
            case TextureFormat::RGBA8Unorm:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case TextureFormat::RGBA8UnormSrgb:
                return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case TextureFormat::BGRA8Unorm:
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            case TextureFormat::BGRA8UnormSrgb:
                return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            case TextureFormat::RGBA16Float:
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case TextureFormat::D32Float:
                return DXGI_FORMAT_D32_FLOAT;
            case TextureFormat::Unknown:
                return DXGI_FORMAT_UNKNOWN;
            }

            return DXGI_FORMAT_UNKNOWN;
        }

        std::uint32_t NextGeneration(std::uint32_t generation)
        {
            ++generation;
            if (generation == 0)
            {
                generation = 1;
            }
            return generation;
        }

        bool IsDeviceLostResult(HRESULT result)
        {
            return result == DXGI_ERROR_DEVICE_REMOVED
                || result == DXGI_ERROR_DEVICE_RESET
                || result == DXGI_ERROR_DEVICE_HUNG
                || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }
    }

    bool D3D12Device::Initialize(const RHIDeviceCreateInfo& createInfo)
    {
        if (m_device != nullptr || m_fenceEvent != nullptr)
        {
            return false;
        }

        UINT factoryFlags = 0;
        if (createInfo.enableValidation)
        {
            ComPtr<ID3D12Debug> debugController;
            if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                return false;
            }

            debugController->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }

        if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory))))
        {
            Shutdown();
            return false;
        }

        BOOL allowTearing = FALSE;
        if (SUCCEEDED(m_factory->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING,
            &allowTearing,
            sizeof(allowTearing))))
        {
            m_tearingSupported = allowTearing == TRUE;
        }

        for (UINT adapterIndex = 0;; ++adapterIndex)
        {
            ComPtr<IDXGIAdapter1> candidate;
            const HRESULT enumerationResult = m_factory->EnumAdapterByGpuPreference(
                adapterIndex,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&candidate));
            if (enumerationResult == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }
            if (FAILED(enumerationResult))
            {
                Shutdown();
                return false;
            }

            DXGI_ADAPTER_DESC1 adapterDesc = {};
            if (FAILED(candidate->GetDesc1(&adapterDesc)))
            {
                continue;
            }
            if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            {
                continue;
            }

            if (SUCCEEDED(D3D12CreateDevice(
                candidate.Get(),
                D3D_FEATURE_LEVEL_11_0,
                __uuidof(ID3D12Device),
                nullptr)))
            {
                m_adapter = candidate;
                break;
            }
        }

        if (m_adapter == nullptr)
        {
            if (FAILED(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&m_adapter))))
            {
                Shutdown();
                return false;
            }
        }

        if (FAILED(D3D12CreateDevice(
            m_adapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device))))
        {
            Shutdown();
            return false;
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_graphicsQueue))))
        {
            Shutdown();
            return false;
        }

        if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
        {
            Shutdown();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC renderTargetHeapDesc = {};
        renderTargetHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        renderTargetHeapDesc.NumDescriptors = MaxTextures;
        if (FAILED(m_device->CreateDescriptorHeap(
            &renderTargetHeapDesc,
            IID_PPV_ARGS(&m_textureRenderTargetHeap))))
        {
            Shutdown();
            return false;
        }
        m_textureRenderTargetDescriptorStride =
            m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC depthStencilHeapDesc = {};
        depthStencilHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        depthStencilHeapDesc.NumDescriptors = MaxTextures;
        if (FAILED(m_device->CreateDescriptorHeap(
            &depthStencilHeapDesc,
            IID_PPV_ARGS(&m_textureDepthStencilHeap))))
        {
            Shutdown();
            return false;
        }
        m_textureDepthStencilDescriptorStride =
            m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            Shutdown();
            return false;
        }

        for (std::uint32_t index = 0; index < MaxFramesInFlight; ++index)
        {
            if (FAILED(m_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&m_commandAllocators[index]))))
            {
                Shutdown();
                return false;
            }
        }

        if (FAILED(m_device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_commandAllocators[0].Get(),
            nullptr,
            IID_PPV_ARGS(&m_commandList))))
        {
            Shutdown();
            return false;
        }
        if (FAILED(m_commandList->Close()))
        {
            Shutdown();
            return false;
        }

        m_commandList.As(&m_commandList4);

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
        const bool nativeRenderPasses = m_commandList4 != nullptr
            && SUCCEEDED(m_device->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5,
                &options5,
                sizeof(options5)))
            && options5.RenderPassesTier != D3D12_RENDER_PASS_TIER_0;
        m_commandContext.Initialize(*this, *m_commandList.Get(), m_commandList4.Get(), nativeRenderPasses);

        m_status = FrameStatus::Ready;
        return true;
    }

    void D3D12Device::Shutdown()
    {
        if (m_frameActive)
        {
            FrameContext activeFrame;
            activeFrame.serial = m_activeFrameSerial;
            AbortFrame(activeFrame);
        }

        WaitIdle();
        ReleaseAllResources();

        for (D3D12SwapchainState& swapchain : m_swapchains)
        {
            ReleaseBackBuffers(swapchain);
            swapchain.renderTargetHeap.Reset();
            swapchain.swapchain.Reset();
            swapchain.desc = {};
            swapchain.occupied = false;
        }

        m_commandList4.Reset();
        m_commandList.Reset();
        for (ComPtr<ID3D12CommandAllocator>& allocator : m_commandAllocators)
        {
            allocator.Reset();
        }
        m_fence.Reset();
        m_textureDepthStencilHeap.Reset();
        m_textureRenderTargetHeap.Reset();
        m_graphicsQueue.Reset();
        m_device.Reset();
        m_adapter.Reset();
        m_factory.Reset();

        if (m_fenceEvent != nullptr)
        {
            CloseHandle(m_fenceEvent);
            m_fenceEvent = nullptr;
        }

        for (std::uint64_t& fenceValue : m_frameFenceValues)
        {
            fenceValue = 0;
        }
        m_nextFenceValue = 1;
        m_lastSubmittedFenceValue = 0;
        m_frameSerial = 0;
        m_activeFrameSerial = 0;
        m_activeSwapchainIndex = 0;
        m_activeFrameSlot = 0;
        m_textureRenderTargetDescriptorStride = 0;
        m_textureDepthStencilDescriptorStride = 0;
        m_status = FrameStatus::InvalidState;
        m_tearingSupported = false;
        m_frameActive = false;
        m_hasPendingRetirementFence = false;
    }

    SwapchainHandle D3D12Device::CreateSwapchain(const SwapchainDesc& desc)
    {
        const DXGI_FORMAT nativeFormat = ToNativeFormat(desc.format);
        HWND window = reinterpret_cast<HWND>(desc.surface.value);
        if (m_status != FrameStatus::Ready
            || m_frameActive
            || window == nullptr
            || false == IsWindow(window)
            || desc.extent.width == 0
            || desc.extent.height == 0
            || nativeFormat == DXGI_FORMAT_UNKNOWN
            || desc.bufferCount < 2
            || desc.bufferCount > MaxBackBuffers
            || desc.maxFramesInFlight == 0
            || desc.maxFramesInFlight > MaxFramesInFlight
            || desc.maxFramesInFlight >= desc.bufferCount)
        {
            return {};
        }

        std::uint32_t swapchainIndex = MaxSwapchains;
        for (std::uint32_t index = 0; index < MaxSwapchains; ++index)
        {
            if (false == m_swapchains[index].occupied && m_swapchains[index].swapchain == nullptr)
            {
                swapchainIndex = index;
                break;
            }
        }
        if (swapchainIndex == MaxSwapchains)
        {
            return {};
        }

        D3D12SwapchainState& state = m_swapchains[swapchainIndex];
        DXGI_SWAP_CHAIN_DESC1 nativeDesc = {};
        nativeDesc.Width = desc.extent.width;
        nativeDesc.Height = desc.extent.height;
        nativeDesc.Format = nativeFormat;
        nativeDesc.SampleDesc.Count = 1;
        nativeDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        nativeDesc.BufferCount = desc.bufferCount;
        nativeDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        nativeDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        nativeDesc.Scaling = DXGI_SCALING_STRETCH;
        if (desc.presentMode == PresentMode::Immediate && m_tearingSupported)
        {
            nativeDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }

        ComPtr<IDXGISwapChain1> swapchain1;
        if (FAILED(m_factory->CreateSwapChainForHwnd(
            m_graphicsQueue.Get(),
            window,
            &nativeDesc,
            nullptr,
            nullptr,
            &swapchain1)))
        {
            return {};
        }
        if (FAILED(swapchain1.As(&state.swapchain)))
        {
            return {};
        }

        m_factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = desc.bufferCount;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&state.renderTargetHeap))))
        {
            state.swapchain.Reset();
            return {};
        }

        state.desc = desc;
        state.descriptorStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        if (false == BuildBackBuffers(swapchainIndex, state))
        {
            ReleaseBackBuffers(state);
            state.renderTargetHeap.Reset();
            state.swapchain.Reset();
            state.desc = {};
            return {};
        }

        state.occupied = true;
        return {swapchainIndex, state.generation};
    }

    void D3D12Device::DestroySwapchain(SwapchainHandle swapchain)
    {
        D3D12SwapchainState* state = FindSwapchain(swapchain);
        if (state == nullptr || m_frameActive)
        {
            return;
        }

        WaitIdle();
        ReleaseBackBuffers(*state);
        state->renderTargetHeap.Reset();
        state->swapchain.Reset();
        state->desc = {};
        state->descriptorStride = 0;
        state->occupied = false;
        state->generation = NextGeneration(state->generation);
        state->backBufferGeneration = NextGeneration(state->backBufferGeneration);
    }

    bool D3D12Device::ResizeSwapchain(SwapchainHandle swapchain, const Extent2D& extent)
    {
        D3D12SwapchainState* state = FindSwapchain(swapchain);
        if (state == nullptr
            || m_frameActive
            || extent.width == 0
            || extent.height == 0)
        {
            return false;
        }

        WaitIdle();
        if (m_status == FrameStatus::DeviceLost)
        {
            return false;
        }

        ReleaseBackBuffers(*state);
        DXGI_SWAP_CHAIN_DESC nativeDesc = {};
        if (FAILED(state->swapchain->GetDesc(&nativeDesc)))
        {
            m_status = FrameStatus::SurfaceLost;
            return false;
        }

        const HRESULT resizeResult = state->swapchain->ResizeBuffers(
            state->desc.bufferCount,
            extent.width,
            extent.height,
            ToNativeFormat(state->desc.format),
            nativeDesc.Flags);
        if (FAILED(resizeResult))
        {
            if (IsDeviceLostResult(resizeResult))
            {
                MarkDeviceLost();
            }
            else
            {
                m_status = FrameStatus::SurfaceLost;
            }
            return false;
        }

        state->desc.extent = extent;
        state->backBufferGeneration = NextGeneration(state->backBufferGeneration);
        if (false == BuildBackBuffers(swapchain.index, *state))
        {
            m_status = FrameStatus::SurfaceLost;
            return false;
        }

        m_status = FrameStatus::Ready;
        return true;
    }

    BeginFrameResult D3D12Device::BeginFrame(SwapchainHandle swapchain)
    {
        BeginFrameResult result;
        D3D12SwapchainState* state = FindSwapchain(swapchain);
        if (m_status != FrameStatus::Ready || m_frameActive || state == nullptr)
        {
            result.status = m_status == FrameStatus::Ready ? FrameStatus::InvalidState : m_status;
            return result;
        }

        const std::uint32_t frameSlot = static_cast<std::uint32_t>(
            m_frameSerial % state->desc.maxFramesInFlight);
        if (false == WaitForFence(m_frameFenceValues[frameSlot]))
        {
            result.status = m_status;
            return result;
        }

        if (FAILED(m_commandAllocators[frameSlot]->Reset())
            || FAILED(m_commandList->Reset(m_commandAllocators[frameSlot].Get(), nullptr)))
        {
            MarkDeviceLost();
            result.status = m_status;
            return result;
        }

        const std::uint32_t backBufferIndex = state->swapchain->GetCurrentBackBufferIndex();
        if (backBufferIndex >= state->desc.bufferCount)
        {
            m_commandList->Close();
            m_status = FrameStatus::SurfaceLost;
            result.status = FrameStatus::SurfaceLost;
            return result;
        }

        ++m_frameSerial;
        if (m_frameSerial == 0)
        {
            ++m_frameSerial;
        }

        m_commandContext.Reset();
        m_activeFrameSerial = m_frameSerial;
        m_activeSwapchainIndex = swapchain.index;
        m_activeFrameSlot = frameSlot;
        m_frameActive = true;

        result.status = FrameStatus::Ready;
        result.frame.serial = m_activeFrameSerial;
        result.frame.slot = frameSlot;
        result.frame.backBuffer = state->backBuffers[backBufferIndex].handle;
        result.frame.commands = &m_commandContext;
        return result;
    }

    FrameStatus D3D12Device::EndFrame(const FrameContext& frame)
    {
        if (false == m_frameActive
            || frame.serial != m_activeFrameSerial
            || frame.slot != m_activeFrameSlot
            || frame.commands != &m_commandContext
            || m_commandContext.IsRenderPassActive())
        {
            return FrameStatus::InvalidState;
        }

        D3D12SwapchainState& state = m_swapchains[m_activeSwapchainIndex];
        const std::uint32_t backBufferIndex = state.swapchain->GetCurrentBackBufferIndex();
        if (backBufferIndex >= state.desc.bufferCount
            || frame.backBuffer != state.backBuffers[backBufferIndex].handle)
        {
            return FrameStatus::InvalidState;
        }

        D3D12BackBuffer& backBuffer = state.backBuffers[backBufferIndex];
        if (backBuffer.state != D3D12_RESOURCE_STATE_PRESENT)
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = backBuffer.resource.Get();
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = backBuffer.state;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            m_commandList->ResourceBarrier(1, &barrier);
            backBuffer.state = D3D12_RESOURCE_STATE_PRESENT;
        }

        if (FAILED(m_commandList->Close()))
        {
            MarkDeviceLost();
            m_frameActive = false;
            return m_status;
        }

        ID3D12CommandList* commandLists[] = {m_commandList.Get()};
        m_graphicsQueue->ExecuteCommandLists(1, commandLists);

        UINT syncInterval = 1;
        UINT presentFlags = 0;
        if (state.desc.presentMode == PresentMode::Immediate && m_tearingSupported)
        {
            syncInterval = 0;
            presentFlags = DXGI_PRESENT_ALLOW_TEARING;
        }

        const HRESULT presentResult = state.swapchain->Present(syncInterval, presentFlags);
        const std::uint64_t fenceValue = m_nextFenceValue++;
        const HRESULT signalResult = m_graphicsQueue->Signal(m_fence.Get(), fenceValue);
        if (SUCCEEDED(signalResult))
        {
            m_frameFenceValues[m_activeFrameSlot] = fenceValue;
            m_lastSubmittedFenceValue = fenceValue;
            AssignPendingRetirementFences(fenceValue);
        }

        m_frameActive = false;
        m_activeFrameSerial = 0;

        if (FAILED(signalResult) || IsDeviceLostResult(presentResult))
        {
            MarkDeviceLost();
            return m_status;
        }
        if (FAILED(presentResult))
        {
            m_status = FrameStatus::SurfaceLost;
            return m_status;
        }

        return FrameStatus::Ready;
    }

    void D3D12Device::AbortFrame(const FrameContext& frame)
    {
        if (false == m_frameActive || frame.serial != m_activeFrameSerial)
        {
            return;
        }

        if (m_commandContext.IsRenderPassActive())
        {
            m_commandContext.EndRenderPass();
        }
        m_commandList->Close();
        m_commandContext.Reset();

        D3D12SwapchainState& swapchain = m_swapchains[m_activeSwapchainIndex];
        for (std::uint32_t index = 0; index < swapchain.desc.bufferCount; ++index)
        {
            swapchain.backBuffers[index].state = D3D12_RESOURCE_STATE_PRESENT;
        }

        AssignPendingRetirementFences(m_lastSubmittedFenceValue);

        m_frameActive = false;
        m_activeFrameSerial = 0;
    }

    FrameStatus D3D12Device::GetStatus() const
    {
        return m_status;
    }

    void D3D12Device::WaitIdle()
    {
        if (m_frameActive
            || m_graphicsQueue == nullptr
            || m_fence == nullptr
            || m_fenceEvent == nullptr)
        {
            return;
        }

        const std::uint64_t fenceValue = m_nextFenceValue++;
        if (FAILED(m_graphicsQueue->Signal(m_fence.Get(), fenceValue)))
        {
            MarkDeviceLost();
            return;
        }
        m_lastSubmittedFenceValue = fenceValue;
        AssignPendingRetirementFences(fenceValue);
        if (WaitForFence(fenceValue))
        {
            CollectRetiredResources();
        }
    }

    D3D12SwapchainState* D3D12Device::FindSwapchain(SwapchainHandle swapchain)
    {
        if (swapchain.index >= MaxSwapchains)
        {
            return nullptr;
        }

        D3D12SwapchainState& state = m_swapchains[swapchain.index];
        if (false == state.occupied || state.generation != swapchain.generation)
        {
            return nullptr;
        }
        return &state;
    }

    const D3D12SwapchainState* D3D12Device::FindSwapchain(SwapchainHandle swapchain) const
    {
        if (swapchain.index >= MaxSwapchains)
        {
            return nullptr;
        }

        const D3D12SwapchainState& state = m_swapchains[swapchain.index];
        if (false == state.occupied || state.generation != swapchain.generation)
        {
            return nullptr;
        }
        return &state;
    }

    bool D3D12Device::BuildBackBuffers(
        std::uint32_t swapchainIndex,
        D3D12SwapchainState& state)
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE heapStart =
            state.renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
        for (std::uint32_t index = 0; index < state.desc.bufferCount; ++index)
        {
            D3D12BackBuffer& backBuffer = state.backBuffers[index];
            if (FAILED(state.swapchain->GetBuffer(index, IID_PPV_ARGS(&backBuffer.resource))))
            {
                return false;
            }

            backBuffer.descriptor.ptr = heapStart.ptr
                + static_cast<SIZE_T>(index) * state.descriptorStride;
            backBuffer.handle.index = BackBufferTextureBase
                + swapchainIndex * MaxBackBuffers
                + index;
            backBuffer.handle.generation = state.backBufferGeneration;
            backBuffer.state = D3D12_RESOURCE_STATE_PRESENT;
            m_device->CreateRenderTargetView(
                backBuffer.resource.Get(),
                nullptr,
                backBuffer.descriptor);
        }
        return true;
    }

    void D3D12Device::ReleaseBackBuffers(D3D12SwapchainState& state)
    {
        for (D3D12BackBuffer& backBuffer : state.backBuffers)
        {
            backBuffer.resource.Reset();
            backBuffer.handle = {};
            backBuffer.descriptor = {};
            backBuffer.state = D3D12_RESOURCE_STATE_PRESENT;
        }
    }

    bool D3D12Device::WaitForFence(std::uint64_t fenceValue)
    {
        if (fenceValue == 0 || m_fence->GetCompletedValue() >= fenceValue)
        {
            return true;
        }

        if (FAILED(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent)))
        {
            MarkDeviceLost();
            return false;
        }

        if (WaitForSingleObject(m_fenceEvent, INFINITE) != WAIT_OBJECT_0)
        {
            MarkDeviceLost();
            return false;
        }
        return true;
    }

    void D3D12Device::MarkDeviceLost()
    {
        m_status = FrameStatus::DeviceLost;
    }
}
