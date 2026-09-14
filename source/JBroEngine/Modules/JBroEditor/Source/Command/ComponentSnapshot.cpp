#include <JBro/Editor/Command/ComponentSnapshot.h>

#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>

#include <utility>

namespace JBro
{
    namespace
    {
        // 표를 타고 내려가며 잎사귀마다 글자를 떠 둔다. 어디가 잎사귀인지는
        // 코덱의 존재가 답한다 - 구조를 가진 타입은 필드로 말한다.
        void CaptureValues(
            const PropertyTable& table,
            void* owner,
            ComponentBase& component,
            ComponentTypeId typeId,
            SetPropertyCommand::Path& path,
            Array<ComponentValue>& out)
        {
            for (std::uint32_t index = 0; index < table.count; ++index)
            {
                const PropertyInfo& property = table.properties[index];
                if (property.type == nullptr || property.Address == nullptr
                    || false == property.serialize)
                {
                    // 저장하지 않는 값은 되살릴 필요도 없다. 다음 프레임이 다시 만든다.
                    continue;
                }
                if (path.depth >= SetPropertyCommand::MaxDepth)
                {
                    continue;
                }
                void* address = property.Address(owner);
                if (address == nullptr)
                {
                    continue;
                }

                path.indices[path.depth] = index;
                ++path.depth;
                if (property.type->fields != nullptr)
                {
                    CaptureValues(*property.type->fields, address, component, typeId,
                        path, out);
                }
                else if (property.type->codec != nullptr)
                {
                    ComponentValue value;
                    value.path = path;
                    if (SetPropertyCommand::ReadValue(component, typeId, path, value.text))
                    {
                        out.Add(std::move(value));
                    }
                }
                --path.depth;
            }
        }
    }

    bool CaptureComponent(ComponentBase& component, ComponentSnapshot& out)
    {
        const ComponentTypeId typeId = component.GetTypeId();
        const PropertyTable* table = PropertyRegistry::Lookup(typeId);
        if (table == nullptr)
        {
            return false;
        }
        out.typeId = typeId;
        out.enabled = component.IsEnabled();
        out.values.Clear();
        SetPropertyCommand::Path path;
        CaptureValues(*table, &component, component, typeId, path, out.values);
        return true;
    }

    bool ApplyComponent(ComponentBase& component, const ComponentSnapshot& snapshot)
    {
        if (component.GetTypeId() != snapshot.typeId)
        {
            // 다른 타입에 값을 쏟으면 길이 우연히 맞는 자리마다 엉뚱한 값이 들어간다.
            return false;
        }
        component.SetEnabled(snapshot.enabled);
        for (std::size_t index = 0; index < snapshot.values.Size(); ++index)
        {
            SetPropertyCommand::ApplyValue(component, snapshot.typeId,
                snapshot.values[index].path, snapshot.values[index].text);
        }
        return true;
    }
}
