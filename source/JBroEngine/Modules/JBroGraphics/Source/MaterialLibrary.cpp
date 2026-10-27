#include <JBro/Graphics/MaterialLibrary.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Core/Log.h>
#include <JBro/Graphics/Renderer.h>

#include <cstdint>
#include <cstring>

namespace JBro
{
    namespace
    {
        Bool SameHandle(AssetHandle a, AssetHandle b)
        {
            return a.index == b.index && a.generation == b.generation;
        }

        void WriteComponent(std::byte* at, MaterialParameterType type, Float value)
        {
            if (type == MaterialParameterType::Int)
            {
                const std::int32_t integer = static_cast<std::int32_t>(value.Get() < 0.0f ? value.Get() - 0.5f : value.Get() + 0.5f);
                std::memcpy(at, &integer, 4);
                return;
            }
            if (type == MaterialParameterType::UInt || type == MaterialParameterType::Bool)
            {
                const std::uint32_t integer = value.Get() <= 0.0f ? 0u
                    : type == MaterialParameterType::Bool ? 1u : static_cast<std::uint32_t>(value.Get() + 0.5f);
                std::memcpy(at, &integer, 4);
                return;
            }
            const float number = value.Get();
            std::memcpy(at, &number, 4);
        }
    }

    MaterialLibrary::~MaterialLibrary()
    {
        Shutdown();
    }

    void MaterialLibrary::Initialize(AssetSystem* assets, Renderer* renderer, IShaderCompiler* compiler)
    {
        Shutdown();
        m_assets = assets;
        m_renderer = renderer;
        m_compiler = compiler;
    }

    void MaterialLibrary::Shutdown()
    {
        for (Material& material : m_materials)
        {
            ReleaseAssets(material);
            if (m_renderer != nullptr && material.renderer.generation != 0)
            {
                m_renderer->UnregisterMaterial(material.renderer);
            }
        }
        m_materials = {};
        if (m_renderer != nullptr)
        {
            for (const TextureSlot& texture : m_textures)
            {
                if (texture.texture.generation != 0)
                {
                    m_renderer->UnregisterTexture(texture.texture);
                }
            }
        }
        m_textures = {};
        m_compiled = {};
        m_assets = nullptr;
        m_renderer = nullptr;
        m_compiler = nullptr;
    }

    UInt32 MaterialLibrary::GetMaterialCount() const noexcept
    {
        UInt32 count = 0;
        for (const Material& material : m_materials)
        {
            count += material.renderer.generation != 0 ? 1u : 0u;
        }
        return count;
    }

    void MaterialLibrary::BuildParameterBlock(const CompiledMaterialShader& shader, const MaterialDocument& document, Array<std::byte>& block)
    {
        block.Resize(shader.parameterBytes);
        if (block.IsEmpty())
        {
            return;
        }
        std::memset(block.Data(), 0, block.Size());
        for (const MaterialParameterInfo& parameter : shader.parameters)
        {
            const UInt32 components = GetMaterialParameterComponents(parameter.type);
            if (parameter.offset + components * 4 > shader.parameterBytes)
            {
                continue;
            }
            const MaterialParameterValue* value = document.FindParameter(parameter.name.View());
            for (UInt32 at = 0; at < components; ++at)
            {
                const Float number = value != nullptr ? (at < value->count ? value->values[at.Get()] : Float(0.0f)) : parameter.defaults[at.Get()];
                WriteComponent(block.Data() + parameter.offset.Get() + at.Get() * 4, parameter.type, number);
            }
        }
    }

