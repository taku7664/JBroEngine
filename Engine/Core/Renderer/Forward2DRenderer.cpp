#include "pch.h"
#include "Forward2DRenderer.h"

#include "Core/Renderer/IRenderMaterial.h"
#include "Core/Renderer/IRenderMesh.h"
#include "Core/Renderer/IRenderScene.h"
#include "Core/Renderer/RenderResources2D.h"
#include "Core/Renderer/RenderScene.h"
#include "Core/Renderer/SpriteVulkanShaders.h"
#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/IRHIDevice.h"

#include <cfloat>
#include <utility>

namespace
{
	struct SpriteVertex
	{
		float Position[2];
		float Uv[2];
	};

	const char* SPRITE_SHADER_SOURCE_HLSL = R"(
cbuffer SpriteConstants : register(b0)
{
	float4 gTransformRow0;
	float4 gTransformRow1;
	float4 gColor;
	float4 gViewRow0;
	float4 gViewRow1;
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSIn
{
	float2 Position : POSITION;
	float2 Uv : TEXCOORD0;
};

struct VSOut
{
	float4 Position : SV_POSITION;
	float2 Uv : TEXCOORD0;
	float4 Color : COLOR0;
};

VSOut VSMain(VSIn input)
{
	VSOut output;
	float3 localPosition = float3(input.Position.xy, 1.0f);
	float2 worldPosition = float2(dot(localPosition, gTransformRow0.xyz), dot(localPosition, gTransformRow1.xyz));
	float3 worldPos3 = float3(worldPosition.x, worldPosition.y, 1.0f);
	output.Position = float4(dot(worldPos3, gViewRow0.xyz), dot(worldPos3, gViewRow1.xyz), 0.0f, 1.0f);
	output.Uv = input.Uv;
	output.Color = gColor;
	return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
	return gTexture.Sample(gSampler, input.Uv) * input.Color;
}
)";

	const char* SPRITE_SHADER_SOURCE_WGSL = R"(
struct SpriteConstants
{
	TransformRow0 : vec4<f32>,
	TransformRow1 : vec4<f32>,
	Color : vec4<f32>,
	ViewRow0 : vec4<f32>,
	ViewRow1 : vec4<f32>,
};

@group(0) @binding(0)
var<uniform> gConstants : SpriteConstants;

@group(0) @binding(1)
var gTexture : texture_2d<f32>;

@group(0) @binding(2)
var gSampler : sampler;

struct VSIn
{
	@location(0) Position : vec2<f32>,
	@location(1) Uv : vec2<f32>,
};

struct VSOut
{
	@builtin(position) Position : vec4<f32>,
	@location(0) Uv : vec2<f32>,
	@location(1) Color : vec4<f32>,
};

@vertex
fn VSMain(input : VSIn) -> VSOut
{
	var output : VSOut;
	let localPosition = vec3<f32>(input.Position.xy, 1.0);
	let worldPosition = vec2<f32>(dot(localPosition, gConstants.TransformRow0.xyz), dot(localPosition, gConstants.TransformRow1.xyz));
	let worldPos3 = vec3<f32>(worldPosition.x, worldPosition.y, 1.0);
	output.Position = vec4<f32>(dot(worldPos3, gConstants.ViewRow0.xyz), dot(worldPos3, gConstants.ViewRow1.xyz), 0.0, 1.0);
	output.Uv = input.Uv;
	output.Color = gConstants.Color;
	return output;
}

@fragment
fn PSMain(input : VSOut) -> @location(0) vec4<f32>
{
	return textureSample(gTexture, gSampler, input.Uv) * input.Color;
}
)";

	const char* SPRITE_BATCH_SHADER_SOURCE_HLSL = R"(
cbuffer SpriteViewConstants : register(b0)
{
	float4 gViewRow0;
	float4 gViewRow1;
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSIn
{
	float2 Position : POSITION;
	float2 Uv : TEXCOORD0;
	float4 TransformRow0 : TEXCOORD1;
	float4 TransformRow1 : TEXCOORD2;
	float4 Color : COLOR0;
};

struct VSOut
{
	float4 Position : SV_POSITION;
	float2 Uv : TEXCOORD0;
	float4 Color : COLOR0;
};

VSOut VSMain(VSIn input)
{
	VSOut output;
	float3 localPosition = float3(input.Position.xy, 1.0f);
	float2 worldPosition = float2(dot(localPosition, input.TransformRow0.xyz), dot(localPosition, input.TransformRow1.xyz));
	float3 worldPos3 = float3(worldPosition.x, worldPosition.y, 1.0f);
	output.Position = float4(dot(worldPos3, gViewRow0.xyz), dot(worldPos3, gViewRow1.xyz), 0.0f, 1.0f);
	output.Uv = input.Uv;
	output.Color = input.Color;
	return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
	return gTexture.Sample(gSampler, input.Uv) * input.Color;
}
)";

	const char* SPRITE_BATCH_SHADER_SOURCE_WGSL = R"(
struct SpriteViewConstants
{
	ViewRow0 : vec4<f32>,
	ViewRow1 : vec4<f32>,
};

@group(0) @binding(0)
var<uniform> gConstants : SpriteViewConstants;

@group(0) @binding(1)
var gTexture : texture_2d<f32>;

@group(0) @binding(2)
var gSampler : sampler;

struct VSIn
{
	@location(0) Position : vec2<f32>,
	@location(1) Uv : vec2<f32>,
	@location(2) TransformRow0 : vec4<f32>,
	@location(3) TransformRow1 : vec4<f32>,
	@location(4) Color : vec4<f32>,
};

struct VSOut
{
	@builtin(position) Position : vec4<f32>,
	@location(0) Uv : vec2<f32>,
	@location(1) Color : vec4<f32>,
};

