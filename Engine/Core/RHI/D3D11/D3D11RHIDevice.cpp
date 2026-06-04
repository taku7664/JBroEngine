#include "pch.h"
#include "D3D11RHIDevice.h"

#include "Core/RHI/D3D11/D3D11CommandContext.h"
#include "Core/RHI/D3D11/D3D11Buffer.h"
#include "Core/RHI/D3D11/D3D11GraphicsPipeline.h"
#include "Core/RHI/D3D11/D3D11Program.h"
#include "Core/RHI/D3D11/D3D11Sampler.h"
#include "Core/RHI/D3D11/D3D11Swapchain.h"
#include "Core/RHI/D3D11/D3D11Texture.h"

#include <cstring>

#if JBRO_PLATFORM_WINDOWS
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#endif

#if JBRO_PLATFORM_WINDOWS
namespace
{
	UINT ToD3DBindFlags(RHIBindFlags flags)
	{
		UINT result = 0;
		if (0 != (flags & static_cast<RHIBindFlags>(ERHIBindFlag::VertexBuffer)))
		{
			result |= D3D11_BIND_VERTEX_BUFFER;
		}
		if (0 != (flags & static_cast<RHIBindFlags>(ERHIBindFlag::IndexBuffer)))
		{
			result |= D3D11_BIND_INDEX_BUFFER;
		}
		if (0 != (flags & static_cast<RHIBindFlags>(ERHIBindFlag::ConstantBuffer)))
		{
			result |= D3D11_BIND_CONSTANT_BUFFER;
		}
		return result;
	}

	UINT ToD3DTextureBindFlags(RHITextureBindFlags flags)
	{
		UINT result = 0;
		if (0 != (flags & static_cast<RHITextureBindFlags>(ERHITextureBindFlag::ShaderResource)))
		{
			result |= D3D11_BIND_SHADER_RESOURCE;
		}
		if (0 != (flags & static_cast<RHITextureBindFlags>(ERHITextureBindFlag::RenderTarget)))
		{
			result |= D3D11_BIND_RENDER_TARGET;
		}
		return result;
	}

	D3D11_USAGE ToD3DUsage(ERHIBufferUsage usage)
	{
		switch (usage)
		{
		case ERHIBufferUsage::Immutable:
			return D3D11_USAGE_IMMUTABLE;
		case ERHIBufferUsage::Dynamic:
			return D3D11_USAGE_DYNAMIC;
		default:
			return D3D11_USAGE_DEFAULT;
		}
	}

	DXGI_FORMAT ToD3DVertexFormat(ERHIVertexFormat format)
	{
		switch (format)
		{
		case ERHIVertexFormat::Float2:
			return DXGI_FORMAT_R32G32_FLOAT;
		case ERHIVertexFormat::Float3:
			return DXGI_FORMAT_R32G32B32_FLOAT;
		case ERHIVertexFormat::Float4:
		case ERHIVertexFormat::Color4:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		default:
			return DXGI_FORMAT_R32G32B32_FLOAT;
		}
	}

	D3D11_PRIMITIVE_TOPOLOGY ToD3DTopology(ERHIPrimitiveTopology topology)
	{
		switch (topology)
		{
		case ERHIPrimitiveTopology::TriangleStrip:
			return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		case ERHIPrimitiveTopology::LineList:
			return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
		case ERHIPrimitiveTopology::LineStrip:
			return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
		default:
			return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		}
	}

	const char* GetShaderProfile(ERHIProgramStage stage)
	{
		return ERHIProgramStage::Pixel == stage ? "ps_5_0" : "vs_5_0";
	}
}
#endif

