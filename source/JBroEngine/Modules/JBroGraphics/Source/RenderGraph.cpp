#include <JBro/Graphics/RenderGraph.h>

#include <cstring>

namespace JBro
{
    namespace
    {
        Bool IsSameDesc(const TransientTextureDesc& left, const TransientTextureDesc& right) noexcept
        {
            return left.extent.width == right.extent.width && left.extent.height == right.extent.height && left.format == right.format
                && left.usage == right.usage;
        }

        TextureAccess ToTextureAccess(std::uint32_t role) noexcept;
    }

    // ── 풀 ──────────────────────────────────────────────────────────────────

    RenderTargetPool::~RenderTargetPool()
    {
        Shutdown();
    }

    void RenderTargetPool::Initialize(IRHIDevice& device)
    {
        Shutdown();
        m_device = &device;
    }

    void RenderTargetPool::Shutdown()
    {
        if (m_device != nullptr)
        {
            for (Slot& slot : m_slots)
            {
                if (slot.texture.IsValid())
                {
                    m_device->DestroyTexture(slot.texture);
                }
            }
        }
        for (Slot& slot : m_slots)
        {
            slot = {};
        }
        m_wantCount = 0;
        m_device = nullptr;
    }

    void RenderTargetPool::Prepare()
    {
        if (m_device == nullptr)
        {
            return;
        }
        for (Slot& slot : m_slots)
        {
            if (slot.texture.IsValid() && ++slot.idleFrames > IdleFrames)
            {
                // 디바이스가 은퇴 펜스로 지난 프레임이 다 그린 뒤에 놓는다 - 여기서 기다리지 않는다.
                m_device->DestroyTexture(slot.texture);
                slot = {};
            }
        }
        for (UInt32 want = 0; want < m_wantCount; ++want)
        {
            for (UInt32 count = 0; count < m_wants[want].count; ++count)
            {
                CreateInSlot(m_wants[want].desc);
            }
        }
        m_wantCount = 0;
    }

    Bool RenderTargetPool::Ensure(const TransientTextureDesc& desc)
    {
        if (m_device == nullptr)
        {
            return false;
        }
        for (Slot& slot : m_slots)
        {
            if (slot.texture.IsValid() && IsSameDesc(slot.desc, desc))
            {
                slot.idleFrames = 0;
                return true;
            }
        }
        return CreateInSlot(desc);
    }

    UInt32 RenderTargetPool::GetTextureCount() const noexcept
    {
        UInt32 count = 0;
        for (const Slot& slot : m_slots)
        {
            if (slot.texture.IsValid())
            {
                ++count;
            }
        }
        return count;
    }

    Bool RenderTargetPool::GetSlot(UInt32 index, TransientTextureDesc& desc, TextureHandle& texture, UInt32& idleFrames) const noexcept
    {
        if (index >= Capacity || false == m_slots[index.Get()].texture.IsValid())
        {
            return false;
        }
        desc = m_slots[index.Get()].desc;
        texture = m_slots[index.Get()].texture;
        idleFrames = m_slots[index.Get()].idleFrames;
        return true;
    }

    UInt32 RenderTargetPool::GetWantCount() const noexcept
    {
        return m_wantCount;
    }

    Bool RenderTargetPool::GetWant(UInt32 index, TransientTextureDesc& desc, UInt32& count) const noexcept
    {
        if (index >= m_wantCount)
        {
            return false;
        }
        desc = m_wants[index.Get()].desc;
        count = m_wants[index.Get()].count;
        return true;
    }

    UInt32 RenderTargetPool::GetWantedCount(const TransientTextureDesc& desc) const noexcept
    {
        for (UInt32 want = 0; want < m_wantCount; ++want)
        {
            if (IsSameDesc(m_wants[want].desc, desc))
            {
                return m_wants[want].count;
            }
        }
        return 0;
    }

    Bool RenderTargetPool::CreateInSlot(const TransientTextureDesc& desc)
    {
        // 빈 자리가 없으면 가장 오래 쓰지 않은 것을 내준다. **같은 모양은 내주지 않는다** - 같은 모양을 하나 더 바란 것인데 그 모양을 내주고
        // 다시 만들면 수가 그대로라, 다음 컴파일이 같은 것을 또 바라고 대신 그리기가 끝나지 않는다(Codex 리뷰).
        Slot* chosen = nullptr;
        for (Slot& slot : m_slots)
        {
            if (false == slot.texture.IsValid())
            {
                chosen = &slot;
                break;
            }
            if (IsSameDesc(slot.desc, desc))
            {
                continue;
            }
            if (chosen == nullptr || slot.idleFrames > chosen->idleFrames)
            {
                chosen = &slot;
            }
        }
        if (chosen == nullptr)
        {
            return false;
        }
        if (chosen->texture.IsValid())
        {
            m_device->DestroyTexture(chosen->texture);
            *chosen = {};
        }
        TextureDesc textureDesc;
        textureDesc.extent = desc.extent;
        textureDesc.format = desc.format;
        textureDesc.usage = desc.usage;
        chosen->texture = m_device->CreateTexture(textureDesc);
        if (false == chosen->texture.IsValid())
        {
            *chosen = {};
            return false;
        }
        chosen->desc = desc;
        chosen->idleFrames = 0;
        return true;
    }

