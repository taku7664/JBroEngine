#include <JBro/Editor/Command/LayerCommands.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Runtime/GameObject.h>

#include <utility>

namespace JBro
{
    namespace
    {
        // 캔버스가 든 레이어 목록에서 그 레이어가 몇 번째인가. 없으면 거짓이다.
        bool FindLayerIndex(Canvas& canvas, LayerId layer, std::size_t& index)
        {
            for (std::size_t at = 0; at < canvas.GetLayerCount(); ++at)
            {
                const Layer* candidate = canvas.GetLayerAt(at);
                if (candidate != nullptr && candidate->GetId() == layer)
                {
                    index = at;
                    return true;
                }
            }
            return false;
        }

        // 이 오브젝트와 그 모든 자손. 레이어는 부분 트리 전체가 함께 간다.
        void CollectSubtree(GameObject& object, Array<GameObject*>& result)
        {
            result.Add(&object);
            const Array<SafePtr<GameObject>>& children = object.GetChildren();
            for (std::size_t index = 0; index < children.Size(); ++index)
            {
                if (GameObject* child = children[index].TryGet())
                {
                    CollectSubtree(*child, result);
                }
            }
        }
    }

    // ── CreateLayerCommand ───────────────────────────────────────────────

    CreateLayerCommand::CreateLayerCommand(Canvas& canvas, const char* name)
        : m_canvas(&canvas)
        , m_name(name != nullptr ? name : "Layer")
    {
    }

    const char* CreateLayerCommand::GetName() const
    {
        return "Create Layer";
    }

    bool CreateLayerCommand::Execute()
    {
        Layer& made = m_canvas->CreateLayer(m_name.c_str());
        m_layerId = made.GetId();
        // 새 레이어는 맨 위(가장 앞)에 선다. 방금 만든 것이 무엇에도 가리지 않아야
        // 거기에 무엇을 놓는지 보인다.
        const std::size_t top = m_canvas->GetLayerCount() - 1;
        m_canvas->MoveLayer(m_layerId, top);
        m_index = top;
        return true;
    }

    void CreateLayerCommand::Undo()
    {
        if (m_layerId != InvalidLayerId)
        {
            m_canvas->DestroyLayer(m_layerId);
            m_layerId = InvalidLayerId;
        }
    }

    void CreateLayerCommand::Redo()
    {
        Layer& made = m_canvas->CreateLayer(m_name.c_str());
        m_layerId = made.GetId();
        m_canvas->MoveLayer(m_layerId, m_index);
    }

    // ── DeleteLayerCommand ───────────────────────────────────────────────

    DeleteLayerCommand::DeleteLayerCommand(
        Canvas& canvas, EditorObjectRegistry& registry, LayerId layer)
        : m_canvas(&canvas)
        , m_registry(&registry)
        , m_layerId(layer)
    {
        // **마지막 하나는 지우지 못한다.** 캔버스가 레이어 없이 설 수 없다.
        if (canvas.GetLayerCount() <= 1)
        {
            return;
        }
        const Layer* found = nullptr;
        for (std::size_t at = 0; at < canvas.GetLayerCount(); ++at)
        {
            const Layer* candidate = canvas.GetLayerAt(at);
            if (candidate != nullptr && candidate->GetId() == layer)
            {
                found = candidate;
                m_index = at;
                break;
            }
        }
        if (found == nullptr)
        {
            return;
        }
        m_name = found->GetName();
        m_visible = found->IsVisible();
        // 이 레이어에 있던 오브젝트를 **번호로** 적어 둔다. 지웠다 되살려도 같은 것을 가리킨다.
        canvas.ForEachObject([this, layer](GameObject& object)
        {
            if (object.GetLayerId() == layer)
            {
                m_objects.Add(m_registry->Track(&object));
            }
        });
        m_captured = true;
    }

    const char* DeleteLayerCommand::GetName() const
    {
        return "Delete Layer";
    }

    bool DeleteLayerCommand::Execute()
    {
        if (false == m_captured)
        {
            return false;
        }
        return m_canvas->DestroyLayer(m_layerId);
    }

