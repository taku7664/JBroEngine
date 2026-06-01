#pragma once

#include <cstdint>
#include <cstddef>

#include "Utillity/Pointer/SafePtr.h"

using RHIBindFlags = std::uint32_t;
using RHITextureBindFlags = std::uint32_t;

enum class ERHIBufferUsage
{
	Default,
	Immutable,
	Dynamic
};

enum class ERHIBindFlag : RHIBindFlags
{
	None = 0,
	VertexBuffer = 1 << 0,
	IndexBuffer = 1 << 1,
	ConstantBuffer = 1 << 2
};

inline RHIBindFlags operator|(ERHIBindFlag lhs, ERHIBindFlag rhs)
{
	return static_cast<RHIBindFlags>(lhs) | static_cast<RHIBindFlags>(rhs);
}

enum class ERHIProgramStage
{
	Vertex,
	Pixel,
	Compute
};

enum class ERHIProgramLanguage
{
	Unknown,
	HLSL,
	WGSL,
	GLSL
};

enum class ERHIPrimitiveTopology
{
	TriangleList,
	TriangleStrip,
	LineList,
	LineStrip
};

enum class ERHIVertexFormat
{
	Float2,
	Float3,
	Float4,
	Color4
};

enum class ERHITextureFormat
{
	RGBA8
};

enum class ERHITextureBindFlag : RHITextureBindFlags
{
	None = 0,
	ShaderResource = 1 << 0,
	RenderTarget = 1 << 1,
	CopySource = 1 << 2,
	CopyDestination = 1 << 3
};

inline RHITextureBindFlags operator|(ERHITextureBindFlag lhs, ERHITextureBindFlag rhs)
{
	return static_cast<RHITextureBindFlags>(lhs) | static_cast<RHITextureBindFlags>(rhs);
}

enum class ERHIFilterMode
{
	Point,
	Linear
};

enum class ERHIAddressMode
{
	Clamp,
	Repeat
};

struct RHIBufferDesc
{
	std::size_t SizeInBytes = 0;
	ERHIBufferUsage Usage = ERHIBufferUsage::Default;
	RHIBindFlags BindFlags = 0;
};

struct RHITexture2DDesc
{
	std::uint32_t Width = 0;
	std::uint32_t Height = 0;
	ERHITextureFormat Format = ERHITextureFormat::RGBA8;
	RHITextureBindFlags BindFlags = static_cast<RHITextureBindFlags>(ERHITextureBindFlag::ShaderResource);
};

struct RHISamplerDesc
{
	ERHIFilterMode Filter = ERHIFilterMode::Linear;
	ERHIAddressMode AddressU = ERHIAddressMode::Clamp;
	ERHIAddressMode AddressV = ERHIAddressMode::Clamp;
};

struct RHIProgramDesc
{
	ERHIProgramStage Stage = ERHIProgramStage::Vertex;
	ERHIProgramLanguage Language = ERHIProgramLanguage::Unknown;
	const char* EntryPoint = nullptr;
	const char* Source = nullptr;
};

struct RHIVertexElementDesc
{
	const char* SemanticName = nullptr;
	std::uint32_t SemanticIndex = 0;
	ERHIVertexFormat Format = ERHIVertexFormat::Float3;
	std::uint32_t Offset = 0;
};

enum class ERHIBlendMode
{
    Opaque,        // 블렌딩 없음 (기본값)
    AlphaBlend,    // src.a × src.rgb + (1-src.a) × dst.rgb  (스프라이트용)
    Additive,      // src.rgb + dst.rgb
};

struct RHIGraphicsPipelineDesc
{
	SafePtr<class IRHIProgram> VertexProgram;
	SafePtr<class IRHIProgram> PixelProgram;
	const RHIVertexElementDesc* VertexElements = nullptr;
	std::uint32_t VertexElementCount = 0;
	ERHIPrimitiveTopology PrimitiveTopology = ERHIPrimitiveTopology::TriangleList;
	ERHIBlendMode BlendMode = ERHIBlendMode::Opaque;
};
