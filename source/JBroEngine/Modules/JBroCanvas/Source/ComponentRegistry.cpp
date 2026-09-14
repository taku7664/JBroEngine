#include <JBro/Canvas/ComponentRegistry.h>

#include <cstring>

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

    Array<const ComponentTypeInfo*> ComponentRegistry::CollectTypes() const
    {
        Array<const ComponentTypeInfo*> types;
        for (const auto& entry : m_types)
        {
            const ComponentTypeInfo* info = &entry.MappedValue;
            const char* name = NameTable::Get().Resolve(info->name);
            if (name == nullptr)
            {
                continue;
            }
            // 이름 자리에 끼워 넣는다. 타입은 수십 개라 이 자리에 정렬을
            // 들여올 이유가 없고, 등록은 시작할 때 한 번뿐이다.
            std::size_t at = types.Size();
            while (at > 0)
            {
                const char* previous = NameTable::Get().Resolve(types[at - 1]->name);
                if (previous == nullptr || std::strcmp(previous, name) <= 0)
                {
                    break;
                }
                --at;
            }
            types.Add(info);
            for (std::size_t index = types.Size() - 1; index > at; --index)
            {
                const ComponentTypeInfo* moved = types[index - 1];
                types[index - 1] = types[index];
                types[index] = moved;
            }
        }
        return types;
    }

    std::size_t ComponentRegistry::GetCount() const
    {
        return m_types.Size();
    }
}
