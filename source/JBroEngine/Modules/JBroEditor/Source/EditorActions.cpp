#include <JBro/Editor/EditorActions.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Editor/Command/ComponentCommands.h>
#include <JBro/Editor/Command/HierarchyCommands.h>
#include <JBro/Editor/Command/LayerCommands.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/ComponentMenuTable.h>
#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/EditorNames.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/GuideFocus.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/ScriptRegistry.h>

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <utility>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::EditorActions
{
    namespace
    {
        // 한 목록(컴포넌트나 스크립트)의 슬롯마다 타입 하나와 살아 있는지를 읽는다. 하위 메뉴 줄 이름을 세는 데 쓴다.
        struct SlotView
        {
            ComponentTypeId typeId = InvalidComponentTypeId;
            Bool alive = false;
        };

        void ReadSlots(const Object::GameObject& object, AttachedKind kind, Array<SlotView>& out)
        {
            out.Clear();
            if (kind == AttachedKind::Script)
            {
                for (const ScriptSlot& slot : object.GetScripts())
                {
                    out.Add(SlotView{ slot.typeId, slot.reference.TryGet() != nullptr });
                }
                return;
            }
            for (const ComponentSlot& slot : object.GetComponents())
            {
                out.Add(SlotView{ slot.typeId, slot.reference.TryGet() != nullptr });
            }
        }

        // **컴포넌트마다 인스턴스마다 하위 메뉴를 세운다**(D-220, D-298). 줄 이름은 번역하지 않는 타입
        // 이름이고, 같은 타입이 둘 이상이면 둘째부터 `(2)` 처럼 번호를 붙인다(인스펙터의 슬롯 순서와 같다).
        // 컴포넌트, 그다음 스크립트다(D-302). 안은 인스펙터 머리 메뉴와 같은 한 벌(`DrawComponentMenuItems`)이다.
        // **거짓이면 슬롯 배열이 바뀌었다.**
        Bool DrawComponentSubmenus(EditorApplication& editor, Object::GameObject& object,
            const ObjectPlacement& placement)
        {
            Array<SlotView> slots;
            Bool separated = false;
            for (const AttachedKind kind : { AttachedKind::Component, AttachedKind::Script })
            {
                ReadSlots(object, kind, slots);
                for (std::size_t index = 0; index < slots.Size(); ++index)
                {
                    const SlotView& slot = slots[index];
                    // **모든 컴포넌트가 하위 메뉴를 갖는다**(D-298). 예전에는 더한 항목이 있는 타입(`Collider2D`)만 섰다.
                    if (false == slot.alive)
                    {
                        continue;
                    }
                    // 같은 타입 중 몇째이고 모두 몇인가. 죽은 슬롯은 세지 않는다(`FindComponentAt` 과 같은 셈).
                    UInt32 ordinal = 0;
                    UInt32 sameType = 0;
                    for (std::size_t other = 0; other < slots.Size(); ++other)
                    {
                        if (slots[other].typeId != slot.typeId || false == slots[other].alive)
                        {
                            continue;
                        }
                        if (other < index)
                        {
                            ++ordinal;
                        }
                        ++sameType;
                    }
                    const char* typeName = EditorNames::DisplayTypeName(NameTable::Get().Resolve(slot.typeId));
                    if (typeName == nullptr)
                    {
                        continue;
                    }
                    // 매 프레임 도는 길이라 글자를 스택에서 만든다.
                    char numbered[128] = {};
                    const char* label = typeName;
                    if (sameType > 1 && ordinal > 0)
                    {
                        std::snprintf(numbered, sizeof(numbered), "%s (%u)", typeName, (ordinal + 1).Get());
                        label = numbered;
                    }
                    if (false == separated)
                    {
                        ImGui::Separator();
                        separated = true;
                    }
                    if (false == Widget::BeginMenu(label))
                    {
                        continue;
                    }
                    const Bool alive = DrawComponentMenuItems(editor, object, index, placement, kind);
                    Widget::EndMenu();
                    if (false == alive)
                    {
                        // 훅이 슬롯 배열을 바꿨을 수 있다. 더 돌면 헛돈다.
                        return false;
                    }
                }
            }
            return true;
        }
    }

    Bool DrawComponentMenuItems(EditorApplication& editor, Object::GameObject& object, std::size_t slot,
        const ObjectPlacement& placement, AttachedKind kind)
    {
        const Bool script = kind == AttachedKind::Script;
        AttachedRef attached;
        ComponentTypeId typeId = InvalidComponentTypeId;
        std::size_t count = 0;
        (void)typeId;
        (void)count;
        if (script)
        {
            const Array<ScriptSlot>& scripts = object.GetScripts();
            count = scripts.Size();
            if (slot < count)
            {
                attached = scripts[slot].reference.TryGet();
                typeId = scripts[slot].typeId;
            }
        }
        else
        {
            const Array<ComponentSlot>& components = object.GetComponents();
            count = components.Size();
            if (slot < count)
            {
                attached = components[slot].reference.TryGet();
                typeId = components[slot].typeId;
            }
        }
        Canvas* canvas = editor.GetCanvas();
        ComponentAddress address;
        if (false == attached.IsValid() || canvas == nullptr
            || false == MakeAttachedAddress(editor.GetObjectIds(), object, attached, address))
        {
            return true;
        }

        // `사용` 과 자리 옮기기(맨 위로·위로·아래로·맨 아래로)는 메뉴에 두지 않는다(D-305) - `사용` 은 인스펙터 칸에, 자리는 인스펙터 머리를 끌어 바꾼다.
        // **복사·붙여넣기**(D-167). 복사는 값만 뜨고, 붙여넣기는 같은 타입을 하나 더 붙이며, 값 붙여넣기는 이 컴포넌트에 덮어쓴다.
        const char* copyLabel = script ? Loc::TextOr(LocKeys::InspectorCopyScript, "Copy Script")
                                       : Loc::TextOr(LocKeys::InspectorCopyComponent, "Copy Component");
        if (Widget::MenuItem(copyLabel, nullptr, true, nullptr, Icons::Copy))
        {
            editor.CopyComponent(attached);
        }
        // 떠 둔 것이 없는 것과, 떠 두었지만 하나만 붙는 타입이 이미 있는 것은 다른 이야기다(D-180).
        const char* whyNoPaste = editor.HasComponentClipboard()
            ? Loc::TextOr(LocKeys::CommonAlreadyAdded, "Already added")
            : Loc::TextOr(LocKeys::BlockedClipboardEmpty, "nothing has been copied");
        // 떠 둔 것이 같은 목록의 것일 때만 붙인다 - 컴포넌트 메뉴에서 스크립트가 붙으면 어디에 생겼는지 알 수 없다(D-302).
        const Bool canPaste = editor.CanPasteComponent(object) && editor.GetComponentClipboardKind() == kind;
        const char* pasteLabel = script ? Loc::TextOr(LocKeys::InspectorPasteScript, "Paste Script")
                                        : Loc::TextOr(LocKeys::InspectorPasteComponent, "Paste Component");
        if (Widget::MenuItem(pasteLabel, nullptr, canPaste, whyNoPaste))
        {
            editor.PasteComponent(object);
            return false;
        }
        if (Widget::MenuItem(Loc::TextOr(LocKeys::InspectorPasteComponentValues, "Paste Component Values"), nullptr,
                editor.CanPasteComponentValues(attached), Loc::TextOr(LocKeys::BlockedClipboardEmpty, "nothing has been copied")))
        {
            editor.PasteComponentValues(object, attached);
        }

        // **그 타입에 더한 항목**(D-220). 항목이 하나라도 서야 앞에 구분선이 선다.
        // 빌트인 컴포넌트만 이 표를 가진다 - 표의 항목은 컴포넌트 타입을 아는 에디터 코드다.
        if (attached.component != nullptr)
        {
            ComponentMenuContext hookContext;
            hookContext.editor = &editor;
            hookContext.address = address;
            hookContext.component = attached.component;
            hookContext.placement = placement;
            if (false == editor.GetComponentMenus().DrawItems(hookContext, true))
            {
                return false;
            }
        }

        // 떼기는 무거운 손짓이라 맨 끝이다.
        ImGui::Separator();
        const char* removeLabel = script ? Loc::TextOr(LocKeys::InspectorRemoveScript, "Remove Script")
                                         : Loc::TextOr(LocKeys::InspectorRemoveComponent, "Remove Component");
        if (Widget::MenuItem(removeLabel, nullptr, true, nullptr, Icons::Delete))
        {
            editor.GetCommands().Execute(MakeOwnerPtr<RemoveComponentCommand>(*canvas, editor.GetObjectIds(), address.objectId, attached));
            return false;
        }
        return true;
    }

    LayerId ResolveTargetLayer(EditorApplication& editor, Object::GameObject* parent)
    {
        // 부모가 있으면 부모를 따른다. 자식만 다른 칸에 있으면 부모를 감춰도 자식이 남는다.
        if (parent != nullptr)
        {
            return parent->GetLayerId();
        }
        if (Object::GameObject* selected = editor.GetSelectedObject())
        {
            return selected->GetLayerId();
        }
        // 레이어를 골랐으면 그 레이어다(D-279, 기존 `ResolveTargetLayer`). 고르는 것이 곧 "여기에 놓겠다" 는 뜻이다.
        return editor.GetSelectedLayer();
    }

    Object::GameObject* CreateObject(EditorApplication& editor, Object::GameObject* parent,
        const ObjectPlacement& placement)
    {
        Canvas* canvas = editor.GetCanvas();
        if (canvas == nullptr)
        {
            return nullptr;
        }
        EditorObjectRegistry& ids = editor.GetObjectIds();
        const EditorObjectId parentId = parent != nullptr
            ? ids.Track(parent) : InvalidEditorObjectId;
        // **트랜스폼을 갖고 태어난다**(D-158). 없으면 캔버스 뷰에 보이지도 않고 옮길 수도 없다.
        const char* transform = editor.GetFrameworkKind() == FrameworkKind::Framework3D
            ? "Component::Transform3D"
            : "Component::Transform2D";
        // 부르는 쪽이 레이어를 대지 않았으면 규칙이 정한다(D-168).
        const LayerId layer = placement.layer != InvalidLayerId
            ? placement.layer
            : ResolveTargetLayer(editor, parent);
        auto command = MakeOwnerPtr<CreateObjectCommand>(
            *canvas, ids, "GameObject", parentId, transform,
            placement.hasPosition ? placement.position : nullptr, layer);
        CreateObjectCommand* raw = command.Get();
        if (false == editor.GetCommands().Execute(std::move(command)))
        {
            return nullptr;
        }
        // 만든 것을 고른다. 만들자마자 이름과 값을 손보는 것이 다음 손짓이다.
        Object::GameObject* created = ids.Resolve(raw->GetObjectId());
        editor.SetSelectedObject(created);
        return created;
    }

    Bool Unparent(EditorApplication& editor, Object::GameObject& object)
    {
        Canvas* canvas = editor.GetCanvas();
        if (canvas == nullptr || object.GetParent() == nullptr)
        {
            return false;
        }
        EditorObjectRegistry& ids = editor.GetObjectIds();
        // 뿌리 맨 뒤로 간다. 어디에 놓을지 고른 것이 아니므로 끝이 가장 덜 놀랍다.
        Array<Object::GameObject*> roots;
        canvas->GetRootObjects(roots);
        return editor.GetCommands().Execute(MakeOwnerPtr<MoveInHierarchyCommand>(
            *canvas, ids, ids.Track(&object), InvalidEditorObjectId, roots.Size()));
    }

    Bool DeleteObject(EditorApplication& editor, Object::GameObject& object)
    {
        Canvas* canvas = editor.GetCanvas();
        if (canvas == nullptr)
        {
            return false;
        }
        // 고른 것을 먼저 비운다 - 지운 뒤에 인스펙터가 죽은 것을 읽지 않게.
        // SafePtr 이 알아서 비우지만, 이 프레임 안에서는 아직 살아 있다.
        editor.SetSelectedObject(nullptr);
        return editor.GetCommands().Execute(
            MakeOwnerPtr<DeleteObjectCommand>(*canvas, editor.GetObjectIds(), &object));
    }

    Bool DeleteSelection(EditorApplication& editor)
    {
        Canvas* canvas = editor.GetCanvas();
        if (canvas == nullptr)
        {
            return false;
        }
        // **레이어를 골랐으면 그 레이어를 지운다**(D-279, 기존 `DeleteSelectedLayer`). 그 안의 오브젝트도 함께 가고, 되돌리면 같이
        // 돌아온다(`DeleteLayerCommand`). 마지막 한 장은 지우지 않는다 - 계층의 메뉴와 같은 규칙이다.
        const LayerId layer = editor.GetSelectedLayer();
        if (layer != InvalidLayerId)
        {
            if (canvas->GetLayerCount() <= 1)
            {
                return false;
            }
            return editor.GetCommands().Execute(MakeOwnerPtr<DeleteLayerCommand>(*canvas, editor.GetObjectIds(), layer));
        }
        // **맨 위 것들만**이다. 부모를 지우면 자식은 따라 사라진다.
        const Array<Object::GameObject*> targets = editor.GetTopLevelSelectedObjects();
        if (targets.Size() == 0)
        {
            return false;
        }
        editor.ClearSelection();
        editor.SetSelectedObject(nullptr);
        Bool any = false;
        for (std::size_t index = 0; index < targets.Size(); ++index)
        {
            Object::GameObject* object = targets[index];
            if (object == nullptr)
            {
                continue;
            }
            if (editor.GetCommands().Execute(
                    MakeOwnerPtr<DeleteObjectCommand>(*canvas, editor.GetObjectIds(), object)))
            {
                any = true;
            }
        }
        return any;
    }

    void BuildAddComponentList(const Object::GameObject& object, AddComponentList& out)
    {
        out.typeNames.Clear();
        out.names.Clear();
        out.groups.Clear();
        out.categories.Clear();
        out.addable.Clear();

        ComponentRegistry& registry = ComponentRegistry::Get();
        // 표는 이름 순으로 나온다. 갈래로 다시 묶되 갈래 안의 이름 순은 그대로 남기려고,
        // 갈래를 처음 만난 차례대로 훑으면서 그 갈래의 것만 골라 담는다. 타입은 몇십 개라
        // 이 자리에 정렬을 들여올 이유가 없다.
        const Array<const ComponentTypeInfo*> types = registry.CollectTypes();
        for (std::size_t lead = 0; lead < types.Size(); ++lead)
        {
            const char* category = types[lead]->category != nullptr
                ? types[lead]->category
                : ComponentCategory::Default;
            Bool seen = false;
            for (std::size_t before = 0; before < lead && false == seen; ++before)
            {
                const char* other = types[before]->category != nullptr
                    ? types[before]->category
                    : ComponentCategory::Default;
                seen = std::strcmp(other, category) == 0;
            }
            if (seen)
            {
                continue;
            }
            const char* groupLabel = EditorNames::ComponentCategoryLabel(category);
            for (std::size_t index = lead; index < types.Size(); ++index)
            {
                const char* other = types[index]->category != nullptr
                    ? types[index]->category
                    : ComponentCategory::Default;
                if (std::strcmp(other, category) != 0)
                {
                    continue;
                }
                const char* name = NameTable::Get().Resolve(types[index]->name);
                if (name == nullptr)
                {
                    continue;
                }
                out.typeNames.Add(types[index]->name);
                out.names.Add(EditorNames::DisplayTypeName(name));
                out.groups.Add(groupLabel);
                out.categories.Add(category);
                out.addable.Add(registry.CanAttach(object, types[index]->name));

            }
        }
    }

    void BuildAddScriptList(const Object::GameObject& object, AddComponentList& out)
    {
        (void)object;
        out.typeNames.Clear();
        out.names.Clear();
        out.groups.Clear();
        out.categories.Clear();
        out.addable.Clear();
        // 표는 해시 순이다. 실행할 때마다 같은 차례로 보이게 이름 순으로 끼운다 - 스크립트는 몇십 개라 정렬을 들여올 이유가 없다.
        ScriptRegistry::Get().ForEach([&out](const ScriptTypeInfo& type)
        {
            // 컴포넌트 목록과 같은 화면 이름이다 - 네임스페이스를 떼고 보인다.
            const char* typeName = NameTable::Get().Resolve(type.name);
            const char* name = typeName != nullptr ? EditorNames::DisplayTypeName(typeName) : nullptr;
            if (name == nullptr)
            {
                return;
            }
            std::size_t at = out.names.Size();
            while (at > 0 && std::strcmp(out.names[at - 1], name) > 0)
            {
                --at;
            }
            out.typeNames.Add(type.name);
            out.names.Add(name);
            out.addable.Add(true);
            for (std::size_t index = out.names.Size() - 1; index > at; --index)
            {
                std::swap(out.typeNames[index], out.typeNames[index - 1]);
                std::swap(out.names[index], out.names[index - 1]);
            }
        });
    }

    Bool AddComponent(EditorApplication& editor, Object::GameObject& object, NameId typeName)
    {
        Canvas* canvas = editor.GetCanvas();
        if (canvas == nullptr || false == CanAttachByName(object, typeName))
        {
            return false;
        }
        const EditorObjectId objectId = editor.GetObjectIds().Track(&object);
        return editor.GetCommands().Execute(MakeOwnerPtr<AddComponentCommand>(
            *canvas, editor.GetObjectIds(), objectId, typeName));
    }

    Bool DrawAddComponentMenu(EditorApplication& editor, Object::GameObject& object)
    {
        // 가이드가 하위 메뉴 · 갈래 · 항목을 차례로 가리킨다(반례 ④). 인스펙터의 목록과 항목 표식이 같다.
        Widget::SetNextItemTarget(GuideFocusTargets::Action("component.add"));
        if (false == Widget::BeginMenu(
                Loc::TextOr(LocKeys::InspectorAddComponent, "Add Component")))
        {
            return false;
        }
        AddComponentList list;
        BuildAddComponentList(object, list);
        Bool added = false;
        const char* drawnGroup = nullptr;
        Bool inGroup = false;
        for (std::size_t index = 0; index < list.typeNames.Size(); ++index)
        {
            // 갈래마다 하위 메뉴 하나다(기존 엔진과 같은 모양). 목록이 길어져도
            // 화면 밖으로 흐르지 않는다.
            if (drawnGroup == nullptr || std::strcmp(drawnGroup, list.groups[index]) != 0)
            {
                if (inGroup)
                {
                    Widget::EndMenu();
                }
                drawnGroup = list.groups[index];
                Widget::SetNextItemTarget(GuideFocusTargets::ComponentCategoryMenu(list.categories[index]));
                inGroup = Widget::BeginMenu(drawnGroup);
            }
            if (false == inGroup)
            {
                continue;
            }
            Widget::SetNextItemTarget(GuideFocusTargets::ComponentListItem(list.typeNames[index]));
            if (Widget::MenuItem(list.names[index], nullptr, list.addable[index],
                    Loc::TextOr(LocKeys::CommonAlreadyAdded, "Already added")))
            {
                added = AddComponent(editor, object, list.typeNames[index]) || added;
            }
        }
        if (inGroup)
        {
            Widget::EndMenu();
        }
        Widget::EndMenu();
        return added;
    }

    Bool DrawAddScriptMenu(EditorApplication& editor, Object::GameObject& object)
    {
        if (false == Widget::BeginMenu(Loc::TextOr(LocKeys::InspectorAddScript, "Add Script")))
        {
            return false;
        }
        AddComponentList list;
        BuildAddScriptList(object, list);
        Bool added = false;
        if (list.typeNames.IsEmpty())
        {
            // 빈 하위 메뉴는 고장과 구분되지 않는다. 왜 비었는지 말한다.
            Widget::MenuItem(Loc::TextOr(LocKeys::InspectorNoScriptTypes, "no script has been built"), nullptr, false);
        }
        for (std::size_t index = 0; index < list.typeNames.Size(); ++index)
        {
            if (Widget::MenuItem(list.names[index]))
            {
                added = AddComponent(editor, object, list.typeNames[index]) || added;
            }
        }
        Widget::EndMenu();
        return added;
    }

    namespace
    {
        // 행동 표의 그 행동을 그린다(D-284). 하고 나서 메뉴의 오브젝트가 없을 수 있으면 참이다.
        Bool DrawRemoving(const char* name, const EditorActionContext& context)
        {
            EditorActionContext itemContext = context;
            if (false == EditorActionUi::DrawItem(name, itemContext))
            {
                return false;
            }
            const EditorActionInfo* action = EditorActionRegistry::Get().Find(name);
            return action != nullptr && action->mayRemoveObject;
        }
    }

    Bool DrawObjectMenu(EditorApplication& editor, Object::GameObject& object,

        const ObjectPlacement& placement)
    {
        // 우클릭한 것을 고른 것으로 삼는다. 메뉴가 무엇에 대한 것인지 보이는 것과 어긋나면 안 된다.
        //
        // **이미 골라져 있으면 선택을 흩뜨리지 않는다** - 여럿 골라 놓고 그중 하나에
        // 우클릭하는 것은 "이것들에 대해" 라는 뜻이다.
        if (false == editor.IsSelected(&object))
        {
            editor.SetSelectedObject(&object);
        }
        // **항목은 행동 표에서 온다**(D-284). 이름·조합키 글자·회색과 그 까닭·가이드 표식이 편집 메뉴·단축키와 한 줄이다.
        // 자리는 여기서 정한다 - 기존 엔진과 같은 차례다.
        EditorActionContext context;
        context.editor = &editor;
        context.object = &object;
        context.placement = placement;
        context.menu = EditorActionMenu::Object;
        if (DrawRemoving("object.create", context) || DrawRemoving("object.unparent", context))
        {
            // 만들거나 부모가 바뀌면 지금 도는 자식 배열이 그 자리에서 달라진다.
            return false;
        }
        // 기존 엔진도 캔버스 뷰와 계층의 오브젝트 메뉴에 이것이 있다(D-180).
        // 인스펙터까지 눈을 옮기지 않고 그 자리에서 붙인다.
        ImGui::Separator();
        DrawAddComponentMenu(editor, object);
        DrawAddScriptMenu(editor, object);
        ImGui::Separator();
        if (DrawRemoving("object.copy", context) || DrawRemoving("object.paste", context)
            || DrawRemoving("object.paste_as_child", context))
        {
            return false;
        }
        // 그리는 차례(D-296)는 오브젝트 메뉴에 하위 메뉴로 두지 않는다 - `SpriteRenderer2D` 의 컴포넌트 메뉴에 선다(D-305).
        // 여럿을 고른 채로는 세우지 않는다(D-220) - 어느 오브젝트의 컴포넌트에 대한 항목인지 흐려진다.
        if (editor.GetSelectionCount() == 1 && false == DrawComponentSubmenus(editor, object, placement))
        {
            return false;
        }
        // 에디터가 자리를 정하지 않은 행동(패널·도구가 올린 것)은 여기 선다.
        if (false == EditorActionUi::DrawExtensions(EditorActionMenu::Object, context))
        {
            return false;
        }
        // 지우기는 되돌릴 수 있어도 무거운 손짓이라 맨 끝에 남긴다(기존 엔진도 그랬다).
        ImGui::Separator();
        // **여기서 `object` 는 이미 없을 수 있다.**
        return false == DrawRemoving("object.delete", context);
    }

    Bool DrawBackgroundMenu(EditorApplication& editor, const ObjectPlacement& placement)
    {
        EditorActionContext context;
        context.editor = &editor;
        context.placement = placement;
        context.menu = EditorActionMenu::Background;
        // 빈자리의 추가와 붙여넣기는 뿌리에 붙는다. 고른 것 밑이 아니다(`object.paste` 가 메뉴를 보고 고른 것을 비운다).
        if (DrawRemoving("object.create", context) || DrawRemoving("object.paste", context))
        {
            return true;
        }
        return false == EditorActionUi::DrawExtensions(EditorActionMenu::Background, context);
    }
}
