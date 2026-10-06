#include <JBro/Editor/Command/ObjectCommands.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/NameTable.h>

#include <cstdio>
#include <utility>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // ── CreateObjectCommand ──────────────────────────────────────────────────

    namespace
    {
        // 만들 자리를 트랜스폼에 써 넣는다(D-168).
        //
        // **커맨드는 `Vector2` 인지 `Vector3` 인지 모른다.** 잎사귀가 내놓는 필드를 앞에서부터
        // 채우므로 둘 다 맞는다 - 타입을 견주기 시작하면 프레임워크가 늘 때마다 여기가 는다.
        void WriteSpawnPosition(
            ComponentBase& component, ComponentTypeId typeId, const Float (&position)[3])
        {
            SetPropertyCommand::Path path;
            void* address = nullptr;
            const TypeDescriptor* type = nullptr;
            if (false == SetPropertyCommand::MakeFieldPath(typeId, "position", path)
                || false == SetPropertyCommand::ResolveLeaf(component, typeId, path, address, type)
                || type->fields == nullptr)
            {
                return;
            }
            const UInt32 count = type->fields->count < 3u ? type->fields->count : UInt32(3u);
            for (UInt32 index = 0; index < count; ++index)
            {
                const PropertyInfo& field = type->fields->properties[index];
                if (field.Address == nullptr || field.type == nullptr
                    || field.type->codec == nullptr || field.type->codec->FromText == nullptr)
                {
                    continue;
                }
                char text[32] = {};
                const Int32 written = std::snprintf(
                    text, sizeof(text), "%g", static_cast<double>(position[index]));
                if (written <= 0)
                {
                    continue;
                }
                field.type->codec->FromText(
                    field.Address(address), text, static_cast<std::size_t>(written));
            }
        }
    }

    CreateObjectCommand::CreateObjectCommand(
        Canvas& canvas,
        EditorObjectRegistry& registry,
        const char* name,
        EditorObjectId parentId,
        const char* defaultComponent,
        const Float* position,
        LayerId layer)
        : m_canvas(&canvas)
        , m_registry(&registry)
        , m_name(name != nullptr ? name : "GameObject")
        , m_defaultComponent(defaultComponent != nullptr ? defaultComponent : "")
        , m_parentId(parentId)
        , m_layer(layer)
    {
        if (position != nullptr)
        {
            m_position[0] = position[0];
            m_position[1] = position[1];
            m_position[2] = position[2];
            m_hasPosition = true;
        }
    }

    const char* CreateObjectCommand::GetName() const
    {
        return "Create Object";
    }

    Bool CreateObjectCommand::Create()
    {
        GameObject* object = m_canvas->CreateObject(m_name.c_str());
        if (object == nullptr)
        {
            return false;
        }
        if (false == m_defaultComponent.empty())
        {
            // 등록부에서 이름으로 찾아 붙인다. 커맨드는 어느 프레임워크인지 모른다 - 모르는 것이 맞다.
            if (const ComponentTypeInfo* info =
                    ComponentRegistry::Get().Find(m_defaultComponent.c_str()))
            {
                if (info->Attach != nullptr)
                {
                    ComponentBase* component = info->Attach(*m_canvas, object);
                    if (component != nullptr && m_hasPosition)
                    {
                        WriteSpawnPosition(*component, info->typeId, m_position);
                    }
                }
            }
        }
        if (m_parentId != InvalidEditorObjectId)
        {
            // 부모가 그 사이에 사라졌으면 뿌리에 둔다. 만들기를 통째로 실패시키면
            // 다시하기가 스택 중간에서 막힌다.
            if (GameObject* parent = m_registry->Resolve(m_parentId))
            {
                object->SetParent(parent);
            }
        }
        // **놓을 레이어가 있으면 거기 둔다**(D-168). 그 사이에 사라진 레이어면 캔버스가
        // 준 기본 레이어 그대로다 - 없는 번호를 들고 있으면 어느 칸에도 나오지 않는다.
        if (m_layer != InvalidLayerId)
        {
            m_canvas->SetObjectLayer(object, m_layer);
        }

        if (m_objectId == InvalidEditorObjectId)
        {
            m_objectId = m_registry->Track(object);
            return m_objectId != InvalidEditorObjectId;
        }
        // 다시하기다. **같은 번호에 다시 건다** - 그 번호를 들고 있는 커맨드들이
        // 이 오브젝트를 계속 찾아야 한다.
        return m_registry->Rebind(m_objectId, object);
    }

    Bool CreateObjectCommand::Execute()
    {
        return Create();
    }

    void CreateObjectCommand::Undo()
    {
        if (GameObject* object = m_registry->Resolve(m_objectId))
        {
            m_canvas->DestroyObject(object);
            m_canvas->FlushPendingDestroy();
        }
    }

    void CreateObjectCommand::Redo()
    {
        Create();
    }

    EditorObjectId CreateObjectCommand::GetObjectId() const
    {
        return m_objectId;
    }

    // ── DeleteObjectCommand ──────────────────────────────────────────────────

    DeleteObjectCommand::DeleteObjectCommand(
        Canvas& canvas,
        EditorObjectRegistry& registry,
        GameObject* object)
        : m_canvas(&canvas)
        , m_registry(&registry)
    {
        if (object == nullptr)
        {
            return;
        }
        m_parentId = object->GetParent() != nullptr
            ? m_registry->Track(object->GetParent())
            : InvalidEditorObjectId;
        m_objectId = m_registry->Track(object);
        m_captured = m_tree.Capture(registry, *object);
    }

    const char* DeleteObjectCommand::GetName() const
    {
        return "Delete Object";
    }

    Bool DeleteObjectCommand::Execute()
    {
        if (false == m_captured || m_tree.objects.IsEmpty())
        {
            // 뜨지 못한 스냅샷이다. 되살릴 수 없는 것은 지우지 않는다.
            //
            // 반쪽도 거절한다. 캔버스는 나무를 통째로 지우는데 스냅샷에 자식이
            // 빠져 있으면 되돌려도 그 자식은 안 돌아온다 - 지우기가 성공했다고
            // 말한 뒤에 조용히 잃는 것이다.
            return false;
        }
        return m_tree.DestroyRoot(*m_canvas, *m_registry);
    }

    void DeleteObjectCommand::Undo()
    {
        GameObject* outerParent = m_parentId != InvalidEditorObjectId
            ? m_registry->Resolve(m_parentId)
            : nullptr;
        m_tree.Restore(*m_canvas, *m_registry, outerParent, true);
    }

    void DeleteObjectCommand::Redo()
    {
        m_tree.DestroyRoot(*m_canvas, *m_registry);
    }

    // ── PasteObjectsCommand ──────────────────────────────────────────────────

    EditorObjectId PasteObjectsCommand::GetSubject() const
    {
        const Array<EditorObjectId> roots = GetPastedRootIds();
        return roots.IsEmpty() ? InvalidEditorObjectId : roots[0];
    }

    PasteObjectsCommand::PasteObjectsCommand(
        Canvas& canvas,
        EditorObjectRegistry& registry,
        const Array<ObjectTreeSnapshot>& trees,
        EditorObjectId parentId)
        : m_canvas(&canvas)
        , m_registry(&registry)
        , m_parentId(parentId)
    {
        for (std::size_t index = 0; index < trees.Size(); ++index)
        {
            m_trees.Add(trees[index]);
        }
    }

    const char* PasteObjectsCommand::GetName() const
    {
        return "Paste Objects";
    }

    Bool PasteObjectsCommand::Paste()
    {
        if (m_trees.IsEmpty())
        {
            return false;
        }
        // 부모가 지워졌으면 뿌리에 붙이지 않고 거절한다. 사용자가 고른 자리가 아니다.
        GameObject* parent = nullptr;
        if (m_parentId != InvalidEditorObjectId)
        {
            parent = m_registry->Resolve(m_parentId);
            if (parent == nullptr)
            {
                return false;
            }
        }
        for (std::size_t index = 0; index < m_trees.Size(); ++index)
        {
            if (false == m_trees[index].Restore(*m_canvas, *m_registry, parent, m_pasted))
            {
                // 반쯤 붙은 것은 도로 지운다. 반쪽을 성공이라 두지 않는다.
                for (std::size_t back = 0; back <= index; ++back)
                {
                    m_trees[back].DestroyRoot(*m_canvas, *m_registry);
                }
                return false;
            }
        }
        m_pasted = true;
        return true;
    }

    void PasteObjectsCommand::DestroyPasted()
    {
        for (std::size_t index = 0; index < m_trees.Size(); ++index)
        {
            m_trees[index].DestroyRoot(*m_canvas, *m_registry);
        }
    }

    Bool PasteObjectsCommand::Execute()
    {
        return Paste();
    }

    void PasteObjectsCommand::Undo()
    {
        DestroyPasted();
    }

    void PasteObjectsCommand::Redo()
    {
        Paste();
    }

    Array<EditorObjectId> PasteObjectsCommand::GetPastedRootIds() const
    {
        Array<EditorObjectId> ids;
        for (std::size_t index = 0; index < m_trees.Size(); ++index)
        {
            ids.Add(m_trees[index].GetRootId());
        }
        return ids;
    }

    RenameObjectCommand::RenameObjectCommand(EditorObjectRegistry& registry,
        EditorObjectId objectId, const char* name)
        : m_registry(&registry)
        , m_objectId(objectId)
        , m_after(name != nullptr ? name : "")
    {
        GameObject* object = registry.Resolve(objectId);
        if (object == nullptr)
        {
            return;
        }
        const char* tag = object->GetName();
        m_before = tag != nullptr ? tag : "";
        m_captured = true;
    }

    const char* RenameObjectCommand::GetName() const
    {
        return "Rename Object";
    }

    void RenameObjectCommand::Apply(const String& name)
    {
        if (GameObject* object = m_registry->Resolve(m_objectId))
        {
            object->SetName(name.c_str());
        }
    }

    Bool RenameObjectCommand::Execute()
    {
        if (false == m_captured || m_before == m_after)
        {
            return false;
        }
        Apply(m_after);
        return true;
    }

    void RenameObjectCommand::Undo()
    {
        Apply(m_before);
    }

    void RenameObjectCommand::Redo()
    {
        Apply(m_after);
    }

    Bool RenameObjectCommand::CanMerge(const EditorCommand& newer) const
    {
        // **같은 오브젝트를 잇달아 고치는 중일 때만 합친다.** 다른 오브젝트로 옮겨 갔는데
        // 합치면 그 이름이 되돌리기에서 사라진다.
        const auto* other = dynamic_cast<const RenameObjectCommand*>(&newer);
        return other != nullptr && other->m_objectId == m_objectId;
    }

    Bool RenameObjectCommand::TryMerge(const EditorCommand& newer)
    {
        if (false == CanMerge(newer))
        {
            return false;
        }
        // 처음 이름은 이쪽 것을 지킨다 - 친 글자 전체를 한 번에 되돌려야 한다.
        m_after = static_cast<const RenameObjectCommand&>(newer).m_after;
        return true;
    }

    SetObjectTagCommand::SetObjectTagCommand(EditorObjectRegistry& registry, const Array<EditorObjectId>& objects, const char* tag)
        : m_registry(&registry)
        , m_after(tag != nullptr ? tag : "")
    {
        // **되살릴 값을 먼저 뜬다**(§11.5). 찾지 못한 오브젝트는 넣지 않는다.
        for (std::size_t index = 0; index < objects.Size(); ++index)
        {
            const GameObject* object = registry.Resolve(objects[index]);
            if (object == nullptr)
            {
                continue;
            }
            const char* before = object->GetTagId() != InvalidNameId ? object->GetTag() : nullptr;
            m_objects.Add(objects[index]);
            m_before.Add(String(before != nullptr ? before : ""));
        }
    }

    const char* SetObjectTagCommand::GetName() const
    {
        return "Set Tag";
    }

    Bool SetObjectTagCommand::Execute()
    {
        Bool changes = false;
        for (std::size_t index = 0; index < m_objects.Size(); ++index)
        {
            changes = changes || m_before[index] != m_after;
        }
        if (false == changes)
        {
            return false;
        }
        Redo();
        return true;
    }

    void SetObjectTagCommand::Undo()
    {
        for (std::size_t index = 0; index < m_objects.Size(); ++index)
        {
            if (GameObject* object = m_registry->Resolve(m_objects[index]))
            {
                object->SetTag(m_before[index].c_str());
            }
        }
    }

    void SetObjectTagCommand::Redo()
    {
        for (std::size_t index = 0; index < m_objects.Size(); ++index)
        {
            if (GameObject* object = m_registry->Resolve(m_objects[index]))
            {
                object->SetTag(m_after.c_str());
            }
        }
    }

    ObjectToggleCommand::ObjectToggleCommand(EditorObjectRegistry& registry,
        const Array<EditorObjectId>& objects, Bool after, Getter getter, Setter setter)
        : m_registry(&registry)
        , m_getter(getter)
        , m_setter(setter)
        , m_after(after)
    {
        for (std::size_t index = 0; index < objects.Size(); ++index)
        {
            GameObject* object = registry.Resolve(objects[index]);
            if (object == nullptr)
            {
                continue;
            }
            // **되살릴 값을 먼저 뜬다**(§11.5). 못 뜬 것은 목록에 넣지 않는다.
            m_objects.Add(objects[index]);
            m_before.Add(m_getter(*object) ? std::uint8_t{1} : std::uint8_t{0});
        }
    }

    void ObjectToggleCommand::Apply(Bool value)
    {
        for (std::size_t index = 0; index < m_objects.Size(); ++index)
        {
            if (GameObject* object = m_registry->Resolve(m_objects[index]))
            {
                m_setter(*object, value);
            }
        }
    }

    SetObjectActiveCommand::SetObjectActiveCommand(EditorObjectRegistry& registry,
        const Array<EditorObjectId>& objects, Bool active)
        : ObjectToggleCommand(registry, objects, active,
            [](const GameObject& object) { return object.IsActiveSelf(); },
            [](GameObject& object, Bool value) { object.SetActive(value); })
    {
    }

    const char* SetObjectActiveCommand::GetName() const
    {
        return "Set Active";
    }

    SetObjectEditorHiddenCommand::SetObjectEditorHiddenCommand(EditorObjectRegistry& registry,
        const Array<EditorObjectId>& objects, Bool hidden)
        : ObjectToggleCommand(registry, objects, hidden,
            [](const GameObject& object) { return object.IsEditorHidden(); },
            [](GameObject& object, Bool value) { object.SetEditorHidden(value); })
    {
    }

    const char* SetObjectEditorHiddenCommand::GetName() const
    {
        return "Hide in Canvas View";
    }

    Bool ObjectToggleCommand::Execute()
    {
        if (m_objects.IsEmpty())
        {
            return false;
        }
        // 이미 다 그 값이면 바뀌는 것이 없다. 빈 칸을 쌓아 두면 되돌리기가 한 번 헛돈다.
        Bool changes = false;
        for (std::size_t index = 0; index < m_before.Size(); ++index)
        {
            if ((m_before[index] != 0) != m_after)
            {
                changes = true;
            }
        }
        if (false == changes)
        {
            return false;
        }
        Apply(m_after);
        return true;
    }

    void ObjectToggleCommand::Undo()
    {
        for (std::size_t index = 0; index < m_objects.Size(); ++index)
        {
            if (GameObject* object = m_registry->Resolve(m_objects[index]))
            {
                m_setter(*object, m_before[index] != 0);
            }
        }
    }

    void ObjectToggleCommand::Redo()
    {
        Apply(m_after);
    }

    SetComponentEnabledCommand::SetComponentEnabledCommand(EditorObjectRegistry& registry,
        const ComponentAddress& address, Bool enabled)
        : m_registry(&registry)
        , m_address(address)
        , m_after(enabled)
    {
        ComponentBase* component = ResolveComponent(registry, address);
        if (component == nullptr)
        {
            return;
        }
        m_before = component->IsEnabled();
        m_captured = true;
    }

    const char* SetComponentEnabledCommand::GetName() const
    {
        return "Set Component Enabled";
    }

    void SetComponentEnabledCommand::Apply(Bool enabled)
    {
        if (ComponentBase* component = ResolveComponent(*m_registry, m_address))
        {
            component->SetEnabled(enabled);
        }
    }

    Bool SetComponentEnabledCommand::Execute()
    {
        if (false == m_captured || m_before == m_after)
        {
            return false;
        }
        Apply(m_after);
        return true;
    }

    void SetComponentEnabledCommand::Undo()
    {
        Apply(m_before);
    }

    void SetComponentEnabledCommand::Redo()
    {
        Apply(m_after);
    }
}
