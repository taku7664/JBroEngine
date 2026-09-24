#include <JBro/Editor/EditorActions.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Editor/Command/ComponentCommands.h>
#include <JBro/Editor/Command/HierarchyCommands.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorNames.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Runtime/GameObject.h>

#include <imgui.h>

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
    }

    LayerId ResolveTargetLayer(EditorApplication& editor, GameObject* parent)
    {
        // 부모가 있으면 부모를 따른다. 자식만 다른 칸에 있으면 부모를 감춰도 자식이 남는다.
        if (parent != nullptr)
        {
            return parent->GetLayerId();
        }
        if (GameObject* selected = editor.GetSelectedObject())
        {
            return selected->GetLayerId();
        }
        return InvalidLayerId;
    }

    GameObject* CreateObject(EditorApplication& editor, GameObject* parent,
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
        GameObject* created = ids.Resolve(raw->GetObjectId());
        editor.SetSelectedObject(created);
        return created;
    }

    bool Unparent(EditorApplication& editor, GameObject& object)
    {
        Canvas* canvas = editor.GetCanvas();
        if (canvas == nullptr || object.GetParent() == nullptr)
        {
            return false;
        }
        EditorObjectRegistry& ids = editor.GetObjectIds();
        // 뿌리 맨 뒤로 간다. 어디에 놓을지 고른 것이 아니므로 끝이 가장 덜 놀랍다.
        Array<GameObject*> roots;
        canvas->GetRootObjects(roots);
        return editor.GetCommands().Execute(MakeOwnerPtr<MoveInHierarchyCommand>(
            *canvas, ids, ids.Track(&object), InvalidEditorObjectId, roots.Size()));
    }

    bool DeleteObject(EditorApplication& editor, GameObject& object)
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
        const Array<GameObject*> targets = editor.GetTopLevelSelectedObjects();
        if (targets.Size() == 0)
        {
            return false;
        }
        editor.ClearSelection();
        editor.SetSelectedObject(nullptr);
        bool any = false;
        for (std::size_t index = 0; index < targets.Size(); ++index)
        {
            GameObject* object = targets[index];
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

    bool DrawCreateObjectItem(EditorApplication& editor, GameObject* parent,
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

    bool DrawCreateChildItem(EditorApplication& editor, GameObject& parent,
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

    bool DrawUnparentItem(EditorApplication& editor, GameObject& object)
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

    bool DrawPasteAsChildItem(EditorApplication& editor, GameObject& object)
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

    bool DrawDeleteItem(EditorApplication& editor, GameObject& object)
    {
        if (false == Widget::MenuItem(Loc::TextOr(LocKeys::HierarchyDelete, "Delete"), "Del",
                editor.GetCanvas() != nullptr, NoProjectReason()))
        {
            return false;
        }
        return DeleteObject(editor, object);
    }

    void BuildAddComponentList(const GameObject& object, AddComponentList& out)
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

    bool AddComponent(EditorApplication& editor, GameObject& object, NameId typeName)
    {
        Canvas* canvas = editor.GetCanvas();
        if (canvas == nullptr
            || false == ComponentRegistry::Get().CanAttach(object, typeName))
        {
            return false;
        }
        const EditorObjectId objectId = editor.GetObjectIds().Track(&object);
        return editor.GetCommands().Execute(MakeOwnerPtr<AddComponentCommand>(
            *canvas, editor.GetObjectIds(), objectId, typeName));
    }

    bool DrawAddComponentMenu(EditorApplication& editor, GameObject& object)
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

    bool DrawObjectMenu(EditorApplication& editor, GameObject& object,
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
