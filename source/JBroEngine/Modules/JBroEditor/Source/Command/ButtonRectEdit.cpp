#include <JBro/Editor/Command/ButtonRectEdit.h>

#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/Command/CompoundCommand.h>
#include <JBro/Editor/Command/SetPropertyCommand.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Framework2D/Component/Slider2D.h>
#include <JBro/Framework2D/Component/Toggle2D.h>
#include <JBro/Types/String.h>

namespace JBro
{
    Bool RectFieldsOf(ComponentBase& component, ComponentTypeId typeId, Vector2*& size, Vector2*& offset)
    {
        if (typeId == MakeStableTypeId(Component::Button2D::StaticTypeName()))
        {
            auto& button = static_cast<Component::Button2D&>(component);
            size = &button.size;
            offset = &button.offset;
            return true;
        }
        if (typeId == MakeStableTypeId(Component::Toggle2D::StaticTypeName()))
        {
            auto& toggle = static_cast<Component::Toggle2D&>(component);
            size = &toggle.size;
            offset = &toggle.offset;
            return true;
        }
        if (typeId == MakeStableTypeId(Component::Slider2D::StaticTypeName()))
        {
            auto& slider = static_cast<Component::Slider2D&>(component);
            size = &slider.size;
            offset = &slider.offset;
            return true;
        }
        if (typeId == MakeStableTypeId(Component::Mask2D::StaticTypeName()))
        {
            auto& mask = static_cast<Component::Mask2D&>(component);
            size = &mask.size;
            offset = &mask.offset;
            return true;
        }
        return false;
    }

    Bool SetButtonRect(EditorApplication& editor, const ComponentAddress& address, Vector2 size, Vector2 offset)
    {
        ComponentBase* component = ResolveComponent(editor.GetObjectIds(), address);
        SetPropertyCommand::Path sizePath;
        SetPropertyCommand::Path offsetPath;
        Vector2* sizeField = nullptr;
        Vector2* offsetField = nullptr;
        if (component == nullptr || false == RectFieldsOf(*component, address.typeId, sizeField, offsetField)
            || false == SetPropertyCommand::MakeFieldPath(address.typeId, "size", sizePath)
            || false == SetPropertyCommand::MakeFieldPath(address.typeId, "offset", offsetPath))
        {
            return false;
        }
        // 글자는 코덱이 만든다 - 값을 넣었다가 읽고 되돌린다(`CommitPoints` 와 같은 절차).
        String beforeSize;
        String beforeOffset;
        if (false == SetPropertyCommand::ReadValue(component, address.typeId, sizePath, beforeSize)
            || false == SetPropertyCommand::ReadValue(component, address.typeId, offsetPath, beforeOffset))
        {
            return false;
        }
        const Vector2 keepSize = *sizeField;
        const Vector2 keepOffset = *offsetField;
        *sizeField = size;
        *offsetField = offset;
        String afterSize;
        String afterOffset;
        const Bool read = SetPropertyCommand::ReadValue(component, address.typeId, sizePath, afterSize)
            && SetPropertyCommand::ReadValue(component, address.typeId, offsetPath, afterOffset);
        *sizeField = keepSize;
        *offsetField = keepOffset;
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
