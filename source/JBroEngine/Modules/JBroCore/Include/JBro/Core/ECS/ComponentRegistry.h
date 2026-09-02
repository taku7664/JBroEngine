#pragma once

#include <JBro/Core/Core.h>

#include <cstddef>
#include <vector>

namespace JBro::Engine
{
    struct ComponentTypeInfo
    {
        ComponentTypeId type;
        std::size_t size = 0;
        std::size_t alignment = 0;
    };

    class CComponentRegistry
    {
    public:
        bool Register(ComponentTypeInfo info);
        const ComponentTypeInfo* Find(ComponentTypeId type) const;
        bool IsRegistered(ComponentTypeId type) const;
        std::size_t GetTypeCount() const;

    private:
        std::vector<ComponentTypeInfo> m_types;
    };
}