    void RenderTargetPool::AddWant(const TransientTextureDesc& desc, UInt32 count)
    {
        for (UInt32 want = 0; want < m_wantCount; ++want)
        {
            if (IsSameDesc(m_wants[want].desc, desc))
            {
                if (count > m_wants[want].count)
                {
                    m_wants[want].count = count;
                }
                return;
            }
        }
        if (m_wantCount < Capacity)
        {
            m_wants[m_wantCount++] = {desc, count};
        }
    }

    // ── 실행 함수가 받는 것 ─────────────────────────────────────────────────

    TextureHandle RenderGraphContext::GetTexture(RenderGraphTexture texture) const noexcept
    {
        if (m_graph == nullptr || texture.index >= m_graph->m_textures.Size())
        {
            return {};
        }
        const RenderGraph::TextureEntry& entry = m_graph->m_textures[texture.index.Get()];
        return entry.available ? entry.texture : TextureHandle{};
    }

    Bool RenderGraphContext::HasTexture(RenderGraphTexture texture) const noexcept
    {
        if (m_graph == nullptr || texture.index >= m_graph->m_textures.Size())
        {
            return false;
        }
        const RenderGraph::TextureEntry& entry = m_graph->m_textures[texture.index.Get()];
        return entry.available && entry.written;
    }

    // ── 선언 ────────────────────────────────────────────────────────────────

    Bool RenderGraph::Initialize(const RenderGraphLimits& limits)
    {
        Shutdown();
        if (limits.maxPasses == 0 || limits.maxTextures == 0 || limits.maxAccesses == 0)
        {
            return false;
        }
        m_textures.Reserve(limits.maxTextures.Get());
        m_passes.Reserve(limits.maxPasses.Get());
        m_accesses.Reserve(limits.maxAccesses.Get());
        m_barriers.Reserve(limits.maxAccesses.Get());
        m_data.Resize(limits.dataBytes);
        m_maxPasses = limits.maxPasses;
        m_maxTextures = limits.maxTextures;
        m_maxAccesses = limits.maxAccesses;
        Reset();
        return true;
    }

    void RenderGraph::Shutdown()
    {
        m_textures = {};
        m_passes = {};
        m_accesses = {};
        m_barriers = {};
        m_data = {};
        m_dataUsed = 0;
        m_maxPasses = 0;
        m_maxTextures = 0;
        m_maxAccesses = 0;
        m_overflow = false;
        m_compiled = false;
    }

    void RenderGraph::Reset()
    {
        m_capturePass = RenderGraphPass::Invalid;
        m_captureTexture = RenderGraphTexture::Invalid;
        m_captureDestination = {};
        m_captured = false;
        m_textures.Clear();
        m_passes.Clear();
        m_accesses.Clear();
        m_barriers.Clear();
        m_dataUsed = 0;
        m_overflow = false;
        m_compiled = false;
    }

    RenderGraphTexture RenderGraph::Import(TextureHandle texture, TextureKind kind, const char* name)
    {
        if (false == texture.IsValid())
        {
            return {};
        }
        for (std::size_t index = 0; index < m_textures.Size(); ++index)
        {
            if (m_textures[index].kind != TextureKind::Transient && m_textures[index].texture == texture)
            {
                return {static_cast<UInt32>(index)};
            }
        }
        if (m_textures.Size() >= m_maxTextures)
        {
            m_overflow = true;
            return {};
        }
        TextureEntry entry;
        entry.texture = texture;
        entry.kind = kind;
        entry.name = name;
        m_textures.Add(entry);
        return {static_cast<UInt32>(m_textures.Size() - 1)};
    }

    RenderGraphTexture RenderGraph::ImportTexture(TextureHandle texture, const char* name)
    {
        return Import(texture, TextureKind::Imported, name);
    }

    RenderGraphTexture RenderGraph::ImportBackBuffer(TextureHandle backBuffer, const char* name)
    {
        return Import(backBuffer, TextureKind::BackBuffer, name);
    }