    void DeleteLayerCommand::Undo()
    {
        if (false == m_captured)
        {
            return;
        }
        // **같은 번호로 돌아오지는 않는다.** 화면에서 보이는 것(이름·자리·보임·그 안의
        // 오브젝트)은 같고, 번호를 들고 있던 오브젝트는 여기서 다시 잇는다.
        Layer& restored = m_canvas->CreateLayer(m_name.c_str());
        restored.SetVisible(m_visible);
        m_layerId = restored.GetId();
        m_canvas->MoveLayer(m_layerId, m_index);
        for (std::size_t index = 0; index < m_objects.Size(); ++index)
        {
            if (GameObject* object = m_registry->Resolve(m_objects[index]))
            {
                m_canvas->SetObjectLayer(object, m_layerId);
            }
        }
    }

    void DeleteLayerCommand::Redo()
    {
        if (m_captured)
        {
            m_canvas->DestroyLayer(m_layerId);
        }
    }

    // ── MoveLayerCommand ─────────────────────────────────────────────────

    MoveLayerCommand::MoveLayerCommand(Canvas& canvas, LayerId layer, std::size_t newIndex)
        : m_canvas(&canvas)
        , m_layerId(layer)
        , m_to(newIndex)
    {
        if (false == FindLayerIndex(canvas, layer, m_from))
        {
            return;
        }
        const std::size_t last = canvas.GetLayerCount() - 1;
        m_to = newIndex > last ? last : newIndex;
        m_captured = true;
    }

    const char* MoveLayerCommand::GetName() const
    {
        return "Move Layer";
    }

    bool MoveLayerCommand::Execute()
    {
        // 제자리로 옮기는 것은 편집이 아니다. 스택에 올리면 Ctrl+Z 가 헛걸음한다.
        if (false == m_captured || m_from == m_to)
        {
            return false;
        }
        return m_canvas->MoveLayer(m_layerId, m_to);
    }

    void MoveLayerCommand::Undo()
    {
        if (m_captured)
        {
            m_canvas->MoveLayer(m_layerId, m_from);
        }
    }

    void MoveLayerCommand::Redo()
    {
        if (m_captured)
        {
            m_canvas->MoveLayer(m_layerId, m_to);
        }
    }

    // ── RenameLayerCommand ───────────────────────────────────────────────

    RenameLayerCommand::RenameLayerCommand(Canvas& canvas, LayerId layer, const char* name)
        : m_canvas(&canvas)
        , m_layerId(layer)
        , m_after(name != nullptr ? name : "")
    {
        const Layer* found = canvas.FindLayer(layer);
        if (found == nullptr)
        {
            return;
        }
        m_before = found->GetName();
        m_captured = true;
    }

    const char* RenameLayerCommand::GetName() const
    {
        return "Rename Layer";
    }

    bool RenameLayerCommand::Execute()
    {
        if (false == m_captured || m_before == m_after)
        {
            return false;
        }
        Layer* layer = m_canvas->FindLayer(m_layerId);
        if (layer == nullptr)
        {
            return false;
        }
        layer->SetName(m_after.c_str());
        return true;
    }

    void RenameLayerCommand::Undo()
    {
        if (Layer* layer = m_canvas->FindLayer(m_layerId))
        {
            layer->SetName(m_before.c_str());
        }
    }

    void RenameLayerCommand::Redo()
    {
        if (Layer* layer = m_canvas->FindLayer(m_layerId))
        {
            layer->SetName(m_after.c_str());
        }
    }

    bool RenameLayerCommand::CanMerge(const EditorCommand& newer) const
    {
        const auto* other = dynamic_cast<const RenameLayerCommand*>(&newer);
        return other != nullptr && other->m_canvas == m_canvas && other->m_layerId == m_layerId;
    }

    bool RenameLayerCommand::TryMerge(const EditorCommand& newer)
    {
        if (false == CanMerge(newer))
        {
            return false;
        }
        // 도착값만 흡수한다. 처음 값은 이 커맨드가 든 것이 맞다.
        m_after = static_cast<const RenameLayerCommand&>(newer).m_after;
        return true;
    }

