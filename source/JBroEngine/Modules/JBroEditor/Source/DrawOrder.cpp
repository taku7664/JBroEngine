#include <JBro/Editor/EditorActions.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/Command/CompoundCommand.h>
#include <JBro/Editor/Command/SetPropertyCommand.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/FixedString.h>

#include <algorithm>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

namespace JBro::EditorActions
{
    namespace
    {
        const ComponentTypeId SpriteTypeId = MakeStableTypeId(Component::SpriteRenderer2D::StaticTypeName());
        const ComponentTypeId TextTypeId = MakeStableTypeId(Component::Text2D::StaticTypeName());

        // 같은 레이어·같은 `renderOrder` 의 그리는 컴포넌트 하나다. 렌더러가 견주는 차례(`drawSequence`, 그다음 컴포넌트 번호)로 줄 선다.
        struct DrawEntry
        {
            ComponentBase* component = nullptr;
            ComponentTypeId typeId = 0;
            GameObject* owner = nullptr;
            Int32 sequence = 0;
            InstanceId id = InvalidInstanceId;
            Bool target = false;
        };

        // 오브젝트가 어느 묶음에서 그려지는가. 첫 `SpriteRenderer2D`, 없으면 첫 `Text2D` 의 `renderOrder` 다.
        Bool FindGroup(Canvas& canvas, GameObject& object, Int32& order)
        {
            if (const auto* sprite = canvas.FindComponentRaw<Component::SpriteRenderer2D>(&object))
            {
                order = sprite->renderOrder;
                return true;
            }
            if (const auto* text = canvas.FindComponentRaw<Component::Text2D>(&object))
            {
                order = text->renderOrder;
                return true;
            }
            return false;
        }

        template <typename T>
        void Add(GameObject& owner, ComponentBase& component, const GameObject& target, Int32 order, ComponentTypeId typeId,
            Array<DrawEntry>& out)
        {
            const T& drawing = static_cast<const T&>(component);
            if (drawing.renderOrder != order)
            {
                return;
            }
            DrawEntry entry;
            entry.component = &component;
            entry.typeId = typeId;
            entry.owner = &owner;
            entry.sequence = drawing.drawSequence;
            entry.id = component.GetInstanceId();
            entry.target = &owner == &target;
            out.Add(entry);
        }

        // 그 레이어에서 그 `renderOrder` 로 그리는 컴포넌트를 모두 모은다. 슬롯을 타입 번호로 가린다.
        void Collect(Canvas& canvas, const GameObject& target, Int32 order, Array<DrawEntry>& out)
        {
            canvas.ForEachObject([&](GameObject& object)
            {
                if (object.GetLayerId() != target.GetLayerId())
                {
                    return;
                }
                for (const ComponentSlot& slot : object.GetComponents())
                {
                    ComponentBase* component = slot.reference.TryGet();
                    if (component == nullptr)
                    {
                        continue;
                    }
                    if (slot.typeId == SpriteTypeId)
                    {
                        Add<Component::SpriteRenderer2D>(object, *component, target, order, SpriteTypeId, out);
                    }
                    else if (slot.typeId == TextTypeId)
                    {
                        Add<Component::Text2D>(object, *component, target, order, TextTypeId, out);
                    }
                }
            });
        }

        // 옮긴 뒤의 줄을 `entries` 에 다시 쓴다. 옮길 데가 없으면 거짓이다.
        Bool Reorder(Array<DrawEntry>& entries, DrawOrderMove move)
        {
            Array<DrawEntry> block;
            Array<DrawEntry> others;
            std::size_t first = entries.Size();
            std::size_t last = 0;
            for (std::size_t index = 0; index < entries.Size(); ++index)
            {
                if (entries[index].target)
                {
                    block.Add(entries[index]);
                    first = (std::min)(first, index);
                    last = index;
                }
                else
                {
                    others.Add(entries[index]);
                }
            }
            if (block.IsEmpty() || others.IsEmpty())
            {
                return false;
            }
            // 끼울 자리는 `others` 의 번호다. 그 앞에 묶음을 통째로 넣는다.
            std::size_t insertAt = 0;
            switch (move)
            {
            case DrawOrderMove::ToFront:
                // 묶음이 이미 맨 위에 붙어 있으면 옮길 데가 없다.
                if (first == entries.Size() - block.Size())
                {
                    return false;
                }
                insertAt = others.Size();
                break;
            case DrawOrderMove::ToBack:
                if (last == block.Size() - 1)
                {
                    return false;
                }
                insertAt = 0;
                break;
            case DrawOrderMove::Forward:
            {
                // 묶음의 맨 위보다 위에 있는 첫 남의 것 하나를 넘는다.
                std::size_t passed = 0;
                Bool found = false;
                for (std::size_t index = 0; index < entries.Size(); ++index)
                {
                    if (entries[index].target)
                    {
                        continue;
                    }
                    if (index > last)
                    {
                        found = true;
                        break;
                    }
                    ++passed;
                }
                if (false == found)
                {
                    return false;
                }
                insertAt = passed + 1;
                break;
            }
            case DrawOrderMove::Backward:
            {
                // 묶음의 맨 아래보다 아래에 있는 마지막 남의 것 하나의 앞으로 간다.
                std::size_t below = 0;
                for (std::size_t index = 0; index < first; ++index)
                {
                    if (false == entries[index].target)
                    {
                        ++below;
                    }
                }
                if (below == 0)
                {
                    return false;
                }
                insertAt = below - 1;
                break;
            }
            }
            Array<DrawEntry> result;
            for (std::size_t index = 0; index <= others.Size(); ++index)
            {
                if (index == insertAt)
                {
                    for (const DrawEntry& entry : block)
                    {
                        result.Add(entry);
                    }
                }
                if (index < others.Size())
                {
                    result.Add(others[index]);
                }
            }
            entries = std::move(result);
            return true;
        }

