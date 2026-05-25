#include "pch.h"
#include "Forward2DRenderer.h"

#include "Core/Renderer/IRenderMaterial.h"
#include "Core/Renderer/IRenderMesh.h"
#include "Core/Renderer/IRenderScene.h"
#include "Core/Renderer/RenderResources2D.h"
#include "Core/Renderer/RenderScene.h"
#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/IRHIDevice.h"

namespace
{
	struct SpriteVertex
	{
		float Position[2];
		float Uv[2];
	};

	struct SpriteConstants
	{
		float TransformRow0[4];
		float TransformRow1[4];
		float Color[4];
		// Full 2×3 view matrix (world → NDC), stored as two float4 rows.
		// NDC.x = dot([worldX, worldY, 1], ViewRow0.xyz)
		// NDC.y = dot([worldX, worldY, 1], ViewRow1.xyz)
		float ViewRow0[4];
		float ViewRow1[4];
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
	m_useExplicitSize = true;
}

void CForward2DRenderer::Render(IRenderScene& scene)
{
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
	for (std::uint32_t i = 0; i < itemCount; ++i)
	{
		const RenderItem& item = items[i];
		if (false == item.Mesh.IsValid() || false == item.Material.IsValid())
		{
			continue;
		}

		SafePtr<IRHIGraphicsPipeline> pipeline = item.Material->GetPipeline();
		if (false == pipeline.IsValid())
		{
			pipeline = m_spritePipeline.GetSafePtr();
		}

		SafePtr<IRHISampler> sampler = item.Material->GetSampler();
		if (false == sampler.IsValid())
		{
			sampler = m_defaultSampler.GetSafePtr();
		}

		SafePtr<IRHITexture> texture = item.Material->GetTexture();
		if (false == texture.IsValid())
		{
			continue;
		}

		SpriteConstants constants;
		constants.TransformRow0[0] = item.Transform.M11;
		constants.TransformRow0[1] = item.Transform.M21;
		constants.TransformRow0[2] = item.Transform.Dx;
		constants.TransformRow0[3] = 0.0f;
		constants.TransformRow1[0] = item.Transform.M12;
		constants.TransformRow1[1] = item.Transform.M22;
		constants.TransformRow1[2] = item.Transform.Dy;
		constants.TransformRow1[3] = 0.0f;
		for (int c = 0; c < 4; ++c)
		{
			constants.Color[c] = item.Color[c];
		}
		// Full 2×3 view matrix: world → NDC
		// NDC.x = dot([worldX, worldY, 1], ViewRow0.xyz)
		// NDC.y = dot([worldX, worldY, 1], ViewRow1.xyz)
		// Supports translation, scale, and rotation (camera's inverse transform).
		float halfW, halfH, cosR, sinR;
		if (m_useExplicitSize)
		{
			// Stretch + rotation mode: explicit half-extents and camera rotation.
			// GPU hardware maps NDC → viewport pixels as a natural stretch/squish.
			halfW = m_viewCamHalfW;
			halfH = m_viewCamHalfH;
			cosR  = m_viewCamCosR;
			sinR  = m_viewCamSinR;
		}
		else
		{
			// Auto mode: derive halfW from the current render-target aspect ratio.
			// No rotation (scene-view editor camera).
			const float width  = static_cast<float>(m_renderTargetSize.Width);
			const float height = static_cast<float>(m_renderTargetSize.Height);
			const float aspect = (width > 0.0f && height > 0.0f) ? width / height : 1.0f;
			const float size   = m_viewCamSize > 0.0f ? m_viewCamSize : 1.0f;
			halfW = size * aspect;
			halfH = size;
			cosR  = 1.0f;
			sinR  = 0.0f;
		}
		// Build view rows from (camPos, halfW, halfH, rotation).
		// View transform: NDC = R(-θ) * (world - camPos) / halfExtents
		//   ViewRow0 = [ cosR/halfW,  sinR/halfW, -(cosR*camX + sinR*camY)/halfW ]
		//   ViewRow1 = [-sinR/halfH,  cosR/halfH,  (sinR*camX - cosR*camY)/halfH ]
		const float camX = m_viewCamX, camY = m_viewCamY;
		constants.ViewRow0[0] =  cosR / halfW;
		constants.ViewRow0[1] =  sinR / halfW;
		constants.ViewRow0[2] = -(cosR * camX + sinR * camY) / halfW;
		constants.ViewRow0[3] = 0.0f;
		constants.ViewRow1[0] = -sinR / halfH;
		constants.ViewRow1[1] =  cosR / halfH;
		constants.ViewRow1[2] =  (sinR * camX - cosR * camY) / halfH;
		constants.ViewRow1[3] = 0.0f;

		RHIBufferDesc constantBufferDesc;
		constantBufferDesc.SizeInBytes = sizeof(SpriteConstants);
		constantBufferDesc.Usage = ERHIBufferUsage::Default;
		constantBufferDesc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::ConstantBuffer);
		OwnerPtr<IRHIBuffer> constantBuffer = m_rhiDevice->CreateBuffer(constantBufferDesc, &constants);
		if (!constantBuffer)
		{
			continue;
		}

		commandContext->SetGraphicsPipeline(pipeline);
		commandContext->SetVertexBuffer(0, item.Mesh->GetVertexBuffer(), sizeof(SpriteVertex), 0);
		commandContext->SetIndexBuffer(item.Mesh->GetIndexBuffer());
		commandContext->SetConstantBuffer(ERHIProgramStage::Vertex, 0, constantBuffer.GetSafePtr());
		commandContext->SetConstantBuffer(ERHIProgramStage::Pixel, 0, constantBuffer.GetSafePtr());
		commandContext->SetTexture(ERHIProgramStage::Pixel, 0, texture);
		commandContext->SetSampler(ERHIProgramStage::Pixel, 0, sampler);
		commandContext->DrawIndexed(item.Mesh->GetIndexCount(), 0, 0);
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

	// Build constants that draw a quad covering exactly NDC [-1,1]×[-1,1].
	// Quad vertices are ±0.5 in local space; scale by 2 → world ±1.
	// ViewRow identity: NDC = world (so ±1 in world = ±1 in NDC).
	SpriteConstants constants = {};
	constants.TransformRow0[0] = 2.0f;   // ScaleX = 2
	constants.TransformRow1[1] = 2.0f;   // ScaleY = 2
	constants.Color[0] = r;
	constants.Color[1] = g;
	constants.Color[2] = b;
	constants.Color[3] = a;
	// Identity view: NDC.x = worldX, NDC.y = worldY
	constants.ViewRow0[0] = 1.0f;  // [1, 0, 0, 0]
	constants.ViewRow1[1] = 1.0f;  // [0, 1, 0, 0]

	RHIBufferDesc cbDesc;
	cbDesc.SizeInBytes = sizeof(SpriteConstants);
	cbDesc.Usage       = ERHIBufferUsage::Default;
	cbDesc.BindFlags   = static_cast<RHIBindFlags>(ERHIBindFlag::ConstantBuffer);
	OwnerPtr<IRHIBuffer> cb = m_rhiDevice->CreateBuffer(cbDesc, &constants);
	if (!cb)
	{
		return;
	}

	commandContext->SetGraphicsPipeline(m_spritePipeline);
	commandContext->SetVertexBuffer(0, m_quadMesh->GetVertexBuffer(), sizeof(SpriteVertex), 0);
	commandContext->SetIndexBuffer(m_quadMesh->GetIndexBuffer());
	commandContext->SetConstantBuffer(ERHIProgramStage::Vertex, 0, cb.GetSafePtr());
	commandContext->SetConstantBuffer(ERHIProgramStage::Pixel,  0, cb.GetSafePtr());
	commandContext->SetTexture(ERHIProgramStage::Pixel, 0, m_whiteTexture.GetSafePtr());
	commandContext->SetSampler(ERHIProgramStage::Pixel, 0, m_defaultSampler.GetSafePtr());
	commandContext->DrawIndexed(m_quadMesh->GetIndexCount(), 0, 0);
}

void CForward2DRenderer::Finalize()
{
	m_whiteTexture.Reset();
	m_quadMesh.Reset();
	m_defaultSampler.Reset();
	m_spritePipeline.Reset();
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
	const ERHIProgramLanguage language = ERHIApi::WebGPU == api ? ERHIProgramLanguage::WGSL : ERHIProgramLanguage::HLSL;
	const char* shaderSource = ERHIApi::WebGPU == api ? SPRITE_SHADER_SOURCE_WGSL : SPRITE_SHADER_SOURCE_HLSL;

	RHIProgramDesc vertexProgramDesc;
	vertexProgramDesc.Stage = ERHIProgramStage::Vertex;
	vertexProgramDesc.Language = language;
	vertexProgramDesc.EntryPoint = "VSMain";
	vertexProgramDesc.Source = shaderSource;
	m_spriteVertexProgram = m_rhiDevice->CreateProgram(vertexProgramDesc);

	RHIProgramDesc pixelProgramDesc;
	pixelProgramDesc.Stage = ERHIProgramStage::Pixel;
	pixelProgramDesc.Language = language;
	pixelProgramDesc.EntryPoint = "PSMain";
	pixelProgramDesc.Source = shaderSource;
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
	m_spritePipeline = m_rhiDevice->CreateGraphicsPipeline(pipelineDesc);
	return static_cast<bool>(m_spritePipeline);
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
