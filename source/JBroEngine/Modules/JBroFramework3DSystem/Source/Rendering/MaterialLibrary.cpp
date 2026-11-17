#include <JBro/Framework3DSystem/Rendering/MaterialLibrary.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Graphics/Renderer.h>

namespace JBro
{
    namespace
    {
        // 풀의 슬롯 번호다(위 8 비트는 타입, D-322 (2)).
        UInt32 SlotOf(AssetHandle handle)
        {
            return handle.index & ((1u << 24) - 1);
        }
    }

    Bool MaterialLibrary::Initialize(Renderer* renderer, AssetSystem* assets)
    {
        m_renderer = renderer;
        m_assets = assets;
        m_materials.Clear();
        m_textures.Clear();
        m_held.Clear();
        return true;
    }

    void MaterialLibrary::Shutdown()
    {
        for (std::size_t index = 0; index < m_materials.Size(); ++index)
        {
            ReleaseEntry(m_materials[index]);
        }
        if (m_assets != nullptr)
        {
            for (std::size_t index = 0; index < m_held.Size(); ++index)
            {
                m_assets->Release(m_held[index].asset);
            }
        }
        m_materials.Clear();
        m_textures.Clear();
        m_held.Clear();
        m_renderer = nullptr;
        m_assets = nullptr;
    }

    Int32 MaterialLibrary::AcquireTexture(AssetId id, Bool srgbByDefault)
    {
        if (id.IsNull() || m_assets == nullptr)
        {
            return -1;
        }
        // 그림의 옵션이 색 공간을 정한다. `Auto` 면 칸의 기본이다.
        const AssetHandle asset = m_assets->Load(id);
        const TextureData* texture = m_assets->GetTexture(asset);
        if (texture == nullptr || texture->width == 0 || texture->height == 0)
        {
            m_assets->Release(asset);
            return -1;
        }
        const Bool srgb = texture->options.colorSpace == TextureColorSpace::Srgb
            || (texture->options.colorSpace == TextureColorSpace::Auto && srgbByDefault);
        for (std::size_t index = 0; index < m_textures.Size(); ++index)
        {
            TextureEntry& entry = m_textures[index];
            if (entry.users > 0 && entry.id == id && entry.srgb == srgb)
            {
                // 이미 올린 것을 나눠 쓴다. 방금 잡은 참조는 놓는다 - 그 항목이 이미 하나를 들고 있다.
                m_assets->Release(asset);
                // 그림이 다시 읽혔으면(다른 재질이 먼저 다시 올리러 왔다) 픽셀을 갈아 끼운다. 크기가 바뀌었으면 새로 올린다.
                if (entry.pixelGeneration != texture->pixelGeneration)
                {
                    const JArrayView<std::byte> pixels{texture->pixels.Data(), static_cast<UInt32>(texture->pixels.Size())};
                    if (false == m_renderer->UpdateTexture(entry.rendererTexture, pixels))
                    {
                        TextureUploadOptions upload;
                        upload.srgb = srgb;
                        upload.mipmaps = texture->options.mipmaps;
                        const AssetHandle replaced = m_renderer->RegisterTexture(Extent2D{texture->width, texture->height}, pixels, upload);
                        if (replaced.generation == 0)
                        {
                            return -1;
                        }
                        m_renderer->UnregisterTexture(entry.rendererTexture);
                        entry.rendererTexture = replaced;
                    }
                    entry.pixelGeneration = texture->pixelGeneration;
                }
                ++entry.users;
                return static_cast<Int32>(index);
            }
        }
        TextureUploadOptions upload;
        upload.srgb = srgb;
        upload.mipmaps = texture->options.mipmaps;
        const AssetHandle uploaded = m_renderer->RegisterTexture(Extent2D{texture->width, texture->height},
            {texture->pixels.Data(), static_cast<UInt32>(texture->pixels.Size())}, upload);
        if (uploaded.generation == 0)
        {
            m_assets->Release(asset);
            return -1;
        }
        TextureEntry entry;
        entry.id = id;
        entry.srgb = srgb;
        entry.asset = asset;
        entry.rendererTexture = uploaded;
        entry.pixelGeneration = texture->pixelGeneration;
        entry.users = 1;
        for (std::size_t index = 0; index < m_textures.Size(); ++index)
        {
            if (m_textures[index].users == 0)
            {
                m_textures[index] = entry;
                return static_cast<Int32>(index);
            }
        }
        m_textures.Add(entry);
        return static_cast<Int32>(m_textures.Size() - 1);
    }

