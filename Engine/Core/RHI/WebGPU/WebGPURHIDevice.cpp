#include "pch.h"
#include "WebGPURHIDevice.h"

#include "Core/RHI/WebGPU/WebGPUBuffer.h"
#include "Core/RHI/WebGPU/WebGPUCommandContext.h"
#include "Core/RHI/WebGPU/WebGPUGraphicsPipeline.h"
#include "Core/RHI/WebGPU/WebGPUProgram.h"
#include "Core/RHI/WebGPU/WebGPUSampler.h"
#include "Core/RHI/WebGPU/WebGPUSwapchain.h"
#include "Core/RHI/WebGPU/WebGPUTexture.h"

#include <cstring>

#if JBRO_PLATFORM_WEB
namespace
{
	constexpr const char* DEFAULT_CANVAS_SELECTOR = "#jbro-canvas";

	WGPUStringView MakeStringView(const char* text)
	{
		WGPUStringView view = {};
		view.data = text;
		view.length = text ? std::strlen(text) : 0;
		return view;
	}

	WGPUBufferUsage ToWebGPUBufferUsage(RHIBindFlags flags)
	{
		WGPUBufferUsage usage = WGPUBufferUsage_CopyDst;
		if (0 != (flags & static_cast<RHIBindFlags>(ERHIBindFlag::VertexBuffer)))
		{
			usage |= WGPUBufferUsage_Vertex;
		}
		if (0 != (flags & static_cast<RHIBindFlags>(ERHIBindFlag::IndexBuffer)))
		{
			usage |= WGPUBufferUsage_Index;
		}
		if (0 != (flags & static_cast<RHIBindFlags>(ERHIBindFlag::ConstantBuffer)))
		{
			usage |= WGPUBufferUsage_Uniform;
		}
		return usage;
	}

	WGPUTextureUsage ToWebGPUTextureUsage(RHITextureBindFlags flags)
	{
		WGPUTextureUsage usage = WGPUTextureUsage_CopyDst;
		if (0 != (flags & static_cast<RHITextureBindFlags>(ERHITextureBindFlag::ShaderResource)))
		{
			usage |= WGPUTextureUsage_TextureBinding;
		}
		if (0 != (flags & static_cast<RHITextureBindFlags>(ERHITextureBindFlag::RenderTarget)))
		{
			usage |= WGPUTextureUsage_RenderAttachment;
		}
		if (0 != (flags & static_cast<RHITextureBindFlags>(ERHITextureBindFlag::CopySource)))
		{
			usage |= WGPUTextureUsage_CopySrc;
		}
		if (0 != (flags & static_cast<RHITextureBindFlags>(ERHITextureBindFlag::CopyDestination)))
		{
			usage |= WGPUTextureUsage_CopyDst;
		}
		return usage;
	}

	WGPUVertexFormat ToWebGPUVertexFormat(ERHIVertexFormat format)
	{
		switch (format)
		{
		case ERHIVertexFormat::Float2:
			return WGPUVertexFormat_Float32x2;
		case ERHIVertexFormat::Float3:
			return WGPUVertexFormat_Float32x3;
		case ERHIVertexFormat::Float4:
		case ERHIVertexFormat::Color4:
			return WGPUVertexFormat_Float32x4;
		default:
			return WGPUVertexFormat_Float32x3;
		}
	}

	WGPUPrimitiveTopology ToWebGPUTopology(ERHIPrimitiveTopology topology)
	{
		switch (topology)
		{
		case ERHIPrimitiveTopology::TriangleStrip:
			return WGPUPrimitiveTopology_TriangleStrip;
		case ERHIPrimitiveTopology::LineList:
			return WGPUPrimitiveTopology_LineList;
		case ERHIPrimitiveTopology::LineStrip:
			return WGPUPrimitiveTopology_LineStrip;
		default:
			return WGPUPrimitiveTopology_TriangleList;
		}
	}

	WGPUFilterMode ToWebGPUFilter(ERHIFilterMode filter)
	{
		return ERHIFilterMode::Point == filter ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
	}

	WGPUAddressMode ToWebGPUAddressMode(ERHIAddressMode mode)
	{
		return ERHIAddressMode::Repeat == mode ? WGPUAddressMode_Repeat : WGPUAddressMode_ClampToEdge;
	}
}
#endif

