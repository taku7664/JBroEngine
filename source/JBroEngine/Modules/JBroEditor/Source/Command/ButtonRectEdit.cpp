#include <JBro/Editor/Command/ButtonRectEdit.h>

#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/Command/CompoundCommand.h>
#include <JBro/Editor/Command/SetPropertyCommand.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Types/String.h>

namespace JBro
{
    Bool SetButtonRect(EditorApplication& editor, const ComponentAddress& address, Vector2 size, Vector2 offset)
    {
        ComponentBase* component = ResolveComponent(editor.GetObjectIds(), address);
        SetPropertyCommand::Path sizePath;
        SetPropertyCommand::Path offsetPath;
        if (component == nullptr || address.typeId != MakeStableTypeId(Component::Button2D::StaticTypeName())
            || false == SetPropertyCommand::MakeFieldPath(address.typeId, "size", sizePath)
            || false == SetPropertyCommand::MakeFieldPath(address.typeId, "offset", offsetPath))
        {
            return false;
        }
        // 글자는 코덱이 만든다 - 값을 넣었다가 읽고 되돌린다(`CommitPoints` 와 같은 절차).
        auto& button = *static_cast<Component::Button2D*>(component);
        String beforeSize;
        String beforeOffset;
        if (false == SetPropertyCommand::ReadValue(component, address.typeId, sizePath, beforeSize)
            || false == SetPropertyCommand::ReadValue(component, address.typeId, offsetPath, beforeOffset))
        {
            return false;
        }
        const Vector2 keepSize = button.size;
        const Vector2 keepOffset = button.offset;
        button.size = size;
        button.offset = offset;
        String afterSize;
        String afterOffset;
        const Bool read = SetPropertyCommand::ReadValue(component, address.typeId, sizePath, afterSize)
            && SetPropertyCommand::ReadValue(component, address.typeId, offsetPath, afterOffset);
        button.size = keepSize;
        button.offset = keepOffset;
        if (false == read)
        {
            return false;
        }
        auto compound = MakeOwnerPtr<CompoundCommand>("Set Button Rect");
        if (afterSize != beforeSize)
        {
            compound->Add(MakeOwnerPtr<SetPropertyCommand>(editor.GetObjectIds(), address, sizePath, beforeSize, afterSize));
        }
        if (afterOffset != beforeOffset)
        {
            compound->Add(MakeOwnerPtr<SetPropertyCommand>(editor.GetObjectIds(), address, offsetPath, beforeOffset, afterOffset));
        }
        if (compound->GetCount() == 0)
        {
            return false;
        }
        return editor.GetCommands().Execute(std::move(compound));
    }
}
