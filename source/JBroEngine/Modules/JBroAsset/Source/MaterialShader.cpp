#include <JBro/Asset/MaterialShader.h>

#include <cstring>

namespace JBro
{
    namespace
    {
        // 셰이더 글자는 ASCII 만 쓴다 - fxc 는 UTF-8 BOM 을, dxc 는 BOM 없는 한국어를 거절한다(`Compile.ps1`).
        constexpr const char Prelude[] = R"hlsl(// JBro material prelude (D-347). The engine supplies the vertex stage; a material shader writes PSMain only.
// The compiler defines JBRO_MATERIAL_POSTPROCESS, JBRO_MATERIAL_SPRITE or JBRO_MATERIAL_MESH, and JBRO_SPIRV for Vulkan.
//
//   b0  engine values (push constants). Sprite and mesh: the view-projection matrix first (the vertex stage reads it).
//   b1  cbuffer Material - the material's parameters (the inspector edits them by name).
//   t0  the input: the previous effect's result (post-process) or the sprite's texture (sprite). A mesh has none.
//   t1..t6 the material's textures (Texture2D, by name). t7 is the engine's: the sprite's light map.
//   s0  the input's sampler (post-process: linear clamp, sprite: the sprite's filter), s1 linear clamp, s2 point clamp, s3 linear repeat.
#ifndef JBRO_MATERIAL_PRELUDE
#define JBRO_MATERIAL_PRELUDE

#if defined(JBRO_SPIRV)
struct JBroEngineValues
{
#if defined(JBRO_MATERIAL_SPRITE) || defined(JBRO_MATERIAL_MESH)
    row_major float4x4 viewProjection;
#endif
    float4 viewport;
    float4 target;
    float4 time;
    float4 flags;
};
[[vk::push_constant]] ConstantBuffer<JBroEngineValues> gJBroEngine;
#define JBroViewport gJBroEngine.viewport
#define JBroTarget gJBroEngine.target
#define JBroTime gJBroEngine.time
#define JBroFlags gJBroEngine.flags
#else
cbuffer JBroEngine : register(b0)
{
#if defined(JBRO_MATERIAL_SPRITE) || defined(JBRO_MATERIAL_MESH)
    row_major float4x4 JBroViewProjection;
#endif
    // The view's viewport in target pixels: x, y, width, height.
    float4 JBroViewport;
    // The target's size: width, height, 1 / width, 1 / height.
    float4 JBroTarget;
    // Seconds since the renderer started, the frame's delta, 0, 0.
    float4 JBroTime;
    // x: 1 when the input holds premultiplied alpha (a layer's texture), else 0.
    float4 JBroFlags;
};
#endif

SamplerState JBroSourceSampler : register(s0);
SamplerState JBroLinearClamp : register(s1);
SamplerState JBroPointClamp : register(s2);
SamplerState JBroLinearRepeat : register(s3);

float JBroSeconds()
{
    return JBroTime.x;
}

float4 JBroUnpremultiply(float4 color)
{
    return color.a > 0.0f ? float4(color.rgb / color.a, color.a) : float4(0.0f, 0.0f, 0.0f, 0.0f);
}

float4 JBroPremultiply(float4 color)
{
    return float4(color.rgb * color.a, color.a);
}

#if defined(JBRO_MATERIAL_SPRITE)

// The sprite vertex stage's output (BuiltinSprite.hlsl VertexOutput).
struct JBroSpriteInput
{
    float4 position : SV_POSITION;
    float4 tint : COLOR0;
    float2 uv : TEXCOORD0;
};

Texture2D JBroSpriteTexture : register(t0);
// The view's light map, the size of the target (BuiltinSprite.hlsl PSLitMain). An unlit sprite gets a 1x1 white texture instead.
Texture2D JBroLightMap : register(t7);

float4 JBroSampleSprite(JBroSpriteInput input)
{
    return JBroSpriteTexture.Sample(JBroSourceSampler, input.uv);
}

// The 2D light on this pixel. The engine multiplies PSMain's colour by it already; a shader calls it only to light something else.
// Read through the point clamp sampler at the pixel centre over the target size: the texel under the pixel in the light map, the one white
// texel otherwise (a Load at the pixel position would fall outside the 1x1 texture and read black).
float3 JBroSpriteLight(JBroSpriteInput input)
{
    return JBroLightMap.SampleLevel(JBroPointClamp, input.position.xy * JBroTarget.zw, 0.0f).rgb;
}

#elif defined(JBRO_MATERIAL_MESH)

// The mesh vertex stage's output (BuiltinMesh.hlsl VertexOutput), in the same order: Vulkan links the stages by position.
struct JBroMeshInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL0;
    float4 tint : COLOR0;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
};

// The view's 3D lights (BuiltinMesh.hlsl MeshLights).
cbuffer JBroLights : register(b2)
{
    float4 JBroLightCount;
    float4 JBroLightAmbient;
    float4 JBroLightPosition[16];
    float4 JBroLightColor[16];
};

// The engine's light on a surface: the view's Light3D components, or one fixed directional light plus ambient when it has none.
// Multiply a colour by it to light it.
float3 JBroMeshLight(float3 normal, float3 worldPosition)
{
    const float3 n = normalize(normal);
    const int count = (int)JBroLightCount.x;
    if (JBroLightCount.y < 0.5f)
    {
        const float3 lightDirection = normalize(float3(0.4f, 0.8f, 0.45f));
        return (0.25f + 0.75f * saturate(dot(n, lightDirection))).xxx;
    }
    float3 light = JBroLightAmbient.rgb;
    for (int at = 0; at < count; ++at)
    {
        float3 towards = JBroLightPosition[at].xyz;
        float attenuation = 1.0f;
        if (JBroLightPosition[at].w > 0.5f)
        {
            const float3 offset = JBroLightPosition[at].xyz - worldPosition;
            const float distance = length(offset);
            towards = offset / max(distance, 0.0001f);
            const float fade = saturate(1.0f - distance / max(JBroLightColor[at].a, 0.0001f));
            attenuation = fade * fade;
        }
        light += JBroLightColor[at].rgb * saturate(dot(n, towards)) * attenuation;
    }
    return light;
}

#else

// The full screen triangle's output (BuiltinPostProcess.hlsl VSMain).
struct JBroPostProcessInput
{
    float4 position : SV_POSITION;
};

Texture2D JBroSource : register(t0);

// 0..1 across the view's viewport, (0, 0) at the top left.
float2 JBroUV(JBroPostProcessInput input)
{
    return (input.position.xy - JBroViewport.xy) / JBroViewport.zw;
}

// The input under this pixel.
float4 JBroLoadSource(JBroPostProcessInput input)
{
    return JBroSource.Load(int3(int2(input.position.xy), 0));
}

// The input at a viewport UV (JBroUV), filtered linearly.
float4 JBroSampleSource(float2 uv)
{
    return JBroSource.SampleLevel(JBroSourceSampler, (JBroViewport.xy + uv * JBroViewport.zw) * JBroTarget.zw, 0.0f);
}

bool JBroSourceIsPremultiplied()
{
    return JBroFlags.x > 0.5f;
}

#endif

#endif
)hlsl";

        constexpr const char Template[] = R"hlsl(// A JBro material shader (D-347). The material (.jmat) picks the domain; this file holds the pixel stage only.