        // 바꿀 줄을 세운다. 못 하면 까닭을, 할 수 있으면 nullptr 를 돌려준다.
        const char* Plan(EditorApplication& editor, GameObject& object, DrawOrderMove move, Array<DrawEntry>& entries)
        {
            Canvas* canvas = editor.GetCanvas();
            if (canvas == nullptr)
            {
                return Loc::TextOr(LocKeys::BlockedNoProject, "no project is open");
            }
            Int32 order = 0;
            if (false == FindGroup(*canvas, object, order))
            {
                return Loc::TextOr(LocKeys::BlockedNoDrawing, "there is no SpriteRenderer2D or Text2D to order");
            }
            Collect(*canvas, object, order, entries);
            std::sort(entries.begin(), entries.end(), [](const DrawEntry& left, const DrawEntry& right)
            {
                if (left.sequence != right.sequence)
                {
                    return left.sequence < right.sequence;
                }
                return left.id < right.id;
            });
            if (false == Reorder(entries, move))
            {
                return move == DrawOrderMove::Forward || move == DrawOrderMove::ToFront
                    ? Loc::TextOr(LocKeys::BlockedAlreadyFront, "it is already drawn in front")
                    : Loc::TextOr(LocKeys::BlockedAlreadyBack, "it is already drawn at the back");
            }
            return nullptr;
        }
    }

    const char* WhyNoDrawOrder(EditorApplication& editor, GameObject& object, DrawOrderMove move)
    {
        Array<DrawEntry> entries;
        return Plan(editor, object, move, entries);
    }

    Bool MoveDrawOrder(EditorApplication& editor, GameObject& object, DrawOrderMove move)
    {
        Array<DrawEntry> entries;
        if (Plan(editor, object, move, entries) != nullptr)
        {
            return false;
        }
        // **새 차례는 맨 위가 0 이고 아래로 갈수록 작다.** 새로 만든 것은 0 이라 맨 위와 겨루고 번호가 커서 그 위에 선다 -
        // 순서를 한 번 바꾼 뒤에도 새것이 위에 오는 지금의 규칙(만든 차례)이 그대로다.
        EditorObjectRegistry& registry = editor.GetObjectIds();
        OwnerPtr<CompoundCommand> compound = MakeOwnerPtr<CompoundCommand>("Draw Order");
        const Int32 top = static_cast<JBro::Int32>(entries.Size()) - 1;
        for (std::size_t index = 0; index < entries.Size(); ++index)
        {
            const DrawEntry& entry = entries[index];
            const Int32 sequence = static_cast<JBro::Int32>(index) - top;
            if (sequence == entry.sequence)
            {
                continue;
            }
            ComponentAddress address;
            SetPropertyCommand::Path path;
            String before;
            if (false == MakeComponentAddress(registry, *entry.owner, *entry.component, address)
                || false == SetPropertyCommand::MakeFieldPath(entry.typeId, "drawSequence", path)
                || false == SetPropertyCommand::ReadValue(*entry.component, entry.typeId, path, before))
            {
                return false;
            }
            Fixed::String<16> text;
            text.Append(sequence.Get());
            compound->Add(MakeOwnerPtr<SetPropertyCommand>(registry, address, path, before, String(text.View())));
        }
        // **한 손짓이 여럿을 바꿔도 커맨드는 하나다**(§11.5) - 되돌리기 한 번에 순서가 통째로 돌아온다.
        return editor.GetCommands().Execute(std::move(compound));
    }
}
