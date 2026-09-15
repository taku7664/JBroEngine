#include <JBro/Editor/Command/SetPropertyCommand.h>

#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Reflection/ReflectedYaml.h>

#include <utility>

namespace JBro
{
    namespace
    {
        // 컨테이너의 글자는 이 키 하나를 가진 YAML 문서다. 캔버스 파일과 같은 걸음으로
        // 쓰고 읽으므로(D-86) 파일에 적히는 모양 그대로다.
        constexpr const char* ContainerKey = "Value";

        bool IsContainer(const TypeDescriptor& type)
        {
            return type.arrayOps != nullptr || type.tableOps != nullptr;
        }

        bool WriteContainer(const TypeDescriptor& type, const void* address, String& text)
        {
            YamlWriter writer;
            ReflectedYamlError error;
            if (false == WriteReflectedValue(writer, ContainerKey, type, address, error))
            {
                return false;
            }
            text = writer.GetText();
            return true;
        }

        bool ReadContainer(const TypeDescriptor& type, void* address, const String& text)
        {
            YamlDocument document;
            YamlError parseError;
            if (false == document.Parse(text.c_str(), text.size(), parseError))
            {
                return false;
            }
            const std::uint32_t node = document.Find(document.GetRoot(), ContainerKey);
            if (node == YamlDocument::InvalidNode)
            {
                return false;
            }
            ReflectedYamlError error;
            return ReadReflectedValue(document, node, type, address, error);
        }
    }

    bool SetPropertyCommand::Path::Equals(const Path& other) const
    {
        if (depth != other.depth)
        {
            return false;
        }
        for (std::uint32_t step = 0; step < depth; ++step)
        {
            if (indices[step] != other.indices[step])
            {
                return false;
            }
        }
        return true;
    }

    bool SetPropertyCommand::ResolveLeaf(
        ComponentBase& component,
        ComponentTypeId typeId,
        const Path& path,
        void*& address,
        const TypeDescriptor*& type)
    {
        const PropertyTable* table = PropertyRegistry::Lookup(typeId);
        if (table == nullptr || path.depth == 0 || path.depth > MaxDepth)
        {
            return false;
        }

        void* owner = &component;
        const TypeDescriptor* found = nullptr;
        for (std::uint32_t step = 0; step < path.depth; ++step)
        {
            if (table == nullptr || path.indices[step] >= table->count)
            {
                return false;
            }
            const PropertyInfo& property = table->properties[path.indices[step]];
            if (property.type == nullptr || property.Address == nullptr)
            {
                return false;
            }
            owner = property.Address(owner);
            if (owner == nullptr)
            {
                return false;
            }
            found = property.type;
            table = found->fields;
        }

        // 잎사귀는 코덱을 가진 값이거나 **컨테이너**다. 길은 처음 만나는 컨테이너에서
        // 멈추고, 그 아래는 컨테이너 전체의 글자가 담는다(D-86).
        if (found == nullptr || (found->codec == nullptr && false == IsContainer(*found)))
        {
            return false;
        }
        address = owner;
        type = found;
        return true;
    }

    bool SetPropertyCommand::ApplyValue(
        ComponentBase& component,
        ComponentTypeId typeId,
        const Path& path,
        const String& text)
    {
        void* address = nullptr;
        const TypeDescriptor* type = nullptr;
        if (false == ResolveLeaf(component, typeId, path, address, type))
        {
            return false;
        }
        if (type->codec != nullptr)
        {
            // 코덱은 못 읽으면 값을 건드리지 않기로 약속한다.
            return type->codec->FromText != nullptr
                && type->codec->FromText(address, text.c_str(), text.size());
        }

        // **컨테이너는 전부 되거나 하나도 안 된다.** 읽기는 비우고 원소를 하나씩 채우므로,
        // 뒤쪽 원소에서 막히면 반쯤 채워진 채로 남는다. 먼저 떠 두었다가 도로 쓴다.
        String previous;
        if (false == WriteContainer(*type, address, previous))
        {
            return false;
        }
        if (ReadContainer(*type, address, text))
        {
            return true;
        }
        ReadContainer(*type, address, previous);
        return false;
    }

    SetPropertyCommand::SetPropertyCommand(
        EditorObjectRegistry& registry,
        const ComponentAddress& address,
        const Path& path,
        String oldValue,
        String newValue)
        : m_registry(&registry)
        , m_address(address)
        , m_path(path)
        , m_oldValue(std::move(oldValue))
        , m_newValue(std::move(newValue))
    {
    }

    const char* SetPropertyCommand::GetName() const
    {
        return "Set Property";
    }

    bool SetPropertyCommand::Execute()
    {
        return WriteValue(m_newValue);
    }

    void SetPropertyCommand::Undo()
    {
        WriteValue(m_oldValue);
    }

    void SetPropertyCommand::Redo()
    {
        WriteValue(m_newValue);
    }

    bool SetPropertyCommand::CanMerge(const EditorCommand& newer) const
    {
        // **같은 잎사귀를 이어서 고치는 중일 때만 합친다.** 다른 필드로 옮겨 갔는데
        // 합치면 그 편집이 되돌리기에서 사라진다.
        //
        // 레지스트리는 비교하지 않는다. 스택 하나에는 에디터 하나의 커맨드만 쌓이고,
        // 에디터는 레지스트리를 하나만 가진다 - 비교해도 거짓이 될 길이 없다.
        const auto* other = dynamic_cast<const SetPropertyCommand*>(&newer);
        return other != nullptr
            && other->m_address.Equals(m_address)
            && other->m_path.Equals(m_path);
    }

    bool SetPropertyCommand::TryMerge(const EditorCommand& newer)
    {
        if (false == CanMerge(newer))
        {
            return false;
        }
        // 처음 값은 이쪽 것을 지킨다 - 드래그 전체를 한 번에 되돌려야 한다.
        m_newValue = static_cast<const SetPropertyCommand&>(newer).m_newValue;
        return true;
    }

    bool SetPropertyCommand::ReadValue(
        ComponentBase& component,
        ComponentTypeId typeId,
        const Path& path,
        String& text)
    {
        void* address = nullptr;
        const TypeDescriptor* type = nullptr;
        if (false == ResolveLeaf(component, typeId, path, address, type))
        {
            return false;
        }
        if (type->codec != nullptr)
        {
            // 크기를 정해 두지 않는다. 처음에는 512바이트 버퍼로 읽었는데, 그보다 긴
            // 값은 읽기가 실패했고 스냅샷은 그 값을 조용히 뺐다.
            return ReflectedValueToText(*type->codec, address, text);
        }
        return WriteContainer(*type, address, text);
    }

    bool SetPropertyCommand::WriteValue(const String& value)
    {
        ComponentBase* component = ResolveComponent(*m_registry, m_address);
        if (component == nullptr)
        {
            // 컴포넌트가 사라졌다. 되돌릴 곳이 없는 것은 실패지 사고가 아니다.
            return false;
        }
        return ApplyValue(*component, m_address.typeId, m_path, value);
    }
}