    void MaterialLibrary::ReleaseTexture(Int32 index)
    {
        if (index < 0 || static_cast<std::size_t>(index.Get()) >= m_textures.Size())
        {
            return;
        }
        TextureEntry& entry = m_textures[static_cast<std::size_t>(index.Get())];
        if (entry.users == 0 || --entry.users > 0)
        {
            return;
        }
        if (m_renderer != nullptr && m_renderer->IsInitialized())
        {
            m_renderer->UnregisterTexture(entry.rendererTexture);
        }
        if (m_assets != nullptr)
        {
            m_assets->Release(entry.asset);
        }
        entry = TextureEntry{};
    }

    void MaterialLibrary::ReleaseEntry(MaterialEntry& entry)
    {
        if (entry.rendererMaterial.generation != 0 && m_renderer != nullptr && m_renderer->IsInitialized())
        {
            m_renderer->UnregisterMaterial(entry.rendererMaterial);
        }
        entry.rendererMaterial = {};
        for (UInt32 slot = 0; slot < SlotCount; ++slot)
        {
            ReleaseTexture(entry.textures[slot.Get()]);
            entry.textures[slot.Get()] = -1;
            entry.texturePixels[slot.Get()] = 0;
        }
    }

    Bool MaterialLibrary::TexturesChanged(const MaterialEntry& entry) const
    {
        for (UInt32 slot = 0; slot < SlotCount; ++slot)
        {
            const Int32 index = entry.textures[slot.Get()];
            if (index < 0)
            {
                continue;
            }
            const TextureData* texture = m_assets->GetTexture(m_textures[static_cast<std::size_t>(index.Get())].asset);
            if (texture == nullptr || texture->pixelGeneration != entry.texturePixels[slot.Get()])
            {
                return true;
            }
        }
        return false;
    }