bool CD3D11RHIDevice::Initialize(const RHIDesc& desc)
{
	m_desc = desc;

#if JBRO_PLATFORM_WINDOWS
	if (ERenderSurfaceType::Win32Hwnd != desc.Surface.NativeHandle.SurfaceType || nullptr == desc.Surface.NativeHandle.Handle)
	{
		return false;
	}

	// Flip-model 스왑체인 — DXGI 의 권장 모드. 요구사항:
	//   - BufferCount >= 2
	//   - SampleDesc.Count == 1 (MSAA 백버퍼 직접 불가, RT 따로 만들어 resolve)
	//   - Format: R8G8B8A8_UNORM / B8G8R8A8_UNORM / R8G8B8A8_UNORM_SRGB(별도 RTV) / R16G16B16A16_FLOAT
	DXGI_SWAP_CHAIN_DESC swapchainDesc = {};
	swapchainDesc.BufferCount = 2;
	swapchainDesc.BufferDesc.Width = static_cast<UINT>(desc.Surface.Size.Width);
	swapchainDesc.BufferDesc.Height = static_cast<UINT>(desc.Surface.Size.Height);
	swapchainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.OutputWindow = static_cast<HWND>(desc.Surface.NativeHandle.Handle);
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.Windowed = TRUE;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	UINT createDeviceFlags = 0;
	if (desc.EnableDebugLayer)
	{
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	}

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_0,
	};
	D3D_FEATURE_LEVEL selectedFeatureLevel = D3D_FEATURE_LEVEL_11_0;

	HRESULT result = D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		createDeviceFlags,
		featureLevels,
		1,
		D3D11_SDK_VERSION,
		&swapchainDesc,
		&m_swapchain,
		&m_device,
		&selectedFeatureLevel,
		&m_deviceContext
	);

	if (FAILED(result) && desc.EnableDebugLayer)
	{
		createDeviceFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
		result = D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			createDeviceFlags,
			featureLevels,
			1,
			D3D11_SDK_VERSION,
			&swapchainDesc,
			&m_swapchain,
			&m_device,
			&selectedFeatureLevel,
			&m_deviceContext
		);
	}

	if (FAILED(result))
	{
		Finalize();
		return false;
	}

	m_rhiSwapchain = MakeOwnerPtr<CD3D11Swapchain>();
	if (!m_rhiSwapchain || false == m_rhiSwapchain->Initialize(desc.Surface))
	{
		Finalize();
		return false;
	}
	static_cast<CD3D11Swapchain*>(m_rhiSwapchain.Get())->BindNativeSwapchain(m_swapchain, m_device);

	m_immediateCommandContext = MakeOwnerPtr<CD3D11CommandContext>();
	if (!m_immediateCommandContext)
	{
		Finalize();
		return false;
	}
	static_cast<CD3D11CommandContext*>(m_immediateCommandContext.Get())->BindNativeContext(
		m_deviceContext,
		static_cast<CD3D11Swapchain*>(m_rhiSwapchain.Get())->GetRenderTargetView(),
		desc.Surface.Size
	);
#endif

	m_isInitialized = true;
	return true;
}

void CD3D11RHIDevice::BeginFrame()
{
	if (m_immediateCommandContext)
	{
		m_immediateCommandContext->BeginFrame();
	}
}

void CD3D11RHIDevice::EndFrame()
{
	if (m_immediateCommandContext)
	{
		m_immediateCommandContext->EndFrame();
	}

	if (m_rhiSwapchain)
	{
		m_rhiSwapchain->Present();
	}
}

void CD3D11RHIDevice::Finalize()
{
	m_immediateCommandContext.Reset();

	if (m_rhiSwapchain)
	{
		static_cast<CD3D11Swapchain*>(m_rhiSwapchain.Get())->Finalize();
		m_rhiSwapchain.Reset();
	}

#if JBRO_PLATFORM_WINDOWS
	if (m_swapchain)
	{
		m_swapchain->Release();
		m_swapchain = nullptr;
	}
	if (m_deviceContext)
	{
		m_deviceContext->Release();
		m_deviceContext = nullptr;
	}
	if (m_device)
	{
		m_device->Release();
		m_device = nullptr;
	}
#endif
	m_isInitialized = false;
}