    RenderGraphTexture RenderGraph::CreateTexture(const TransientTextureDesc& desc, const char* name)
    {
        if (desc.extent.width == 0 || desc.extent.height == 0 || desc.format == TextureFormat::Unknown)
        {
            return {};
        }
        if (m_textures.Size() >= m_maxTextures)
        {
            m_overflow = true;
            return {};
        }
        TextureEntry entry;
        entry.desc = desc;
        entry.name = name;
        entry.kind = TextureKind::Transient;
        m_textures.Add(entry);
        return {static_cast<UInt32>(m_textures.Size() - 1)};
    }

    RenderGraphPass RenderGraph::AddPass(RenderGraphPassKind kind, const char* name, RenderGraphExecute execute, UInt32 group)
    {
        if (execute == nullptr)
        {
            return {};
        }
        if (m_passes.Size() >= m_maxPasses)
        {
            m_overflow = true;
            return {};
        }
        PassEntry entry;
        entry.name = name;
        entry.execute = execute;
        entry.kind = kind;
        entry.group = group;
        entry.firstAccess = static_cast<UInt32>(m_accesses.Size());
        m_passes.Add(entry);
        return {static_cast<UInt32>(m_passes.Size() - 1)};
    }

    Bool RenderGraph::IsLastPass(RenderGraphPass pass) const noexcept
    {
        return pass.IsValid() && m_passes.Size() != 0 && pass.index == m_passes.Size() - 1;
    }

    Bool RenderGraph::AddAccess(RenderGraphPass pass, const AccessEntry& access)
    {
        // 패스의 접근은 접근 배열에서 연속이어야 하므로 막 더한 패스에만 붙는다.
        if (false == IsLastPass(pass) || access.texture >= m_textures.Size())
        {
            return false;
        }
        if (m_accesses.Size() >= m_maxAccesses)
        {
            m_overflow = true;
            return false;
        }
        m_accesses.Add(access);
        ++m_passes[pass.index.Get()].accessCount;
        return true;
    }

    Bool RenderGraph::SetColorAttachment(RenderGraphPass pass, RenderGraphTexture texture, LoadOperation load, const ClearColor& clear)
    {
        if (false == IsLastPass(pass) || m_passes[pass.index.Get()].kind != RenderGraphPassKind::Raster)
        {
            return false;
        }
        const PassEntry& entry = m_passes[pass.index.Get()];
        UInt32 colors = 0;
        for (UInt32 at = 0; at < entry.accessCount; ++at)
        {
            if (m_accesses[(entry.firstAccess + at).Get()].role == AccessRole::Color)
            {
                ++colors;
            }
        }
        if (colors >= MaxColorAttachments)
        {
            return false;
        }
        AccessEntry access;
        access.texture = texture.index;
        access.role = AccessRole::Color;
        access.load = load;
        access.clear = clear;
        return AddAccess(pass, access);
    }

    Bool RenderGraph::SetDepthAttachment(RenderGraphPass pass, RenderGraphTexture texture, LoadOperation load, StoreOperation store,
        Bool optional)
    {
        if (false == IsLastPass(pass) || m_passes[pass.index.Get()].kind != RenderGraphPassKind::Raster)
        {
            return false;
        }
        const PassEntry& entry = m_passes[pass.index.Get()];
        for (UInt32 at = 0; at < entry.accessCount; ++at)
        {
            if (m_accesses[(entry.firstAccess + at).Get()].role == AccessRole::Depth)
            {
                return false;
            }
        }
        AccessEntry access;
        access.texture = texture.index;
        access.role = AccessRole::Depth;
        access.load = load;
        access.store = store;
        access.optional = optional;
        return AddAccess(pass, access);
    }

    Bool RenderGraph::Read(RenderGraphPass pass, RenderGraphTexture texture, Bool optional)
    {
        if (false == IsLastPass(pass) || m_passes[pass.index.Get()].kind == RenderGraphPassKind::Copy)
        {
            return false;
        }
        AccessEntry access;
        access.texture = texture.index;
        access.role = AccessRole::Read;
        access.optional = optional;
        return AddAccess(pass, access);
    }

    Bool RenderGraph::ReadCopySource(RenderGraphPass pass, RenderGraphTexture texture)
    {
        if (false == IsLastPass(pass) || m_passes[pass.index.Get()].kind != RenderGraphPassKind::Copy)
        {
            return false;
        }
        AccessEntry access;
        access.texture = texture.index;
        access.role = AccessRole::CopySource;
        return AddAccess(pass, access);
    }

    Bool RenderGraph::WriteCopyDestination(RenderGraphPass pass, RenderGraphTexture texture)
    {
        if (false == IsLastPass(pass) || m_passes[pass.index.Get()].kind != RenderGraphPassKind::Copy)
        {
            return false;
        }
        AccessEntry access;
        access.texture = texture.index;
        access.role = AccessRole::CopyDestination;
        return AddAccess(pass, access);
    }