@vertex
fn VSMain(input : VSIn) -> VSOut
{
	var output : VSOut;
	let localPosition = vec3<f32>(input.Position.xy, 1.0);
	let worldPosition = vec2<f32>(dot(localPosition, input.TransformRow0.xyz), dot(localPosition, input.TransformRow1.xyz));
	let worldPos3 = vec3<f32>(worldPosition.x, worldPosition.y, 1.0);
	output.Position = vec4<f32>(dot(worldPos3, gConstants.ViewRow0.xyz), dot(worldPos3, gConstants.ViewRow1.xyz), 0.0, 1.0);
	output.Uv = input.Uv;
	output.Color = input.Color;
	return output;
}

@fragment
fn PSMain(input : VSOut) -> @location(0) vec4<f32>
{
	return textureSample(gTexture, gSampler, input.Uv) * input.Color;
}
)";
}

bool CForward2DRenderer::Initialize(const RendererDesc& desc)
{
	if (m_isInitialized)
	{
		return true;
	}

	m_rhiDevice = desc.RHIDevice;
	if (false == m_rhiDevice.IsValid())
	{
		return false;
	}

	if (false == CreateSpritePipeline() || false == CreateQuadMesh())
	{
		if (ERHIApi::D3D11 == m_rhiDevice->GetApi())
		{
			Finalize();
			return false;
		}

		m_isInitialized = true;
		return true;
	}

	CreateSpriteBatchPipeline();

	RHISamplerDesc samplerDesc;
	samplerDesc.Filter = ERHIFilterMode::Linear;
	samplerDesc.AddressU = ERHIAddressMode::Clamp;
	samplerDesc.AddressV = ERHIAddressMode::Clamp;
	m_defaultSampler = m_rhiDevice->CreateSampler(samplerDesc);

	// 1×1 white texture used by FillViewportColor.
	{
		const std::uint8_t whitePixel[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
		RHITexture2DDesc whiteDesc;
		whiteDesc.Width     = 1;
		whiteDesc.Height    = 1;
		whiteDesc.Format    = ERHITextureFormat::RGBA8;
		whiteDesc.BindFlags = static_cast<RHITextureBindFlags>(ERHITextureBindFlag::ShaderResource);
		m_whiteTexture = m_rhiDevice->CreateTexture2D(whiteDesc, whitePixel);
	}

	m_isInitialized = true;
	return true;
}

void CForward2DRenderer::BeginFrame()
{
	m_spriteConstantBufferCursor = 0;
	m_spriteViewConstantBufferCursor = 0;
	m_spriteInstanceBufferCursor = 0;
}

void CForward2DRenderer::SetRenderTargetSize(const RenderSurfaceSize& size)
{
	m_renderTargetSize = size;
}

void CForward2DRenderer::SetViewCamera(float posX, float posY, float orthographicSize)
{
	m_viewCamX      = posX;
	m_viewCamY      = posY;
	m_viewCamSize   = orthographicSize > 0.0f ? orthographicSize : 1.0f;
	m_useExplicitSize = false;
}

void CForward2DRenderer::SetViewCameraEx(float posX, float posY, float halfW, float halfH, float cosR, float sinR)
{
	m_viewCamX      = posX;
	m_viewCamY      = posY;
	m_viewCamHalfW  = halfW > 0.0f ? halfW : 1.0f;
	m_viewCamHalfH  = halfH > 0.0f ? halfH : 1.0f;
	m_viewCamCosR   = cosR;
	m_viewCamSinR   = sinR;
}

void CForward2DRenderer::SetSurfacePreRotation(float cosR, float sinR)
{
	m_surfacePreRotCos = cosR;
	m_surfacePreRotSin = sinR;
	m_useExplicitSize = true;
}

void CForward2DRenderer::Render(IRenderScene& scene)
{
	RenderImpl(scene, nullptr);
}

RenderCullingStats CForward2DRenderer::GetLastCullingStats() const
{
	return m_lastCullingStats;
}

void CForward2DRenderer::RenderExcluding(IRenderScene& scene, const std::unordered_set<RenderObjectId>& excluded)
{
	RenderImpl(scene, &excluded);
}

CForward2DRenderer::ViewParameters CForward2DRenderer::BuildViewParameters() const
{
	ViewParameters view;
	if (m_useExplicitSize)
	{
		view.HalfW = m_viewCamHalfW;
		view.HalfH = m_viewCamHalfH;
		view.CosR = m_viewCamCosR;
		view.SinR = m_viewCamSinR;
		return view;
	}

	const float width = static_cast<float>(m_renderTargetSize.Width);
	const float height = static_cast<float>(m_renderTargetSize.Height);
	const float aspect = (width > 0.0f && height > 0.0f) ? width / height : 1.0f;
	const float size = m_viewCamSize > 0.0f ? m_viewCamSize : 1.0f;
	view.HalfW = size * aspect;
	view.HalfH = size;
	view.CosR = 1.0f;
	view.SinR = 0.0f;
	return view;
}

CForward2DRenderer::SpriteDrawResources CForward2DRenderer::ResolveSpriteDrawResources(const RenderItem& item) const
{
	SpriteDrawResources resources;
	resources.Mesh = item.Mesh;
	resources.Pipeline = item.Pipeline;
	resources.Texture = item.Texture;
	resources.Sampler = item.Sampler;

	if (item.Material.IsValid())
	{
		if (false == resources.Pipeline.IsValid())
		{
			resources.Pipeline = item.Material->GetPipeline();
		}
		if (false == resources.Texture.IsValid())
		{
			resources.Texture = item.Material->GetTexture();
		}
		if (false == resources.Sampler.IsValid())
		{
			resources.Sampler = item.Material->GetSampler();
		}
	}

	if (false == resources.Pipeline.IsValid())
	{
		resources.Pipeline = m_spritePipeline.GetSafePtr();
	}
	if (false == resources.Sampler.IsValid())
	{
		resources.Sampler = m_defaultSampler.GetSafePtr();
	}

	return resources;
}

CForward2DRenderer::SpriteConstants CForward2DRenderer::BuildSpriteConstants(
	const RenderItem& item,
	const ViewParameters& view) const
{
	SpriteConstants constants = {};
	constants.TransformRow0[0] = item.Transform.M11;
	constants.TransformRow0[1] = item.Transform.M21;
	constants.TransformRow0[2] = item.Transform.Dx;
	constants.TransformRow1[0] = item.Transform.M12;
	constants.TransformRow1[1] = item.Transform.M22;
	constants.TransformRow1[2] = item.Transform.Dy;
	for (int c = 0; c < 4; ++c)
	{
		constants.Color[c] = item.Color[c];
	}

	const float camX = m_viewCamX;
	const float camY = m_viewCamY;
	constants.ViewRow0[0] =  view.CosR / view.HalfW;
	constants.ViewRow0[1] =  view.SinR / view.HalfW;
	constants.ViewRow0[2] = -(view.CosR * camX + view.SinR * camY) / view.HalfW;
	constants.ViewRow1[0] = -view.SinR / view.HalfH;
	constants.ViewRow1[1] =  view.CosR / view.HalfH;
	constants.ViewRow1[2] =  (view.SinR * camX - view.CosR * camY) / view.HalfH;
	ApplySurfacePreRotation(constants.ViewRow0, constants.ViewRow1);
	return constants;
}

CForward2DRenderer::SpriteViewConstants CForward2DRenderer::BuildSpriteViewConstants(const ViewParameters& view) const
{
	SpriteViewConstants constants = {};
	const float camX = m_viewCamX;
	const float camY = m_viewCamY;
	constants.ViewRow0[0] =  view.CosR / view.HalfW;
	constants.ViewRow0[1] =  view.SinR / view.HalfW;
	constants.ViewRow0[2] = -(view.CosR * camX + view.SinR * camY) / view.HalfW;
	constants.ViewRow1[0] = -view.SinR / view.HalfH;
	constants.ViewRow1[1] =  view.CosR / view.HalfH;
	constants.ViewRow1[2] =  (view.SinR * camX - view.CosR * camY) / view.HalfH;
	ApplySurfacePreRotation(constants.ViewRow0, constants.ViewRow1);
	return constants;
}

void CForward2DRenderer::ApplySurfacePreRotation(float (&viewRow0)[4], float (&viewRow1)[4]) const
{
	// 최종 클립 좌표에 surface 회전 S=[c -s; s c] 를 곱한다(clip_final = S * (View * world)).
	const float c = m_surfacePreRotCos;
	const float s = m_surfacePreRotSin;
	for (int i = 0; i < 3; ++i)
	{
		const float r0 = viewRow0[i];
		const float r1 = viewRow1[i];
		viewRow0[i] = c * r0 - s * r1;
		viewRow1[i] = s * r0 + c * r1;
	}
}

CForward2DRenderer::SpriteInstanceData CForward2DRenderer::BuildSpriteInstanceData(const RenderItem& item) const
{
	SpriteInstanceData instance = {};
	instance.TransformRow0[0] = item.Transform.M11;
	instance.TransformRow0[1] = item.Transform.M21;
	instance.TransformRow0[2] = item.Transform.Dx;
	instance.TransformRow1[0] = item.Transform.M12;
	instance.TransformRow1[1] = item.Transform.M22;
	instance.TransformRow1[2] = item.Transform.Dy;
	for (int c = 0; c < 4; ++c)
	{
		instance.Color[c] = item.Color[c];
	}
	return instance;
}

CForward2DRenderer::SpriteConstants CForward2DRenderer::BuildViewportColorConstants(float r, float g, float b, float a) const
{
	SpriteConstants constants = {};
	constants.TransformRow0[0] = 2.0f;
	constants.TransformRow1[1] = 2.0f;
	constants.Color[0] = r;
	constants.Color[1] = g;
	constants.Color[2] = b;
	constants.Color[3] = a;
	constants.ViewRow0[0] = 1.0f;
	constants.ViewRow1[1] = 1.0f;
	return constants;
}

bool CForward2DRenderer::IsSpriteItemVisibleInView(const RenderItem& item, const ViewParameters& view) const
{
	if (view.HalfW <= 0.0f || view.HalfH <= 0.0f)
	{
		return true;
	}

	constexpr float kCorners[4][2] = {
		{ -0.5f, -0.5f },
		{  0.5f, -0.5f },
		{  0.5f,  0.5f },
		{ -0.5f,  0.5f },
	};

	float minX =  FLT_MAX;
	float minY =  FLT_MAX;
	float maxX = -FLT_MAX;
	float maxY = -FLT_MAX;

	for (const auto& corner : kCorners)
	{
		const float worldX = item.Transform.M11 * corner[0] + item.Transform.M21 * corner[1] + item.Transform.Dx;
		const float worldY = item.Transform.M12 * corner[0] + item.Transform.M22 * corner[1] + item.Transform.Dy;
		const float dx = worldX - m_viewCamX;
		const float dy = worldY - m_viewCamY;
		const float viewX =  view.CosR * dx + view.SinR * dy;
		const float viewY = -view.SinR * dx + view.CosR * dy;

		minX = std::min(minX, viewX);
		minY = std::min(minY, viewY);
		maxX = std::max(maxX, viewX);
		maxY = std::max(maxY, viewY);
	}

	return !(maxX < -view.HalfW || minX > view.HalfW || maxY < -view.HalfH || minY > view.HalfH);
}

SafePtr<IRHIBuffer> CForward2DRenderer::AcquireSpriteConstantBuffer(IRHICommandContext& commandContext, const SpriteConstants& constants)
{
	const std::size_t bufferIndex = m_spriteConstantBufferCursor++;
	if (bufferIndex >= m_spriteConstantBuffers.size())
	{
		RHIBufferDesc desc;
		desc.SizeInBytes = sizeof(SpriteConstants);
		desc.Usage = ERHIBufferUsage::Default;
		desc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::ConstantBuffer);

		OwnerPtr<IRHIBuffer> buffer = m_rhiDevice->CreateBuffer(desc, nullptr);
		if (!buffer)
		{
			return nullptr;
		}
		m_spriteConstantBuffers.push_back(std::move(buffer));
	}

	SafePtr<IRHIBuffer> buffer = m_spriteConstantBuffers[bufferIndex].GetSafePtr();
	commandContext.UpdateBuffer(buffer, &constants, sizeof(SpriteConstants));
	return buffer;
}

