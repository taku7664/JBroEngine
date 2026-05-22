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
	};

	const char* SPRITE_SHADER_SOURCE_HLSL = R"(
cbuffer SpriteConstants : register(b0)
{
	float4 gTransformRow0;
	float4 gTransformRow1;
	float4 gColor;
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
	float2 position = float2(dot(localPosition, gTransformRow0.xyz), dot(localPosition, gTransformRow1.xyz));
	output.Position = float4(position.xy, 0.0f, 1.0f);
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
	let position = vec2<f32>(dot(localPosition, gConstants.TransformRow0.xyz), dot(localPosition, gConstants.TransformRow1.xyz));
	output.Position = vec4<f32>(position.xy, 0.0, 1.0);
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

	m_isInitialized = true;
	return true;
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

void CForward2DRenderer::Finalize()
{
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