    Bool RenderGraph::ReadWriteStorage(RenderGraphPass pass, RenderGraphTexture texture)
    {
        if (false == IsLastPass(pass) || m_passes[pass.index.Get()].kind != RenderGraphPassKind::Compute)
        {
            return false;
        }
        AccessEntry access;
        access.texture = texture.index;
        access.role = AccessRole::Storage;
        return AddAccess(pass, access);
    }

    Bool RenderGraph::RunIfAvailable(RenderGraphPass pass, RenderGraphTexture texture)
    {
        if (false == IsLastPass(pass) || texture.index >= m_textures.Size())
        {
            return false;
        }
        m_passes[pass.index.Get()].condition = texture.index;
        m_passes[pass.index.Get()].runIfMissing = false;
        return true;
    }

    Bool RenderGraph::RunIfMissing(RenderGraphPass pass, RenderGraphTexture texture)
    {
        if (false == IsLastPass(pass) || texture.index >= m_textures.Size())
        {
            return false;
        }
        m_passes[pass.index.Get()].condition = texture.index;
        m_passes[pass.index.Get()].runIfMissing = true;
        return true;
    }

    Bool RenderGraph::CopyPassData(RenderGraphPass pass, const void* data, std::size_t size, std::size_t alignment)
    {
        if (false == IsLastPass(pass))
        {
            return false;
        }
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(m_data.Data());
        const std::uintptr_t at = (base + m_dataUsed + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1);
        const std::size_t offset = static_cast<std::size_t>(at - base);
        if (offset + size > m_data.Size())
        {
            m_overflow = true;
            return false;
        }
        std::memcpy(m_data.Data() + offset, data, size);
        m_passes[pass.index.Get()].data = m_data.Data() + offset;
        m_dataUsed = offset + size;
        return true;
    }

    // ── 컴파일 ──────────────────────────────────────────────────────────────

    Bool RenderGraph::Reads(const AccessEntry& access) noexcept
    {
        switch (access.role)
        {
        case AccessRole::Color:
        case AccessRole::Depth:
            return access.load == LoadOperation::Load;
        case AccessRole::Read:
        case AccessRole::CopySource:
        case AccessRole::Storage:
            return true;
        case AccessRole::CopyDestination:
            return false;
        }
        return false;
    }

    Bool RenderGraph::Writes(const AccessEntry& access) noexcept
    {
        switch (access.role)
        {
        case AccessRole::Color:
        case AccessRole::Depth:
        case AccessRole::CopyDestination:
        case AccessRole::Storage:
            return true;
        case AccessRole::Read:
        case AccessRole::CopySource:
            return false;
        }
        return false;
    }

    Bool RenderGraph::Compile(RenderTargetPool& pool)
    {
        m_compiled = false;
        m_barriers.Clear();
        if (m_overflow)
        {
            return false;
        }
        for (TextureEntry& texture : m_textures)
        {
            texture.firstPass = RenderGraphPass::Invalid;
            texture.lastPass = RenderGraphPass::Invalid;
            texture.allocated = false;
            if (texture.kind == TextureKind::Transient)
            {
                texture.texture = {};
                texture.available = false;
            }
            else
            {
                texture.available = true;
            }
        }
        // 수명은 모든 패스를 넣고 잰다. 뒤에서 가려지는 패스는 수명을 줄이기만 하므로 고른 텍스처가 겹치는 일은 없다.
        for (std::size_t pass = 0; pass < m_passes.Size(); ++pass)
        {
            const PassEntry& entry = m_passes[pass];
            for (UInt32 at = 0; at <= entry.accessCount; ++at)
            {
                const UInt32 index = at < entry.accessCount ? m_accesses[(entry.firstAccess + at).Get()].texture : entry.condition;
                if (index == RenderGraphTexture::Invalid)
                {
                    continue;
                }
                TextureEntry& texture = m_textures[index.Get()];
                if (texture.firstPass == RenderGraphPass::Invalid)
                {
                    texture.firstPass = static_cast<UInt32>(pass);
                }
                texture.lastPass = static_cast<UInt32>(pass);
            }
        }
        Allocate(pool);
        CullPasses();
        BuildBarriers();
        m_compiled = true;
        return true;
    }

