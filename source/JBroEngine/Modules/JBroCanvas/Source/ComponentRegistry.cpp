#include <JBro/Canvas/ComponentRegistry.h>

#include <JBro/Runtime/ScriptRegistry.h>

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

    namespace
    {
        void FillScriptEntry(const ScriptTypeInfo& script, ComponentTypeInfo& out)
        {
            out = ComponentTypeInfo{};
            out.name = script.name;
            out.typeId = script.typeId;
            out.category = ComponentCategory::Script;
            out.multiplicity = ComponentMultiplicity::Multiple;
            out.Attach = [](Canvas& canvas, Object::GameObject* owner, NameId name) -> ComponentBase*
            {
                return canvas.AttachScript(owner, name);
            };
            out.Detach = [](Canvas& canvas, Object::GameObject* owner, ComponentBase* component) -> bool
            {
                // 스크립트 풀에서 온 것만 뗀다. 내림 변환은 그 타입이 스크립트 표에 있을 때만 옳다.
                if (component == nullptr || ScriptRegistry::Get().Find(component->GetTypeId()) == nullptr)
                {
                    return false;
                }
                return canvas.DetachScript(owner, static_cast<GameScriptBase*>(component));
            };
        }

        const char* NameOf(const ComponentTypeInfo& info)
        {
            return NameTable::Get().Resolve(info.name);
        }

        // 이름 자리에 끼워 넣는다. 타입은 수십 개라 이 자리에 정렬을 들여올 이유가 없다.
        void InsertByName(Array<ComponentTypeInfo>& types, std::size_t from, const ComponentTypeInfo& info)
        {
            const char* name = NameOf(info);
            std::size_t at = types.Size();
            while (at > from)
            {
                const char* previous = NameOf(types[at - 1]);
                if (previous == nullptr || std::strcmp(previous, name) <= 0)
                {
                    break;
                }
                --at;
            }
            types.Add(info);
            for (std::size_t index = types.Size() - 1; index > at; --index)
            {
                const ComponentTypeInfo moved = types[index - 1];
                types[index - 1] = types[index];
                types[index] = moved;
            }
        }
    }

    bool ComponentRegistry::FindAttachable(NameId name, ComponentTypeInfo& out) const
    {
        if (const ComponentTypeInfo* builtin = Find(name))
        {
            out = *builtin;
            return true;
        }
        if (const ScriptTypeInfo* script = ScriptRegistry::Get().Find(name))
        {
            FillScriptEntry(*script, out);
            return true;
        }
        return false;
    }

    Array<ComponentTypeInfo> ComponentRegistry::CollectAttachableTypes() const
    {
        Array<ComponentTypeInfo> types;
        const Array<const ComponentTypeInfo*> builtins = CollectTypes();
        for (const ComponentTypeInfo* builtin : builtins)
        {
            types.Add(*builtin);
        }
        const std::size_t scriptsFrom = types.Size();
        ScriptRegistry::Get().ForEach([&types, scriptsFrom, this](const ScriptTypeInfo& script)
        {
            // 빌트인과 같은 이름이면 빌트인이 이긴다(`FindAttachable` 과 같은 순서). 두 번 보이지 않게 뺀다.
            if (Find(script.name) != nullptr || NameTable::Get().Resolve(script.name) == nullptr)
            {
                return;
            }
            ComponentTypeInfo entry;
            FillScriptEntry(script, entry);
            InsertByName(types, scriptsFrom, entry);
        });
        return types;
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

    bool ComponentRegistry::CanAttach(const Object::GameObject& object, NameId name) const
    {
        ComponentTypeInfo found;
        if (false == FindAttachable(name, found))
        {
            return false;
        }
        const ComponentTypeInfo* info = &found;
        if (info->multiplicity == ComponentMultiplicity::Multiple)
        {
            return true;
        }
        // 슬롯이 들고 있는 타입 id 사본을 본다. 컴포넌트를 따라가지 않으므로
        // 죽은 참조가 섞여 있어도 안전하고, 오브젝트당 컴포넌트는 몇 개뿐이다.
        for (const ComponentSlot& slot : object.GetComponents())
        {
            if (slot.typeId == info->typeId)
            {
                return false;
            }
        }
        return true;
    }

    std::size_t ComponentRegistry::GetCount() const
    {
        return m_types.Size();
    }
}