SafePtr<IRHIBuffer> CForward2DRenderer::AcquireSpriteViewConstantBuffer(IRHICommandContext& commandContext, const SpriteViewConstants& constants)
{
	const std::size_t bufferIndex = m_spriteViewConstantBufferCursor++;
	if (bufferIndex >= m_spriteViewConstantBuffers.size())
	{
		RHIBufferDesc desc;
		desc.SizeInBytes = sizeof(SpriteViewConstants);
		desc.Usage = ERHIBufferUsage::Default;
		desc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::ConstantBuffer);

		OwnerPtr<IRHIBuffer> buffer = m_rhiDevice->CreateBuffer(desc, nullptr);
		if (!buffer)
		{
			return nullptr;
		}
		m_spriteViewConstantBuffers.push_back(std::move(buffer));
	}

	SafePtr<IRHIBuffer> buffer = m_spriteViewConstantBuffers[bufferIndex].GetSafePtr();
	commandContext.UpdateBuffer(buffer, &constants, sizeof(SpriteViewConstants));
	return buffer;
}

SafePtr<IRHIBuffer> CForward2DRenderer::AcquireSpriteInstanceBuffer(IRHICommandContext& commandContext, const SpriteInstanceData* instances, std::uint32_t instanceCount)
{
	if (nullptr == instances || 0 == instanceCount)
	{
		return nullptr;
	}

	const std::size_t requiredBytes = sizeof(SpriteInstanceData) * static_cast<std::size_t>(instanceCount);
	const std::size_t bufferIndex = m_spriteInstanceBufferCursor++;
	if (bufferIndex >= m_spriteInstanceBuffers.size()
		|| !m_spriteInstanceBuffers[bufferIndex]
		|| m_spriteInstanceBuffers[bufferIndex]->GetDesc().SizeInBytes < requiredBytes)
	{
		RHIBufferDesc desc;
		desc.SizeInBytes = requiredBytes;
		desc.Usage = ERHIBufferUsage::Default;
		desc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::VertexBuffer);

		OwnerPtr<IRHIBuffer> buffer = m_rhiDevice->CreateBuffer(desc, nullptr);
		if (!buffer)
		{
			return nullptr;
		}

		if (bufferIndex >= m_spriteInstanceBuffers.size())
		{
			m_spriteInstanceBuffers.push_back(std::move(buffer));
		}
		else
		{
			m_spriteInstanceBuffers[bufferIndex] = std::move(buffer);
		}
	}

	SafePtr<IRHIBuffer> buffer = m_spriteInstanceBuffers[bufferIndex].GetSafePtr();
	commandContext.UpdateBuffer(buffer, instances, requiredBytes);
	return buffer;
}