    MaterialLibrary::Compiled* MaterialLibrary::Compile(AssetHandle shaderAsset, MaterialDomain domain)
    {
        const ShaderAssetData* data = m_assets != nullptr ? m_assets->GetShader(shaderAsset) : nullptr;
        if (data == nullptr)
        {
            return nullptr;
        }
        Compiled* slot = nullptr;
        for (const OwnerPtr<Compiled>& compiled : m_compiled)
        {
            if (SameHandle(compiled->shader, shaderAsset) && compiled->domain == domain)
            {
                slot = compiled.Get();
                break;
            }
        }
        if (slot != nullptr && slot->dataGeneration == data->dataGeneration)
        {
            return slot;
        }
        if (slot == nullptr)
        {
            // 셰이더 에셋은 머티리얼(과 인스펙터)이 잡는다. 여기는 핸들과 세대만 기억한다 - 놓여 내려간 셰이더의 칸은 세대가 달라 맞지 않는다.
            m_compiled.Add(MakeOwnerPtr<Compiled>());
            slot = m_compiled[m_compiled.Size() - 1].Get();
            slot->shader = shaderAsset;
            slot->domain = domain;
        }
        slot->dataGeneration = data->dataGeneration;
        slot->result = {};
        slot->error.clear();
        slot->ok = false;
        if (data->hasCompiled)
        {
            // 패키지가 구운 묶음이다(게임).
            const CompiledMaterialShader* variant = data->compiled.Find(domain);
            if (variant != nullptr)
            {
                slot->result = *variant;
                slot->ok = true;
            }
            else
            {
                slot->error = domain == MaterialDomain::Sprite ? "the package has no Sprite build of this shader"
                                                               : "the package has no PostProcess build of this shader";
            }
            return slot;
        }
        if (m_compiler == nullptr)
        {
            slot->error = "no shader compiler: a game loads only shaders the build compiled";
            return slot;
        }
        ++m_compileCount;
        slot->ok = m_compiler->Compile(data->source.View(), data->path.empty() ? "material.hlsl" : data->path.c_str(), domain, slot->result,
            slot->error);
        return slot;
    }

    AssetHandle MaterialLibrary::EnsureTexture(AssetHandle textureAsset)
    {
        const TextureData* data = m_assets->GetTexture(textureAsset);
        if (data == nullptr || m_renderer == nullptr || data->pixels.IsEmpty())
        {
            return {};
        }
        TextureSlot* slot = nullptr;
        for (TextureSlot& texture : m_textures)
        {
            if (SameHandle(texture.asset, textureAsset))
            {
                slot = &texture;
                break;
            }
        }
        if (slot != nullptr && slot->pixelGeneration == data->pixelGeneration && slot->texture.generation != 0)
        {
            return slot->texture;
        }
        if (slot == nullptr)
        {
            m_textures.Add(TextureSlot{textureAsset, 0, {}});
            slot = &m_textures[m_textures.Size() - 1];
        }
        const JArrayView<std::byte> pixels = {data->pixels.Data(), static_cast<JBro::UInt32>(data->pixels.Size())};
        if (slot->texture.generation != 0 && m_renderer->UpdateTexture(slot->texture, pixels))
        {
            slot->pixelGeneration = data->pixelGeneration;
            return slot->texture;
        }
        if (slot->texture.generation != 0)
        {
            m_renderer->UnregisterTexture(slot->texture);
        }
        slot->texture = m_renderer->RegisterTexture({data->width, data->height}, pixels);
        slot->pixelGeneration = data->pixelGeneration;
        return slot->texture;
    }

    void MaterialLibrary::ReleaseAssets(Material& material)
    {
        if (m_assets != nullptr)
        {
            if (material.shader.generation != 0)
            {
                m_assets->Release(material.shader);
            }
            m_assets->ReleaseAll(material.textures);
        }
        material.shader = {};
        material.textures.Clear();
        material.textureGenerations.Clear();
    }

    Bool MaterialLibrary::IsStale(const Material& material) const
    {
        const MaterialAssetData* data = m_assets->GetMaterial(material.asset);
        if (data == nullptr || data->dataGeneration != material.dataGeneration)
        {
            return true;
        }
        if (material.shader.generation != 0)
        {
            const ShaderAssetData* shader = m_assets->GetShader(material.shader);
            if (shader == nullptr || shader->dataGeneration != material.shaderGeneration)
            {
                return true;
            }
        }
        for (std::size_t at = 0; at < material.textures.Size(); ++at)
        {
            const TextureData* texture = m_assets->GetTexture(material.textures[at]);
            if (texture == nullptr || texture->pixelGeneration != material.textureGenerations[at])
            {
                return true;
            }
        }
        return false;
    }