    void RenderGraph::Allocate(RenderTargetPool& pool)
    {
        constexpr UInt32 Free = 0xFFFFFFFFu;
        UInt32 busyUntil[RenderTargetPool::Capacity];
        for (UInt32& slot : busyUntil)
        {
            slot = Free;
        }
        // 풀에 자리가 없는 텍스처도 같은 규칙으로 가상의 자리에 놓는다. 가상 자리의 수가 그 모양으로 더 만들어야 할 수다.
        struct Virtual
        {
            TransientTextureDesc desc;
            UInt32 busyUntil = 0;
        };
        Virtual virtuals[RenderTargetPool::Capacity];
        UInt32 virtualCount = 0;

        for (std::size_t pass = 0; pass < m_passes.Size(); ++pass)
        {
            const PassEntry& entry = m_passes[pass];
            // 조건으로만 가리키는 텍스처도 고른다 - 고르지 않으면 늘 없는 것이라 그 조건이 영영 바뀌지 않는다(Codex 리뷰).
            for (UInt32 at = 0; at <= entry.accessCount; ++at)
            {
                const UInt32 index = at < entry.accessCount ? m_accesses[(entry.firstAccess + at).Get()].texture : entry.condition;
                if (index == RenderGraphTexture::Invalid)
                {
                    continue;
                }
                TextureEntry& texture = m_textures[index.Get()];
                if (texture.kind != TextureKind::Transient || texture.allocated)
                {
                    continue;
                }
                texture.allocated = true;
                Bool placed = false;
                for (UInt32 slot = 0; slot < RenderTargetPool::Capacity; ++slot)
                {
                    RenderTargetPool::Slot& candidate = pool.m_slots[slot.Get()];
                    if (candidate.texture.IsValid() && IsSameDesc(candidate.desc, texture.desc)
                        && (busyUntil[slot.Get()] == Free || busyUntil[slot.Get()] < texture.firstPass))
                    {
                        busyUntil[slot.Get()] = texture.lastPass;
                        candidate.idleFrames = 0;
                        texture.texture = candidate.texture;
                        texture.available = true;
                        placed = true;
                        break;
                    }
                }
                if (placed)
                {
                    continue;
                }
                Bool reused = false;
                for (UInt32 slot = 0; slot < virtualCount; ++slot)
                {
                    Virtual& candidate = virtuals[slot.Get()];
                    if (IsSameDesc(candidate.desc, texture.desc) && candidate.busyUntil < texture.firstPass)
                    {
                        candidate.busyUntil = texture.lastPass;
                        reused = true;
                        break;
                    }
                }
                if (false == reused && virtualCount < RenderTargetPool::Capacity)
                {
                    virtuals[virtualCount.Get()] = {texture.desc, texture.lastPass};
                    ++virtualCount;
                }
            }
        }
        for (UInt32 slot = 0; slot < virtualCount; ++slot)
        {
            Bool seen = false;
            UInt32 count = 0;
            for (UInt32 other = 0; other < virtualCount; ++other)
            {
                if (IsSameDesc(virtuals[other.Get()].desc, virtuals[slot.Get()].desc))
                {
                    seen = seen || other < slot;
                    ++count;
                }
            }
            if (false == seen)
            {
                pool.AddWant(virtuals[slot.Get()].desc, count);
            }
        }
    }

    void RenderGraph::CullPasses()
    {
        // 앞에서 뒤로: 조건·없는 텍스처·쓰이지 않은 입력.
        for (TextureEntry& texture : m_textures)
        {
            texture.written = texture.kind != TextureKind::Transient;
        }
        for (PassEntry& entry : m_passes)
        {
            entry.skip = RenderGraphSkip::None;
            if (entry.condition != RenderGraphTexture::Invalid)
            {
                const Bool available = m_textures[entry.condition.Get()].available;
                if (entry.runIfMissing && available)
                {
                    entry.skip = RenderGraphSkip::TexturePresent;
                }
                else if (false == entry.runIfMissing && false == available)
                {
                    entry.skip = RenderGraphSkip::MissingTexture;
                }
            }
            for (UInt32 at = 0; at < entry.accessCount && entry.skip == RenderGraphSkip::None; ++at)
            {
                const AccessEntry& access = m_accesses[(entry.firstAccess + at).Get()];
                if (false == m_textures[access.texture.Get()].available && false == access.optional)
                {
                    entry.skip = RenderGraphSkip::MissingTexture;
                }
            }
            for (UInt32 at = 0; at < entry.accessCount && entry.skip == RenderGraphSkip::None; ++at)
            {
                const AccessEntry& access = m_accesses[(entry.firstAccess + at).Get()];
                if (Reads(access) && false == access.optional && false == m_textures[access.texture.Get()].written)
                {
                    entry.skip = RenderGraphSkip::InputNotWritten;
                }
            }
            if (entry.skip != RenderGraphSkip::None)
            {
                continue;
            }
            for (UInt32 at = 0; at < entry.accessCount; ++at)
            {
                const AccessEntry& access = m_accesses[(entry.firstAccess + at).Get()];
                if (Writes(access) && m_textures[access.texture.Get()].available)
                {
                    m_textures[access.texture.Get()].written = true;
                }
            }
        }

        // 뒤에서 앞으로: 쓴 것을 아무도 읽지 않는 패스. 들인 텍스처에 쓰는 패스가 결과이고, 텍스처에 쓰지 않는 패스(버퍼만 쓰는 컴퓨트)는
        // 무엇을 남기는지 그래프가 모르므로 남긴다.
        for (TextureEntry& texture : m_textures)
        {
            texture.readLater = false;
        }
        for (std::size_t reverse = m_passes.Size(); reverse > 0; --reverse)
        {
            PassEntry& entry = m_passes[reverse - 1];
            if (entry.skip != RenderGraphSkip::None)
            {
                continue;
            }
            Bool anyWrite = false;
            Bool needed = false;
            for (UInt32 at = 0; at < entry.accessCount; ++at)
            {
                const AccessEntry& access = m_accesses[(entry.firstAccess + at).Get()];
                if (false == Writes(access))
                {
                    continue;
                }
                const TextureEntry& texture = m_textures[access.texture.Get()];
                if (texture.kind == TextureKind::Transient && false == texture.available)
                {
                    continue;
                }
                anyWrite = true;
                if (texture.kind != TextureKind::Transient || texture.readLater)
                {
                    needed = true;
                }
            }
            if (anyWrite && false == needed)
            {
                entry.skip = RenderGraphSkip::Unused;
                continue;
            }
            // 통째로 덮어쓰면 그 앞의 쓰기는 이 패스 뒤의 읽기와 상관없다.
            for (UInt32 at = 0; at < entry.accessCount; ++at)
            {
                const AccessEntry& access = m_accesses[(entry.firstAccess + at).Get()];
                if (Writes(access) && false == Reads(access))
                {
                    m_textures[access.texture.Get()].readLater = false;
                }
            }
            for (UInt32 at = 0; at < entry.accessCount; ++at)
            {
                const AccessEntry& access = m_accesses[(entry.firstAccess + at).Get()];
                if (Reads(access))
                {
                    m_textures[access.texture.Get()].readLater = true;
                }
            }
        }
    }

