#include <JBro/Framework3DSystem/Rendering/MeshMaterialLibrary.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>
#include <JBro/ScreenUi/Rendering/SpriteLibrary.h>

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

    Bool MeshMaterialLibrary::Initialize(Renderer* renderer, AssetSystem* assets)
    {
        m_renderer = renderer;
        m_assets = assets;
        m_materials.Clear();
        m_textures.Clear();
        m_held.Clear();
        return true;
    }

    void MeshMaterialLibrary::Shutdown()
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

    Int32 MeshMaterialLibrary::AcquireTexture(AssetId id, Bool srgbByDefault, AssetHandle& picture)
    {
        picture = {};
        if (id.IsNull() || m_assets == nullptr)
        {
            return -1;
        }
        // 재질은 제 그림을 든다 - 텍스처를 남과 나눠 써도 제 그림이 다시 읽혔는지 본다.
        picture = m_assets->Load(id);
        const TextureData* texture = m_assets->GetTexture(picture);
        if (texture == nullptr || texture->width == 0 || texture->height == 0)
        {
            return -1;
        }
        // 그림의 옵션이 색 공간을 정한다. `Auto` 면 칸의 기본이다.
        const Bool srgb = texture->options.colorSpace == TextureColorSpace::Srgb
            || (texture->options.colorSpace == TextureColorSpace::Auto && srgbByDefault);
        for (std::size_t index = 0; index < m_textures.Size(); ++index)
        {
            TextureEntry& entry = m_textures[index];
            const Bool same = entry.id == id || (texture->sharedImageKey != 0 && entry.sharedImageKey == texture->sharedImageKey);
            if (entry.users == 0 || false == same || entry.srgb != srgb)
            {
                continue;
            }
            // 이미 올린 것을 나눠 쓴다. 그 텍스처의 그림이 다시 읽혔으면(다른 재질이 먼저 다시 올리러 왔다) 픽셀을 갈아 끼운다.
            // 크기가 바뀌었으면 새로 올린다.
            const TextureData* held = m_assets->GetTexture(entry.asset);
            if (held != nullptr && entry.pixelGeneration != held->pixelGeneration)
            {
                const JArrayView<std::byte> pixels{held->pixels.Data(), static_cast<UInt32>(held->pixels.Size())};
                if (false == m_renderer->UpdateTexture(entry.rendererTexture, pixels))
                {
                    TextureUploadOptions upload;
                    upload.srgb = srgb;
                    upload.mipmaps = held->options.mipmaps;
                    const AssetHandle replaced = m_renderer->RegisterTexture(Extent2D{held->width, held->height}, pixels, upload);
                    if (replaced.generation == 0)
                    {
                        return -1;
                    }
                    m_renderer->UnregisterTexture(entry.rendererTexture);
                    entry.rendererTexture = replaced;
                }
                entry.pixelGeneration = held->pixelGeneration;
            }
            ++entry.users;
            return static_cast<Int32>(index);
        }
        TextureUploadOptions upload;
        upload.srgb = srgb;
        upload.mipmaps = texture->options.mipmaps;
        const AssetHandle uploaded = m_renderer->RegisterTexture(Extent2D{texture->width, texture->height},
            {texture->pixels.Data(), static_cast<UInt32>(texture->pixels.Size())}, upload);
        if (uploaded.generation == 0)
        {
            return -1;
        }
        TextureEntry entry;
        entry.id = id;
        entry.sharedImageKey = texture->sharedImageKey;
        entry.srgb = srgb;
        // 텍스처도 제 그림을 하나 잡는다 - 처음 올린 재질이 떠나도 나눠 쓰는 재질이 남으면 텍스처의 그림이 산다.
        entry.asset = m_assets->Load(id);
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

    void MeshMaterialLibrary::SetRenderTargetSource(SpriteLibrary* sprites)
    {
        m_renderTargetSource = sprites;
    }

    Bool MeshMaterialLibrary::AcquireRenderTarget(AssetId id, AssetHandle& picture, AssetHandle& target)
    {
        picture = {};
        target = {};
        if (id.IsNull() || m_assets == nullptr)
        {
            return false;
        }
        const AssetHandle loaded = m_assets->Load(id);
        if (m_assets->GetRenderTexture(loaded) == nullptr)
        {
            m_assets->Release(loaded);
            return false;
        }
        picture = loaded;
        TextureHandle texture;
        Extent2D extent;
        TextureFilter filter = TextureFilter::Linear;
        if (m_renderTargetSource != nullptr)
        {
            m_renderTargetSource->ResolveRenderTexture(loaded, target, texture, extent, filter);
        }
        return true;
    }

    void MeshMaterialLibrary::CollectRenderTargetReads(Array<RenderTargetRead>& reads) const
    {
        reads.Clear();
        for (std::size_t index = 0; index < m_materials.Size(); ++index)
        {
            const MaterialEntry& entry = m_materials[index];
            if (entry.rendererMaterial.generation == 0)
            {
                continue;
            }
            for (UInt32 slot = 0; slot < SlotCount; ++slot)
            {
                if (entry.renderTargets[slot.Get()].generation != 0)
                {
                    reads.Add(RenderTargetRead{entry.rendererMaterial, entry.renderTargets[slot.Get()]});
                }
            }
        }
    }

    void MeshMaterialLibrary::ReleaseTexture(Int32 index)
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

    void MeshMaterialLibrary::ReleaseEntry(MaterialEntry& entry)
    {
        if (entry.rendererMaterial.generation != 0 && m_renderer != nullptr && m_renderer->IsInitialized())
        {
            m_renderer->UnregisterMeshMaterial(entry.rendererMaterial);
        }
        entry.rendererMaterial = {};
        for (UInt32 slot = 0; slot < SlotCount; ++slot)
        {
            ReleaseTexture(entry.textures[slot.Get()]);
            if (m_assets != nullptr && entry.pictures[slot.Get()].generation != 0)
            {
                m_assets->Release(entry.pictures[slot.Get()]);
            }
            entry.pictures[slot.Get()] = {};
            entry.textures[slot.Get()] = -1;
            entry.texturePixels[slot.Get()] = 0;
            entry.renderTargetSlot[slot.Get()] = false;
            entry.renderTargets[slot.Get()] = {};
        }
    }

    Bool MeshMaterialLibrary::TexturesChanged(const MaterialEntry& entry) const
    {
        for (UInt32 slot = 0; slot < SlotCount; ++slot)
        {
            // 렌더 텍스처 칸은 렌더 타깃이 다시 만들어졌는지(크기가 바뀌었다) 본다(D-439).
            if (entry.renderTargetSlot[slot.Get()])
            {
                AssetHandle target;
                TextureHandle texture;
                Extent2D extent;
                TextureFilter filter = TextureFilter::Linear;
                if (m_renderTargetSource != nullptr)
                {
                    m_renderTargetSource->ResolveRenderTexture(entry.pictures[slot.Get()], target, texture, extent, filter);
                }
                if (target.index != entry.renderTargets[slot.Get()].index || target.generation != entry.renderTargets[slot.Get()].generation)
                {
                    return true;
                }
                continue;
            }
            if (entry.textures[slot.Get()] < 0)
            {
                continue;
            }
            // 재질이 가리키는 그 그림이다 - 나눠 쓰는 텍스처를 처음 올린 그림이 아니다.
            const TextureData* texture = m_assets->GetTexture(entry.pictures[slot.Get()]);
            if (texture == nullptr || texture->pixelGeneration != entry.texturePixels[slot.Get()])
            {
                return true;
            }
        }
        return false;
    }

    AssetHandle MeshMaterialLibrary::ResolveAsset(AssetHandle materialAsset)
    {
        if (m_assets == nullptr || m_renderer == nullptr || false == m_renderer->IsInitialized() || materialAsset.generation == 0)
        {
            return {};
        }
        const MaterialData* material = m_assets->GetMaterial(materialAsset);
        // 셰이더 머티리얼은 그래픽 모듈의 `MaterialLibrary` 가 푼다.
        if (material == nullptr || false == material->standard)
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
        const AssetId ids[SlotCount] = {source.baseColorTextureId, source.metallicRoughnessTextureId, source.normalTextureId,
            source.occlusionTextureId, source.emissiveTextureId};
        const Bool srgbByDefault[SlotCount] = {true, false, false, false, true};
        AssetHandle handles[SlotCount];
        for (UInt32 at = 0; at < SlotCount; ++at)
        {
            // 렌더 텍스처 칸(D-439)은 올리지 않고 렌더 타깃을 그대로 읽는다.
            if (AcquireRenderTarget(ids[at.Get()], entry.pictures[at.Get()], entry.renderTargets[at.Get()]))
            {
                entry.renderTargetSlot[at.Get()] = true;
                handles[at.Get()] = entry.renderTargets[at.Get()];
                continue;
            }
            entry.textures[at.Get()] = AcquireTexture(ids[at.Get()], srgbByDefault[at.Get()], entry.pictures[at.Get()]);
            if (entry.textures[at.Get()] >= 0)
            {
                handles[at.Get()] = m_textures[static_cast<std::size_t>(entry.textures[at.Get()].Get())].rendererTexture;
                const TextureData* data = m_assets->GetTexture(entry.pictures[at.Get()]);
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
        desc.blend = source.alpha == MaterialAlpha::Blend;
        desc.doubleSided = source.doubleSided;
        // 기본색 그림이 가장 가까운 텍셀(픽셀 아트)이면 재질이 그렇게 읽는다. 칸마다 샘플러를 두지 않는다 - 셰이더의 샘플러는 하나다.
        const TextureData* base = entry.textures[0] >= 0 ? m_assets->GetTexture(entry.pictures[0]) : nullptr;
        desc.nearestFilter = base != nullptr && base->filter == TextureFilter::Nearest;
        desc.baseColorTexture = handles[0];
        desc.metallicRoughnessTexture = handles[1];
        desc.normalTexture = handles[2];
        desc.occlusionTexture = handles[3];
        desc.emissiveTexture = handles[4];
        entry.rendererMaterial = m_renderer->RegisterMeshMaterial(desc);
        if (entry.rendererMaterial.generation == 0)
        {
            entry.failedGeneration = material->dataGeneration;
            ReleaseEntry(entry);
            return {};
        }
        entry.dataGeneration = material->dataGeneration;
        return entry.rendererMaterial;
    }

    AssetHandle MeshMaterialLibrary::ResolveMeshMaterial(AssetHandle meshAsset)
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

    std::size_t MeshMaterialLibrary::GetUploadedMaterialCount() const
    {
        std::size_t count = 0;
        for (std::size_t index = 0; index < m_materials.Size(); ++index)
        {
            count += m_materials[index].rendererMaterial.generation != 0 ? 1 : 0;
        }
        return count;
    }

    std::size_t MeshMaterialLibrary::GetUploadedTextureCount() const
    {
        std::size_t count = 0;
        for (std::size_t index = 0; index < m_textures.Size(); ++index)
        {
            count += m_textures[index].users > 0 ? 1 : 0;
        }
        return count;
    }
}