bool CForward2DRenderer::DrawSpriteItem(
	IRHICommandContext& commandContext,
	RenderStateCache& stateCache,
	const RenderItem& item,
	const SpriteDrawResources& resources,
	const ViewParameters& view)
{
	if (false == resources.Mesh.IsValid() || false == resources.Pipeline.IsValid() || false == resources.Texture.IsValid() || false == resources.Sampler.IsValid())
	{
		return false;
	}

	const SpriteConstants constants = BuildSpriteConstants(item, view);
	return DrawSpriteQuad(commandContext, stateCache, resources.Pipeline, resources.Mesh, resources.Texture, resources.Sampler, constants);
}

bool CForward2DRenderer::DrawSpriteQuad(
	IRHICommandContext& commandContext,
	RenderStateCache& stateCache,
	SafePtr<IRHIGraphicsPipeline> pipeline,
	SafePtr<IRenderMesh> mesh,
	SafePtr<IRHITexture> texture,
	SafePtr<IRHISampler> sampler,
	const SpriteConstants& constants)
{
	if (false == pipeline.IsValid() || false == mesh.IsValid() || false == texture.IsValid() || false == sampler.IsValid())
	{
		return false;
	}

	SafePtr<IRHIBuffer> vertexBuffer = mesh->GetVertexBuffer();
	SafePtr<IRHIBuffer> indexBuffer = mesh->GetIndexBuffer();
	if (false == vertexBuffer.IsValid() || false == indexBuffer.IsValid())
	{
		return false;
	}

	SafePtr<IRHIBuffer> constantBuffer = AcquireSpriteConstantBuffer(commandContext, constants);
	if (false == constantBuffer.IsValid())
	{
		return false;
	}

	if (stateCache.Pipeline != pipeline)
	{
		commandContext.SetGraphicsPipeline(pipeline);
		stateCache.Pipeline = pipeline;
	}

	constexpr std::uint32_t spriteVertexStride = sizeof(SpriteVertex);
	constexpr std::uint32_t spriteVertexOffset = 0;
	if (stateCache.VertexBuffer != vertexBuffer
		|| stateCache.VertexStride != spriteVertexStride
		|| stateCache.VertexOffset != spriteVertexOffset)
	{
		commandContext.SetVertexBuffer(0, vertexBuffer, spriteVertexStride, spriteVertexOffset);
		stateCache.VertexBuffer = vertexBuffer;
		stateCache.VertexStride = spriteVertexStride;
		stateCache.VertexOffset = spriteVertexOffset;
	}
	if (stateCache.IndexBuffer != indexBuffer)
	{
		commandContext.SetIndexBuffer(indexBuffer);
		stateCache.IndexBuffer = indexBuffer;
	}

	commandContext.SetConstantBuffer(ERHIProgramStage::Vertex, 0, constantBuffer);
	commandContext.SetConstantBuffer(ERHIProgramStage::Pixel, 0, constantBuffer);

	if (stateCache.Texture != texture)
	{
		commandContext.SetTexture(ERHIProgramStage::Pixel, 0, texture);
		stateCache.Texture = texture;
	}
	if (stateCache.Sampler != sampler)
	{
		commandContext.SetSampler(ERHIProgramStage::Pixel, 0, sampler);
		stateCache.Sampler = sampler;
	}

	commandContext.DrawIndexed(mesh->GetIndexCount(), 0, 0);
	return true;
}