    void RenderGraph::BuildBarriers()
    {
        // 그래프가 만든 텍스처만 옮긴다. 들인 텍스처는 쓰임을 모르고, RHI 가 첨부·복사를 할 때 맞춘다.
        for (TextureEntry& texture : m_textures)
        {
            texture.written = texture.kind != TextureKind::Transient;
        }
        for (PassEntry& entry : m_passes)
        {
            entry.firstBarrier = static_cast<UInt32>(m_barriers.Size());
            entry.barrierCount = 0;
            if (entry.skip != RenderGraphSkip::None)
            {
                continue;
            }
            for (UInt32 at = 0; at < entry.accessCount; ++at)
            {
                const AccessEntry& access = m_accesses[(entry.firstAccess + at).Get()];
                const TextureEntry& texture = m_textures[access.texture.Get()];
                if (texture.kind != TextureKind::Transient || false == texture.available)
                {
                    continue;
                }
                // 쓰이지 않은 텍스처를 선택으로 읽으면 실행 함수가 읽지 않는다. 옮길 것도 없다.
                if (Reads(access) && access.optional && false == texture.written)
                {
                    continue;
                }
                const TextureAccess wanted = ToTextureAccess(static_cast<std::uint32_t>(access.role));
                Bool duplicate = false;
                for (UInt32 earlier = 0; earlier < entry.barrierCount; ++earlier)
                {
                    const TextureBarrier& barrier = m_barriers[(entry.firstBarrier + earlier).Get()];
                    if (barrier.texture == texture.texture && barrier.access == wanted)
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate)
                {
                    continue;
                }
                TextureBarrier barrier;
                barrier.texture = texture.texture;
                barrier.mipLevel = AllTextureMips;
                barrier.access = wanted;
                m_barriers.Add(barrier);
                ++entry.barrierCount;
            }
            for (UInt32 at = 0; at < entry.accessCount; ++at)
            {
                const AccessEntry& access = m_accesses[(entry.firstAccess + at).Get()];
                if (Writes(access) && m_textures[access.texture.Get()].available)
                {
                    m_textures[access.texture.Get()].written = true;
                }
            }
        }
    }

    namespace
    {
        TextureAccess ToTextureAccess(std::uint32_t role) noexcept
        {
            // `AccessRole` 의 차례다: Color, Depth, Read, CopySource, CopyDestination, Storage.
            constexpr TextureAccess table[] = {TextureAccess::RenderTarget, TextureAccess::DepthWrite, TextureAccess::ShaderRead,
                TextureAccess::CopySource, TextureAccess::CopyDestination, TextureAccess::Storage};
            return role < 6 ? table[role] : TextureAccess::ShaderRead;
        }
    }

    // ── 실행 ────────────────────────────────────────────────────────────────

