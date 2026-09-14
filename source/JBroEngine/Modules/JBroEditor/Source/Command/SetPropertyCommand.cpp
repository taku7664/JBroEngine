#include "SetPropertyCommand.h"

#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>

#include <utility>

namespace JBro
{
    namespace
    {
        constexpr std::size_t TextCapacity = 512;

        // 컴포넌트에서 길을 따라 내려가 잎사귀의 주소와 그 타입을 찾는다.
        // 길 중간이 사라졌거나(등록 해제) 잎사귀에 코덱이 없으면 거짓이다.
        bool Resolve(
            ComponentBase& component,
            ComponentTypeId typeId,
            const SetPropertyCommand::Path& path,
            void*& address,
            const TypeDescriptor*& type)
        {
            const PropertyTable* table = PropertyRegistry::Lookup(typeId);
            if (table == nullptr || path.depth == 0
                || path.depth > SetPropertyCommand::MaxDepth)
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

            if (found == nullptr || found->codec == nullptr)
            {
                return false;
            }
            address = owner;
            type = found;
            return true;
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

    SetPropertyCommand::SetPropertyCommand(
        SafePtr<ComponentBase> component,
        ComponentTypeId typeId,
        const Path& path,
        String oldValue,
        String newValue)
        : m_component(component)
        , m_typeId(typeId)
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

    bool SetPropertyCommand::TryMerge(const EditorCommand& newer)
    {
        // **같은 잎사귀를 이어서 고치는 중일 때만 합친다.** 다른 필드로 옮겨 갔는데
        // 합치면 그 편집이 되돌리기에서 사라진다.
        const auto* other = dynamic_cast<const SetPropertyCommand*>(&newer);
        if (other == nullptr
            || other->m_component.TryGet() != m_component.TryGet()
            || other->m_typeId != m_typeId
            || false == other->m_path.Equals(m_path))
        {
            return false;
        }
        // 처음 값은 이쪽 것을 지킨다 - 드래그 전체를 한 번에 되돌려야 한다.
        m_newValue = other->m_newValue;
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
        if (false == Resolve(component, typeId, path, address, type)
            || type->codec->ToText == nullptr)
        {
            return false;
        }
        char buffer[TextCapacity] = {};
        std::size_t required = 0;
        if (false == type->codec->ToText(address, buffer, sizeof(buffer), required))
        {
            // 버퍼보다 긴 값이다. 반쪽을 되돌리기 값으로 쓰면 되돌렸을 때 잘린다.
            return false;
        }
        text = buffer;
        return true;
    }

    bool SetPropertyCommand::WriteValue(const String& value)
    {
        ComponentBase* component = m_component.TryGet();
        if (component == nullptr)
        {
            // 컴포넌트가 사라졌다. 되돌릴 곳이 없는 것은 실패지 사고가 아니다.
            return false;
        }
        void* address = nullptr;
        const TypeDescriptor* type = nullptr;
        if (false == Resolve(*component, m_typeId, m_path, address, type)
            || type->codec->FromText == nullptr)
        {
            return false;
        }
        return type->codec->FromText(address, value.c_str(), value.size());
    }
}