OwnerPtr<IRHIBuffer> CD3D11RHIDevice::CreateBuffer(const RHIBufferDesc& desc, const void* initialData)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_device || 0 == desc.SizeInBytes)
	{
		return nullptr;
	}

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = static_cast<UINT>(desc.SizeInBytes);
	bufferDesc.Usage = ToD3DUsage(desc.Usage);
	bufferDesc.BindFlags = ToD3DBindFlags(desc.BindFlags);
	bufferDesc.CPUAccessFlags = ERHIBufferUsage::Dynamic == desc.Usage ? D3D11_CPU_ACCESS_WRITE : 0;

	D3D11_SUBRESOURCE_DATA data = {};
	data.pSysMem = initialData;

	ID3D11Buffer* buffer = nullptr;
	HRESULT result = m_device->CreateBuffer(&bufferDesc, initialData ? &data : nullptr, &buffer);
	if (FAILED(result) || nullptr == buffer)
	{
		return nullptr;
	}

	OwnerPtr<CD3D11Buffer> rhiBuffer = MakeOwnerPtr<CD3D11Buffer>(desc);
	rhiBuffer->BindNativeBuffer(buffer);
	return rhiBuffer;
#else
	(void)desc;
	(void)initialData;
	return nullptr;
#endif
}

OwnerPtr<IRHITexture> CD3D11RHIDevice::CreateTexture2D(const RHITexture2DDesc& desc, const void* initialData)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_device || 0 == desc.Width || 0 == desc.Height)
	{
		return nullptr;
	}

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width = desc.Width;
	textureDesc.Height = desc.Height;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = ToD3DTextureBindFlags(desc.BindFlags);

	D3D11_SUBRESOURCE_DATA data = {};
	data.pSysMem = initialData;
	data.SysMemPitch = desc.Width * 4;

	ID3D11Texture2D* texture = nullptr;
	HRESULT result = m_device->CreateTexture2D(&textureDesc, initialData ? &data : nullptr, &texture);
	if (FAILED(result) || nullptr == texture)
	{
		return nullptr;
	}

	ID3D11ShaderResourceView* shaderResourceView = nullptr;
	if (0 != (textureDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE))
	{
		result = m_device->CreateShaderResourceView(texture, nullptr, &shaderResourceView);
		if (FAILED(result) || nullptr == shaderResourceView)
		{
			texture->Release();
			return nullptr;
		}
	}

	ID3D11RenderTargetView* renderTargetView = nullptr;
	if (0 != (textureDesc.BindFlags & D3D11_BIND_RENDER_TARGET))
	{
		result = m_device->CreateRenderTargetView(texture, nullptr, &renderTargetView);
		if (FAILED(result) || nullptr == renderTargetView)
		{
			if (shaderResourceView)
			{
				shaderResourceView->Release();
			}
			texture->Release();
			return nullptr;
		}
	}

	OwnerPtr<CD3D11Texture> rhiTexture = MakeOwnerPtr<CD3D11Texture>(desc);
	rhiTexture->BindNativeTexture(texture, shaderResourceView, renderTargetView);
	return rhiTexture;
#else
	(void)desc;
	(void)initialData;
	return nullptr;
#endif
}

OwnerPtr<IRHISampler> CD3D11RHIDevice::CreateSampler(const RHISamplerDesc& desc)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_device)
	{
		return nullptr;
	}

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = ERHIFilterMode::Point == desc.Filter ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = ERHIAddressMode::Repeat == desc.AddressU ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = ERHIAddressMode::Repeat == desc.AddressV ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	ID3D11SamplerState* sampler = nullptr;
	HRESULT result = m_device->CreateSamplerState(&samplerDesc, &sampler);
	if (FAILED(result) || nullptr == sampler)
	{
		return nullptr;
	}

	OwnerPtr<CD3D11Sampler> rhiSampler = MakeOwnerPtr<CD3D11Sampler>(desc);
	rhiSampler->BindNativeSampler(sampler);
	return rhiSampler;
#else
	(void)desc;
	return nullptr;
#endif
}

OwnerPtr<IRHIProgram> CD3D11RHIDevice::CreateProgram(const RHIProgramDesc& desc)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == desc.Source || nullptr == desc.EntryPoint || ERHIProgramLanguage::HLSL != desc.Language)
	{
		return nullptr;
	}

	ID3DBlob* bytecode = nullptr;
	ID3DBlob* errors = nullptr;
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
	flags |= D3DCOMPILE_DEBUG;
