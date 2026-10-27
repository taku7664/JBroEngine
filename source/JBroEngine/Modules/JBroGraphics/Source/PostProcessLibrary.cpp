#include <JBro/Graphics/PostProcessLibrary.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Asset/ColorLutFile.h>
#include <JBro/Core/Log.h>
#include <JBro/Graphics/PostProcessProfile.h>
#include <JBro/Graphics/Renderer.h>

#include <cstring>

namespace JBro
{
    namespace
    {
        Bool SameHandle(AssetHandle a, AssetHandle b)
        {
            return a.index == b.index && a.generation == b.generation;
        }
    }

    PostProcessLibrary::~PostProcessLibrary()
    {
        Shutdown();
    }

    void PostProcessLibrary::Initialize(AssetSystem* assets, Renderer* renderer)
    {
        Shutdown();
        m_assets = assets;
        m_renderer = renderer;
    }

    void PostProcessLibrary::Shutdown()
    {
        for (Profile& profile : m_profiles)
        {
            ReleaseProfile(profile);
        }
        m_profiles = {};
        if (m_renderer != nullptr)
        {
            for (const LutTexture& lut : m_luts)
            {
                if (lut.texture.generation != 0)
                {
                    m_renderer->UnregisterTexture(lut.texture);
                }
            }
        }
        m_luts = {};
        m_assets = nullptr;
        m_renderer = nullptr;
    }

    UInt32 PostProcessLibrary::GetLutTextureCount() const noexcept
    {
        UInt32 count = 0;
        for (const LutTexture& lut : m_luts)
        {
            if (lut.texture.generation != 0)
            {
                ++count;
            }
        }
        return count;
    }

    void PostProcessLibrary::ReleaseProfile(Profile& profile)
    {
        if (m_assets != nullptr)
        {
            m_assets->ReleaseAll(profile.acquired);
        }
        profile.acquired.Clear();
        profile.entries.Clear();
        profile.blocks.Clear();
        profile.luts.Clear();
        profile.lutGenerations.Clear();
    }

    Bool PostProcessLibrary::IsStale(const Profile& profile) const
    {
        const PostProcessProfileData* data = m_assets->GetPostProcessProfile(profile.asset);
        if (data == nullptr || data->dataGeneration != profile.dataGeneration)
        {
            return true;
        }
        for (std::size_t at = 0; at < profile.luts.Size(); ++at)
        {
            const ColorLutData* lut = m_assets->GetColorLut(profile.luts[at]);
            if (lut == nullptr || lut->dataGeneration != profile.lutGenerations[at])
            {
                return true;
            }
        }
        return false;
    }

    AssetHandle PostProcessLibrary::EnsureLut(AssetHandle lutAsset)
    {
        const ColorLutData* data = m_assets->GetColorLut(lutAsset);
        if (data == nullptr || m_renderer == nullptr)
        {
            return {};
        }
        LutTexture* slot = nullptr;
        for (LutTexture& lut : m_luts)
        {
            if (SameHandle(lut.asset, lutAsset))
            {
                slot = &lut;
                break;
            }
        }
        if (slot != nullptr && slot->dataGeneration == data->dataGeneration && slot->texture.generation != 0)
        {
            return slot->texture;
        }
        if (slot == nullptr)
        {
            m_luts.Add(LutTexture{lutAsset, 0, {}});
            slot = &m_luts[m_luts.Size() - 1];
        }
        if (slot->texture.generation != 0)
        {
            m_renderer->UnregisterTexture(slot->texture);
            slot->texture = {};
        }
        Array<std::byte> texels;
        PackColorLutRgba16(data->lut, texels);
        slot->texture = m_renderer->RegisterVolumeTexture(
            data->lut.size, data->lut.size, data->lut.size, {texels.Data(), static_cast<JBro::UInt32>(texels.Size())});
        slot->dataGeneration = data->dataGeneration;
        if (slot->texture.generation == 0)
        {
            Log::Write(LogLevel::Warning, "post-process", "a colour LUT of size %u could not be uploaded", data->lut.size.Get());
        }
        return slot->texture;
    }

