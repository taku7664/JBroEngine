#include <JBro/Editor/Command/LayerCommands.h>

#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

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
        m_space = found->GetSpace();
        m_scaleMode = found->GetScaleMode();
        m_blend = found->GetBlend();
        m_opacity = found->GetOpacity();
        m_parallax = found->GetParallax();
        m_sourceAsset = found->GetSourceAsset();
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
        restored.SetSpace(m_space);
        restored.SetScaleMode(m_scaleMode);
        restored.SetBlend(m_blend);
        restored.SetOpacity(m_opacity);
        restored.SetParallax(m_parallax);
        restored.SetSourceAsset(m_sourceAsset);
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

    SetLayerSpaceCommand::SetLayerSpaceCommand(Canvas& canvas, EditorObjectRegistry& registry, LayerId layer, LayerSpace space,
        ScreenScaleMode scaleMode, const Array<RootMove>& moves)
        : m_canvas(&canvas)
        , m_registry(&registry)
        , m_layerId(layer)
        , m_spaceAfter(space)
        , m_modeAfter(scaleMode)
        , m_after(moves)
    {
        const Layer* found = canvas.FindLayer(layer);
        if (found == nullptr)
        {
            return;
        }
        m_spaceBefore = found->GetSpace();
        m_modeBefore = found->GetScaleMode();
        // 옮길 루트의 옛 자리를 먼저 뜬다. 하나라도 못 뜨면 실행하지 않는다.
        for (const RootMove& move : moves)
        {
            GameObject* object = registry.Resolve(move.object);
            const auto* transform = object != nullptr ? canvas.FindComponentRaw<Component::Transform2D>(object) : nullptr;
            if (transform == nullptr)
            {
                return;
            }
            m_before.Add(RootMove{ move.object, transform->position.x, transform->position.y });
        }
        m_captured = true;
    }

    const char* SetLayerSpaceCommand::GetName() const
    {
        return "Change Layer Space";
    }

    void SetLayerSpaceCommand::Apply(bool after)
    {
        Layer* layer = m_canvas->FindLayer(m_layerId);
        if (layer == nullptr)
        {
            return;
        }
        layer->SetSpace(after ? m_spaceAfter : m_spaceBefore);
        layer->SetScaleMode(after ? m_modeAfter : m_modeBefore);
        const Array<RootMove>& moves = after ? m_after : m_before;
        for (const RootMove& move : moves)
        {
            GameObject* object = m_registry->Resolve(move.object);
            auto* transform = object != nullptr ? m_canvas->FindComponentRaw<Component::Transform2D>(object) : nullptr;
            if (transform != nullptr)
            {
                transform->position = { move.x, move.y };
            }
        }
    }

    bool SetLayerSpaceCommand::Execute()
    {
        if (false == m_captured || (m_spaceBefore == m_spaceAfter && m_modeBefore == m_modeAfter && m_after.IsEmpty()))
        {
            return false;
        }
        if (m_canvas->FindLayer(m_layerId) == nullptr)
        {
            return false;
        }
        Apply(true);
        return true;
    }

    void SetLayerSpaceCommand::Undo()
    {
        Apply(false);
    }

    void SetLayerSpaceCommand::Redo()
    {
        Apply(true);
    }

    // ── SetLayerCompositeCommand ─────────────────────────────────────────

    SetLayerCompositeCommand::SetLayerCompositeCommand(Canvas& canvas, LayerId layer, LayerBlend blend, float opacity)
        : m_canvas(&canvas)
        , m_layerId(layer)
        , m_blendAfter(blend)
    {
        const Layer* found = canvas.FindLayer(layer);
        if (found == nullptr)
        {
            return;
        }
        m_blendBefore = found->GetBlend();
        m_opacityBefore = found->GetOpacity();
        // 레이어가 받는 값으로 자른 것을 든다. 그래야 "바뀐 것이 없다" 를 레이어와 같은 눈으로 잰다.
        Layer probe(InvalidLayerId, "");
        probe.SetOpacity(m_opacityBefore);
        probe.SetOpacity(opacity);
        m_opacityAfter = probe.GetOpacity();
        m_captured = true;
    }

    const char* SetLayerCompositeCommand::GetName() const
    {
        return "Set Layer Blend";
    }

    bool SetLayerCompositeCommand::Execute()
    {
        if (false == m_captured || (m_blendBefore == m_blendAfter && m_opacityBefore == m_opacityAfter)
            || m_canvas->FindLayer(m_layerId) == nullptr)
        {
            return false;
        }
        Apply(m_blendAfter, m_opacityAfter);
        return true;
    }

    void SetLayerCompositeCommand::Undo()
    {
        Apply(m_blendBefore, m_opacityBefore);
    }

    void SetLayerCompositeCommand::Redo()
    {
        Apply(m_blendAfter, m_opacityAfter);
    }

    bool SetLayerCompositeCommand::CanMerge(const EditorCommand& newer) const
    {
        const auto* other = dynamic_cast<const SetLayerCompositeCommand*>(&newer);
        return other != nullptr && other->m_canvas == m_canvas && other->m_layerId == m_layerId;
    }

    bool SetLayerCompositeCommand::TryMerge(const EditorCommand& newer)
    {
        if (false == CanMerge(newer))
        {
            return false;
        }
        // 도착값만 흡수한다. 처음 값은 끌기를 시작하기 전의 것이다.
        const auto& other = static_cast<const SetLayerCompositeCommand&>(newer);
        m_blendAfter = other.m_blendAfter;
        m_opacityAfter = other.m_opacityAfter;
        return true;
    }

    void SetLayerCompositeCommand::Apply(LayerBlend blend, float opacity)
    {
        if (Layer* layer = m_canvas->FindLayer(m_layerId))
        {
            layer->SetBlend(blend);
            layer->SetOpacity(opacity);
        }
    }

    // ── SetLayerSourceAssetCommand ───────────────────────────────────────

    SetLayerSourceAssetCommand::SetLayerSourceAssetCommand(Canvas& canvas, LayerId layer, const Uuid& asset)
        : m_canvas(&canvas)
        , m_layerId(layer)
        , m_after(asset)
    {
        if (const Layer* found = canvas.FindLayer(layer))
        {
            m_before = found->GetSourceAsset();
            m_captured = true;
        }
    }

    const char* SetLayerSourceAssetCommand::GetName() const
    {
        return "Link Layer Asset";
    }

    bool SetLayerSourceAssetCommand::Execute()
    {
        Layer* layer = m_captured ? m_canvas->FindLayer(m_layerId) : nullptr;
        if (layer == nullptr || m_before == m_after)
        {
            return false;
        }
        layer->SetSourceAsset(m_after);
        return true;
    }

    void SetLayerSourceAssetCommand::Undo()
    {
        if (Layer* layer = m_canvas->FindLayer(m_layerId))
        {
            layer->SetSourceAsset(m_before);
        }
    }

    void SetLayerSourceAssetCommand::Redo()
    {
        if (Layer* layer = m_canvas->FindLayer(m_layerId))
        {
            layer->SetSourceAsset(m_after);
        }
    }

    // ── AddLayerFromAssetCommand ─────────────────────────────────────────

    AddLayerFromAssetCommand::AddLayerFromAssetCommand(Canvas& canvas, EditorObjectRegistry& registry, const String& text, const Uuid& asset)
        : m_canvas(&canvas)
        , m_registry(&registry)
        , m_text(text)
        , m_asset(asset)
    {
    }

    const char* AddLayerFromAssetCommand::GetName() const
    {
        return "Add Layer From Asset";
    }

    bool AddLayerFromAssetCommand::Execute()
    {
        if (m_captured)
        {
            Redo();
            return m_layerId != InvalidLayerId;
        }
        CanvasFileError error;
        LayerId created = InvalidLayerId;
        if (false == ReadLayerText(*m_canvas, m_text.c_str(), m_text.size(), created, error))
        {
            m_error = error.message;
            return false;
        }
        Layer* layer = m_canvas->FindLayer(created);
        if (layer == nullptr)
        {
            return false;
        }
        layer->SetSourceAsset(m_asset);
        m_layerId = created;
        m_index = layer->GetOrder();
        m_name = layer->GetName();
        m_visible = layer->IsVisible();
        m_space = layer->GetSpace();
        m_scaleMode = layer->GetScaleMode();
        m_blend = layer->GetBlend();
        m_opacity = layer->GetOpacity();
        m_parallax = layer->GetParallax();
        // **되살릴 값을 뜨지 못하면 넣은 것도 거둔다**(§11.5). 반쪽 스냅샷으로는 되돌리기·다시 하기가 오브젝트를 잃는다.
        Array<GameObject*> roots;
        m_canvas->GetRootObjects(roots);
        for (std::size_t index = 0; index < roots.Size(); ++index)
        {
            if (roots[index]->GetLayerId() != created)
            {
                continue;
            }
            ObjectTreeSnapshot tree;
            if (false == tree.Capture(*m_registry, *roots[index]))
            {
                m_trees.Clear();
                m_captured = true;
                Undo();
                m_captured = false;
                m_error = "the layer could not be captured for undo";
                return false;
            }
            m_trees.Add(std::move(tree));
        }
        m_captured = true;
        return true;
    }

    void AddLayerFromAssetCommand::Undo()
    {
        if (false == m_captured || m_layerId == InvalidLayerId)
        {
            return;
        }
        for (std::size_t index = m_trees.Size(); index > 0; --index)
        {
            m_trees[index - 1].DestroyRoot(*m_canvas, *m_registry);
        }
        // 뜨지 못한 채 거두는 길(Execute 의 실패)에서는 나무가 없다 - 레이어 위에 남은 것을 직접 지운다.
        if (m_trees.IsEmpty())
        {
            Array<GameObject*> roots;
            m_canvas->GetRootObjects(roots);
            for (std::size_t index = 0; index < roots.Size(); ++index)
            {
                if (roots[index]->GetLayerId() == m_layerId)
                {
                    m_canvas->DestroyObject(roots[index]);
                }
            }
            m_canvas->FlushPendingDestroy();
        }
        m_canvas->DestroyLayer(m_layerId);
        m_layerId = InvalidLayerId;
    }

    void AddLayerFromAssetCommand::Redo()
    {
        if (false == m_captured || m_layerId != InvalidLayerId)
        {
            return;
        }
        Layer& layer = m_canvas->CreateLayer(m_name.c_str());
        layer.SetVisible(m_visible);
        layer.SetSpace(m_space);
        layer.SetScaleMode(m_scaleMode);
        layer.SetBlend(m_blend);
        layer.SetOpacity(m_opacity);
        layer.SetParallax(m_parallax);
        layer.SetSourceAsset(m_asset);
        m_layerId = layer.GetId();
        m_canvas->MoveLayer(m_layerId, m_index);
        for (std::size_t index = 0; index < m_trees.Size(); ++index)
        {
            ObjectTreeSnapshot& tree = m_trees[index];
            for (std::size_t entry = 0; entry < tree.objects.Size(); ++entry)
            {
                tree.objects[entry].layer = m_layerId;
            }
            tree.Restore(*m_canvas, *m_registry, nullptr, true);
        }
    }

    // ── SetLayerParallaxCommand ──────────────────────────────────────────

    SetLayerParallaxCommand::SetLayerParallaxCommand(Canvas& canvas, LayerId layer, float factor)
        : m_canvas(&canvas)
        , m_layerId(layer)
    {
        const Layer* found = canvas.FindLayer(layer);
        if (found == nullptr)
        {
            return;
        }
        m_before = found->GetParallax();
        // 레이어가 받는 값으로 든다 - 받지 않는 값이면 처음 값 그대로라 바뀐 것이 없다.
        Layer probe(InvalidLayerId, "");
        probe.SetParallax(m_before);
        probe.SetParallax(factor);
        m_after = probe.GetParallax();
        m_captured = true;
    }

    const char* SetLayerParallaxCommand::GetName() const
    {
        return "Set Layer Parallax";
    }

    bool SetLayerParallaxCommand::Execute()
    {
        Layer* layer = m_captured ? m_canvas->FindLayer(m_layerId) : nullptr;
        if (layer == nullptr || m_before == m_after)
        {
            return false;
        }
        layer->SetParallax(m_after);
        return true;
    }

    void SetLayerParallaxCommand::Undo()
    {
        if (Layer* layer = m_canvas->FindLayer(m_layerId))
        {
            layer->SetParallax(m_before);
        }
    }

    void SetLayerParallaxCommand::Redo()
    {
        if (Layer* layer = m_canvas->FindLayer(m_layerId))
        {
            layer->SetParallax(m_after);
        }
    }

    bool SetLayerParallaxCommand::CanMerge(const EditorCommand& newer) const
    {
        const auto* other = dynamic_cast<const SetLayerParallaxCommand*>(&newer);
        return other != nullptr && other->m_canvas == m_canvas && other->m_layerId == m_layerId;
    }

    bool SetLayerParallaxCommand::TryMerge(const EditorCommand& newer)
    {
        if (false == CanMerge(newer))
        {
            return false;
        }
        m_after = static_cast<const SetLayerParallaxCommand&>(newer).m_after;
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