bool CForward2DRenderer::CanBatchSpriteItem(const RenderItem& item, const SpriteDrawResources& resources) const
{
	if (!m_spriteBatchPipeline || !m_quadMesh)
	{
		return false;
	}
	if (false == item.Material.IsValid()
		|| false == resources.Mesh.IsValid()
		|| false == resources.Texture.IsValid()
		|| false == resources.Sampler.IsValid()
		|| false == resources.Pipeline.IsValid())
	{
		return false;
	}
	if (resources.Mesh != m_quadMesh.GetSafePtr())
	{
		return false;
	}

	return resources.Pipeline == m_spritePipeline.GetSafePtr();
}

bool CForward2DRenderer::DrawSpriteBatch(IRHICommandContext& commandContext, RenderStateCache& stateCache, const RenderItem* items, std::uint32_t itemCount, const SpriteDrawResources& resources, const ViewParameters& view)
{
	if (nullptr == items || 0 == itemCount || !m_spriteBatchPipeline || !m_quadMesh)
	{
		return false;
	}

	const RenderItem& firstItem = items[0];
	if (false == firstItem.Material.IsValid())
	{
		return false;
	}

	if (false == CanBatchSpriteItem(firstItem, resources))
	{
		return false;
	}

	SafePtr<IRHIBuffer> vertexBuffer = resources.Mesh->GetVertexBuffer();
	SafePtr<IRHIBuffer> indexBuffer = resources.Mesh->GetIndexBuffer();
	if (false == vertexBuffer.IsValid() || false == indexBuffer.IsValid())
	{
		return false;
	}

	m_spriteBatchInstances.clear();
	m_spriteBatchInstances.reserve(itemCount);
	for (std::uint32_t i = 0; i < itemCount; ++i)
	{
		m_spriteBatchInstances.push_back(BuildSpriteInstanceData(items[i]));
	}

	const SpriteViewConstants viewConstants = BuildSpriteViewConstants(view);
	SafePtr<IRHIBuffer> viewConstantBuffer = AcquireSpriteViewConstantBuffer(commandContext, viewConstants);
	SafePtr<IRHIBuffer> instanceBuffer = AcquireSpriteInstanceBuffer(commandContext, m_spriteBatchInstances.data(), itemCount);
	if (false == viewConstantBuffer.IsValid() || false == instanceBuffer.IsValid())
	{
		return false;
	}

	SafePtr<IRHIGraphicsPipeline> pipeline = m_spriteBatchPipeline.GetSafePtr();
	if (stateCache.Pipeline != pipeline)
	{
		commandContext.SetGraphicsPipeline(pipeline);
		stateCache.Pipeline = pipeline;
	}

	constexpr std::uint32_t spriteVertexStride = sizeof(SpriteVertex);
	constexpr std::uint32_t spriteVertexOffset = 0;
	if (stateCache.VertexBuffer != vertexBuffer
		|| stateCache.VertexStride != spriteVertexStride
		|| stateCache.VertexOffset != spriteVertexOffset)
	{
		commandContext.SetVertexBuffer(0, vertexBuffer, spriteVertexStride, spriteVertexOffset);
		stateCache.VertexBuffer = vertexBuffer;
		stateCache.VertexStride = spriteVertexStride;
		stateCache.VertexOffset = spriteVertexOffset;
	}

	constexpr std::uint32_t spriteInstanceStride = sizeof(SpriteInstanceData);
	constexpr std::uint32_t spriteInstanceOffset = 0;
	if (stateCache.InstanceBuffer != instanceBuffer
		|| stateCache.InstanceStride != spriteInstanceStride
		|| stateCache.InstanceOffset != spriteInstanceOffset)
	{
		commandContext.SetVertexBuffer(1, instanceBuffer, spriteInstanceStride, spriteInstanceOffset);
		stateCache.InstanceBuffer = instanceBuffer;
		stateCache.InstanceStride = spriteInstanceStride;
		stateCache.InstanceOffset = spriteInstanceOffset;
	}

	if (stateCache.IndexBuffer != indexBuffer)
	{
		commandContext.SetIndexBuffer(indexBuffer);
		stateCache.IndexBuffer = indexBuffer;
	}

	commandContext.SetConstantBuffer(ERHIProgramStage::Vertex, 0, viewConstantBuffer);
	commandContext.SetConstantBuffer(ERHIProgramStage::Pixel, 0, viewConstantBuffer);

	if (stateCache.Texture != resources.Texture)
	{
		commandContext.SetTexture(ERHIProgramStage::Pixel, 0, resources.Texture);
		stateCache.Texture = resources.Texture;
	}
	if (stateCache.Sampler != resources.Sampler)
	{
		commandContext.SetSampler(ERHIProgramStage::Pixel, 0, resources.Sampler);
		stateCache.Sampler = resources.Sampler;
	}

	commandContext.DrawIndexedInstanced(resources.Mesh->GetIndexCount(), itemCount, 0, 0, 0);
	return true;
}

