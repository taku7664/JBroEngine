#include "pch.h"
#include "D3D11Swapchain.h"

#if JBRO_PLATFORM_WINDOWS
#include <d3d11.h>
#include <dxgi.h>
#endif

bool CD3D11Swapchain::Initialize(const RenderSurfaceDesc& surfaceDesc)
{
	m_surfaceDesc = surfaceDesc;
	return true;
}

void CD3D11Swapchain::Resize(const RenderSurfaceSize& size)
{
	m_surfaceDesc.Size = size;
}

void CD3D11Swapchain::Present()
{
#if JBRO_PLATFORM_WINDOWS
	if (m_swapchain)
	{
		m_swapchain->Present(1, 0);
	}
#endif
}

RenderSurfaceSize CD3D11Swapchain::GetSize() const
{
	return m_surfaceDesc.Size;
}

void CD3D11Swapchain::Finalize()
{
#if JBRO_PLATFORM_WINDOWS
	if (m_renderTargetView)
	{
		m_renderTargetView->Release();
		m_renderTargetView = nullptr;
	}
	m_swapchain = nullptr;
	m_device = nullptr;
#endif
}

#if JBRO_PLATFORM_WINDOWS
void CD3D11Swapchain::BindNativeSwapchain(IDXGISwapChain* swapchain, ID3D11Device* device)
{
	m_swapchain = swapchain;
	m_device = device;
	CreateBackBufferView();
}

ID3D11RenderTargetView* CD3D11Swapchain::GetRenderTargetView() const
{
	return m_renderTargetView;
}

void CD3D11Swapchain::CreateBackBufferView()
{
	if (nullptr == m_swapchain || nullptr == m_device)
	{
		return;
	}

	ID3D11Texture2D* backBuffer = nullptr;
	HRESULT result = m_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
	if (FAILED(result) || nullptr == backBuffer)
	{
		return;
	}

	result = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
	backBuffer->Release();
	if (FAILED(result))
	{
		m_renderTargetView = nullptr;
	}
}
#endif
