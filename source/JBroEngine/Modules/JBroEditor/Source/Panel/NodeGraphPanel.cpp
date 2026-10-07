#include "NodeGraphPanel.h"

#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/NodeGraph/GraphYaml.h>
#include <JBro/NodeGraph/MathDomain.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace JBro
{
    namespace
    {
        // 바꾸기 전후의 그래프 글자를 떠 둔 커맨드다. 대상은 같은 패널의 문서이고, 스택이 문서와 함께 죽는다.
        class NodeGraphEditCommand final : public EditorCommand
        {
        public:
            NodeGraphEditCommand(NodeGraphDocument& document, const char* name, String before, String after, const char* mergeKey)
                : m_document(document), m_name(name), m_before(static_cast<String&&>(before)), m_after(static_cast<String&&>(after)),
                  m_mergeKey(mergeKey == nullptr ? String() : String(mergeKey))
            {
            }

            const char* GetName() const override
            {
                return m_name;
            }

            Bool Execute() override
            {
                return m_document.Apply(m_after);
            }

            void Undo() override
            {
                m_document.Apply(m_before);
            }

            void Redo() override
            {
                m_document.Apply(m_after);
            }

            Bool CanMerge(const EditorCommand& newer) const override
            {
                const auto* other = dynamic_cast<const NodeGraphEditCommand*>(&newer);
                return other != nullptr && &other->m_document == &m_document && !m_mergeKey.empty() && other->m_mergeKey == m_mergeKey;
            }

            Bool TryMerge(const EditorCommand& newer) override
            {
                const auto* other = dynamic_cast<const NodeGraphEditCommand*>(&newer);
                if (other == nullptr)
                {
                    return false;
                }
                m_after = other->m_after;
                return true;
            }

        private:
            NodeGraphDocument& m_document;
            const char* m_name;
            String m_before;
            String m_after;
            String m_mergeKey;
        };

        NodeGraphPanel* PanelOf(const EditorActionContext& context)
        {
            return static_cast<NodeGraphPanel*>(context.panel);
        }

        Bool HasPanel(const EditorActionContext& context)
        {
            return context.panel != nullptr;
        }

        Bool HasSelection(const EditorActionContext& context)
        {
            return HasPanel(context) && !PanelOf(context)->GetCanvasState().interaction.GetSelection().IsEmpty();
        }

        const char* WhyNoSelection(const EditorActionContext&)
        {
            return Loc::TextOr(LocKeys::NodeGraphNoSelection, "No node is selected");
        }

        Bool CanUndo(const EditorActionContext& context)
        {
            return HasPanel(context) && PanelOf(context)->GetCommands().CanUndo();
        }

        Bool CanRedo(const EditorActionContext& context)
        {
            return HasPanel(context) && PanelOf(context)->GetCommands().CanRedo();
        }

        const char* WhyNothing(const EditorActionContext&)
        {
            return Loc::TextOr(LocKeys::NodeGraphNothingToSave, "Nothing has changed");
        }

        Bool CanPasteHere(const EditorActionContext& context)
        {
            return HasPanel(context) && PanelOf(context)->CanPaste();
        }

        const char* WhyNoPaste(const EditorActionContext&)
        {
            return Loc::TextOr(LocKeys::NodeGraphNothingToPaste, "Nothing has been copied");
        }

        Bool CanSave(const EditorActionContext& context)
        {
            return HasPanel(context) && PanelOf(context)->IsDirty();
        }

        // 붙여넣는 자리는 마우스 아래 그래프 자리다. 마우스가 캔버스 밖이면 화면 가운데다.
        Vector2 PastePoint(NodeGraphPanel& panel)
        {
            const Widget::NodeGraphCanvasState& state = panel.GetCanvasState();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            return NodeGraphView::ToGraph(state.camera, Vector2{mouse.x, mouse.y});
        }
    }

    Bool NodeGraphDocument::Apply(const String& text)
    {
        NodeGraph::Graph read;
        Array<String> readProblems;
        if (!NodeGraph::ReadGraph(text.View(), domain, read, readProblems))
        {
            return false;
        }
        graph = static_cast<NodeGraph::Graph&&>(read);
        problems = static_cast<Array<String>&&>(readProblems);
        changes = UInt64(changes.Get() + 1);
        return true;
    }

    void NodeGraphPanel::RegisterActions()
    {
        EditorActionRegistry& actions = EditorActionRegistry::Get();
        struct Row
        {
            const char* name;
            const char* labelKey;
            const char* fallback;
            EditorShortcutBinding primary;
            EditorShortcutBinding secondary;
            Bool (*canExecute)(const EditorActionContext&);
            const char* (*whyBlocked)(const EditorActionContext&);
            Bool (*execute)(EditorActionContext&);
        };
        auto bind = [](ImGuiKey key, Bool control = false, Bool shift = false)
        {
            EditorShortcutBinding binding;
            binding.key = key;
            binding.control = control;
            binding.shift = shift;
            return binding;
        };
        // **이 창에 포커스가 있을 때만 돈다.** 같은 조합의 전역 행동(캔버스의 실행 취소·오브젝트 삭제)을 가린다 - 그래프 창에서
        // Ctrl+Z 를 누르면 캔버스가 아니라 그래프가 되돌아가야 한다(사용자 확인, 그래프 창마다 스택).
        const Row rows[] = {
            {"node_graph.undo", LocKeys::MenuUndo, "Undo", bind(ImGuiKey_Z, true), {}, &CanUndo, &WhyNothing,
                [](EditorActionContext& context) -> Bool { return PanelOf(context)->Undo(); }},
            {"node_graph.redo", LocKeys::MenuRedo, "Redo", bind(ImGuiKey_Y, true), bind(ImGuiKey_Z, true, true), &CanRedo, &WhyNothing,
                [](EditorActionContext& context) -> Bool { return PanelOf(context)->Redo(); }},
            {"node_graph.copy", LocKeys::NodeGraphCopy, "Copy", bind(ImGuiKey_C, true), {}, &HasSelection, &WhyNoSelection,
                [](EditorActionContext& context) -> Bool { return PanelOf(context)->CopySelection(); }},
            {"node_graph.paste", LocKeys::NodeGraphPaste, "Paste", bind(ImGuiKey_V, true), {}, &CanPasteHere, &WhyNoPaste,
                [](EditorActionContext& context) -> Bool { return PanelOf(context)->Paste(PastePoint(*PanelOf(context))); }},
            {"node_graph.duplicate", LocKeys::NodeGraphDuplicate, "Duplicate", bind(ImGuiKey_D, true), {}, &HasSelection, &WhyNoSelection,
                [](EditorActionContext& context) -> Bool { return PanelOf(context)->DuplicateSelection(); }},
            {"node_graph.delete", LocKeys::NodeGraphDelete, "Delete", bind(ImGuiKey_Delete), {}, &HasSelection, &WhyNoSelection,
                [](EditorActionContext& context) -> Bool { return PanelOf(context)->DeleteSelection(); }},
            {"node_graph.frame", LocKeys::NodeGraphFrame, "Frame All", bind(ImGuiKey_F), {}, &HasPanel, nullptr,
                [](EditorActionContext& context) -> Bool { PanelOf(context)->FrameAll(); return true; }},
            {"node_graph.save", LocKeys::NodeGraphSave, "Save", bind(ImGuiKey_S, true), {}, &CanSave, &WhyNothing,
                [](EditorActionContext& context) -> Bool { return PanelOf(context)->Save(); }},
        };
        for (const Row& row : rows)
        {
            EditorActionInfo info;
            info.name = row.name;
            info.labelKey = row.labelKey;
            info.fallbackLabel = row.fallback;
            info.categoryKey = LocKeys::NodeGraphTitle;
            info.panelType = TypeName;
            info.primary = row.primary;
            info.secondary = row.secondary;
            info.CanExecute = row.canExecute;
            info.WhyBlocked = row.whyBlocked;
            info.Execute = row.execute;
            actions.Register(info);
        }
    }

    const char* NodeGraphPanel::GetTitle() const
    {
        return TypeName;
    }

    const char* NodeGraphPanel::GetDisplayTitle() const
    {
        return m_title.empty() ? Loc::TextOr(LocKeys::NodeGraphTitle, "Node Graph") : m_title.c_str();
    }

    Bool NodeGraphPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    Bool NodeGraphPanel::HasMenuBar() const
    {
        return true;
    }

    Bool NodeGraphPanel::Show(AssetId asset)
    {
        const AssetRecord* record = m_editor != nullptr ? m_editor->GetAssetRegistry().Find(asset) : nullptr;
        if (record == nullptr || record->type != AssetType::NodeGraph)
        {
            return false;
        }
        String text;
        if (!m_editor->ReadAssetText(asset, text))
        {
            return false;
        }
        String domainName;
        if (!NodeGraph::ReadGraphDomain(text.View(), domainName))
        {
            return false;
        }
        const NodeGraph::Domain* domain = m_editor->GetNodeGraphDomains().Find(domainName.View());
        if (domain == nullptr)
        {
            Log::Write(LogLevel::Warning, "editor", "the node graph names an unknown domain (%s): %s", record->relativePath.c_str(), domainName.c_str());
            return false;
        }
        m_document.domain = domain;
        if (!m_document.Apply(text))
        {
            return false;
        }
        m_asset = asset;
        m_path = record->relativePath;
        // 저장한 글자는 다시 쓴 글자로 둔다 - 손으로 쓴 파일의 띄어쓰기가 달라도 열자마자 저장 안 됨이 되지 않는다.
        m_savedText = NodeGraph::WriteGraph(m_document.graph, domain);
        m_commands.Clear();
        m_canvas.interaction.ClearSelection();
        m_canvas.framed = false;
        m_message.clear();
        m_dirtyRevision = static_cast<std::uint64_t>(-1);
        UpdateTitle();
        return true;
    }

    Bool NodeGraphPanel::IsDirty() const
    {
        return m_dirty;
    }

    void NodeGraphPanel::UpdateTitle()
    {
        // 저장 안 됨은 글자를 견주어 안다. 커맨드를 돌렸을 때만 다시 쓴다.
        if (m_dirtyRevision.Get() != m_commands.GetRevision().Get())
        {
            m_dirtyRevision = m_commands.GetRevision();
            m_dirty = m_document.domain != nullptr && NodeGraph::WriteGraph(m_document.graph, m_document.domain) != m_savedText;
        }
        m_title = m_path;
        if (m_dirty)
        {
            m_title.append(" *", 2);
        }
    }

    Bool NodeGraphPanel::Save()
    {
        if (m_asset.IsNull() || m_document.domain == nullptr)
        {
            return false;
        }
        const String text = NodeGraph::WriteGraph(m_document.graph, m_document.domain);
        if (!m_editor->WriteAssetText(m_asset, text))
        {
            m_message = Loc::TextOr(LocKeys::NodeGraphSaveFailed, "The node graph could not be saved");
            m_messageIsError = true;
            return false;
        }
        m_savedText = text;
        m_message.clear();
        m_dirtyRevision = static_cast<std::uint64_t>(-1);
        UpdateTitle();
        return true;
    }

    Bool NodeGraphPanel::ReloadFromDisk()
    {
        if (m_asset.IsNull())
        {
            return false;
        }
        const AssetId asset = m_asset;
        if (!Show(asset))
        {
            m_message = Loc::TextOr(LocKeys::NodeGraphReadFailed, "The node graph file could not be read");
            m_messageIsError = true;
            return false;
        }
        return true;
    }

    Bool NodeGraphPanel::Commit(const char* name, const NodeGraph::Graph& edited, const char* mergeKey)
    {
        if (m_document.domain == nullptr)
        {
            return false;
        }
        String before = NodeGraph::WriteGraph(m_document.graph, m_document.domain);
        String after = NodeGraph::WriteGraph(edited, m_document.domain);
        if (before == after)
        {
            return false;
        }
        const Bool executed = m_commands.Execute(MakeOwnerPtr<NodeGraphEditCommand>(m_document, name, static_cast<String&&>(before),
            static_cast<String&&>(after), mergeKey));
        SyncSelection();
        UpdateTitle();
        return executed;
    }

    void NodeGraphPanel::SyncSelection()
    {
        if (m_seenChanges.Get() != m_document.changes.Get())
        {
            m_seenChanges = m_document.changes;
            m_canvas.interaction.Prune(m_document.graph);
        }
    }

    NodeGraph::NodeId NodeGraphPanel::AddNode(std::string_view type, Vector2 position)
    {
        if (m_document.domain == nullptr)
        {
            return NodeGraph::NodeId(NodeGraph::InvalidId);
        }
        NodeGraph::Graph edited = m_document.graph;
        const NodeGraph::NodeId id = edited.AddNode(*m_document.domain, type, position);
        if (id.Get() == NodeGraph::InvalidId || !Commit("Add Node", edited))
        {
            return NodeGraph::NodeId(NodeGraph::InvalidId);
        }
        const NodeGraph::NodeId selected[] = {id};
        m_canvas.interaction.Select({selected, 1});
        return id;
    }

    Bool NodeGraphPanel::Connect(NodeGraph::PinRef from, NodeGraph::PinRef to)
    {
        if (m_document.domain == nullptr)
        {
            return false;
        }
        NodeGraph::Graph edited = m_document.graph;
        if (edited.Connect(*m_document.domain, from, to) != NodeGraph::ConnectResult::Connected)
        {
            return false;
        }
        return Commit("Connect", edited);
    }

    Bool NodeGraphPanel::Disconnect(NodeGraph::LinkId link)
    {
        NodeGraph::Graph edited = m_document.graph;
        return edited.Disconnect(link) && Commit("Disconnect", edited);
    }

    Bool NodeGraphPanel::DisconnectNode(NodeGraph::NodeId node)
    {
        NodeGraph::Graph edited = m_document.graph;
        Array<NodeGraph::LinkId> touching;
        for (const NodeGraph::Link& link : edited.GetLinks())
        {
            if (link.from.node.Get() == node.Get() || link.to.node.Get() == node.Get())
            {
                touching.Add(link.id);
            }
        }
        for (const NodeGraph::LinkId& link : touching)
        {
            edited.Disconnect(link);
        }
        return Commit("Disconnect", edited);
    }

    Bool NodeGraphPanel::MoveSelection(Vector2 delta)
    {
        NodeGraph::Graph edited = m_document.graph;
        for (const NodeGraph::NodeId& id : m_canvas.interaction.GetSelection())
        {
            if (const NodeGraph::Node* node = edited.FindNode(id))
            {
                edited.SetPosition(id, Vector2{node->position.x.Get() + delta.x.Get(), node->position.y.Get() + delta.y.Get()});
            }
        }
        return Commit("Move Nodes", edited);
    }

    Bool NodeGraphPanel::DeleteSelection()
    {
        NodeGraph::Graph edited = m_document.graph;
        for (const NodeGraph::NodeId& id : m_canvas.interaction.GetSelection())
        {
            edited.RemoveNode(id);
        }
        if (!Commit("Delete Nodes", edited))
        {
            return false;
        }
        m_canvas.interaction.ClearSelection();
        return true;
    }

    Bool NodeGraphPanel::SetInput(NodeGraph::NodeId node, std::size_t pin, const NodeGraph::PinValue& value)
    {
        NodeGraph::Graph edited = m_document.graph;
        if (!edited.SetInput(node, pin, value))
        {
            return false;
        }
        // 같은 노드의 같은 핀을 끄는 동안은 커맨드 하나다(매니저가 끌기가 이어지는지 본다).
        char key[64];
        std::snprintf(key, sizeof(key), "input:%llu:%zu", static_cast<unsigned long long>(node.Get()), pin);
        return Commit("Edit Input", edited, key);
    }

    String NodeGraphPanel::SelectionText() const
    {
        // 고른 노드와 그 사이의 링크만 담은 그래프의 글자다. 도메인 이름이 첫 줄이라 다른 도메인에는 붙지 않는다.
        NodeGraph::Graph copied;
        copied.SetDomain(m_document.domain->GetName());
        for (const NodeGraph::NodeId& id : m_canvas.interaction.GetSelection())
        {
            if (const NodeGraph::Node* node = m_document.graph.FindNode(id))
            {
                copied.InsertNode(*node);
            }
        }
        for (const NodeGraph::Link& link : m_document.graph.GetLinks())
        {
            if (copied.FindNode(link.from.node) != nullptr && copied.FindNode(link.to.node) != nullptr)
            {
                copied.Connect(*m_document.domain, link.from, link.to, link.id);
            }
        }
        return NodeGraph::WriteGraph(copied, m_document.domain);
    }

    Bool NodeGraphPanel::CopySelection()
    {
        if (m_document.domain == nullptr || m_canvas.interaction.GetSelection().IsEmpty())
        {
            return false;
        }
        ImGui::SetClipboardText(SelectionText().c_str());
        return true;
    }

    Bool NodeGraphPanel::CanPaste() const
    {
        if (m_document.domain == nullptr)
        {
            return false;
        }
        const char* clipboard = ImGui::GetClipboardText();
        String domain;
        return clipboard != nullptr && NodeGraph::ReadGraphDomain(clipboard, domain) && domain == m_document.domain->GetName();
    }

    Bool NodeGraphPanel::Paste(Vector2 at)
    {
        return CanPaste() && PasteText(ImGui::GetClipboardText(), at, "Paste Nodes");
    }

    Bool NodeGraphPanel::PasteText(const char* text, Vector2 at, const char* commandName)
    {
        NodeGraph::Graph copied;
        Array<String> problems;
        if (m_document.domain == nullptr || text == nullptr || !NodeGraph::ReadGraph(text, m_document.domain, copied, problems)
            || copied.GetNodes().IsEmpty())
        {
            return false;
        }
        // 복사한 묶음의 왼쪽 위를 `at` 에 놓는다. 번호는 새로 받고 묶음 안의 링크를 새 번호로 다시 잇는다.
        Float left = copied.GetNodes()[0].position.x;
        Float top = copied.GetNodes()[0].position.y;
        for (const NodeGraph::Node& node : copied.GetNodes())
        {
            left = std::min(left.Get(), node.position.x.Get());
            top = std::min(top.Get(), node.position.y.Get());
        }
        NodeGraph::Graph edited = m_document.graph;
        Array<NodeGraph::NodeId> fromIds;
        Array<NodeGraph::NodeId> toIds;
        for (const NodeGraph::Node& node : copied.GetNodes())
        {
            const Vector2 place{at.x.Get() + node.position.x.Get() - left.Get(), at.y.Get() + node.position.y.Get() - top.Get()};
            const NodeGraph::NodeId added = edited.AddNode(*m_document.domain, node.type.View(), place);
            if (added.Get() == NodeGraph::InvalidId)
            {
                // 하나만 둘 노드가 이미 있거나 모르는 종류다 - 그것만 빼고 붙인다.
                continue;
            }
            for (std::size_t pin = 0; pin < node.inputs.Size(); ++pin)
            {
                edited.SetInput(added, pin, node.inputs[pin]);
            }
            fromIds.Add(node.id);
            toIds.Add(added);
        }
        auto mapped = [&](NodeGraph::NodeId id)
        {
            for (std::size_t index = 0; index < fromIds.Size(); ++index)
            {
                if (fromIds[index].Get() == id.Get())
                {
                    return toIds[index];
                }
            }
            return NodeGraph::NodeId(NodeGraph::InvalidId);
        };
        for (const NodeGraph::Link& link : copied.GetLinks())
        {
            const NodeGraph::NodeId from = mapped(link.from.node);
            const NodeGraph::NodeId to = mapped(link.to.node);
            if (from.Get() != NodeGraph::InvalidId && to.Get() != NodeGraph::InvalidId)
            {
                edited.Connect(*m_document.domain, NodeGraph::PinRef{from, link.from.pin}, NodeGraph::PinRef{to, link.to.pin});
            }
        }
        if (toIds.IsEmpty() || !Commit(commandName, edited))
        {
            return false;
        }
        m_canvas.interaction.Select(toIds.View());
        return true;
    }

    Bool NodeGraphPanel::DuplicateSelection()
    {
        if (m_document.domain == nullptr || m_canvas.interaction.GetSelection().IsEmpty())
        {
            return false;
        }
        // 복제는 클립보드를 거치지 않는다 - 사용자가 복사해 둔 것을 덮지 않는다. 원본에서 조금 비켜 놓는다.
        Float left = 1.0e30f;
        Float top = 1.0e30f;
        for (const NodeGraph::NodeId& id : m_canvas.interaction.GetSelection())
        {
            if (const NodeGraph::Node* node = m_document.graph.FindNode(id))
            {
                left = std::min(left.Get(), node->position.x.Get());
                top = std::min(top.Get(), node->position.y.Get());
            }
        }
        return PasteText(SelectionText().c_str(), Vector2{left.Get() + 30.0f, top.Get() + 30.0f}, "Duplicate Nodes");
    }

    Bool NodeGraphPanel::Undo()
    {
        const Bool undone = m_commands.Undo();
        SyncSelection();
        UpdateTitle();
        return undone;
    }

    Bool NodeGraphPanel::Redo()
    {
        const Bool redone = m_commands.Redo();
        SyncSelection();
        UpdateTitle();
        return redone;
    }

    void NodeGraphPanel::FrameAll()
    {
        m_frameRequested = true;
    }

    void NodeGraphPanel::ConnectDropped(NodeGraph::Graph& graph, NodeGraph::NodeId added) const
    {
        const NodeGraph::Node* node = graph.FindNode(added);
        const NodeGraph::NodeTypeInfo* type = node == nullptr ? nullptr : m_document.domain->FindNodeType(node->type.View());
        if (type == nullptr)
        {
            return;
        }
        const std::size_t count = m_dropFromOutput ? type->inputs.Size() : type->outputs.Size();
        for (std::size_t pin = 0; pin < count; ++pin)
        {
            const NodeGraph::PinRef other{added, pin};
            const NodeGraph::ConnectResult result = m_dropFromOutput
                ? graph.Connect(*m_document.domain, m_dropPin, other)
                : graph.Connect(*m_document.domain, other, m_dropPin);
            if (result == NodeGraph::ConnectResult::Connected)
            {
                return;
            }
        }
    }

    void NodeGraphPanel::HandleEvents(const NodeGraphView::Events& events)
    {
        if (events.moved)
        {
            MoveSelection(events.moveDelta);
        }
        if (events.connect)
        {
            Connect(events.connectFrom, events.connectTo);
        }
        if (events.disconnect)
        {
            Disconnect(events.disconnectLink);
        }
        if (events.contextMenu || events.linkDropped)
        {
            m_menuFromDrop = events.linkDropped;
            m_menuPoint = events.linkDropped ? events.dropPoint : events.contextPoint;
            m_menuHit = events.linkDropped ? NodeGraphView::Hit{} : events.contextHit;
            m_dropPin = events.droppedPin;
            m_dropFromOutput = events.droppedFromOutput;
            // 우클릭한 노드가 고름 밖이면 그것만 고른다 - 메뉴의 삭제·복제가 그 노드를 가리킨다.
            if (m_menuHit.kind == NodeGraphView::HitKind::Node && !m_canvas.interaction.IsSelected(m_menuHit.node))
            {
                const NodeGraph::NodeId only[] = {m_menuHit.node};
                m_canvas.interaction.Select({only, 1});
            }
            Widget::OpenContextMenu("##nodeGraphMenu");
        }
    }

    void NodeGraphPanel::DrawAddNodeMenu()
    {
        const NodeGraph::Domain& domain = *m_document.domain;
        // 묶음 이름은 노드 종류 이름처럼 번역하지 않는다. 묶음이 없는 종류는 맨 위에 둔다.
        Array<String> categories;
        for (const NodeGraph::NodeTypeInfo& type : domain.GetNodeTypes())
        {
            if (!type.category.empty() && !categories.Contains(type.category))
            {
                categories.Add(type.category);
            }
        }
        auto drawItem = [&](const NodeGraph::NodeTypeInfo& type)
        {
            Bool taken = false;
            if (type.unique)
            {
                for (const NodeGraph::Node& node : m_document.graph.GetNodes())
                {
                    if (node.type == type.name)
                    {
                        taken = true;
                        break;
                    }
                }
            }
            if (Widget::MenuItem(type.name.c_str(), nullptr, !taken, Loc::TextOr(LocKeys::NodeGraphOnlyOne, "A graph holds only one of these")))
            {
                NodeGraph::Graph edited = m_document.graph;
                const NodeGraph::NodeId added = edited.AddNode(domain, type.name.View(), m_menuPoint);
                if (added.Get() != NodeGraph::InvalidId)
                {
                    if (m_menuFromDrop)
                    {
                        ConnectDropped(edited, added);
                    }
                    if (Commit("Add Node", edited))
                    {
                        const NodeGraph::NodeId selected[] = {added};
                        m_canvas.interaction.Select({selected, 1});
                    }
                }
            }
        };
        for (const NodeGraph::NodeTypeInfo& type : domain.GetNodeTypes())
        {
            if (type.category.empty())
            {
                drawItem(type);
            }
        }
        for (const String& category : categories)
        {
            if (Widget::BeginMenu(category.c_str()))
            {
                for (const NodeGraph::NodeTypeInfo& type : domain.GetNodeTypes())
                {
                    if (type.category == category)
                    {
                        drawItem(type);
                    }
                }
                Widget::EndMenu();
            }
        }
    }

    void NodeGraphPanel::DrawContextMenu()
    {
        if (!Widget::BeginOpenedContextMenu("##nodeGraphMenu"))
        {
            return;
        }
        EditorActionContext context;
        context.editor = m_editor;
        context.panel = this;
        if (m_menuHit.kind == NodeGraphView::HitKind::Node)
        {
            EditorActionUi::DrawItem("node_graph.copy", context);
            EditorActionUi::DrawItem("node_graph.duplicate", context);
            if (Widget::MenuItem(Loc::TextOr(LocKeys::NodeGraphDisconnectAll, "Break All Links")))
            {
                DisconnectNode(m_menuHit.node);
            }
            EditorActionUi::DrawItem("node_graph.delete", context);
        }
        else if (m_menuHit.kind == NodeGraphView::HitKind::Link)
        {
            if (Widget::MenuItem(Loc::TextOr(LocKeys::NodeGraphDisconnect, "Break Link")))
            {
                Disconnect(m_menuHit.link);
            }
        }
        else
        {
            if (Widget::BeginMenu(Loc::TextOr(LocKeys::NodeGraphAddNode, "Add Node")))
            {
                DrawAddNodeMenu();
                Widget::EndMenu();
            }
            if (!m_menuFromDrop && Widget::MenuItem(Loc::TextOr(LocKeys::NodeGraphPaste, "Paste"), nullptr, CanPaste(),
                    Loc::TextOr(LocKeys::NodeGraphNothingToPaste, "Nothing has been copied")))
            {
                Paste(m_menuPoint);
            }
        }
        Widget::EndContextMenu();
    }

    void NodeGraphPanel::OnMenuBar()
    {
        EditorActionContext context;
        context.editor = m_editor;
        context.panel = this;
        if (Widget::BeginMenu(Loc::TextOr(LocKeys::MenuFile, "File")))
        {
            EditorActionUi::DrawItem("node_graph.save", context);
            if (Widget::MenuItem(Loc::TextOr(LocKeys::NodeGraphReload, "Reload From Disk")))
            {
                ReloadFromDisk();
            }
            if (Widget::MenuItem(Loc::TextOr(LocKeys::NodeGraphClose, "Close")))
            {
                m_editor->ClosePanel(*this);
            }
            Widget::EndMenu();
        }
        if (Widget::BeginMenu(Loc::TextOr(LocKeys::MenuEdit, "Edit")))
        {
            EditorActionUi::DrawItem("node_graph.undo", context);
            EditorActionUi::DrawItem("node_graph.redo", context);
            EditorActionUi::DrawItem("node_graph.copy", context);
            EditorActionUi::DrawItem("node_graph.paste", context);
            EditorActionUi::DrawItem("node_graph.duplicate", context);
            EditorActionUi::DrawItem("node_graph.delete", context);
            EditorActionUi::DrawItem("node_graph.frame", context);
            Widget::EndMenu();
        }
    }

    void NodeGraphPanel::DrawSidebar(Float width)
    {
        const ArrayView<const NodeGraph::NodeId> selection = m_canvas.interaction.GetSelection();
        if (selection.Size() != 1)
        {
            Widget::HintText(selection.IsEmpty() ? Loc::TextOr(LocKeys::NodeGraphNoSelection, "No node is selected")
                                                 : Loc::TextOr(LocKeys::NodeGraphManySelected, "Several nodes are selected"));
            return;
        }
        const NodeGraph::Node* node = m_document.graph.FindNode(selection[0]);
        if (node == nullptr)
        {
            return;
        }
        const NodeGraph::NodeTypeInfo* type = m_document.domain->FindNodeType(node->type.View());
        Widget::SectionHeader(node->type.c_str()).Draw();
        if (type == nullptr)
        {
            Widget::SeverityTextF(Widget::Severity::Warning, "%s", Loc::TextOr(LocKeys::NodeGraphUnknownType, "This domain has no node of this type"));
            return;
        }
        if (type->inputs.IsEmpty())
        {
            return;
        }
        Widget::FormLayout layout("##nodeInputs", 4.0f, ImVec2(2.0f, 1.0f), 0.0f, width);
        for (std::size_t pin = 0; pin < type->inputs.Size() && pin < node->inputs.Size(); ++pin)
        {
            const NodeGraph::PinInfo& info = type->inputs[pin];
            layout.Row(Widget::FieldLabel(info.name.c_str()),
                [&] {
                    if (m_document.graph.FindLinkInto(NodeGraph::PinRef{node->id, pin}) != nullptr)
                    {
                        Widget::HintText(Loc::TextOr(LocKeys::NodeGraphLinked, "Linked"));
                        return;
                    }
                    const NodeGraph::PinTypeInfo* pinType = m_document.domain->FindPinType(info.type);
                    const Int32 count = static_cast<JBro::Int32>(pinType == nullptr ? 4u : std::min(pinType->components.Get(), 4u));
                    char id[48];
                    std::snprintf(id, sizeof(id), "##input%zu", pin);
                    NodeGraph::PinValue value = node->inputs[pin];
                    if (Widget::ScalarRunField(id, value.values, count, 0.01f, false, 0.0f, 0.0f))
                    {
                        SetInput(node->id, pin, value);
                    }
                });
        }
    }

    void NodeGraphPanel::DrawPreview()
    {
        // 샘플 도메인 `Math` 는 결과 노드의 값을 보인다 - 그래프를 결과물로 바꾸는 자리가 실제로 도는지 눈으로 본다.
        if (m_document.domain == nullptr || m_document.domain->GetName().View() != NodeGraph::MathDomainName)
        {
            return;
        }
        Widget::SectionHeader(Loc::TextOr(LocKeys::NodeGraphPreview, "Preview")).SpacingBefore().Draw();
        NodeGraph::PinValue result;
        if (!NodeGraph::EvaluateMath(m_document.graph, *m_document.domain, result))
        {
            Widget::HintText(Loc::TextOr(LocKeys::NodeGraphNoOutput, "Add an Output node to see the result"));
            return;
        }
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float side = ImGui::GetFrameHeight() * 2.0f;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec4 color(std::clamp(result.values[0].Get(), 0.0f, 1.0f), std::clamp(result.values[1].Get(), 0.0f, 1.0f),
            std::clamp(result.values[2].Get(), 0.0f, 1.0f), 1.0f);
        draw->AddRectFilled(at, ImVec2(at.x + side, at.y + side), ImGui::ColorConvertFloat4ToU32(color), 4.0f);
        draw->AddRect(at, ImVec2(at.x + side, at.y + side), ImGui::ColorConvertFloat4ToU32(EditorTheme::Line), 4.0f);
        ImGui::Dummy(ImVec2(side, side));
        ImGui::SameLine();
        Widget::TextF("%.3f  %.3f  %.3f  %.3f", static_cast<double>(result.values[0].Get()), static_cast<double>(result.values[1].Get()),
            static_cast<double>(result.values[2].Get()), static_cast<double>(result.values[3].Get()));
    }

    void NodeGraphPanel::OnDraw()
    {
        SyncSelection();
        UpdateTitle();
        if (m_document.domain == nullptr)
        {
            Widget::HintText(Loc::TextOr(LocKeys::NodeGraphReadFailed, "The node graph file could not be read"));
            return;
        }
        if (!m_message.empty())
        {
            Widget::SeverityTextF(m_messageIsError ? Widget::Severity::Error : Widget::Severity::Info, "%s", m_message.c_str());
        }
        if (!m_document.problems.IsEmpty())
        {
            // 파일을 읽을 때 빠진 것이다. 무엇이 빠졌는지는 파일의 말이라 그대로 보인다.
            Widget::SeverityTextF(Widget::Severity::Warning, "%s", Loc::TextOr(LocKeys::NodeGraphProblems, "Some parts of the file could not be read"));
            String joined;
            for (const String& problem : m_document.problems)
            {
                joined.append(problem);
                joined.push_back('\n');
            }
            Widget::HoveredTooltip(joined.c_str());
        }

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float sidebar = std::min(280.0f, std::max(160.0f, avail.x * 0.3f));
        const ImVec2 canvasSize(std::max(64.0f, avail.x - sidebar - ImGui::GetStyle().ItemSpacing.x), std::max(64.0f, avail.y));
        if (m_frameRequested)
        {
            NodeGraphView::Frame(m_canvas.camera, m_canvas.style, m_document.graph, *m_document.domain, Vector2{canvasSize.x, canvasSize.y});
            m_frameRequested = false;
        }
        NodeGraphView::Events events;
        Widget::NodeGraphCanvas("##nodeGraph", m_document.graph, *m_document.domain, m_canvas, canvasSize, events);
        HandleEvents(events);
        DrawContextMenu();

        ImGui::SameLine();
        ImGui::BeginGroup();
        DrawSidebar(sidebar);
        DrawPreview();
        ImGui::EndGroup();
    }
}