void CForward2DRenderer::RenderImpl(IRenderScene& scene, const std::unordered_set<RenderObjectId>* excluded)
{
	m_lastCullingStats = {};

	if (false == m_isInitialized || false == m_rhiDevice.IsValid())
	{
		return;
	}

	if (!m_spritePipeline || !m_quadMesh)
	{
		return;
	}

	if (CRenderScene* concreteScene = dynamic_cast<CRenderScene*>(&scene))
	{
		concreteScene->Sort();
	}

	SafePtr<IRHICommandContext> commandContext = m_rhiDevice->GetImmediateCommandContext();
	if (false == commandContext.IsValid())
	{
		return;
	}

	const RenderItem* items = scene.GetRenderItems();
	const std::uint32_t itemCount = scene.GetRenderItemCount();
	const ViewParameters view = BuildViewParameters();
	RenderStateCache stateCache;
	for (std::uint32_t i = 0; i < itemCount;)
	{
		const RenderItem& item = items[i];

		// 에디터 씬뷰 숨김(EditorHidden): 해당 오브젝트 키가 제외 집합에 있으면 스킵.
		if (excluded && excluded->find(item.Entity) != excluded->end())
		{
			++i;
			continue;
		}
		++m_lastCullingStats.SubmittedCount;

		if (false == IsSpriteItemVisibleInView(item, view))
		{
			++m_lastCullingStats.CulledCount;
			++i;
			continue;
		}

		const SpriteDrawResources resources = ResolveSpriteDrawResources(item);
		if (CanBatchSpriteItem(item, resources))
		{
			std::uint32_t batchCount = 1;
			while (i + batchCount < itemCount)
			{
				const RenderItem& nextItem = items[i + batchCount];
				if (excluded && excluded->find(nextItem.Entity) != excluded->end())
				{
					break;
				}

				if (false == IsSpriteItemVisibleInView(nextItem, view))
				{
					break;
				}

				const SpriteDrawResources nextResources = ResolveSpriteDrawResources(nextItem);
				if (nextResources.Texture != resources.Texture
					|| nextResources.Sampler != resources.Sampler
					|| false == CanBatchSpriteItem(nextItem, nextResources))
				{
					break;
				}
				++batchCount;
			}

			if (batchCount > 1)
			{
				if (DrawSpriteBatch(*commandContext.TryGet(), stateCache, items + i, batchCount, resources, view))
				{
					m_lastCullingStats.SubmittedCount += batchCount - 1;
					m_lastCullingStats.DrawnCount += batchCount;
					i += batchCount;
					continue;
				}
			}
		}

		if (DrawSpriteItem(*commandContext.TryGet(), stateCache, item, resources, view))
		{
			++m_lastCullingStats.DrawnCount;
		}
		++i;
	}
}

void CForward2DRenderer::RenderFiltered(IRenderScene& scene, const std::unordered_set<RenderObjectId>& objects)
{
	m_lastCullingStats = {};

	if (false == m_isInitialized || false == m_rhiDevice.IsValid()) return;
	if (!m_spritePipeline || !m_quadMesh) return;
	if (objects.empty()) return;

	if (CRenderScene* concreteScene = dynamic_cast<CRenderScene*>(&scene))
		concreteScene->Sort();

	SafePtr<IRHICommandContext> commandContext = m_rhiDevice->GetImmediateCommandContext();
	if (false == commandContext.IsValid()) return;

	const RenderItem* items = scene.GetRenderItems();
	const std::uint32_t itemCount = scene.GetRenderItemCount();
	const ViewParameters view = BuildViewParameters();
	RenderStateCache stateCache;
	for (std::uint32_t i = 0; i < itemCount;)
	{
		const RenderItem& item = items[i];

		// ── 오브젝트 필터 ─────────────────────────────────────────────────────────
		if (objects.find(item.Entity) == objects.end())
		{
			++i;
			continue;
		}
		++m_lastCullingStats.SubmittedCount;

		if (false == IsSpriteItemVisibleInView(item, view))
		{
			++m_lastCullingStats.CulledCount;
			++i;
			continue;
		}

		const SpriteDrawResources resources = ResolveSpriteDrawResources(item);
		if (CanBatchSpriteItem(item, resources))
		{
			std::uint32_t batchCount = 1;
			while (i + batchCount < itemCount)
			{
				const RenderItem& nextItem = items[i + batchCount];
				if (objects.find(nextItem.Entity) == objects.end())
				{
					break;
				}

				if (false == IsSpriteItemVisibleInView(nextItem, view))
				{
					break;
				}

				const SpriteDrawResources nextResources = ResolveSpriteDrawResources(nextItem);
				if (nextResources.Texture != resources.Texture
					|| nextResources.Sampler != resources.Sampler
					|| false == CanBatchSpriteItem(nextItem, nextResources))
				{
					break;
				}
				++batchCount;
			}

			if (batchCount > 1)
			{
				if (DrawSpriteBatch(*commandContext.TryGet(), stateCache, items + i, batchCount, resources, view))
				{
					m_lastCullingStats.SubmittedCount += batchCount - 1;
					m_lastCullingStats.DrawnCount += batchCount;
					i += batchCount;
					continue;
				}
			}
		}

		if (DrawSpriteItem(*commandContext.TryGet(), stateCache, item, resources, view))
		{
			++m_lastCullingStats.DrawnCount;
		}
		++i;
	}
}