    void MaterialLibrary::Build(Material& material)
    {
        ++m_buildCount;
        ReleaseAssets(material);
        material.error.clear();
        material.hasError = true;
        const auto fail = [&](const char* why) {
            material.error = why;
            if (material.renderer.generation != 0)
            {
                m_renderer->UnregisterMaterial(material.renderer);
                material.renderer = {};
            }
        };
        const MaterialAssetData* data = m_assets->GetMaterial(material.asset);
        if (data == nullptr)
        {
            material.dataGeneration = 0;
            fail("the material is not loaded");
            return;
        }
        material.dataGeneration = data->dataGeneration;
        MaterialDocument document;
        String error;
        Array<String> warnings;
        if (false == ParseMaterial(data->text.data(), data->text.size(), document, &error, &warnings))
        {
            Log::Write(LogLevel::Warning, "material", "a material could not be read: %s", error.c_str());
            fail(error.c_str());
            return;
        }
        for (const String& warning : warnings)
        {
            Log::Write(LogLevel::Warning, "material", "%s", warning.c_str());
        }
        if (document.shader.IsNull())
        {
            fail("the material names no shader");
            return;
        }
        material.shader = m_assets->Load(document.shader);
        const ShaderAssetData* shader = m_assets->GetShader(material.shader);
        if (shader == nullptr)
        {
            if (material.shader.generation != 0)
            {
                m_assets->Release(material.shader);
                material.shader = {};
            }
            fail("the material's shader could not be loaded");
            return;
        }
        material.shaderGeneration = shader->dataGeneration;
        material.domain = document.domain;
        Compiled* compiled = Compile(material.shader, document.domain);
        if (compiled == nullptr || false == compiled->ok)
        {
            const String why = compiled != nullptr ? compiled->error : String("the shader could not be compiled");
            Log::Write(LogLevel::Warning, "material", "a material shader could not be compiled: %s", why.c_str());
            fail(why.c_str());
            return;
        }
        MaterialDesc desc;
        desc.shader = &compiled->result;
        Array<std::byte> block;
        BuildParameterBlock(compiled->result, document, block);
        desc.parameters = {block.Data(), static_cast<JBro::UInt32>(block.Size())};
        for (const MaterialTextureInfo& slot : compiled->result.textures)
        {
            const MaterialTextureValue* value = document.FindTexture(slot.name.View());
            if (value == nullptr || slot.slot == 0 || slot.slot > MaxMaterialTextures)
            {
                continue;
            }
            const AssetHandle texture = m_assets->Load(value->texture);
            if (texture.generation == 0)
            {
                continue;
            }
            material.textures.Add(texture);
            const TextureData* pixels = m_assets->GetTexture(texture);
            material.textureGenerations.Add(pixels != nullptr ? pixels->pixelGeneration : UInt32(0));
            desc.textures[(slot.slot - 1).Get()] = EnsureTexture(texture);
        }
        const Bool sameShader = material.renderer.generation != 0 && SameHandle(material.registeredShader, material.shader)
            && material.registeredGeneration == material.shaderGeneration && material.registeredDomain == material.domain;
        if (sameShader && m_renderer->UpdateMaterial(material.renderer, desc))
        {
            material.hasError = false;
            return;
        }
        if (material.renderer.generation != 0)
        {
            m_renderer->UnregisterMaterial(material.renderer);
        }
        material.renderer = m_renderer->RegisterMaterial(desc);
        if (material.renderer.generation == 0)
        {
            fail("the renderer could not create the material (no code for this graphics API?)");
            return;
        }
        material.registeredShader = material.shader;
        material.registeredGeneration = material.shaderGeneration;
        material.registeredDomain = material.domain;
        material.hasError = false;
    }

    AssetHandle MaterialLibrary::Resolve(AssetHandle materialAsset)
    {
        if (m_assets == nullptr || m_renderer == nullptr || materialAsset.generation == 0)
        {
            return {};
        }
        for (const Material& material : m_materials)
        {
            if (SameHandle(material.asset, materialAsset))
            {
                return material.renderer;
            }
        }
        if (m_assets->GetMaterial(materialAsset) == nullptr)
        {
            return {};
        }
        Material& added = m_materials.Add(Material{});
        added.asset = materialAsset;
        Build(added);
        return added.renderer;
    }

    void MaterialLibrary::Update()
    {
        if (m_assets == nullptr || m_renderer == nullptr)
        {
            return;
        }
        for (Material& material : m_materials)
        {
            if (IsStale(material))
            {
                Build(material);
            }
        }
    }

    const String* MaterialLibrary::GetError(AssetHandle materialAsset) const
    {
        for (const Material& material : m_materials)
        {
            if (SameHandle(material.asset, materialAsset))
            {
                return material.hasError ? &material.error : nullptr;
            }
        }
        return nullptr;
    }

    const CompiledMaterialShader* MaterialLibrary::GetCompiledShader(AssetHandle shaderAsset, MaterialDomain domain, const String** error)
    {
        Compiled* compiled = Compile(shaderAsset, domain);
        if (error != nullptr)
        {
            *error = compiled != nullptr && false == compiled->ok ? &compiled->error : nullptr;
        }
        return compiled != nullptr && compiled->ok ? &compiled->result : nullptr;
    }
}
