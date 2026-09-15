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

        // 빼서 끼운다. 조작 함수에는 옮기기가 없으므로 끝에 임시 자리를 하나 만들어
        // 원소 코덱의 `Assign` 으로 밀어 옮긴다 - 원소 타입을 모르기 때문이다.
        bool MoveElement(const ArrayOps& ops, const TypeDescriptor& element, void* array,
            std::size_t from, std::size_t to)
        {
            if (element.codec == nullptr || element.codec->Assign == nullptr)
            {
                return false;
            }
            if (false == ops.AddDefault(array))
            {
                return false;
            }
            // **늘린 뒤에 주소를 받는다.** 늘리면서 저장소가 옮겨 갈 수 있고, 그 전에 받은
            // 주소는 죽는다. 처음 구현(인스펙터 안)은 늘리기 전에 받아 두고 거기에 썼다.
            const std::size_t scratchIndex = ops.GetSize(array) - 1;
            const auto slot = [&](std::size_t index) { return ops.GetElement(array, index); };
            element.codec->Assign(slot(scratchIndex), slot(from));
            if (from < to)
            {
                for (std::size_t at = from; at < to; ++at)
                {
                    element.codec->Assign(slot(at), slot(at + 1));
                }
            }
            else
            {
                for (std::size_t at = from; at > to; --at)
                {
                    element.codec->Assign(slot(at), slot(at - 1));
                }
            }
            element.codec->Assign(slot(to), slot(scratchIndex));
            return ops.RemoveAt(array, scratchIndex);
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
            || ops.RemoveAt == nullptr)
        {
            return false;
        }
        const std::size_t size = ops.GetSize(array);

        switch (edit.kind)
        {
        case ListEdit::Kind::SetElement:
        {
            if (edit.index >= size)
            {
                return false;
            }
            void* element = ops.GetElement(array, edit.index);
            return element != nullptr && SetElement(*arrayType.element, element, edit);
        }
        case ListEdit::Kind::Add:
            return ops.AddDefault(array);
        case ListEdit::Kind::Remove:
            return edit.index < size && ops.RemoveAt(array, edit.index);
        case ListEdit::Kind::Move:
            if (edit.index >= size || edit.to >= size)
            {
                return false;
            }
            if (edit.index == edit.to)
            {
                return true;
            }
            return MoveElement(ops, *arrayType.element, array, edit.index, edit.to);
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