void CForward2DRenderer::FillViewportColor(float r, float g, float b, float a)
{
	if (!m_spritePipeline || !m_quadMesh || !m_whiteTexture || !m_rhiDevice.IsValid())
	{
		return;
	}
	SafePtr<IRHICommandContext> commandContext = m_rhiDevice->GetImmediateCommandContext();
	if (false == commandContext.IsValid())
	{
		return;
	}

	const SpriteConstants constants = BuildViewportColorConstants(r, g, b, a);
	RenderStateCache stateCache;
	DrawSpriteQuad(
		*commandContext.TryGet(),
		stateCache,
		m_spritePipeline.GetSafePtr(),
		m_quadMesh.GetSafePtr(),
		m_whiteTexture.GetSafePtr(),
		m_defaultSampler.GetSafePtr(),
		constants);
}

void CForward2DRenderer::Finalize()
{
	m_whiteTexture.Reset();
	m_spriteBatchInstances.clear();
	m_spriteInstanceBuffers.clear();
	m_spriteViewConstantBuffers.clear();
	m_spriteConstantBuffers.clear();
	m_spriteInstanceBufferCursor = 0;
	m_spriteViewConstantBufferCursor = 0;
	m_spriteConstantBufferCursor = 0;
	m_quadMesh.Reset();
	m_defaultSampler.Reset();
	m_spriteBatchPipeline.Reset();
	m_spritePipeline.Reset();
	m_spriteBatchPixelProgram.Reset();
	m_spriteBatchVertexProgram.Reset();
	m_spritePixelProgram.Reset();
	m_spriteVertexProgram.Reset();
	m_rhiDevice.Reset();
	m_isInitialized = false;
}

bool CForward2DRenderer::CreateGpuResource(IRenderResource& resource)
{
	(void)resource;
	return true;
}

void CForward2DRenderer::DestroyGpuResource(IRenderResource& resource)
{
	(void)resource;
}

SafePtr<IRHIGraphicsPipeline> CForward2DRenderer::GetSpritePipeline() const
{
	return m_spritePipeline.GetSafePtr();
}

SafePtr<IRHISampler> CForward2DRenderer::GetDefaultSampler() const
{
	return m_defaultSampler.GetSafePtr();
}

SafePtr<IRenderMesh> CForward2DRenderer::GetQuadMesh() const
{
	return m_quadMesh.GetSafePtr();
}

bool CForward2DRenderer::CreateSpritePipeline()
{
	const ERHIApi api = m_rhiDevice->GetApi();
	const ERHIProgramLanguage language =
		ERHIApi::WebGPU == api ? ERHIProgramLanguage::WGSL :
		ERHIApi::Vulkan == api ? ERHIProgramLanguage::SPIRV :
		ERHIProgramLanguage::HLSL;
	const char* shaderSource =
		ERHIApi::WebGPU == api ? SPRITE_SHADER_SOURCE_WGSL :
		ERHIApi::Vulkan == api ? reinterpret_cast<const char*>(JBro::Renderer::VulkanShaders::SpriteVertexSpv) :
		SPRITE_SHADER_SOURCE_HLSL;

	RHIProgramDesc vertexProgramDesc;
	vertexProgramDesc.Stage = ERHIProgramStage::Vertex;
	vertexProgramDesc.Language = language;
	vertexProgramDesc.EntryPoint = ERHIApi::Vulkan == api ? "main" : "VSMain";
	vertexProgramDesc.Source = shaderSource;
	vertexProgramDesc.SourceSize = ERHIApi::Vulkan == api ? JBro::Renderer::VulkanShaders::SpriteVertexSpvSize : 0;
	m_spriteVertexProgram = m_rhiDevice->CreateProgram(vertexProgramDesc);

	RHIProgramDesc pixelProgramDesc;
	pixelProgramDesc.Stage = ERHIProgramStage::Pixel;
	pixelProgramDesc.Language = language;
	pixelProgramDesc.EntryPoint = ERHIApi::Vulkan == api ? "main" : "PSMain";
	pixelProgramDesc.Source = ERHIApi::Vulkan == api ? reinterpret_cast<const char*>(JBro::Renderer::VulkanShaders::SpritePixelSpv) : shaderSource;
	pixelProgramDesc.SourceSize = ERHIApi::Vulkan == api ? JBro::Renderer::VulkanShaders::SpritePixelSpvSize : 0;
	m_spritePixelProgram = m_rhiDevice->CreateProgram(pixelProgramDesc);

	if (!m_spriteVertexProgram || !m_spritePixelProgram)
	{
		return false;
	}

	RHIVertexElementDesc elements[2];
	elements[0].SemanticName = "POSITION";
	elements[0].Format = ERHIVertexFormat::Float2;
	elements[0].Offset = 0;
	elements[1].SemanticName = "TEXCOORD";
	elements[1].Format = ERHIVertexFormat::Float2;
	elements[1].Offset = sizeof(float) * 2;

	RHIGraphicsPipelineDesc pipelineDesc;
	pipelineDesc.VertexProgram = m_spriteVertexProgram.GetSafePtr();
	pipelineDesc.PixelProgram = m_spritePixelProgram.GetSafePtr();
	pipelineDesc.VertexElements = elements;
	pipelineDesc.VertexElementCount = 2;
	pipelineDesc.PrimitiveTopology = ERHIPrimitiveTopology::TriangleList;
	pipelineDesc.BlendMode = ERHIBlendMode::AlphaBlend; // 스프라이트 투명도 지원
	m_spritePipeline = m_rhiDevice->CreateGraphicsPipeline(pipelineDesc);
	return static_cast<bool>(m_spritePipeline);
}

