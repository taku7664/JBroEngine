#include <JBro/Editor/Command/ListEdit.h>

#include <JBro/Reflection/TypeDescriptor.h>

#include <utility>

namespace JBro
{
    namespace
    {
        bool SetElement(const TypeDescriptor& element, void* address, const ListEdit& edit)
        {
            if (edit.deltaCount == 0)
            {
                // 델타가 없는 값이다. 주된 대상에서 고른 값을 그대로 준다(D-83).
                return element.codec != nullptr && element.codec->FromText != nullptr
                    && element.codec->FromText(address, edit.text.c_str(), edit.text.size());
            }
            ScalarRun run;
            if (false == CollectNumbers(element, address, run) || run.count != edit.deltaCount)
            {
                return false;
            }
            for (std::uint32_t at = 0; at < run.count; ++at)
            {
                *run.values[at] += edit.delta[at];
            }
            return true;
        }
    }

    bool ApplyListEdit(const TypeDescriptor& arrayType, void* array, const ListEdit& edit)
    {
        if (arrayType.arrayOps == nullptr || arrayType.element == nullptr || array == nullptr)
        {
            return false;
        }
        const ArrayOps& ops = *arrayType.arrayOps;
        if (ops.GetSize == nullptr || ops.GetElement == nullptr || ops.AddDefault == nullptr
            || ops.RemoveAt == nullptr || ops.Move == nullptr)
        {
            return false;
        }

        switch (edit.kind)
        {
        case ListEdit::Kind::SetElement:
        {
            if (edit.index >= ops.GetSize(array))
            {
                return false;
            }
            void* element = ops.GetElement(array, edit.index);
            return element != nullptr && SetElement(*arrayType.element, element, edit);
        }
        case ListEdit::Kind::Add:
            return ops.AddDefault(array);
        case ListEdit::Kind::Remove:
            // 범위는 조작 함수가 본다 - 끝을 넘는 번호는 `RemoveAt` 이 거절한다.
            return ops.RemoveAt(array, edit.index);
        case ListEdit::Kind::Move:
            // 옮기기도 원소 타입을 아는 조작 함수가 한다(D-89). 처음에는 여기서 원소 코덱의
            // `Assign` 으로 밀었고, 코덱이 없는 `Vec2`·`Color` 목록은 옮기지 못했다.
            return ops.Move(array, edit.index, edit.to);
        }
        return false;
    }

    OwnerPtr<CompoundCommand> MakeListEditCommand(
        EditorObjectRegistry& registry,
        const Array<ComponentAddress>& targets,
        const SetPropertyCommand::Path& path,
        const Array<ListEdit>& edits)
    {
        auto compound = MakeOwnerPtr<CompoundCommand>("Edit List");
        for (std::size_t index = 0; index < targets.Size(); ++index)
        {
            const ComponentAddress& address = targets[index];
            ComponentBase* component = ResolveComponent(registry, address);
            void* array = nullptr;
            const TypeDescriptor* type = nullptr;
            if (component == nullptr
                || false == SetPropertyCommand::ResolveLeaf(*component, address.typeId, path,
                    array, type)
                || type->arrayOps == nullptr)
            {
                continue;
            }

            String before;
            if (false == SetPropertyCommand::ReadValue(*component, address.typeId, path, before))
            {
                continue;
            }
            bool applied = true;
            for (std::size_t at = 0; at < edits.Size() && applied; ++at)
            {
                applied = ApplyListEdit(*type, array, edits[at]);
            }
            String after;
            const bool read = applied
                && SetPropertyCommand::ReadValue(*component, address.typeId, path, after);
            // **도로 되돌린다.** 쓰는 것은 커맨드의 몫이고, 편집이 중간에 막혔으면 반쯤
            // 바뀐 배열이 남아 있다.
            SetPropertyCommand::ApplyValue(*component, address.typeId, path, before);
            if (false == read || after == before)
            {
                continue;
            }
            compound->Add(MakeOwnerPtr<SetPropertyCommand>(
                registry, address, path, std::move(before), std::move(after)));
        }
        return compound;
    }
}
