#include <JBro/Core/ECS/ComponentRegistry.h>

#include <algorithm>

namespace JBro::Engine
{
    bool CComponentRegistry::Register(ComponentTypeInfo info)
    {
        if (false == info.type.IsValid() || info.size == 0 || info.alignment == 0)
        {
            return false;
        }

        const auto found = std::lower_bound(m_types.begin(), m_types.end(), info.type.value,
            [](const ComponentTypeInfo& existing, std::uint64_t value)
            {
                return existing.type.value < value;
            });

        if (found != m_types.end() && found->type == info.type)
        {
            return found->size == info.size && found->alignment == info.alignment;
        }

        m_types.insert(found, info);
        return true;
    }

    const ComponentTypeInfo* CComponentRegistry::Find(ComponentTypeId type) const
    {
        const auto found = std::lower_bound(m_types.begin(), m_types.end(), type.value,
            [](const ComponentTypeInfo& existing, std::uint64_t value)
            {
                return existing.type.value < value;
            });
        return found != m_types.end() && found->type == type ? &*found : nullptr;
    }

    bool CComponentRegistry::IsRegistered(ComponentTypeId type) const
    {
        return Find(type) != nullptr;
    }

    std::size_t CComponentRegistry::GetTypeCount() const
    {
        return m_types.size();
    }
}