    Bool RenderGraph::Execute(IRHICommandContext& commands)
    {
        if (false == m_compiled)
        {
            return false;
        }
        for (TextureEntry& texture : m_textures)
        {
            texture.written = texture.kind != TextureKind::Transient;
        }
        RenderGraphContext context;
        context.m_graph = this;
        context.m_commands = &commands;
        m_captured = false;
        UInt32 nextTimestamp = 0;
        Bool timing = m_timestampCapacity >= 2;
        for (std::size_t passIndex = 0; passIndex < m_passes.Size(); ++passIndex)
        {
            PassEntry& entry = m_passes[passIndex];
            entry.timestamp = RenderGraphPass::Invalid;
            if (entry.skip != RenderGraphSkip::None)
            {
                continue;
            }
            // 앞 시각은 배리어 앞이다 - 패스가 부른 상태 전이도 그 패스의 시간이다.
            if (timing && nextTimestamp + 2 <= m_timestampCapacity && commands.WriteTimestamp(nextTimestamp))
            {
                entry.timestamp = nextTimestamp;
            }
            else
            {
                timing = false;
            }
            if (entry.barrierCount != 0
                && false == commands.Barrier({m_barriers.Data() + entry.firstBarrier.Get(), entry.barrierCount}))
            {
                return false;
            }
            if (entry.kind == RenderGraphPassKind::Raster)
            {
                ColorAttachmentDesc colors[MaxColorAttachments];
                UInt32 colorCount = 0;
                DepthStencilAttachmentDesc depth;
                Bool hasDepth = false;
                for (UInt32 at = 0; at < entry.accessCount; ++at)
                {
                    const AccessEntry& access = m_accesses[(entry.firstAccess + at).Get()];
                    const TextureEntry& texture = m_textures[access.texture.Get()];
                    if (access.role == AccessRole::Color)
                    {
                        ColorAttachmentDesc& color = colors[colorCount.Get()];
                        color.texture = texture.texture;
                        color.loadOperation = access.load;
                        color.storeOperation = StoreOperation::Store;
                        color.clearColor = access.clear;
                        ++colorCount;
                    }
                    else if (access.role == AccessRole::Depth && texture.available)
                    {
                        depth.texture = texture.texture;
                        depth.depthLoadOperation = access.load;
                        depth.depthStoreOperation = access.store;
                        depth.stencilLoadOperation = LoadOperation::Discard;
                        depth.stencilStoreOperation = StoreOperation::Discard;
                        hasDepth = true;
                    }
                }
                RenderPassDesc pass;
                pass.colorAttachments = {colors, colorCount};
                pass.depthStencilAttachment = hasDepth ? &depth : nullptr;
                if (false == commands.BeginRenderPass(pass))
                {
                    return false;
                }
                const Bool recorded = entry.execute(context, entry.data);
                commands.EndRenderPass();
                if (false == recorded)
                {
                    return false;
                }
            }
            else if (false == entry.execute(context, entry.data))
            {
                return false;
            }
            for (UInt32 at = 0; at < entry.accessCount; ++at)
            {
                const AccessEntry& access = m_accesses[(entry.firstAccess + at).Get()];
                if (Writes(access) && m_textures[access.texture.Get()].available)
                {
                    m_textures[access.texture.Get()].written = true;
                }
            }
            if (entry.timestamp != RenderGraphPass::Invalid)
            {
                // 뒤 시각을 적지 못하면 앞 시각만 남아 자리가 비므로, 그 패스부터 재지 않은 것으로 친다.
                if (commands.WriteTimestamp(entry.timestamp + 1))
                {
                    nextTimestamp = entry.timestamp + 2;
                }
                else
                {
                    entry.timestamp = RenderGraphPass::Invalid;
                    timing = false;
                }
            }
            // 미리보기: 이 패스가 끝난 직후의 그 텍스처다. 복사하지 못해도 프레임은 그대로 간다 - 진단이다.
            if (passIndex == m_capturePass && m_captureTexture < m_textures.Size())
            {
                const TextureEntry& texture = m_textures[m_captureTexture.Get()];
                if (texture.available && texture.written && m_captureDestination.IsValid())
                {
                    m_captured = commands.CopyTexture(texture.texture, m_captureDestination);
                }
            }
        }
        return true;
    }

    void RenderGraph::SetTimestampCapacity(UInt32 capacity) noexcept
    {
        m_timestampCapacity = capacity;
    }

    UInt32 RenderGraph::GetPassTimestamp(RenderGraphPass pass) const noexcept
    {
        return pass.index < m_passes.Size() ? m_passes[pass.index.Get()].timestamp : RenderGraphPass::Invalid;
    }