bool CForward2DRenderer::CreateSpriteBatchPipeline()
{
	const ERHIApi api = m_rhiDevice->GetApi();
	const ERHIProgramLanguage language =
		ERHIApi::WebGPU == api ? ERHIProgramLanguage::WGSL :
		ERHIApi::Vulkan == api ? ERHIProgramLanguage::SPIRV :
		ERHIProgramLanguage::HLSL;
	const char* shaderSource =
		ERHIApi::WebGPU == api ? SPRITE_BATCH_SHADER_SOURCE_WGSL :
		ERHIApi::Vulkan == api ? reinterpret_cast<const char*>(JBro::Renderer::VulkanShaders::SpriteBatchVertexSpv) :
		SPRITE_BATCH_SHADER_SOURCE_HLSL;

	RHIProgramDesc vertexProgramDesc;
	vertexProgramDesc.Stage = ERHIProgramStage::Vertex;
	vertexProgramDesc.Language = language;
	vertexProgramDesc.EntryPoint = ERHIApi::Vulkan == api ? "main" : "VSMain";
	vertexProgramDesc.Source = shaderSource;
	vertexProgramDesc.SourceSize = ERHIApi::Vulkan == api ? JBro::Renderer::VulkanShaders::SpriteBatchVertexSpvSize : 0;
	m_spriteBatchVertexProgram = m_rhiDevice->CreateProgram(vertexProgramDesc);

	RHIProgramDesc pixelProgramDesc;
	pixelProgramDesc.Stage = ERHIProgramStage::Pixel;
	pixelProgramDesc.Language = language;
	pixelProgramDesc.EntryPoint = ERHIApi::Vulkan == api ? "main" : "PSMain";
	pixelProgramDesc.Source = ERHIApi::Vulkan == api ? reinterpret_cast<const char*>(JBro::Renderer::VulkanShaders::SpritePixelSpv) : shaderSource;
	pixelProgramDesc.SourceSize = ERHIApi::Vulkan == api ? JBro::Renderer::VulkanShaders::SpritePixelSpvSize : 0;
	m_spriteBatchPixelProgram = m_rhiDevice->CreateProgram(pixelProgramDesc);

	if (!m_spriteBatchVertexProgram || !m_spriteBatchPixelProgram)
	{
		return false;
	}

	RHIVertexElementDesc elements[5];
	elements[0].SemanticName = "POSITION";
	elements[0].Format = ERHIVertexFormat::Float2;
	elements[0].Offset = 0;
	elements[0].InputSlot = 0;
	elements[0].InputRate = ERHIVertexInputRate::PerVertex;

	elements[1].SemanticName = "TEXCOORD";
	elements[1].SemanticIndex = 0;
	elements[1].Format = ERHIVertexFormat::Float2;
	elements[1].Offset = sizeof(float) * 2;
	elements[1].InputSlot = 0;
	elements[1].InputRate = ERHIVertexInputRate::PerVertex;

	elements[2].SemanticName = "TEXCOORD";
	elements[2].SemanticIndex = 1;
	elements[2].Format = ERHIVertexFormat::Float4;
	elements[2].Offset = 0;
	elements[2].InputSlot = 1;
	elements[2].InputRate = ERHIVertexInputRate::PerInstance;

	elements[3].SemanticName = "TEXCOORD";
	elements[3].SemanticIndex = 2;
	elements[3].Format = ERHIVertexFormat::Float4;
	elements[3].Offset = sizeof(float) * 4;
	elements[3].InputSlot = 1;
	elements[3].InputRate = ERHIVertexInputRate::PerInstance;

	elements[4].SemanticName = "COLOR";
	elements[4].SemanticIndex = 0;
	elements[4].Format = ERHIVertexFormat::Color4;
	elements[4].Offset = sizeof(float) * 8;
	elements[4].InputSlot = 1;
	elements[4].InputRate = ERHIVertexInputRate::PerInstance;

	RHIGraphicsPipelineDesc pipelineDesc;
	pipelineDesc.VertexProgram = m_spriteBatchVertexProgram.GetSafePtr();
	pipelineDesc.PixelProgram = m_spriteBatchPixelProgram.GetSafePtr();
	pipelineDesc.VertexElements = elements;
	pipelineDesc.VertexElementCount = 5;
	pipelineDesc.PrimitiveTopology = ERHIPrimitiveTopology::TriangleList;
	pipelineDesc.BlendMode = ERHIBlendMode::AlphaBlend;
	m_spriteBatchPipeline = m_rhiDevice->CreateGraphicsPipeline(pipelineDesc);
	return static_cast<bool>(m_spriteBatchPipeline);
}

bool CForward2DRenderer::CreateQuadMesh()
{
	const SpriteVertex vertices[] = {
		{ { -0.5f,  0.5f }, { 0.0f, 0.0f } },
		{ {  0.5f,  0.5f }, { 1.0f, 0.0f } },
		{ {  0.5f, -0.5f }, { 1.0f, 1.0f } },
		{ { -0.5f, -0.5f }, { 0.0f, 1.0f } },
	};
	const std::uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };

	RHIBufferDesc vertexBufferDesc;
	vertexBufferDesc.SizeInBytes = sizeof(vertices);
	vertexBufferDesc.Usage = ERHIBufferUsage::Immutable;
	vertexBufferDesc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::VertexBuffer);

	RHIBufferDesc indexBufferDesc;
	indexBufferDesc.SizeInBytes = sizeof(indices);
	indexBufferDesc.Usage = ERHIBufferUsage::Immutable;
	indexBufferDesc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::IndexBuffer);

	OwnerPtr<IRHIBuffer> vertexBuffer = m_rhiDevice->CreateBuffer(vertexBufferDesc, vertices);
	OwnerPtr<IRHIBuffer> indexBuffer = m_rhiDevice->CreateBuffer(indexBufferDesc, indices);
	if (!vertexBuffer || !indexBuffer)
	{
		return false;
	}

	m_quadMesh = MakeOwnerPtr<CRenderMesh>(std::move(vertexBuffer), std::move(indexBuffer), 4, 6);
	return true;
}