    void PostProcessLibrary::Build(Profile& profile)
    {
        ReleaseProfile(profile);
        const PostProcessProfileData* data = m_assets->GetPostProcessProfile(profile.asset);
        if (data == nullptr)
        {
            profile.dataGeneration = 0;
            return;
        }
        profile.dataGeneration = data->dataGeneration;
        PostProcessProfileDocument document;
        String error;
        Array<String> warnings;
        if (false == ParsePostProcessProfile(data->text.data(), data->text.size(), document, &error, &warnings))
        {
            Log::Write(LogLevel::Warning, "post-process", "a post-process profile could not be read: %s", error.c_str());
            return;
        }
        for (const String& warning : warnings)
        {
            Log::Write(LogLevel::Warning, "post-process", "%s", warning.c_str());
        }
        // 블록은 16 바이트 칸에 이어 둔다. 크기를 먼저 재어 한 번만 잡는다 - 블록 안의 주소가 다음 항목을 넣다 옮겨지지 않게.
        std::size_t total = 0;
        for (const PostProcessProfileEntry& entry : document.entries)
        {
            if (entry.enabled && entry.type != PostProcessEffectRegistry::NotFound)
            {
                total += (entry.block.Size() + 15u) & ~static_cast<std::size_t>(15u);
            }
        }
        profile.blocks.Resize(total);
        std::size_t offset = 0;
        for (const PostProcessProfileEntry& entry : document.entries)
        {
            if (false == entry.enabled || entry.type == PostProcessEffectRegistry::NotFound)
            {
                continue;
            }
            const PostProcessEffectType* type = PostProcessEffectRegistry::Get(entry.type);
            std::byte* block = profile.blocks.Data() + offset;
            if (entry.block.Size() != 0)
            {
                std::memcpy(block, entry.block.Data(), entry.block.Size());
            }
            // 블록 안의 `xxxId` 를 싣는다(컴포넌트와 같은 해석 패스).
            if (type->parameters != nullptr)
            {
                m_assets->BindComponentAssets(*type->parameters, block, profile.acquired);
            }
            if (entry.type == PostProcessEffects::ColorLookup)
            {
                ColorLookupEffect* lookup = reinterpret_cast<ColorLookupEffect*>(block);
                if (const ColorLutData* lut = m_assets->GetColorLut(lookup->colorLut))
                {
                    lookup->lutTexture = EnsureLut(lookup->colorLut);
                    lookup->lutDomainMin = {lut->lut.domainMin[0], lut->lut.domainMin[1], lut->lut.domainMin[2]};
                    lookup->lutDomainMax = {lut->lut.domainMax[0], lut->lut.domainMax[1], lut->lut.domainMax[2]};
                    profile.luts.Add(lookup->colorLut);
                    profile.lutGenerations.Add(lut->dataGeneration);
                }
            }
            PostProcessEntry resolved;
            resolved.type = entry.type;
            resolved.blockOffset = static_cast<JBro::UInt32>(offset);
            resolved.enabled = true;
            profile.entries.Add(resolved);
            offset += (entry.block.Size() + 15u) & ~static_cast<std::size_t>(15u);
        }
    }

    PostProcessStack PostProcessLibrary::Resolve(AssetHandle profileAsset)
    {
        if (m_assets == nullptr || profileAsset.generation == 0 || m_assets->GetPostProcessProfile(profileAsset) == nullptr)
        {
            return {};
        }
        Profile* found = nullptr;
        for (Profile& profile : m_profiles)
        {
            if (profile.used && SameHandle(profile.asset, profileAsset))
            {
                found = &profile;
                break;
            }
        }
        if (found == nullptr)
        {
            for (Profile& profile : m_profiles)
            {
                if (false == profile.used)
                {
                    found = &profile;
                    break;
                }
            }
            if (found == nullptr)
            {
                m_profiles.Add(Profile{});
                found = &m_profiles[m_profiles.Size() - 1];
            }
            found->used = true;
            found->asset = profileAsset;
            found->dataGeneration = 0;
            Build(*found);
            ++m_resolveCount;
        }
        else if (IsStale(*found))
        {
            Build(*found);
            ++m_resolveCount;
        }
        return {{found->entries.Data(), static_cast<JBro::UInt32>(found->entries.Size())},
            {found->blocks.Data(), static_cast<JBro::UInt32>(found->blocks.Size())}};
    }
}