bool CWebGPURHIDevice::Initialize(const RHIDesc& desc)
{
	m_desc = desc;
#if JBRO_PLATFORM_WEB
	m_instance = wgpuCreateInstance(nullptr);
	m_device = emscripten_webgpu_get_device();
	if (nullptr == m_instance || nullptr == m_device)
	{
		Finalize();
		return false;
	}
	m_queue = wgpuDeviceGetQueue(m_device);

	const char* canvasSelector = DEFAULT_CANVAS_SELECTOR;
	if (ERenderSurfaceType::HtmlCanvas == desc.Surface.NativeHandle.SurfaceType && nullptr != desc.Surface.NativeHandle.Handle)
	{
		canvasSelector = static_cast<const char*>(desc.Surface.NativeHandle.Handle);
	}

	WGPUSurfaceSourceCanvasHTMLSelector_Emscripten canvasSource = {};
	canvasSource.chain.sType = WGPUSType_SurfaceSourceCanvasHTMLSelector_Emscripten;
	canvasSource.selector = MakeStringView(canvasSelector);

	WGPUSurfaceDescriptor surfaceDesc = {};
	surfaceDesc.nextInChain = &canvasSource.chain;
	m_surface = wgpuInstanceCreateSurface(m_instance, &surfaceDesc);
	if (nullptr == m_surface)
	{
		Finalize();
		return false;
	}

	m_rhiSwapchain = MakeOwnerPtr<CWebGPUSwapchain>();
	if (!m_rhiSwapchain || false == m_rhiSwapchain->Initialize(desc.Surface))
	{
		Finalize();
		return false;
	}
	if (false == static_cast<CWebGPUSwapchain*>(m_rhiSwapchain.Get())->BindNativeSurface(m_device, m_surface, WGPUTextureFormat_BGRA8Unorm))
	{
		Finalize();
		return false;
	}
	m_surface = nullptr;

	m_immediateCommandContext = MakeOwnerPtr<CWebGPUCommandContext>();
	if (!m_immediateCommandContext)
	{
		Finalize();
		return false;
	}
	static_cast<CWebGPUCommandContext*>(m_immediateCommandContext.Get())->BindNativeContext(
		m_device,
		m_queue,
		static_cast<CWebGPUSwapchain*>(m_rhiSwapchain.Get())
	);
#endif
	m_isInitialized = true;
	return true;
}

void CWebGPURHIDevice::BeginFrame()
{
	if (m_immediateCommandContext)
	{
		m_immediateCommandContext->BeginFrame();
	}
}

void CWebGPURHIDevice::EndFrame()
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

void CWebGPURHIDevice::Finalize()
{
	m_immediateCommandContext.Reset();
	if (m_rhiSwapchain)
	{
		static_cast<CWebGPUSwapchain*>(m_rhiSwapchain.Get())->Finalize();
		m_rhiSwapchain.Reset();
	}
#if JBRO_PLATFORM_WEB
	if (m_surface)
	{
		wgpuSurfaceRelease(m_surface);
		m_surface = nullptr;
	}
	if (m_queue)
	{
		wgpuQueueRelease(m_queue);
		m_queue = nullptr;
	}
	if (m_device)
	{
		wgpuDeviceRelease(m_device);
		m_device = nullptr;
	}
	if (m_instance)
	{
		wgpuInstanceRelease(m_instance);
		m_instance = nullptr;
	}
#endif
	m_isInitialized = false;
}

OwnerPtr<IRHIBuffer> CWebGPURHIDevice::CreateBuffer(const RHIBufferDesc& desc, const void* initialData)
{
#if JBRO_PLATFORM_WEB
	if (nullptr == m_device || 0 == desc.SizeInBytes)
	{
		return nullptr;
	}

	WGPUBufferDescriptor bufferDesc = {};
	bufferDesc.size = desc.SizeInBytes;
	bufferDesc.usage = ToWebGPUBufferUsage(desc.BindFlags);
	bufferDesc.mappedAtCreation = false;
	WGPUBuffer buffer = wgpuDeviceCreateBuffer(m_device, &bufferDesc);
	if (nullptr == buffer)
	{
		return nullptr;
	}

	if (initialData && m_queue)
	{
		wgpuQueueWriteBuffer(m_queue, buffer, 0, initialData, desc.SizeInBytes);
	}

	OwnerPtr<CWebGPUBuffer> rhiBuffer = MakeOwnerPtr<CWebGPUBuffer>(desc);
	rhiBuffer->BindNativeBuffer(buffer);
	return rhiBuffer;
#else
	(void)desc;
	(void)initialData;
	return nullptr;
#endif
}

