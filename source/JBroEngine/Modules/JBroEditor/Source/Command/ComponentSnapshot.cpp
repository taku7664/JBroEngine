#include <JBro/Editor/Command/ComponentSnapshot.h>

#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>

#include <utility>

namespace JBro
{
    namespace
    {
        // 표를 타고 내려가며 잎사귀마다 글자를 떠 둔다. 구조를 가진 타입은 필드로
        // 내려가고, 코덱을 가진 값과 컨테이너가 잎사귀다(D-86).
        //
        // **하나라도 못 뜨면 거짓이다.** 처음에는 컨테이너를 건너뛰고 읽기에 실패한 값을
        // 빼고도 성공이라 말했다 - 되살린 컴포넌트에서 그 값만 기본값이 되고 아무도 모른다.
        bool CaptureValues(
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
                bool captured = true;
                if (property.type->fields != nullptr)
                {
                    captured = CaptureValues(*property.type->fields, address, component,
                        typeId, path, out);
                }
                else
                {
                    ComponentValue value;
                    value.path = path;
                    captured = SetPropertyCommand::ReadValue(component, typeId, path, value.text);
                    if (captured)
                    {
                        out.Add(std::move(value));
                    }
                }
                --path.depth;
                if (false == captured)
                {
                    return false;
                }
            }
            return true;
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
        return CaptureValues(*table, &component, component, typeId, path, out.values);
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
