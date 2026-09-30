#include <JBro/Editor/EditorActions.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Editor/Command/ComponentCommands.h>
#include <JBro/Editor/Command/HierarchyCommands.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/ComponentMenuTable.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorNames.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/ScriptRegistry.h>

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <utility>

namespace JBro::EditorActions
{
    namespace
    {
        // 항목이 회색일 때 띄우는 까닭들이다(D-181). 같은 말을 자리마다 다시 쓰면
        // 한 곳만 고쳐져 화면마다 다른 말이 나온다.
        const char* NoProjectReason()
        {
            return Loc::TextOr(LocKeys::BlockedNoProject, "no project is open");
        }

        const char* NothingSelectedReason()
        {
            return Loc::TextOr(LocKeys::InspectorNothingSelected, "nothing is selected");
        }

        const char* ClipboardEmptyReason()
        {
            return Loc::TextOr(LocKeys::BlockedClipboardEmpty, "nothing has been copied");
        }

        // **컴포넌트마다 더한 항목을 인스턴스마다 하위 메뉴로 세운다**(D-220). 줄 이름은 번역하지 않는 타입
        // 이름이고, 같은 타입이 둘 이상이면 둘째부터 `(2)` 처럼 번호를 붙인다(인스펙터의 슬롯 순서와 같다).
        // 항목이 없는 타입은 줄을 만들지 않는다. **거짓이면 오브젝트가 더 이상 없을 수 있다.**
        bool DrawComponentSubmenus(EditorApplication& editor, Object::GameObject& object,
            const ObjectPlacement& placement)
        {
            ComponentMenuTable& table = editor.GetComponentMenus();
            const Array<ComponentSlot>& components = object.GetComponents();
            bool separated = false;
            for (std::size_t index = 0; index < components.Size(); ++index)
            {
                const ComponentSlot& slot = components[index];
                ComponentBase* component = slot.reference.TryGet();
                if (component == nullptr || false == table.Has(slot.typeId))
                {
                    continue;
                }
                // 같은 타입 중 몇째이고 모두 몇인가. 죽은 슬롯은 세지 않는다(`FindComponentAt` 과 같은 셈).
                std::uint32_t ordinal = 0;
                std::uint32_t sameType = 0;
                for (std::size_t other = 0; other < components.Size(); ++other)
                {
                    if (components[other].typeId != slot.typeId
                        || components[other].reference.TryGet() == nullptr)
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
                    std::snprintf(numbered, sizeof(numbered), "%s (%u)", typeName, ordinal + 1);
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
                ComponentMenuContext context;
                context.editor = &editor;
                context.address.objectId = editor.GetObjectIds().Track(&object);
                context.address.typeId = slot.typeId;
                context.address.ordinal = ordinal;
                context.component = component;
                context.placement = placement;
                const bool alive = table.DrawItems(context);
                Widget::EndMenu();
                if (false == alive)
                {
                    // 훅이 슬롯 배열을 바꿨을 수 있다. 더 돌면 헛돈다.
                    return false;
                }
            }
            return true;
        }
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
        return InvalidLayerId;
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

    bool Unparent(EditorApplication& editor, Object::GameObject& object)
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

    bool DeleteObject(EditorApplication& editor, Object::GameObject& object)
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

    bool DeleteSelection(EditorApplication& editor)
    {
        Canvas* canvas = editor.GetCanvas();
        if (canvas == nullptr)
        {
            return false;
        }
        // **맨 위 것들만**이다. 부모를 지우면 자식은 따라 사라진다.
        const Array<Object::GameObject*> targets = editor.GetTopLevelSelectedObjects();
        if (targets.Size() == 0)
        {
            return false;
        }
        editor.ClearSelection();
        editor.SetSelectedObject(nullptr);
        bool any = false;
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

    bool DrawCreateObjectItem(EditorApplication& editor, Object::GameObject* parent,
        const ObjectPlacement& placement)
    {
        if (false == Widget::MenuItem(
                Loc::TextOr(LocKeys::HierarchyCreateObject, "Create Object"), nullptr,
                editor.GetCanvas() != nullptr, NoProjectReason()))
        {
            return false;
        }
        return CreateObject(editor, parent, placement) != nullptr;
    }

    bool DrawCreateChildItem(EditorApplication& editor, Object::GameObject& parent,
        const ObjectPlacement& placement)
    {
        if (false == Widget::MenuItem(
                Loc::TextOr(LocKeys::HierarchyCreateChild, "Create Child"), nullptr,
                editor.GetCanvas() != nullptr, NoProjectReason()))
        {
            return false;
        }
        return CreateObject(editor, &parent, placement) != nullptr;
    }

    bool DrawUnparentItem(EditorApplication& editor, Object::GameObject& object)
    {
        // **부모가 없으면 항목 자체를 내지 않는다.** 회색으로 두면 무엇을 해야 켜지는지
        // 알 수 없고, 뿌리 오브젝트에는 영원히 켜지지 않는다.
        if (object.GetParent() == nullptr)
        {
            return false;
        }
        if (false == Widget::MenuItem(Loc::TextOr(LocKeys::HierarchyUnparent, "Unparent")))
        {
            return false;
        }
        return Unparent(editor, object);
    }

    bool DrawCopyItem(EditorApplication& editor)
    {
        if (false == Widget::MenuItem(Loc::TextOr(LocKeys::HierarchyCopy, "Copy"), "Ctrl+C",
                editor.GetSelectionCount() != 0, NothingSelectedReason()))
        {
            return false;
        }
        return editor.CopySelection();
    }

    bool DrawPasteItem(EditorApplication& editor)
    {
        if (false == Widget::MenuItem(Loc::TextOr(LocKeys::HierarchyPaste, "Paste"), "Ctrl+V",
                editor.HasClipboard(), ClipboardEmptyReason()))
        {
            return false;
        }
        return editor.PasteClipboard();
    }

    bool DrawPasteAsChildItem(EditorApplication& editor, Object::GameObject& object)
    {
        // **고른 것 안으로 붙인다**(D-166, 기존 `PasteObjectsAsChild`). 줄에서 연 메뉴이므로 그 줄이 곧 부모다.
        if (false == Widget::MenuItem(
                Loc::TextOr(LocKeys::HierarchyPasteAsChild, "Paste As Child"), "Ctrl+Shift+V",
                editor.HasClipboard(), ClipboardEmptyReason()))
        {
            return false;
        }
        editor.SetSelectedObject(&object);
        return editor.PasteClipboard(true);
    }

    bool DrawDeleteItem(EditorApplication& editor, Object::GameObject& object)
    {
        if (false == Widget::MenuItem(Loc::TextOr(LocKeys::HierarchyDelete, "Delete"), "Del",
                editor.GetCanvas() != nullptr, NoProjectReason()))
        {
            return false;
        }
        return DeleteObject(editor, object);
    }

    void BuildAddComponentList(const Object::GameObject& object, AddComponentList& out)
    {
        out.typeNames.Clear();
        out.names.Clear();
        out.groups.Clear();
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
            bool seen = false;
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

    bool AddComponent(EditorApplication& editor, Object::GameObject& object, NameId typeName)
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

    bool DrawAddComponentMenu(EditorApplication& editor, Object::GameObject& object)
    {
        if (false == Widget::BeginMenu(
                Loc::TextOr(LocKeys::InspectorAddComponent, "Add Component")))
        {
            return false;
        }
        AddComponentList list;
        BuildAddComponentList(object, list);
        bool added = false;
        const char* drawnGroup = nullptr;
        bool inGroup = false;
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
                inGroup = Widget::BeginMenu(drawnGroup);
            }
            if (false == inGroup)
            {
                continue;
            }
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

    bool DrawAddScriptMenu(EditorApplication& editor, Object::GameObject& object)
    {
        if (false == Widget::BeginMenu(Loc::TextOr(LocKeys::InspectorAddScript, "Add Script")))
        {
            return false;
        }
        AddComponentList list;
        BuildAddScriptList(object, list);
        bool added = false;
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

    bool DrawObjectMenu(EditorApplication& editor, Object::GameObject& object,
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
        bool alive = true;
        if (DrawCreateChildItem(editor, object, placement))
        {
            alive = false;
        }
        if (alive && DrawUnparentItem(editor, object))
        {
            // 부모가 바뀌면 지금 도는 자식 배열이 그 자리에서 달라진다.
            alive = false;
        }
        if (alive)
        {
            // 기존 엔진도 캔버스 뷰와 계층의 오브젝트 메뉴에 이것이 있다(D-180).
            // 인스펙터까지 눈을 옮기지 않고 그 자리에서 붙인다.
            ImGui::Separator();
            DrawAddComponentMenu(editor, object);
            DrawAddScriptMenu(editor, object);
        }
        if (alive)
        {
            ImGui::Separator();
            DrawCopyItem(editor);
            if (DrawPasteItem(editor))
            {
                alive = false;
            }
        }
        if (alive && DrawPasteAsChildItem(editor, object))
        {
            alive = false;
        }
        if (alive && editor.GetSelectionCount() == 1)
        {
            // 여럿을 고른 채로는 세우지 않는다(D-220) - 어느 오브젝트의 컴포넌트에 대한 항목인지 흐려진다.
            // 지우기는 되돌릴 수 있어도 무거운 손짓이라 맨 끝에 남긴다(기존 엔진도 그랬다).
            alive = DrawComponentSubmenus(editor, object, placement);
        }
        if (alive)
        {
            ImGui::Separator();
            if (DrawDeleteItem(editor, object))
            {
                // **여기서 `object` 는 이미 없을 수 있다.**
                alive = false;
            }
        }
        return alive;
    }

    bool DrawBackgroundMenu(EditorApplication& editor, const ObjectPlacement& placement)
    {
        bool changed = DrawCreateObjectItem(editor, nullptr, placement);
        if (changed)
        {
            return true;
        }
        // 빈자리의 붙여넣기는 뿌리에 붙는다. 고른 것 밑이 아니다.
        if (Widget::MenuItem(Loc::TextOr(LocKeys::HierarchyPaste, "Paste"), "Ctrl+V",
                editor.HasClipboard(), ClipboardEmptyReason()))
        {
            editor.ClearSelection();
            changed = editor.PasteClipboard();
        }
        return changed;
    }
}