    AssetHandle MaterialLibrary::ResolveAsset(AssetHandle materialAsset)
    {
        if (m_assets == nullptr || m_renderer == nullptr || false == m_renderer->IsInitialized() || materialAsset.generation == 0)
        {
            return {};
        }
        const MaterialData* material = m_assets->GetMaterial(materialAsset);
        if (material == nullptr)
        {
            return {};
        }
        const UInt32 slot = SlotOf(materialAsset);
        if (slot >= m_materials.Size())
        {
            m_materials.Resize(static_cast<std::size_t>(slot) + 1);
        }
        MaterialEntry& entry = m_materials[slot.Get()];
        const Bool sameAsset = entry.asset.index == materialAsset.index && entry.asset.generation == materialAsset.generation;
        if (sameAsset && entry.rendererMaterial.generation != 0 && entry.dataGeneration == material->dataGeneration
            && false == TexturesChanged(entry))
        {
            return entry.rendererMaterial;
        }
        if (sameAsset && entry.rendererMaterial.generation == 0 && entry.failedGeneration == material->dataGeneration)
        {
            return {};
        }
        // 처음이거나, 슬롯을 다른 재질이 다시 썼거나, 재질·그림이 다시 읽혔다. 옛것을 놓고 새로 올린다.
        ReleaseEntry(entry);
        entry.asset = materialAsset;
        const MaterialDesc& source = material->desc;
        const AssetId ids[SlotCount] = {source.baseColorTexture, source.metallicRoughnessTexture, source.normalTexture,
            source.occlusionTexture, source.emissiveTexture};
        const Bool srgbByDefault[SlotCount] = {true, false, false, false, true};
        AssetHandle handles[SlotCount];
        for (UInt32 at = 0; at < SlotCount; ++at)
        {
            entry.textures[at.Get()] = AcquireTexture(ids[at.Get()], srgbByDefault[at.Get()]);
            if (entry.textures[at.Get()] >= 0)
            {
                const TextureEntry& texture = m_textures[static_cast<std::size_t>(entry.textures[at.Get()].Get())];
                handles[at.Get()] = texture.rendererTexture;
                const TextureData* data = m_assets->GetTexture(texture.asset);
                entry.texturePixels[at.Get()] = data != nullptr ? data->pixelGeneration : UInt32(0);
            }
        }
        MeshMaterialDesc desc;
        desc.shading = source.shader == MaterialShader::Unlit ? MeshShading::Unlit : MeshShading::Standard;
        for (std::size_t channel = 0; channel < 4; ++channel)
        {
            desc.baseColor[channel] = source.baseColor[channel];
        }
        desc.emissive[0] = source.emissive.R;
        desc.emissive[1] = source.emissive.G;
        desc.emissive[2] = source.emissive.B;
        desc.metallic = source.metallic;
        desc.roughness = source.roughness;
        desc.cutout = source.alpha == MaterialAlpha::Cutout;
        desc.alphaCutoff = source.alphaCutoff;
        // 기본색 그림이 가장 가까운 텍셀(픽셀 아트)이면 재질이 그렇게 읽는다. 칸마다 샘플러를 두지 않는다 - 셰이더의 샘플러는 하나다.
        const TextureData* base = entry.textures[0] >= 0
            ? m_assets->GetTexture(m_textures[static_cast<std::size_t>(entry.textures[0].Get())].asset) : nullptr;
        desc.nearestFilter = base != nullptr && base->filter == TextureFilter::Nearest;
        desc.baseColorTexture = handles[0];
        desc.metallicRoughnessTexture = handles[1];
        desc.normalTexture = handles[2];
        desc.occlusionTexture = handles[3];
        desc.emissiveTexture = handles[4];
        entry.rendererMaterial = m_renderer->RegisterMaterial(desc);
        if (entry.rendererMaterial.generation == 0)
        {
            entry.failedGeneration = material->dataGeneration;
            ReleaseEntry(entry);
            return {};
        }
        entry.dataGeneration = material->dataGeneration;
        return entry.rendererMaterial;
    }

    AssetHandle MaterialLibrary::ResolveMeshMaterial(AssetHandle meshAsset)
    {
        if (m_assets == nullptr || meshAsset.generation == 0)
        {
            return {};
        }
        const MeshData* mesh = m_assets->GetMesh(meshAsset);
        if (mesh == nullptr || mesh->material.IsNull())
        {
            return {};
        }
        // 한 번 실은 것은 들고 있는다 - 프레임마다 싣고 놓지 않는다. 메시 수만큼이라 짧다.
        for (std::size_t index = 0; index < m_held.Size(); ++index)
        {
            if (m_held[index].id == mesh->material)
            {
                return ResolveAsset(m_held[index].asset);
            }
        }
        const AssetHandle loaded = m_assets->Load(mesh->material);
        if (loaded.generation == 0)
        {
            return {};
        }
        m_held.Add(HeldMaterial{mesh->material, loaded});
        return ResolveAsset(loaded);
    }

    std::size_t MaterialLibrary::GetUploadedMaterialCount() const
    {
        std::size_t count = 0;
        for (std::size_t index = 0; index < m_materials.Size(); ++index)
        {
            count += m_materials[index].rendererMaterial.generation != 0 ? 1 : 0;
        }
        return count;
    }

    std::size_t MaterialLibrary::GetUploadedTextureCount() const
    {
        std::size_t count = 0;
        for (std::size_t index = 0; index < m_textures.Size(); ++index)
        {
            count += m_textures[index].users > 0 ? 1 : 0;
        }
        return count;
    }
}