// The names in cbuffer Material and the Texture2D names after t0 are what the material inspector shows.
#include "JBro/Material.hlsli"

cbuffer Material : register(b1)
{
    float4 tintColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    float strength = 1.0f;
};

#if defined(JBRO_MATERIAL_SPRITE)

float4 PSMain(JBroSpriteInput input) : SV_TARGET
{
    const float4 color = JBroSampleSprite(input) * input.tint;
    return lerp(color, color * tintColor, strength);
}

#elif defined(JBRO_MATERIAL_MESH)

float4 PSMain(JBroMeshInput input) : SV_TARGET
{
    const float4 color = float4(input.tint.rgb * JBroMeshLight(input.normal, input.worldPosition), input.tint.a);
    return lerp(color, color * tintColor, strength);
}

#else

float4 PSMain(JBroPostProcessInput input) : SV_TARGET
{
    const float4 color = JBroLoadSource(input);
    return lerp(color, color * tintColor, strength);
}

#endif
)hlsl";

        // 스프라이트의 입구다. 사용자 `PSMain` 의 색에 라이트맵을 곱한다 - 빌트인 스프라이트의 `PSLitMain` 과 같은 셈이다.
        constexpr const char SpriteEpilogue[] = R"hlsl(
#line 1 "JBro/MaterialEntry.hlsli"
float4 JBroSpriteMain(JBroSpriteInput input) : SV_TARGET
{
    const float4 color = PSMain(input);
    return float4(color.rgb * JBroSpriteLight(input), color.a);
}
)hlsl";

        constexpr std::uint32_t Magic = 0x48534D4Au; // "JMSH"
        constexpr std::uint32_t Version = 1;

        class Writer
        {
        public:
            explicit Writer(Array<std::byte>& out) : m_out(out)
            {
            }
            void U32(std::uint32_t value)
            {
                Bytes(&value, sizeof(value));
            }
            void F32(float value)
            {
                Bytes(&value, sizeof(value));
            }
            void Text(const String& text)
            {
                U32(static_cast<std::uint32_t>(text.size()));
                Bytes(text.c_str(), text.size());
            }
            void Blob(const Array<std::byte>& bytes)
            {
                U32(static_cast<std::uint32_t>(bytes.Size()));
                Bytes(bytes.Data(), bytes.Size());
            }
            void Bytes(const void* data, std::size_t size)
            {
                const std::size_t at = m_out.Size();
                m_out.Resize(at + size);
                if (size != 0)
                {
                    std::memcpy(m_out.Data() + at, data, size);
                }
            }

        private:
            Array<std::byte>& m_out;
        };

        class Reader
        {
        public:
            Reader(const std::byte* data, std::size_t size) : m_data(data), m_size(size)
            {
            }
            Bool U32(std::uint32_t& value)
            {
                return Bytes(&value, sizeof(value));
            }
            Bool F32(float& value)
            {
                return Bytes(&value, sizeof(value));
            }
            Bool Text(String& text)
            {
                std::uint32_t length = 0;
                if (false == U32(length) || length > m_size - m_at)
                {
                    return false;
                }
                text.assign(reinterpret_cast<const char*>(m_data + m_at), length);
                m_at += length;
                return true;
            }
            Bool Blob(Array<std::byte>& bytes)
            {
                std::uint32_t length = 0;
                if (false == U32(length) || length > m_size - m_at)
                {
                    return false;
                }
                bytes.Resize(length);
                if (length != 0)
                {
                    std::memcpy(bytes.Data(), m_data + m_at, length);
                }
                m_at += length;
                return true;
            }
            Bool Bytes(void* out, std::size_t size)
            {
                if (size > m_size - m_at)
                {
                    return false;
                }
                std::memcpy(out, m_data + m_at, size);
                m_at += size;
                return true;
            }
            Bool AtEnd() const
            {
                return m_at == m_size;
            }

        private:
            const std::byte* m_data = nullptr;
            std::size_t m_size = 0;
            std::size_t m_at = 0;
        };
    }

    UInt32 GetMaterialParameterComponents(MaterialParameterType type) noexcept
    {
        switch (type)
        {
        case MaterialParameterType::Float2:
            return 2;
        case MaterialParameterType::Float3:
            return 3;
        case MaterialParameterType::Float4:
            return 4;
        default:
            return 1;
        }
    }

    const MaterialParameterInfo* CompiledMaterialShader::FindParameter(std::string_view name) const noexcept
    {
        for (const MaterialParameterInfo& parameter : parameters)
        {
            if (parameter.name.View() == name)
            {
                return &parameter;
            }
        }
        return nullptr;
    }

    const MaterialTextureInfo* CompiledMaterialShader::FindTexture(std::string_view name) const noexcept
    {
        for (const MaterialTextureInfo& texture : textures)
        {
            if (texture.name.View() == name)
            {
                return &texture;
            }
        }
        return nullptr;
    }

    const CompiledMaterialShader* CompiledShaderSet::Find(MaterialDomain domain) const noexcept
    {
        for (const CompiledMaterialShader& variant : variants)
        {
            if (variant.domain == domain)
            {
                return &variant;
            }
        }
        return nullptr;
    }

    void WriteCompiledShaderSet(const CompiledShaderSet& set, Array<std::byte>& out)
    {
        out.Clear();
        Writer writer(out);
        writer.U32(Magic);
        writer.U32(Version);
        writer.U32(static_cast<std::uint32_t>(set.variants.Size()));
        for (const CompiledMaterialShader& variant : set.variants)
        {
            writer.U32(static_cast<std::uint32_t>(variant.domain));
            for (UInt32 format = 0; format < ShaderCodeFormatCount; ++format)
            {
                writer.Blob(variant.pixel[format.Get()]);
            }
            writer.U32(variant.parameterBytes.Get());
            writer.U32(static_cast<std::uint32_t>(variant.parameters.Size()));
            for (const MaterialParameterInfo& parameter : variant.parameters)
            {
                writer.Text(parameter.name);
                writer.U32(static_cast<std::uint32_t>(parameter.type));
                writer.U32(parameter.offset.Get());
                for (const Float value : parameter.defaults)
                {
                    writer.F32(value.Get());
                }
                writer.U32(parameter.color ? 1u : 0u);
            }
            writer.U32(static_cast<std::uint32_t>(variant.textures.Size()));
            for (const MaterialTextureInfo& texture : variant.textures)
            {
                writer.Text(texture.name);
                writer.U32(texture.slot.Get());
            }
        }
    }

    Bool ReadCompiledShaderSet(const std::byte* data, std::size_t size, CompiledShaderSet& set)
    {
        set.variants.Clear();
        Reader reader(data, size);
        std::uint32_t magic = 0;
        std::uint32_t version = 0;
        std::uint32_t count = 0;
        if (false == reader.U32(magic) || magic != Magic || false == reader.U32(version) || version != Version || false == reader.U32(count)
            || count > MaterialDomainCount)
        {
            return false;
        }
        for (std::uint32_t at = 0; at < count; ++at)
        {
            CompiledMaterialShader variant;
            std::uint32_t domain = 0;
            if (false == reader.U32(domain) || domain >= MaterialDomainCount.Get())
            {
                return false;
            }
            variant.domain = static_cast<MaterialDomain>(domain);
            for (UInt32 format = 0; format < ShaderCodeFormatCount; ++format)
            {
                if (false == reader.Blob(variant.pixel[format.Get()]))
                {
                    return false;
                }
            }
            std::uint32_t parameterBytes = 0;
            std::uint32_t parameterCount = 0;
            if (false == reader.U32(parameterBytes) || parameterBytes > MaxMaterialParameterBytes.Get() || false == reader.U32(parameterCount)
                || parameterCount > MaxMaterialParameterBytes.Get() / 4)
            {
                return false;
            }
            variant.parameterBytes = parameterBytes;
            for (std::uint32_t index = 0; index < parameterCount; ++index)
            {
                MaterialParameterInfo parameter;
                std::uint32_t type = 0;
                std::uint32_t offset = 0;
                std::uint32_t color = 0;
                if (false == reader.Text(parameter.name) || false == reader.U32(type) || type > static_cast<std::uint32_t>(MaterialParameterType::Bool)
                    || false == reader.U32(offset))
                {
                    return false;
                }
                parameter.type = static_cast<MaterialParameterType>(type);
                parameter.offset = offset;
                if (offset + GetMaterialParameterComponents(parameter.type).Get() * 4 > parameterBytes)
                {
                    return false;
                }
                for (Float& value : parameter.defaults)
                {
                    float read = 0.0f;
                    if (false == reader.F32(read))
                    {
                        return false;
                    }
                    value = read;
                }
                if (false == reader.U32(color))
                {
                    return false;
                }
                parameter.color = color != 0;
                variant.parameters.Add(std::move(parameter));
            }
            std::uint32_t textureCount = 0;
            if (false == reader.U32(textureCount) || textureCount > MaxMaterialTextures.Get())
            {
                return false;
            }
            for (std::uint32_t index = 0; index < textureCount; ++index)
            {
                MaterialTextureInfo texture;
                std::uint32_t slot = 0;
                if (false == reader.Text(texture.name) || false == reader.U32(slot) || slot == 0 || slot > MaxMaterialTextures.Get())
                {
                    return false;
                }
                texture.slot = slot;
                variant.textures.Add(std::move(texture));
            }
            set.variants.Add(std::move(variant));
        }
        return reader.AtEnd();
    }

    const char* GetMaterialEntryPoint(MaterialDomain domain) noexcept
    {
        return domain == MaterialDomain::Sprite ? "JBroSpriteMain" : "PSMain";
    }

    const char* GetMaterialEpilogue(MaterialDomain domain) noexcept
    {
        return domain == MaterialDomain::Sprite ? SpriteEpilogue : "";
    }

    const char* GetMaterialPrelude() noexcept
    {
        return Prelude;
    }

    const char* GetMaterialShaderTemplate() noexcept
    {
        return Template;
    }
}