OwnerPtr<IRHITexture> CWebGPURHIDevice::CreateTexture2D(const RHITexture2DDesc& desc, const void* initialData)
{
#if JBRO_PLATFORM_WEB
	if (nullptr == m_device || nullptr == m_queue || 0 == desc.Width || 0 == desc.Height)
	{
		return nullptr;
	}

	WGPUTextureDescriptor textureDesc = {};
	textureDesc.dimension = WGPUTextureDimension_2D;
	textureDesc.size.width = desc.Width;
	textureDesc.size.height = desc.Height;
	textureDesc.size.depthOrArrayLayers = 1;
	textureDesc.mipLevelCount = 1;
	textureDesc.sampleCount = 1;
	textureDesc.format = WGPUTextureFormat_BGRA8Unorm;
	textureDesc.usage = ToWebGPUTextureUsage(desc.BindFlags);
	WGPUTexture texture = wgpuDeviceCreateTexture(m_device, &textureDesc);
	if (nullptr == texture)
	{
		return nullptr;
	}

	if (initialData)
	{
		WGPUImageCopyTexture destination = {};
		destination.texture = texture;
		destination.mipLevel = 0;
		destination.origin = WGPUOrigin3D{ 0, 0, 0 };
		destination.aspect = WGPUTextureAspect_All;

		WGPUTextureDataLayout layout = {};
		layout.offset = 0;
		layout.bytesPerRow = desc.Width * 4;
		layout.rowsPerImage = desc.Height;

		WGPUExtent3D writeSize = {};
		writeSize.width = desc.Width;
		writeSize.height = desc.Height;
		writeSize.depthOrArrayLayers = 1;
		wgpuQueueWriteTexture(m_queue, &destination, initialData, desc.Width * desc.Height * 4, &layout, &writeSize);
	}

	WGPUTextureViewDescriptor viewDesc = {};
	WGPUTextureView textureView = wgpuTextureCreateView(texture, &viewDesc);
	if (nullptr == textureView)
	{
		wgpuTextureRelease(texture);
		return nullptr;
	}

	OwnerPtr<CWebGPUTexture> rhiTexture = MakeOwnerPtr<CWebGPUTexture>(desc);
	rhiTexture->BindNativeTexture(texture, textureView);
	return rhiTexture;
#else
	(void)desc;
	(void)initialData;
	return nullptr;
#endif
}

OwnerPtr<IRHISampler> CWebGPURHIDevice::CreateSampler(const RHISamplerDesc& desc)
{
#if JBRO_PLATFORM_WEB
	if (nullptr == m_device)
	{
		return nullptr;
	}

	WGPUSamplerDescriptor samplerDesc = {};
	samplerDesc.addressModeU = ToWebGPUAddressMode(desc.AddressU);
	samplerDesc.addressModeV = ToWebGPUAddressMode(desc.AddressV);
	samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
	samplerDesc.magFilter = ToWebGPUFilter(desc.Filter);
	samplerDesc.minFilter = ToWebGPUFilter(desc.Filter);
	samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
	WGPUSampler sampler = wgpuDeviceCreateSampler(m_device, &samplerDesc);
	if (nullptr == sampler)
	{
		return nullptr;
	}

	OwnerPtr<CWebGPUSampler> rhiSampler = MakeOwnerPtr<CWebGPUSampler>(desc);
	rhiSampler->BindNativeSampler(sampler);
	return rhiSampler;
#else
	(void)desc;
	return nullptr;
#endif
}

OwnerPtr<IRHIProgram> CWebGPURHIDevice::CreateProgram(const RHIProgramDesc& desc)
{
#if JBRO_PLATFORM_WEB
	if (nullptr == m_device || nullptr == desc.Source || ERHIProgramLanguage::WGSL != desc.Language)
	{
		return nullptr;
	}

	WGPUShaderSourceWGSL wgsl = {};
	wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
	wgsl.code = MakeStringView(desc.Source);

	WGPUShaderModuleDescriptor shaderDesc = {};
	shaderDesc.nextInChain = &wgsl.chain;
	WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(m_device, &shaderDesc);
	if (nullptr == shaderModule)
	{
		return nullptr;
	}

	OwnerPtr<CWebGPUProgram> program = MakeOwnerPtr<CWebGPUProgram>(desc.Stage, desc.Language);
	program->BindNativeShaderModule(shaderModule);
	return program;
#else
	(void)desc;
	return nullptr;
#endif
}