#endif
	HRESULT result = D3DCompile(desc.Source, std::strlen(desc.Source), nullptr, nullptr, nullptr, desc.EntryPoint, GetShaderProfile(desc.Stage), flags, 0, &bytecode, &errors);
	if (errors)
	{
		errors->Release();
	}
	if (FAILED(result) || nullptr == bytecode)
	{
		return nullptr;
	}

	OwnerPtr<CD3D11Program> program = MakeOwnerPtr<CD3D11Program>(desc.Stage, desc.Language);
	program->BindNativeBlob(bytecode);
	return program;
#else
	(void)desc;
	return nullptr;
#endif
}

OwnerPtr<IRHIGraphicsPipeline> CD3D11RHIDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc)
{
#if JBRO_PLATFORM_WINDOWS
	if (nullptr == m_device || false == desc.VertexProgram.IsValid() || false == desc.PixelProgram.IsValid())
	{
		return nullptr;
	}

	CD3D11Program* vertexProgram = static_cast<CD3D11Program*>(desc.VertexProgram.TryGet());
	CD3D11Program* pixelProgram = static_cast<CD3D11Program*>(desc.PixelProgram.TryGet());
	if (nullptr == vertexProgram || nullptr == pixelProgram || nullptr == vertexProgram->GetBytecode() || nullptr == pixelProgram->GetBytecode())
	{
		return nullptr;
	}

	ID3D11VertexShader* vertexShader = nullptr;
	HRESULT result = m_device->CreateVertexShader(vertexProgram->GetBytecode()->GetBufferPointer(), vertexProgram->GetBytecode()->GetBufferSize(), nullptr, &vertexShader);
	if (FAILED(result) || nullptr == vertexShader)
	{
		return nullptr;
	}

	ID3D11PixelShader* pixelShader = nullptr;
	result = m_device->CreatePixelShader(pixelProgram->GetBytecode()->GetBufferPointer(), pixelProgram->GetBytecode()->GetBufferSize(), nullptr, &pixelShader);
	if (FAILED(result) || nullptr == pixelShader)
	{
		vertexShader->Release();
		return nullptr;
	}

	std::vector<D3D11_INPUT_ELEMENT_DESC> inputElements;
	for (std::uint32_t i = 0; i < desc.VertexElementCount; ++i)
	{
		const RHIVertexElementDesc& element = desc.VertexElements[i];
		D3D11_INPUT_ELEMENT_DESC d3dElement = {};
		d3dElement.SemanticName = element.SemanticName;
		d3dElement.SemanticIndex = element.SemanticIndex;
		d3dElement.Format = ToD3DVertexFormat(element.Format);
		d3dElement.InputSlot = element.InputSlot;
		d3dElement.AlignedByteOffset = element.Offset;
		d3dElement.InputSlotClass = ERHIVertexInputRate::PerInstance == element.InputRate
			? D3D11_INPUT_PER_INSTANCE_DATA
			: D3D11_INPUT_PER_VERTEX_DATA;
		d3dElement.InstanceDataStepRate = ERHIVertexInputRate::PerInstance == element.InputRate ? 1 : 0;
		inputElements.push_back(d3dElement);
	}

	// VertexElementCount == 0: SV_VertexID 전용 셰이더 (풀스크린 삼각형 등).
	// CreateInputLayout에 NumElements=0을 전달하면 E_INVALIDARG가 반환되므로,
	// inputLayout = nullptr로 두고 IASetInputLayout(nullptr)을 호출하면 된다.
	ID3D11InputLayout* inputLayout = nullptr;
	if (!inputElements.empty())
	{
		result = m_device->CreateInputLayout(
			inputElements.data(), static_cast<UINT>(inputElements.size()),
			vertexProgram->GetBytecode()->GetBufferPointer(),
			vertexProgram->GetBytecode()->GetBufferSize(),
			&inputLayout);
		if (FAILED(result) || nullptr == inputLayout)
		{
			vertexShader->Release();
			pixelShader->Release();
			return nullptr;
		}
	}

	if (m_deviceContext)
	{
		m_deviceContext->IASetPrimitiveTopology(ToD3DTopology(desc.PrimitiveTopology));
	}

	// ── BlendState 생성 ─────────────────────────────────────────────────────
	ID3D11BlendState* blendState = nullptr;
	if (desc.BlendMode != ERHIBlendMode::Opaque)
	{
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.AlphaToCoverageEnable = FALSE;
		blendDesc.IndependentBlendEnable = FALSE;
		D3D11_RENDER_TARGET_BLEND_DESC& rt = blendDesc.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (desc.BlendMode == ERHIBlendMode::AlphaBlend)
		{
			rt.SrcBlend       = D3D11_BLEND_SRC_ALPHA;
			rt.DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
			rt.BlendOp        = D3D11_BLEND_OP_ADD;
			rt.SrcBlendAlpha  = D3D11_BLEND_ONE;
			rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			rt.BlendOpAlpha   = D3D11_BLEND_OP_ADD;
		}
		else // Additive
		{
			rt.SrcBlend       = D3D11_BLEND_SRC_ALPHA;
			rt.DestBlend      = D3D11_BLEND_ONE;
			rt.BlendOp        = D3D11_BLEND_OP_ADD;
			rt.SrcBlendAlpha  = D3D11_BLEND_ZERO;
			rt.DestBlendAlpha = D3D11_BLEND_ONE;
			rt.BlendOpAlpha   = D3D11_BLEND_OP_ADD;
		}
		m_device->CreateBlendState(&blendDesc, &blendState);
	}

	OwnerPtr<CD3D11GraphicsPipeline> pipeline = MakeOwnerPtr<CD3D11GraphicsPipeline>(desc);
	pipeline->BindNativePipeline(inputLayout, vertexShader, pixelShader);
	if (blendState)
		pipeline->SetBlendState(blendState); // pipeline이 소유권 획득
	return pipeline;
#else
	(void)desc;
	return nullptr;
#endif
}

