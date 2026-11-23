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
        if (m_renderer != nullptr)
        {
            for (const Instance& instance : m_instances)
            {
                if (instance.renderer.generation != 0)
                {
                    m_renderer->UnregisterMaterial(instance.renderer);
                }
            }
        }
        m_instances = {};
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
                const EnumNames& names = *TypeDescriptorOf<MaterialDomain>::Get().enumNames;
                slot->error = "the package has no ";
                slot->error.append(static_cast<std::uint32_t>(domain) < names.count ? names.names[static_cast<std::size_t>(domain)] : "?");
                slot->error.append(" build of this shader");
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
        const MaterialData* data = m_assets->GetMaterial(material.asset);
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
            // 아직 디코드 중인 그림은 기다린다(D-455) - 서면 세대가 올라 그때 다시 짓는다. 기다리는 동안 프레임마다 다시 짓지 않는다.
            if (m_assets->IsPending(material.textures[at]))
            {
                continue;
            }
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
        material.compiled = nullptr;
        const auto fail = [&](const char* why) {
            material.error = why;
            if (material.renderer.generation != 0)
            {
                m_renderer->UnregisterMaterial(material.renderer);
                material.renderer = {};
            }
        };
        const MaterialData* data = m_assets->GetMaterial(material.asset);
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
        ShaderMaterialDesc desc;
        desc.shader = &compiled->result;
        BuildParameterBlock(compiled->result, document, material.baseBlock);
        // 덮어쓰기와 볼륨 섞기가 쓸 것을 지을 때 한 번 떠 둔다 - 이름은 번호로, 칸은 종류로.
        material.compiled = compiled;
        material.shaderId = document.shader;
        material.parameterNames.Clear();
        material.cellKinds.Resize(material.baseBlock.Size() / 4);
        for (std::size_t cell = 0; cell < material.cellKinds.Size(); ++cell)
        {
            material.cellKinds[cell] = static_cast<std::uint8_t>(MaterialCellKind::Unused);
        }
        for (const MaterialParameterInfo& parameter : compiled->result.parameters)
        {
            material.parameterNames.Add(MakeNameId(parameter.name.c_str()));
            const UInt32 components = GetMaterialParameterComponents(parameter.type);
            const MaterialCellKind kind = parameter.type == MaterialParameterType::Int ? MaterialCellKind::Int
                : parameter.type == MaterialParameterType::UInt                         ? MaterialCellKind::UInt
                : parameter.type == MaterialParameterType::Bool                         ? MaterialCellKind::Bool
                                                                                        : MaterialCellKind::Float;
            for (UInt32 at = 0; at < components; ++at)
            {
                const std::size_t cell = (parameter.offset.Get() / 4) + at.Get();
                if (cell < material.cellKinds.Size())
                {
                    material.cellKinds[cell] = static_cast<std::uint8_t>(kind);
                }
            }
        }
        ApplyOverrides(material);
        desc.parameters = {material.liveBlock.Data(), static_cast<JBro::UInt32>(material.liveBlock.Size())};
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
        for (UInt32 at = 0; at < MaxMaterialTextures; ++at)
        {
            material.rendererTextures[at.Get()] = desc.textures[at.Get()];
        }
        ++m_parameterStamp;
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

    void MaterialLibrary::ApplyOverrides(Material& material)
    {
        material.liveBlock.Resize(material.baseBlock.Size());
        if (false == material.baseBlock.IsEmpty())
        {
            std::memcpy(material.liveBlock.Data(), material.baseBlock.Data(), material.baseBlock.Size());
        }
        material.appliedOverrideStamp = m_overrideStamp;
        if (material.compiled == nullptr || m_assets == nullptr)
        {
            return;
        }
        LayOverrides(material, InvalidInstanceId, material.liveBlock);
    }

    void MaterialLibrary::LayOverrides(const Material& material, InstanceId owner, Array<std::byte>& block) const
    {
        const Array<MaterialParameterInfo>& parameters = material.compiled->result.parameters;
        for (UInt32 at = 0; at < m_overrideCount; ++at)
        {
            const ParameterOverride& entry = m_overrides[at.Get()];
            if (false == entry.set || entry.owner != owner || false == SameHandle(m_assets->Find(entry.material), material.asset))
            {
                continue;
            }
            for (std::size_t index = 0; index < parameters.Size() && index < material.parameterNames.Size(); ++index)
            {
                if (material.parameterNames[index] != entry.name)
                {
                    continue;
                }
                const MaterialParameterInfo& parameter = parameters[index];
                const UInt32 components = GetMaterialParameterComponents(parameter.type);
                const Bool integer = parameter.type == MaterialParameterType::Int || parameter.type == MaterialParameterType::UInt
                    || parameter.type == MaterialParameterType::Bool;
                const Bool fits = entry.count == components || (parameter.type == MaterialParameterType::Float3 && entry.count == 4);
                if (integer != entry.integer || false == fits || parameter.offset + components * 4 > block.Size())
                {
                    break;
                }
                for (UInt32 component = 0; component < components; ++component)
                {
                    WriteComponent(block.Data() + parameter.offset.Get() + component.Get() * 4, parameter.type,
                        entry.values[component.Get()]);
                }
                break;
            }
        }
    }

    void MaterialLibrary::Refresh(Material& material)
    {
        ApplyOverrides(material);
        if (material.compiled == nullptr || material.renderer.generation == 0)
        {
            return;
        }
        ShaderMaterialDesc desc;
        desc.shader = &material.compiled->result;
        desc.parameters = {material.liveBlock.Data(), static_cast<JBro::UInt32>(material.liveBlock.Size())};
        for (UInt32 at = 0; at < MaxMaterialTextures; ++at)
        {
            desc.textures[at.Get()] = material.rendererTextures[at.Get()];
        }
        m_renderer->UpdateMaterial(material.renderer, desc);
        ++m_parameterStamp;
    }

    void MaterialLibrary::SyncInstances()
    {
        if (m_syncedOverrideStamp == m_overrideStamp && m_syncedParameterStamp == m_parameterStamp)
        {
            return;
        }
        m_syncedOverrideStamp = m_overrideStamp;
        m_syncedParameterStamp = m_parameterStamp;
        // 오브젝트의 값이 있는 (주인, 머티리얼)마다 인스턴스를 둔다.
        for (UInt32 at = 0; at < m_overrideCount; ++at)
        {
            const ParameterOverride& entry = m_overrides[at.Get()];
            if (false == entry.set || entry.owner == InvalidInstanceId)
            {
                continue;
            }
            const AssetHandle asset = m_assets->Find(entry.material);
            Bool found = false;
            for (const Instance& instance : m_instances)
            {
                found = found || (instance.owner == entry.owner && SameHandle(instance.asset, asset));
            }
            if (false == found && asset.generation != 0)
            {
                Instance added;
                added.owner = entry.owner;
                added.asset = asset;
                m_instances.Add(std::move(added));
            }
        }
        for (std::size_t index = m_instances.Size(); index > 0; --index)
        {
            Instance& instance = m_instances[index - 1];
            const Material* material = nullptr;
            for (const Material& candidate : m_materials)
            {
                if (SameHandle(candidate.asset, instance.asset))
                {
                    material = &candidate;
                }
            }
            Bool wanted = false;
            for (UInt32 at = 0; at < m_overrideCount; ++at)
            {
                const ParameterOverride& entry = m_overrides[at.Get()];
                wanted = wanted || (entry.set && entry.owner == instance.owner && SameHandle(m_assets->Find(entry.material), instance.asset));
            }
            // 값이 모두 지워졌으면 인스턴스도 없앤다. 다시 한 배치로 그려진다.
            if (false == wanted)
            {
                if (instance.renderer.generation != 0)
                {
                    m_renderer->UnregisterMaterial(instance.renderer);
                }
                m_instances.RemoveAt(index - 1);
                continue;
            }
            // 원본을 아직 못 지었으면(지을 때 실패했거나 아직 쓰이지 않았다) 원본이 설 때 다시 온다.
            if (material == nullptr || material->renderer.generation == 0 || material->compiled == nullptr)
            {
                instance.renderer = {};
                continue;
            }
            instance.block.Resize(material->liveBlock.Size());
            if (false == material->liveBlock.IsEmpty())
            {
                std::memcpy(instance.block.Data(), material->liveBlock.Data(), material->liveBlock.Size());
            }
            LayOverrides(*material, instance.owner, instance.block);
            ShaderMaterialDesc desc;
            desc.shader = &material->compiled->result;
            desc.parameters = {instance.block.Data(), static_cast<JBro::UInt32>(instance.block.Size())};
            for (UInt32 at = 0; at < MaxMaterialTextures; ++at)
            {
                desc.textures[at.Get()] = material->rendererTextures[at.Get()];
            }
            // 원본이 다시 올라가면 렌더러가 옛 인스턴스를 함께 지웠다.
            if (false == SameHandle(instance.base, material->renderer) || instance.renderer.generation == 0)
            {
                instance.renderer = m_renderer->RegisterMaterialInstance(material->renderer, desc);
                instance.base = material->renderer;
            }
            else
            {
                m_renderer->UpdateMaterial(instance.renderer, desc);
            }
        }
    }

    AssetHandle MaterialLibrary::Resolve(AssetHandle materialAsset, InstanceId owner)
    {
        for (const Instance& instance : m_instances)
        {
            if (instance.owner == owner && SameHandle(instance.asset, materialAsset) && instance.renderer.generation != 0)
            {
                return instance.renderer;
            }
        }
        return Resolve(materialAsset);
    }

    UInt32 MaterialLibrary::FindParameter(AssetId material, const char* parameter)
    {
        return FindParameter(material, parameter, InvalidInstanceId);
    }

    UInt32 MaterialLibrary::FindParameter(AssetId material, const char* parameter, InstanceId owner)
    {
        if (material.IsNull() || parameter == nullptr || parameter[0] == '\0')
        {
            return InvalidParameter;
        }
        const NameId name = MakeNameId(parameter);
        for (UInt32 at = 0; at < m_overrideCount; ++at)
        {
            if (m_overrides[at.Get()].material == material && m_overrides[at.Get()].name == name && m_overrides[at.Get()].owner == owner)
            {
                return at;
            }
        }
        if (m_overrideCount >= MaxParameterOverrides)
        {
            return InvalidParameter;
        }
        ParameterOverride& entry = m_overrides[m_overrideCount.Get()];
        entry = {};
        entry.material = material;
        entry.name = name;
        entry.owner = owner;
        return m_overrideCount++;
    }

    Bool MaterialLibrary::SetParameter(UInt32 parameter, const Float* values, UInt32 count, Bool integer)
    {
        if (parameter >= m_overrideCount || values == nullptr || count == 0 || count > 4)
        {
            return false;
        }
        ParameterOverride& entry = m_overrides[parameter.Get()];
        Bool same = entry.set && entry.count == count && entry.integer == integer;
        for (UInt32 at = 0; at < count && same; ++at)
        {
            same = entry.values[at.Get()] == values[at.Get()];
        }
        if (same)
        {
            return true;
        }
        for (UInt32 at = 0; at < 4; ++at)
        {
            entry.values[at.Get()] = at < count ? values[at.Get()] : Float(0.0f);
        }
        entry.count = count;
        entry.integer = integer;
        entry.set = true;
        ++m_overrideStamp;
        return true;
    }

    void MaterialLibrary::ClearParameter(UInt32 parameter)
    {
        if (parameter < m_overrideCount && m_overrides[parameter.Get()].set)
        {
            m_overrides[parameter.Get()].set = false;
            ++m_overrideStamp;
        }
    }

    void MaterialLibrary::ClearParameters()
    {
        Bool any = false;
        for (UInt32 at = 0; at < m_overrideCount; ++at)
        {
            any = any || m_overrides[at.Get()].set;
            m_overrides[at.Get()].set = false;
        }
        if (any)
        {
            ++m_overrideStamp;
        }
    }

    Bool MaterialLibrary::DescribeParameters(AssetHandle materialAsset, AssetId& shader, JArrayView<std::byte>& block,
        JArrayView<std::uint8_t>& kinds) const
    {
        for (const Material& material : m_materials)
        {
            if (SameHandle(material.asset, materialAsset))
            {
                if (material.hasError || material.compiled == nullptr)
                {
                    return false;
                }
                shader = material.shaderId;
                block = {material.liveBlock.Data(), static_cast<JBro::UInt32>(material.liveBlock.Size())};
                kinds = {material.cellKinds.Data(), static_cast<JBro::UInt32>(material.cellKinds.Size())};
                return true;
            }
        }
        return false;
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
        // 표준 머티리얼(D-399)은 3D 프레임워크의 `MeshMaterialLibrary` 가 푼다 - 여기서는 셰이더 머티리얼만이다.
        const MaterialData* asset = m_assets->GetMaterial(materialAsset);
        if (asset == nullptr || asset->standard)
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
            else if (material.appliedOverrideStamp != m_overrideStamp)
            {
                Refresh(material);
            }
        }
        SyncInstances();
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