OwnerPtr<IRHIGraphicsPipeline> CWebGPURHIDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc)
{
#if JBRO_PLATFORM_WEB
	if (nullptr == m_device || false == desc.VertexProgram.IsValid() || false == desc.PixelProgram.IsValid())
	{
		return nullptr;
	}

	CWebGPUProgram* vertexProgram = static_cast<CWebGPUProgram*>(desc.VertexProgram.TryGet());
	CWebGPUProgram* pixelProgram = static_cast<CWebGPUProgram*>(desc.PixelProgram.TryGet());
	if (nullptr == vertexProgram || nullptr == pixelProgram || nullptr == vertexProgram->GetShaderModule() || nullptr == pixelProgram->GetShaderModule())
	{
		return nullptr;
	}

	WGPUBindGroupLayoutEntry bindEntries[3] = {};
	bindEntries[0].binding = 0;
	bindEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	bindEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
	bindEntries[0].buffer.minBindingSize = sizeof(float) * 16;
	bindEntries[1].binding = 1;
	bindEntries[1].visibility = WGPUShaderStage_Fragment;
	bindEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
	bindEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
	bindEntries[1].texture.multisampled = false;
	bindEntries[2].binding = 2;
	bindEntries[2].visibility = WGPUShaderStage_Fragment;
	bindEntries[2].sampler.type = WGPUSamplerBindingType_Filtering;

	WGPUBindGroupLayoutDescriptor bindLayoutDesc = {};
	bindLayoutDesc.entryCount = 3;
	bindLayoutDesc.entries = bindEntries;
	WGPUBindGroupLayout bindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_device, &bindLayoutDesc);
	if (nullptr == bindGroupLayout)
	{
		return nullptr;
	}

	WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
	pipelineLayoutDesc.bindGroupLayoutCount = 1;
	pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;
	WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);
	if (nullptr == pipelineLayout)
	{
		wgpuBindGroupLayoutRelease(bindGroupLayout);
		return nullptr;
	}

	std::vector<WGPUVertexAttribute> attributes;
	std::vector<WGPUVertexBufferLayout> buffers;
	for (std::uint32_t i = 0; i < desc.VertexElementCount; ++i)
	{
		const RHIVertexElementDesc& element = desc.VertexElements[i];
		WGPUVertexAttribute attribute = {};
		attribute.format = ToWebGPUVertexFormat(element.Format);
		attribute.offset = element.Offset;
		attribute.shaderLocation = i;
		attributes.push_back(attribute);
	}

	WGPUVertexBufferLayout vertexBufferLayout = {};
	vertexBufferLayout.arrayStride = sizeof(float) * 4;
	vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
	vertexBufferLayout.attributeCount = static_cast<std::uint32_t>(attributes.size());
	vertexBufferLayout.attributes = attributes.data();
	buffers.push_back(vertexBufferLayout);

	WGPUColorTargetState colorTarget = {};
	colorTarget.format = WGPUTextureFormat_BGRA8Unorm;
	colorTarget.writeMask = WGPUColorWriteMask_All;

	WGPUFragmentState fragmentState = {};
	fragmentState.module = pixelProgram->GetShaderModule();
	fragmentState.entryPoint = MakeStringView("PSMain");
	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTarget;

	WGPURenderPipelineDescriptor pipelineDesc = {};
	pipelineDesc.layout = pipelineLayout;
	pipelineDesc.vertex.module = vertexProgram->GetShaderModule();
	pipelineDesc.vertex.entryPoint = MakeStringView("VSMain");
	pipelineDesc.vertex.bufferCount = static_cast<std::uint32_t>(buffers.size());
	pipelineDesc.vertex.buffers = buffers.data();
	pipelineDesc.primitive.topology = ToWebGPUTopology(desc.PrimitiveTopology);
	pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
	pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
	pipelineDesc.primitive.cullMode = WGPUCullMode_None;
	pipelineDesc.fragment = &fragmentState;
	pipelineDesc.multisample.count = 1;
	pipelineDesc.multisample.mask = 0xffffffffu;
	pipelineDesc.multisample.alphaToCoverageEnabled = false;
	WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(m_device, &pipelineDesc);
	wgpuPipelineLayoutRelease(pipelineLayout);
	if (nullptr == pipeline)
	{
		wgpuBindGroupLayoutRelease(bindGroupLayout);
		return nullptr;
	}

	OwnerPtr<CWebGPUGraphicsPipeline> rhiPipeline = MakeOwnerPtr<CWebGPUGraphicsPipeline>(desc);
	rhiPipeline->BindNativePipeline(pipeline, bindGroupLayout);
	return rhiPipeline;
#else
	(void)desc;
	return nullptr;
#endif
}

SafePtr<IRHISwapchain> CWebGPURHIDevice::GetSwapchain() const
{
	return m_rhiSwapchain.GetSafePtr();
}

SafePtr<IRHICommandContext> CWebGPURHIDevice::GetImmediateCommandContext() const
{
	return m_immediateCommandContext.GetSafePtr();
}

RHINativeDeviceDesc CWebGPURHIDevice::GetNativeDeviceDesc() const
{
	RHINativeDeviceDesc nativeDesc;
#if JBRO_PLATFORM_WEB
	nativeDesc.Device = m_device;
	nativeDesc.DeviceContext = m_queue;
#endif
	return nativeDesc;
}

ERHIApi CWebGPURHIDevice::GetApi() const
{
	return ERHIApi::WebGPU;
}

const char* CWebGPURHIDevice::GetName() const
{
	return "WebGPU";
}