void CD3D11RHIDevice::HandleSurfaceResize(const RenderSurfaceSize& size)
{
#if JBRO_PLATFORM_WINDOWS
	if (!m_rhiSwapchain || size.Width <= 0 || size.Height <= 0)
	{
		return;
	}

	// Resize the DXGI swapchain buffers and recreate the back-buffer RTV.
	m_rhiSwapchain->Resize(size);

	// Propagate the new RTV and size to the immediate command context so that
	// BeginRenderPass (with no explicit target) binds the correct back buffer.
	CD3D11Swapchain*     d3dSwapchain = static_cast<CD3D11Swapchain*>(m_rhiSwapchain.Get());
	CD3D11CommandContext* d3dContext  = static_cast<CD3D11CommandContext*>(m_immediateCommandContext.Get());
	if (d3dSwapchain && d3dContext)
	{
		d3dContext->UpdateNativeRenderTarget(d3dSwapchain->GetRenderTargetView(), size);
	}
#else
	(void)size;
#endif
}

SafePtr<IRHISwapchain> CD3D11RHIDevice::GetSwapchain() const
{
	return m_rhiSwapchain.GetSafePtr();
}

SafePtr<IRHICommandContext> CD3D11RHIDevice::GetImmediateCommandContext() const
{
	return m_immediateCommandContext.GetSafePtr();
}

RHINativeDeviceDesc CD3D11RHIDevice::GetNativeDeviceDesc() const
{
	RHINativeDeviceDesc nativeDesc;
#if JBRO_PLATFORM_WINDOWS
	nativeDesc.Device = m_device;
	nativeDesc.DeviceContext = m_deviceContext;
#endif
	return nativeDesc;
}

ERHIApi CD3D11RHIDevice::GetApi() const
{
	return ERHIApi::D3D11;
}

const char* CD3D11RHIDevice::GetName() const
{
	return "D3D11";
}
