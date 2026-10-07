#include <JBro/NodeGraph/Domain.h>

namespace JBro::NodeGraph
{
    Domain::Domain(String name)
        : m_name(static_cast<String&&>(name))
    {
    }

    PinType Domain::AddPinType(PinTypeInfo info)
    {
        const PinType type = static_cast<UInt32>(m_pinTypes.Size());
        m_pinTypes.Add(static_cast<PinTypeInfo&&>(info));
        return type;
    }

    void Domain::AddConversion(PinType from, PinType to)
    {
        if (CanConvert(from, to))
        {
            return;
        }
        m_conversions.Add(Conversion{from, to});
    }

    Bool Domain::AddNodeType(NodeTypeInfo info)
    {
        if (FindNodeType(info.name.View()) != nullptr)
        {
            return false;
        }
        m_nodeTypes.Add(static_cast<NodeTypeInfo&&>(info));
        return true;
    }

    const PinTypeInfo* Domain::FindPinType(PinType type) const noexcept
    {
        if (type.Get() >= m_pinTypes.Size())
        {
            return nullptr;
        }
        return &m_pinTypes[type.Get()];
    }

    const NodeTypeInfo* Domain::FindNodeType(std::string_view name) const noexcept
    {
        return m_nodeTypes.FindBy([name](const NodeTypeInfo& info) { return info.name.View() == name; });
    }

    Bool Domain::CanConvert(PinType from, PinType to) const noexcept
    {
        if (from.Get() == to.Get())
        {
            return true;
        }
        return m_conversions.FindBy([from, to](const Conversion& conversion)
            {
                return conversion.from.Get() == from.Get() && conversion.to.Get() == to.Get();
            }) != nullptr;
    }

    std::size_t Domain::FindInput(const NodeTypeInfo& type, std::string_view pin) noexcept
    {
        const std::size_t index = type.inputs.IndexOfBy([pin](const PinInfo& info) { return info.name.View() == pin; });
        return index == Array<PinInfo>::InvalidIndex ? InvalidPinIndex : index;
    }

    std::size_t Domain::FindOutput(const NodeTypeInfo& type, std::string_view pin) noexcept
    {
        const std::size_t index = type.outputs.IndexOfBy([pin](const PinInfo& info) { return info.name.View() == pin; });
        return index == Array<PinInfo>::InvalidIndex ? InvalidPinIndex : index;
    }

    Bool DomainRegistry::Add(OwnerPtr<Domain> domain)
    {
        if (!domain || Find(domain->GetName().View()) != nullptr)
        {
            return false;
        }
        m_domains.Add(static_cast<OwnerPtr<Domain>&&>(domain));
        return true;
    }

    const Domain* DomainRegistry::Find(std::string_view name) const noexcept
    {
        for (const OwnerPtr<Domain>& domain : m_domains)
        {
            if (domain->GetName().View() == name)
            {
                return domain.Get();
            }
        }
        return nullptr;
    }
}