    // ── SetLayerVisibleCommand ───────────────────────────────────────────

    SetLayerVisibleCommand::SetLayerVisibleCommand(Canvas& canvas, LayerId layer, bool visible)
        : m_canvas(&canvas)
        , m_layerId(layer)
        , m_after(visible)
    {
        const Layer* found = canvas.FindLayer(layer);
        if (found == nullptr)
        {
            return;
        }
        m_before = found->IsVisible();
        m_captured = true;
    }

    const char* SetLayerVisibleCommand::GetName() const
    {
        return "Show Layer";
    }

    bool SetLayerVisibleCommand::Execute()
    {
        if (false == m_captured || m_before == m_after)
        {
            return false;
        }
        Layer* layer = m_canvas->FindLayer(m_layerId);
        if (layer == nullptr)
        {
            return false;
        }
        layer->SetVisible(m_after);
        return true;
    }

    void SetLayerVisibleCommand::Undo()
    {
        if (Layer* layer = m_canvas->FindLayer(m_layerId))
        {
            layer->SetVisible(m_before);
        }
    }

    void SetLayerVisibleCommand::Redo()
    {
        if (Layer* layer = m_canvas->FindLayer(m_layerId))
        {
            layer->SetVisible(m_after);
        }
    }

    // ── SetObjectLayerCommand ────────────────────────────────────────────

    SetObjectLayerCommand::SetObjectLayerCommand(
        Canvas& canvas,
        EditorObjectRegistry& registry,
        EditorObjectId objectId,
        LayerId layer)
        : m_canvas(&canvas)
        , m_registry(&registry)
        , m_objectId(objectId)
        , m_after(layer)
    {
        GameObject* object = registry.Resolve(objectId);
        if (object == nullptr || canvas.FindLayer(layer) == nullptr)
        {
            return;
        }
        // **되살릴 값을 먼저 뜬다**(§11.5). 부분 트리의 오브젝트마다 원래 레이어다 -
        // 자식이 부모와 다른 레이어에 있었을 수도 있고, 되돌리면 저마다 제 자리로 가야 한다.
        Array<GameObject*> subtree;
        CollectSubtree(*object, subtree);
        for (std::size_t index = 0; index < subtree.Size(); ++index)
        {
            Placement placement;
            placement.objectId = registry.Track(subtree[index]);
            placement.layer = subtree[index]->GetLayerId();
            m_before.Add(placement);
        }
        m_captured = m_before.Size() != 0;
    }

    const char* SetObjectLayerCommand::GetName() const
    {
        return "Move To Layer";
    }

    bool SetObjectLayerCommand::Apply(LayerId layer)
    {
        bool any = false;
        for (std::size_t index = 0; index < m_before.Size(); ++index)
        {
            if (GameObject* object = m_registry->Resolve(m_before[index].objectId))
            {
                if (m_canvas->SetObjectLayer(object, layer))
                {
                    any = true;
                }
            }
        }
        return any;
    }

    bool SetObjectLayerCommand::Execute()
    {
        if (false == m_captured)
        {
            return false;
        }
        // 이미 그 레이어에 다 있으면 편집이 아니다.
        bool alreadyThere = true;
        for (std::size_t index = 0; index < m_before.Size(); ++index)
        {
            if (m_before[index].layer != m_after)
            {
                alreadyThere = false;
                break;
            }
        }
        if (alreadyThere)
        {
            return false;
        }
        return Apply(m_after);
    }

    void SetObjectLayerCommand::Undo()
    {
        for (std::size_t index = 0; index < m_before.Size(); ++index)
        {
            if (GameObject* object = m_registry->Resolve(m_before[index].objectId))
            {
                m_canvas->SetObjectLayer(object, m_before[index].layer);
            }
        }
    }

    void SetObjectLayerCommand::Redo()
    {
        Apply(m_after);
    }
}
