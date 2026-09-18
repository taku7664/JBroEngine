#include "D3D11Device.h"

#include <cstring>

namespace JBro::Internal
{
    bool D3D11Device::Initialize(const RHIDeviceCreateInfo& createInfo)
    {
        if (m_device != nullptr)
        {
            return false;
        }
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (createInfo.enableValidation)
        {
            flags |= D3D11_CREATE_DEVICE_DEBUG;
        }
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL selected = D3D_FEATURE_LEVEL_11_0;
        HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
            D3D11_SDK_VERSION, &m_device, &selected, &m_context);
        if (FAILED(result) && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0)
        {
            // SDK 레이어가 없는 기계다. 검증 없이 다시 만든다 - 검증 하나 때문에 디바이스가 없으면 안 된다.
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                D3D11_SDK_VERSION, &m_device, &selected, &m_context);
        }
        if (FAILED(result) || m_device == nullptr || m_context == nullptr)
        {
            m_device.Reset();
            m_context.Reset();
            return false;
        }
        // 디바이스가 선 어댑터의 팩토리를 쓴다. 다른 팩토리로 스왑체인을 만들면 GPU 가 둘인 기계에서 어긋난다.
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(m_device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter))
            || FAILED(adapter->GetParent(IID_PPV_ARGS(&m_factory))))
        {
            m_device.Reset();
            m_context.Reset();
            return false;
        }
        // 테어링(sync interval 0 에 진짜로 vblank 를 기다리지 않기)은 팩토리가 지원할 때만.
        ComPtr<IDXGIFactory5> factory5;
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(m_factory.As(&factory5))
            && SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing,
                sizeof(allowTearing))))
        {
            m_tearingSupported = allowTearing == TRUE;
        }
        if ((flags & D3D11_CREATE_DEVICE_DEBUG) != 0)
        {
            // 오류를 세는 길이다. 없으면 0 을 돌려주고, 그것은 "조용했다" 가 아니라 "못 들었다" 다.
            m_device.As(&m_infoQueue);
            if (m_infoQueue != nullptr)
            {
                m_infoQueue->SetMuteDebugOutput(FALSE);
            }
        }
        m_commandContext.Bind(this, m_context.Get());
        m_status = FrameStatus::Ready;
        return true;
    }

    void D3D11Device::Shutdown()
    {
        if (m_context != nullptr)
        {
            m_context->ClearState();
            m_context->Flush();
        }
        for (D3D11SwapchainState& state : m_swapchains)
        {
            state = {};
        }
        for (D3D11BufferState& state : m_buffers)
        {
            state = {};
        }
        for (D3D11TextureState& state : m_textures)
        {
            state = {};
        }
        for (D3D11PipelineState& state : m_graphicsPipelines)
        {
            state = {};
        }
        for (D3D11SamplerState& state : m_samplers)
        {
            state = {};
        }
        m_commandContext.Reset();
        m_infoQueue.Reset();
        m_context.Reset();
        m_device.Reset();
        m_factory.Reset();
        m_frameActive = false;
        m_status = FrameStatus::InvalidState;
    }

    void D3D11Device::MarkDeviceLost()
    {
        m_status = FrameStatus::DeviceLost;
        m_frameActive = false;
    }

    D3D11SwapchainState* D3D11Device::FindSwapchain(SwapchainHandle swapchain)
    {
        if (false == swapchain.IsValid() || swapchain.index >= MaxSwapchains)
        {
            return nullptr;
        }
        D3D11SwapchainState& state = m_swapchains[swapchain.index];
        if (false == state.occupied || state.generation != swapchain.generation)
        {
            return nullptr;
        }
        return &state;
    }

    bool D3D11Device::BuildBackBuffer(D3D11SwapchainState& state)
    {
        state.backBuffer.Reset();
        state.backBufferView.Reset();
        state.presentedCopy.Reset();
        if (FAILED(state.swapchain->GetBuffer(0, IID_PPV_ARGS(&state.backBuffer))))
        {
            return false;
        }
        if (FAILED(m_device->CreateRenderTargetView(state.backBuffer.Get(), nullptr, &state.backBufferView)))
        {
            return false;
        }
        D3D11_TEXTURE2D_DESC copyDesc = {};
        state.backBuffer->GetDesc(&copyDesc);
        copyDesc.BindFlags = 0;
        copyDesc.MiscFlags = 0;
        copyDesc.Usage = D3D11_USAGE_DEFAULT;
        copyDesc.CPUAccessFlags = 0;
        return SUCCEEDED(m_device->CreateTexture2D(&copyDesc, nullptr, &state.presentedCopy));
    }

    SwapchainHandle D3D11Device::CreateSwapchain(const SwapchainDesc& desc)
    {
        if (m_status != FrameStatus::Ready || m_frameActive || desc.surface.value == 0
            || desc.extent.width == 0 || desc.extent.height == 0 || desc.bufferCount < 2
            || ToNativeFormat(desc.format) == DXGI_FORMAT_UNKNOWN || desc.format == TextureFormat::D32Float)
        {
            return {};
        }
        std::uint32_t index = MaxSwapchains;
        for (std::uint32_t at = 0; at < MaxSwapchains; ++at)
        {
            if (false == m_swapchains[at].occupied)
            {
                index = at;
                break;
            }
        }
        if (index == MaxSwapchains)
        {
            return {};
        }
        DXGI_SWAP_CHAIN_DESC1 native = {};
        native.Width = desc.extent.width;
        native.Height = desc.extent.height;
        native.Format = ToNativeFormat(desc.format);
        native.SampleDesc.Count = 1;
        native.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        native.BufferCount = desc.bufferCount;
        native.Scaling = DXGI_SCALING_STRETCH;
        native.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        native.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        if (m_tearingSupported)
        {
            native.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }
        D3D11SwapchainState& state = m_swapchains[index];
        const HWND window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(desc.surface.value));
        if (FAILED(m_factory->CreateSwapChainForHwnd(m_device.Get(), window, &native, nullptr, nullptr,
                &state.swapchain)))
        {
            state.swapchain.Reset();
            return {};
        }
        // Alt+Enter 로 DXGI 가 혼자 전체 화면으로 가지 않게 한다. 창 크기는 플랫폼이 다룬다.
        m_factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        state.desc = desc;
        if (false == BuildBackBuffer(state))
        {
            state.swapchain.Reset();
            return {};
        }
        state.occupied = true;
        return SwapchainHandle{index, state.generation};
    }

    void D3D11Device::DestroySwapchain(SwapchainHandle swapchain)
    {
        D3D11SwapchainState* state = FindSwapchain(swapchain);
        if (state == nullptr || m_frameActive)
        {
            return;
        }
        if (m_context != nullptr)
        {
            m_context->ClearState();
        }
        const std::uint32_t generation = state->generation + 1;
        const std::uint32_t backBufferGeneration = state->backBufferGeneration + 1;
        *state = {};
        state->generation = generation;
        state->backBufferGeneration = backBufferGeneration;
    }

    bool D3D11Device::ResizeSwapchain(SwapchainHandle swapchain, const Extent2D& extent)
    {
        D3D11SwapchainState* state = FindSwapchain(swapchain);
        if (state == nullptr || m_frameActive || extent.width == 0 || extent.height == 0)
        {
            return false;
        }
        // DXGI 는 백버퍼를 잡고 있는 참조가 없어야 크기를 바꾼다. 컨텍스트가 미뤄 둔 참조까지 `Flush` 로 놓는다.
        m_context->ClearState();
        m_context->Flush();
        state->backBuffer.Reset();
        state->backBufferView.Reset();
        state->presentedCopy.Reset();
        const UINT flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        if (FAILED(state->swapchain->ResizeBuffers(0, extent.width, extent.height, DXGI_FORMAT_UNKNOWN, flags))
            || false == BuildBackBuffer(*state))
        {
            // 백버퍼가 없는 스왑체인이다. 다음 `BeginFrame` 이 SurfaceLost 를 돌려 주고 호스트가 다시 시도한다.
            m_status = FrameStatus::SurfaceLost;
            return false;
        }
        ++state->backBufferGeneration;
        state->desc.extent = extent;
        m_status = FrameStatus::Ready;
        return true;
    }

    BeginFrameResult D3D11Device::BeginFrame(SwapchainHandle swapchain)
    {
        BeginFrameResult result;
        if (m_status != FrameStatus::Ready || m_frameActive)
        {
            result.status = m_status == FrameStatus::Ready ? FrameStatus::InvalidState : m_status;
            return result;
        }
        D3D11SwapchainState* state = FindSwapchain(swapchain);
        if (state == nullptr)
        {
            result.status = FrameStatus::InvalidState;
            return result;
        }
        m_activeSwapchainIndex = swapchain.index;
        m_frameActive = true;
        m_commandContext.Reset();
        result.status = FrameStatus::Ready;
        result.frame.serial = ++m_frameSerial;
        result.frame.slot = 0;
        result.frame.backBuffer = TextureHandle{BackBufferTextureBase + swapchain.index, state->backBufferGeneration};
        result.frame.commands = &m_commandContext;
        return result;
    }

    FrameStatus D3D11Device::EndFrame(const FrameContext& frame)
    {
        if (false == m_frameActive || frame.serial != m_frameSerial)
        {
            return FrameStatus::InvalidState;
        }
        if (m_commandContext.IsRenderPassActive())
        {
            m_commandContext.EndRenderPass();
        }
        D3D11SwapchainState& state = m_swapchains[m_activeSwapchainIndex];
        m_frameActive = false;
        if (false == state.occupied || state.swapchain == nullptr)
        {
            return FrameStatus::InvalidState;
        }
        // 제시하면 이 버퍼는 우리 것이 아니다. 되읽기용 사본을 먼저 뜬다 - 한 프레임에 복사 하나다.
        if (state.presentedCopy != nullptr)
        {
            m_context->CopyResource(state.presentedCopy.Get(), state.backBuffer.Get());
        }
        const bool vsync = state.desc.presentMode == PresentMode::VSync;
        const UINT presentFlags = (false == vsync && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        const HRESULT result = state.swapchain->Present(vsync ? 1 : 0, presentFlags);
        if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
        {
            MarkDeviceLost();
            return FrameStatus::DeviceLost;
        }
        if (FAILED(result))
        {
            // D3D12 와 같다: 표면을 다시 세울 때까지 프레임을 열지 않는다.
            m_status = FrameStatus::SurfaceLost;
            return FrameStatus::SurfaceLost;
        }
        return FrameStatus::Ready;
    }

    void D3D11Device::AbortFrame(const FrameContext& frame)
    {
        if (false == m_frameActive || frame.serial != m_frameSerial)
        {
            return;
        }
        if (m_commandContext.IsRenderPassActive())
        {
            m_commandContext.EndRenderPass();
        }
        // 이미 컨텍스트로 간 명령은 되돌릴 수 없다. 백버퍼는 제시하지 않으니 화면에는 안 나온다.
        m_frameActive = false;
    }

    FrameStatus D3D11Device::GetStatus() const
    {
        return m_status;
    }

    void D3D11Device::WaitIdle()
    {
        if (m_context == nullptr)
        {
            return;
        }
        // D3D11 은 자원 수명을 참조 계수로 스스로 지킨다. 여기서는 밀어 두는 것으로 충분하다 -
        // 되읽기는 `Map(READ)` 이 스스로 GPU 를 기다린다.
        m_context->Flush();
    }

    std::uint32_t D3D11Device::GetFramesInFlight() const
    {
        return 1;
    }

    std::uint32_t D3D11Device::GetValidationErrorCount() const
    {
        if (m_infoQueue == nullptr)
        {
            return 0;
        }
        std::uint32_t errors = 0;
        const UINT64 count = m_infoQueue->GetNumStoredMessages();
        for (UINT64 index = 0; index < count; ++index)
        {
            SIZE_T length = 0;
            if (FAILED(m_infoQueue->GetMessage(index, nullptr, &length)) || length == 0)
            {
                continue;
            }
            // 메시지 머리만 읽으면 되지만 API 가 통째로 달라 한다. 긴 것(INFO 는 1KB 를 넘기도 한다)은 힙으로 읽는다 -
            // 길다고 오류로 세면 조용한 테스트가 거짓으로 진다.
            alignas(D3D11_MESSAGE) unsigned char storage[1024];
            Array<std::byte> longStorage;
            unsigned char* bytes = storage;
            if (length > sizeof(storage))
            {
                longStorage.Resize(length + alignof(D3D11_MESSAGE));
                const std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(longStorage.Data());
                const std::uintptr_t aligned = (raw + alignof(D3D11_MESSAGE) - 1) & ~(alignof(D3D11_MESSAGE) - 1);
                bytes = reinterpret_cast<unsigned char*>(aligned);
            }
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes);
            if (SUCCEEDED(m_infoQueue->GetMessage(index, message, &length))
                && (message->Severity == D3D11_MESSAGE_SEVERITY_ERROR
                    || message->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION))
            {
                ++errors;
            }
        }
        return errors;
    }

    bool D3D11Device::ResolveReadableTexture(TextureHandle texture, ID3D11Texture2D*& resource, TextureDesc& desc)
    {
        if (false == texture.IsValid() || texture.index < BackBufferTextureBase)
        {
            return false;
        }
        if (texture.index < TextureResourceBase)
        {
            const std::uint32_t index = texture.index - BackBufferTextureBase;
            if (index >= MaxSwapchains)
            {
                return false;
            }
            D3D11SwapchainState& state = m_swapchains[index];
            if (false == state.occupied || state.backBufferGeneration != texture.generation
                || state.presentedCopy == nullptr)
            {
                return false;
            }
            resource = state.presentedCopy.Get();
            desc = {};
            desc.extent = state.desc.extent;
            desc.format = state.desc.format;
            desc.usage = TextureUsage::RenderTarget;
            return true;
        }
        const std::uint32_t slot = texture.index - TextureResourceBase;
        if (slot >= MaxTextures)
        {
            return false;
        }
        D3D11TextureState& state = m_textures[slot];
        if (false == state.occupied || state.generation != texture.generation)
        {
            return false;
        }
        resource = state.texture.Get();
        desc = state.desc;
        return true;
    }

    bool D3D11Device::ReadTexture(
        TextureHandle texture,
        std::byte* destination,
        std::size_t destinationSize,
        TextureReadback& result)
    {
        result = {};
        if (m_status != FrameStatus::Ready || m_device == nullptr || m_frameActive
            || destination == nullptr || destinationSize == 0)
        {
            return false;
        }
        ID3D11Texture2D* source = nullptr;
        TextureDesc desc;
        if (false == ResolveReadableTexture(texture, source, desc) || source == nullptr)
        {
            return false;
        }
        const std::uint32_t pixelSize = PixelSize(desc.format);
        if (pixelSize == 0 || desc.format == TextureFormat::D32Float || desc.depthOrLayers != 1)
        {
            return false;
        }
        const std::size_t tightRowPitch = static_cast<std::size_t>(desc.extent.width) * pixelSize;
        const std::size_t requiredBytes = tightRowPitch * desc.extent.height;
        if (destinationSize < requiredBytes)
        {
            return false;
        }

        D3D11_TEXTURE2D_DESC stagingDesc = {};
        source->GetDesc(&stagingDesc);
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(m_device->CreateTexture2D(&stagingDesc, nullptr, &staging)))
        {
            return false;
        }
        m_context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source, 0, nullptr);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            return false;
        }
        const auto* rows = static_cast<const std::byte*>(mapped.pData);
        for (std::uint32_t y = 0; y < desc.extent.height; ++y)
        {
            std::memcpy(destination + y * tightRowPitch, rows + static_cast<std::size_t>(y) * mapped.RowPitch,
                tightRowPitch);
        }
        m_context->Unmap(staging.Get(), 0);
        result.extent = desc.extent;
        result.format = desc.format;
        result.rowPitch = static_cast<std::uint32_t>(tightRowPitch);
        result.writtenBytes = static_cast<std::uint32_t>(requiredBytes);
        return true;
    }
}
