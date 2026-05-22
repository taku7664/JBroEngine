#pragma once

#include "Core/Platform/PlatformDefines.h"
#include "Core/RHI/IRHISwapchain.h"

#if JBRO_PLATFORM_WINDOWS
struct IDXGISwapChain;
struct ID3D11Device;
struct ID3D11RenderTargetView;
#endif

class CD3D11Swapchain final : public IRHISwapchain
{
public:
	bool Initialize(const RenderSurfaceDesc& surfaceDesc) override;
	void Resize(const RenderSurfaceSize& size) override;
	void Present() override;
	RenderSurfaceSize GetSize() const override;

	void Finalize();

#if JBRO_PLATFORM_WINDOWS
	void BindNativeSwapchain(IDXGISwapChain* swapchain, ID3D11Device* device);
	ID3D11RenderTargetView* GetRenderTargetView() const;
#endif

private:
#if JBRO_PLATFORM_WINDOWS
	void CreateBackBufferView();
#endif

private:
	RenderSurfaceDesc m_surfaceDesc;
#if JBRO_PLATFORM_WINDOWS
	IDXGISwapChain* m_swapchain = nullptr;
	ID3D11Device* m_device = nullptr;
	ID3D11RenderTargetView* m_renderTargetView = nullptr;
#endif
};
