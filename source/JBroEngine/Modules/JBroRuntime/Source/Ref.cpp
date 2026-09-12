#include <JBro/Internal/InstanceRegistry.h>

#include <limits>

namespace JBro::Internal
{
    InstanceRegistry::InstanceRegistry()
    {
        m_entries.Add({});
    }

    namespace
    {
        InstanceRegistry* g_boundRegistry = nullptr;
    }

    InstanceRegistry& InstanceRegistry::Local()
    {
        static InstanceRegistry registry;
        return registry;
    }

    InstanceRegistry& InstanceRegistry::Get()
    {
        return g_boundRegistry == nullptr ? Local() : *g_boundRegistry;
    }

    void InstanceRegistry::Bind(InstanceRegistry* registry)
    {
        g_boundRegistry = registry;
    }

    InstanceHandle InstanceRegistry::Register(
        InstanceId objectId,
        InstanceId componentId,
        RefCategory category,
        void* pointer)
    {
        const InstanceId persistentId = GetPersistentId(objectId, componentId, category);
        if (persistentId == InvalidInstanceId || pointer == nullptr)
        {
            return {};
        }

        std::uint32_t slot = 0;
        if (false == m_freeSlots.IsEmpty())
        {
            slot = m_freeSlots.Last();
            m_freeSlots.RemoveAt(m_freeSlots.Size() - 1);
        }
        else
        {
            if (m_entries.Size() >= (std::numeric_limits<std::uint32_t>::max)())
            {
                return {};
            }
            m_entries.Reserve(m_entries.Size() + 1);
            m_freeSlots.Reserve(m_entries.Size() + 1);
            slot = static_cast<std::uint32_t>(m_entries.Size());
            m_entries.Add({});
        }

        bool inserted = false;
        try
        {
            inserted = m_idToSlot.TryAdd(persistentId, slot);
        }
        catch (...)
        {
            m_freeSlots.Add(slot);
            throw;
        }
        if (false == inserted)
        {
            m_freeSlots.Add(slot);
            return {};
        }

        Entry& entry = m_entries[slot];
        entry.Pointer = pointer;
        entry.ObjectId = objectId;
        entry.ComponentId = componentId;
        entry.Category = category;
        entry.Alive = true;
        ++m_liveCount;
        return {slot, entry.Generation};
    }

    bool InstanceRegistry::Unregister(InstanceHandle handle)
    {
        if (false == handle.IsSet() || handle.Slot >= m_entries.Size())
        {
            return false;
        }

        Entry& entry = m_entries[handle.Slot];
        if (false == entry.Alive || entry.Generation != handle.Gen)
        {
            return false;
        }

        const InstanceId persistentId = GetPersistentId(
            entry.ObjectId,
            entry.ComponentId,
            entry.Category);
        m_idToSlot.Remove(persistentId);
        entry.Pointer = nullptr;
        entry.ObjectId = InvalidInstanceId;
        entry.ComponentId = InvalidInstanceId;
        entry.Generation = NextGeneration(entry.Generation);
        entry.Alive = false;
        m_freeSlots.Add(handle.Slot);
        --m_liveCount;
        return true;
    }

    void* InstanceRegistry::Resolve(InstanceHandle handle, RefCategory category) const
    {
        if (false == handle.IsSet() || handle.Slot >= m_entries.Size())
        {
            return nullptr;
        }

        const Entry& entry = m_entries[handle.Slot];
        if (false == entry.Alive
            || entry.Generation != handle.Gen
            || entry.Category != category)
        {
            return nullptr;
        }
        return entry.Pointer;
    }

    ResolvedInstance InstanceRegistry::Resolve(
        InstanceId objectId,
        InstanceId componentId,
        RefCategory category) const
    {
        ++m_persistentLookupCount;
        const InstanceId persistentId = GetPersistentId(objectId, componentId, category);
        if (persistentId == InvalidInstanceId)
        {
            return {};
        }

        const std::uint32_t* slot = m_idToSlot.Find(persistentId);
        if (slot == nullptr || *slot >= m_entries.Size())
        {
            return {};
        }

        const Entry& entry = m_entries[*slot];
        if (false == entry.Alive
            || entry.Category != category
            || entry.ObjectId != objectId
            || entry.ComponentId != componentId)
        {
            return {};
        }
        return {entry.Pointer, {*slot, entry.Generation}};
    }

    void InstanceRegistry::Clear()
    {
        m_idToSlot.Clear();
        m_freeSlots.Clear();
        m_freeSlots.Reserve(m_entries.Size());
        for (std::size_t index = 1; index < m_entries.Size(); ++index)
        {
            Entry& entry = m_entries[index];
            if (entry.Alive)
            {
                entry.Generation = NextGeneration(entry.Generation);
            }
            entry.Pointer = nullptr;
            entry.ObjectId = InvalidInstanceId;
            entry.ComponentId = InvalidInstanceId;
            entry.Alive = false;
            m_freeSlots.Add(static_cast<std::uint32_t>(index));
        }
        m_liveCount = 0;
        m_persistentLookupCount = 0;
    }

    std::size_t InstanceRegistry::GetLiveCount() const
    {
        return m_liveCount;
    }

    void InstanceRegistry::ResetDiagnostics()
    {
        m_persistentLookupCount = 0;
    }

    std::size_t InstanceRegistry::GetPersistentLookupCount() const
    {
        return m_persistentLookupCount;
    }

    InstanceId InstanceRegistry::GetPersistentId(
        InstanceId objectId,
        InstanceId componentId,
        RefCategory category)
    {
        if (category == RefCategory::Component || category == RefCategory::Script)
        {
            return componentId;
        }
        return objectId;
    }

    std::uint32_t InstanceRegistry::NextGeneration(std::uint32_t generation)
    {
        ++generation;
        if (generation == 0)
        {
            generation = 1;
        }
        return generation;
    }

    void* ResolveInstanceByHandle(InstanceHandle handle, RefCategory category)
    {
        return InstanceRegistry::Get().Resolve(handle, category);
    }

    ResolvedInstance ResolveInstanceById(
        InstanceId objectId,
        InstanceId componentId,
        RefCategory category)
    {
        return InstanceRegistry::Get().Resolve(objectId, componentId, category);
    }

    bool PatchInstanceRefCache(
        InstanceRef& reference,
        RefCategory category)
    {
        const ResolvedInstance resolved = InstanceRegistry::Get().Resolve(
            reference.ObjectId,
            reference.ComponentId,
            category);
        reference.Cached = resolved.Handle;
        return resolved.Pointer != nullptr;
    }
}
