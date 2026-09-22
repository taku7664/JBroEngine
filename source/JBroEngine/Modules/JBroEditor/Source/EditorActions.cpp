#include <JBro/Editor/EditorActions.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/Command/HierarchyCommands.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Runtime/GameObject.h>

#include <imgui.h>

#include <utility>

namespace JBro::EditorActions
{
    namespace
    {
        // 못 하는 항목은 회색으로 그린다. 스코프로 두어 돌아 나가는 길이 생겨도
        // `EndDisabled` 를 잊지 않는다.
        class DisabledIf
        {
        public:
            explicit DisabledIf(bool disabled)
                : m_disabled(disabled)
            {
                if (m_disabled)
                {
                    ImGui::BeginDisabled();
                }
            }
            ~DisabledIf()
            {
                if (m_disabled)
                {
                    ImGui::EndDisabled();
                }
            }
            DisabledIf(const DisabledIf&) = delete;
            DisabledIf& operator=(const DisabledIf&) = delete;

        private:
            bool m_disabled = false;
        };
    }

    GameObject* CreateObject(EditorApplication& editor, GameObject* parent)
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
        auto command = MakeOwnerPtr<CreateObjectCommand>(
            *canvas, ids, "GameObject", parentId, transform);
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

    bool DrawCreateObjectItem(EditorApplication& editor, GameObject* parent)
    {
        const DisabledIf disabled(editor.GetCanvas() == nullptr);
        if (false == ImGui::MenuItem(
                Loc::TextOr(LocKeys::HierarchyCreateObject, "Create Object")))
        {
            return false;
        }
        return CreateObject(editor, parent) != nullptr;
    }

    bool DrawCreateChildItem(EditorApplication& editor, GameObject& parent)
    {
        const DisabledIf disabled(editor.GetCanvas() == nullptr);
        if (false == ImGui::MenuItem(
                Loc::TextOr(LocKeys::HierarchyCreateChild, "Create Child")))
        {
            return false;
        }
        return CreateObject(editor, &parent) != nullptr;
    }

    bool DrawUnparentItem(EditorApplication& editor, GameObject& object)
    {
        // **부모가 없으면 항목 자체를 내지 않는다.** 회색으로 두면 무엇을 해야 켜지는지
        // 알 수 없고, 뿌리 오브젝트에는 영원히 켜지지 않는다.
        if (object.GetParent() == nullptr)
        {
            return false;
        }
        if (false == ImGui::MenuItem(Loc::TextOr(LocKeys::HierarchyUnparent, "Unparent")))
        {
            return false;
        }
        return Unparent(editor, object);
    }

    bool DrawCopyItem(EditorApplication& editor)
    {
        const DisabledIf disabled(editor.GetSelectionCount() == 0);
        if (false == ImGui::MenuItem(Loc::TextOr(LocKeys::HierarchyCopy, "Copy"), "Ctrl+C"))
        {
            return false;
        }
        return editor.CopySelection();
    }

    bool DrawPasteItem(EditorApplication& editor)
    {
        const DisabledIf disabled(false == editor.HasClipboard());
        if (false == ImGui::MenuItem(Loc::TextOr(LocKeys::HierarchyPaste, "Paste"), "Ctrl+V"))
        {
            return false;
        }
        return editor.PasteClipboard();
    }

    bool DrawDeleteItem(EditorApplication& editor, GameObject& object)
    {
        const DisabledIf disabled(editor.GetCanvas() == nullptr);
        if (false == ImGui::MenuItem(Loc::TextOr(LocKeys::HierarchyDelete, "Delete"), "Del"))
        {
            return false;
        }
        return DeleteObject(editor, object);
    }

    bool DrawBackgroundMenu(EditorApplication& editor)
    {
        bool changed = DrawCreateObjectItem(editor, nullptr);
        if (changed)
        {
            return true;
        }
        // 빈자리의 붙여넣기는 뿌리에 붙는다. 고른 것 밑이 아니다.
        const bool hasClipboard = editor.HasClipboard();
        {
            const DisabledIf disabled(false == hasClipboard);
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::HierarchyPaste, "Paste"), "Ctrl+V"))
            {
                editor.ClearSelection();
                changed = editor.PasteClipboard();
            }
        }
        return changed;
    }
}
