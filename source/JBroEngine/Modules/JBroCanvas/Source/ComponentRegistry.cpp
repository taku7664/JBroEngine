#include <JBro/Canvas/ComponentRegistry.h>

namespace JBro
{
    ComponentRegistry& ComponentRegistry::Get()
    {
        static ComponentRegistry registry;
        return registry;
    }

    bool ComponentRegistry::Register(const ComponentTypeInfo& info)
    {
        if (info.name == InvalidNameId
            || info.typeId == InvalidComponentTypeId
            || info.Attach == nullptr)
        {
            return false;
        }
        // 이름과 타입 id 는 같은 문자열에 건 같은 해시다. 어긋나면 씬 파일이 가리키는
        // 타입과 실제로 붙는 타입이 갈린다(`ScriptRegistry` 가 같은 이유로 같은 검사를 한다).
        if (info.name != info.typeId)
        {
            return false;
        }
        // 같은 이름이 이미 있으면 TryAdd 가 거절한다.
        return m_types.TryAdd(info.name, info);
    }

    const ComponentTypeInfo* ComponentRegistry::Find(NameId name) const
    {
        return m_types.Find(name);
    }

    const ComponentTypeInfo* ComponentRegistry::Find(const char* name) const
    {
        return Find(MakeNameId(name));
    }

    std::size_t ComponentRegistry::GetCount() const
    {
        return m_types.Size();
    }
}