    Bool RenderGraph::SetCapture(RenderGraphPass pass, RenderGraphTexture texture, TextureHandle destination)
    {
        if (pass.index >= m_passes.Size() || texture.index >= m_textures.Size() || false == destination.IsValid())
        {
            return false;
        }
        m_capturePass = pass.index;
        m_captureTexture = texture.index;
        m_captureDestination = destination;
        return true;
    }

    Bool RenderGraph::WasCaptured() const noexcept
    {
        return m_captured;
    }

    // ── 살펴보기 ────────────────────────────────────────────────────────────

    UInt32 RenderGraph::GetPassCount() const noexcept
    {
        return static_cast<UInt32>(m_passes.Size());
    }

    UInt32 RenderGraph::GetTextureCount() const noexcept
    {
        return static_cast<UInt32>(m_textures.Size());
    }

    const char* RenderGraph::GetPassName(RenderGraphPass pass) const noexcept
    {
        return pass.index < m_passes.Size() ? m_passes[pass.index.Get()].name : nullptr;
    }

    RenderGraphSkip RenderGraph::GetPassSkip(RenderGraphPass pass) const noexcept
    {
        return pass.index < m_passes.Size() ? m_passes[pass.index.Get()].skip : RenderGraphSkip::None;
    }

    TextureHandle RenderGraph::GetCompiledTexture(RenderGraphTexture texture) const noexcept
    {
        if (texture.index >= m_textures.Size() || false == m_textures[texture.index.Get()].available)
        {
            return {};
        }
        return m_textures[texture.index.Get()].texture;
    }

    JArrayView<TextureBarrier> RenderGraph::GetPassBarriers(RenderGraphPass pass) const noexcept
    {
        if (pass.index >= m_passes.Size())
        {
            return {};
        }
        const PassEntry& entry = m_passes[pass.index.Get()];
        return {m_barriers.Data() + entry.firstBarrier.Get(), entry.barrierCount};
    }

    Bool RenderGraph::IsCompiled() const noexcept
    {
        return m_compiled;
    }

    RenderGraphPassKind RenderGraph::GetPassKind(RenderGraphPass pass) const noexcept
    {
        return pass.index < m_passes.Size() ? m_passes[pass.index.Get()].kind : RenderGraphPassKind::Raster;
    }

    UInt32 RenderGraph::GetPassGroup(RenderGraphPass pass) const noexcept
    {
        return pass.index < m_passes.Size() ? m_passes[pass.index.Get()].group : UInt32(0);
    }

    UInt32 RenderGraph::GetPassAccessCount(RenderGraphPass pass) const noexcept
    {
        return pass.index < m_passes.Size() ? m_passes[pass.index.Get()].accessCount : UInt32(0);
    }

    RenderGraphAccessInfo RenderGraph::GetPassAccess(RenderGraphPass pass, UInt32 index) const noexcept
    {
        RenderGraphAccessInfo info;
        if (pass.index >= m_passes.Size() || index >= m_passes[pass.index.Get()].accessCount)
        {
            return info;
        }
        const AccessEntry& access = m_accesses[(m_passes[pass.index.Get()].firstAccess + index).Get()];
        info.texture = {access.texture};
        info.access = access.role;
        info.load = access.load;
        info.optional = access.optional;
        return info;
    }

    RenderGraphTexture RenderGraph::GetPassCondition(RenderGraphPass pass, Bool& runIfMissing) const noexcept
    {
        runIfMissing = false;
        if (pass.index >= m_passes.Size())
        {
            return {};
        }
        runIfMissing = m_passes[pass.index.Get()].runIfMissing;
        return {m_passes[pass.index.Get()].condition};
    }

    const char* RenderGraph::GetTextureName(RenderGraphTexture texture) const noexcept
    {
        return texture.index < m_textures.Size() ? m_textures[texture.index.Get()].name : nullptr;
    }

    RenderGraphTextureKind RenderGraph::GetTextureKind(RenderGraphTexture texture) const noexcept
    {
        return texture.index < m_textures.Size() ? m_textures[texture.index.Get()].kind : RenderGraphTextureKind::Transient;
    }

    TransientTextureDesc RenderGraph::GetTextureDesc(RenderGraphTexture texture) const noexcept
    {
        return texture.index < m_textures.Size() ? m_textures[texture.index.Get()].desc : TransientTextureDesc{};
    }

    Bool RenderGraph::GetTextureLifetime(RenderGraphTexture texture, UInt32& firstPass, UInt32& lastPass) const noexcept
    {
        if (texture.index >= m_textures.Size() || m_textures[texture.index.Get()].firstPass == RenderGraphPass::Invalid)
        {
            return false;
        }
        firstPass = m_textures[texture.index.Get()].firstPass;
        lastPass = m_textures[texture.index.Get()].lastPass;
        return true;
    }
}
