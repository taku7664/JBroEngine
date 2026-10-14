#include "NodeGraphPanel.h"

#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/ConfirmPopup.h>
#include <JBro/Editor/EditorApplication.h>
#include "NodeGraphInspectorPanel.h"
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

        // 그래프 창의 행동이면 그 창, 노드 인스펙터의 행동이면 그것이 보이는 그래프 창이다.
        NodeGraphPanel* PanelOf(const EditorActionContext& context)
        {
            if (context.panel != nullptr && std::strcmp(context.panel->GetTitle(), NodeGraphPanel::TypeName) == 0)
            {
                return static_cast<NodeGraphPanel*>(context.panel);
            }
            return context.editor == nullptr ? nullptr : NodeGraphPanel::FindActive(*context.editor);
        }

        Bool HasPanel(const EditorActionContext& context)
        {
            return PanelOf(context) != nullptr;
        }

        UInt64 g_focusSerial = 0;

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
            // 맞추기는 캔버스 뷰처럼 스페이스바다(D-298).
            {"node_graph.frame", LocKeys::NodeGraphFrame, "Frame", bind(ImGuiKey_Space), {}, &HasPanel, nullptr,
                [](EditorActionContext& context) -> Bool { PanelOf(context)->FrameSelection(); return true; }},
            // 주석은 언리얼 블루프린트처럼 C 다(D-321). 고른 노드가 있으면 감싼다.
            {"node_graph.comment", LocKeys::NodeGraphAddComment, "Add Comment", bind(ImGuiKey_C), {}, &HasPanel, nullptr,
                [](EditorActionContext& context) -> Bool
                {
                    NodeGraphPanel* panel = PanelOf(context);
                    return panel->AddComment(PastePoint(*panel)).Get() != NodeGraph::InvalidId;
                }},
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
        // **노드 인스펙터에 포커스가 있어도 실행 취소·저장은 그 그래프의 것이다.** 올리지 않으면 같은 조합의 전역 행동이 돌아,
        // 인스펙터에서 값을 고치고 Ctrl+Z 를 누르면 캔버스가 되돌아간다.
        const Row inspectorRows[] = {
            {"node_inspector.undo", LocKeys::MenuUndo, "Undo", bind(ImGuiKey_Z, true), {}, &CanUndo, &WhyNothing,
                [](EditorActionContext& context) -> Bool { return PanelOf(context)->Undo(); }},
            {"node_inspector.redo", LocKeys::MenuRedo, "Redo", bind(ImGuiKey_Y, true), bind(ImGuiKey_Z, true, true), &CanRedo, &WhyNothing,
                [](EditorActionContext& context) -> Bool { return PanelOf(context)->Redo(); }},
            {"node_inspector.save", LocKeys::NodeGraphSave, "Save", bind(ImGuiKey_S, true), {}, &CanSave, &WhyNothing,
                [](EditorActionContext& context) -> Bool { return PanelOf(context)->Save(); }},
        };
        for (const Row& row : inspectorRows)
        {
            EditorActionInfo info;
            info.name = row.name;
            info.labelKey = row.labelKey;
            info.fallbackLabel = row.fallback;
            info.categoryKey = LocKeys::NodeGraphInspectorTitle;
            info.panelType = NodeGraphInspectorPanel::TypeName;
            info.primary = row.primary;
            info.secondary = row.secondary;
            info.CanExecute = row.canExecute;
            info.WhyBlocked = row.whyBlocked;
            info.Execute = row.execute;
            actions.Register(info);
        }
    }

    NodeGraphPanel* NodeGraphPanel::FindActive(EditorApplication& editor)
    {
        Array<EditorPanel*> panels;
        editor.FindPanels(TypeName, panels);
        NodeGraphPanel* best = nullptr;
        for (EditorPanel* panel : panels)
        {
            auto* graph = static_cast<NodeGraphPanel*>(panel);
            if (best == nullptr || graph->m_focusSerial.Get() > best->m_focusSerial.Get())
            {
                best = graph;
            }
        }
        return best;
    }

    void NodeGraphPanel::RevealInspector(Bool focus)
    {
        EditorPanel* inspector = m_editor->FindPanel(NodeGraphInspectorPanel::TypeName);
        if (inspector == nullptr)
        {
            inspector = m_editor->CreatePanel(NodeGraphInspectorPanel::TypeName);
        }
        if (inspector != nullptr && (focus || false == inspector->IsOpen()))
        {
            inspector->RequestFocus();
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
        // 막 연 그래프가 노드 인스펙터의 대상이다. 인스펙터가 없으면 같은 도크의 오른쪽에 세운다.
        g_focusSerial = UInt64(g_focusSerial.Get() + 1);
        m_focusSerial = g_focusSerial;
        RevealInspector(false);
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

    void NodeGraphPanel::RequestReload()
    {
        if (!IsDirty())
        {
            ReloadFromDisk();
            return;
        }
        // 다시 읽으면 저장하지 않은 것이 사라진다 - 되돌릴 수 없으므로 먼저 묻는다(D-174 의 묻는 창).
        m_askingReload = true;
        m_editor->OpenPopup(MakeOwnerPtr<ConfirmPopup>(
            Loc::TextOr(LocKeys::FileWindowUnsavedTitle, "Unsaved changes"),
            Loc::TextOr(LocKeys::FileWindowReloadMessage, "There are changes that are not saved. Reloading from disk throws them away"),
            nullptr,
            Loc::TextOr(LocKeys::FileWindowReloadDiscard, "Reload without saving"),
            Loc::TextOr(LocKeys::CommonCancel, "Cancel"),
            &NodeGraphPanel::AnswerReload, nullptr, "##NodeGraphReload"));
    }

    void NodeGraphPanel::RequestClose()
    {
        if (!IsDirty())
        {
            m_editor->ClosePanel(*this);
            return;
        }
        m_askingClose = true;
        m_editor->OpenPopup(MakeOwnerPtr<ConfirmPopup>(
            Loc::TextOr(LocKeys::FileWindowUnsavedTitle, "Unsaved changes"),
            Loc::TextOr(LocKeys::FileWindowCloseMessage, "There are changes that are not saved"),
            Loc::TextOr(LocKeys::FileWindowSaveAndClose, "Save and close"),
            Loc::TextOr(LocKeys::FileWindowCloseWithoutSaving, "Close without saving"),
            Loc::TextOr(LocKeys::CommonCancel, "Cancel"),
            &NodeGraphPanel::AnswerClose, nullptr, "##NodeGraphClose"));
    }

    Bool NodeGraphPanel::HasUnsavedChanges() const
    {
        return IsDirty();
    }

    Bool NodeGraphPanel::SaveChanges()
    {
        return !IsDirty() || Save();
    }

    Bool NodeGraphPanel::OnCloseRequested()
    {
        if (!IsDirty())
        {
            return true;
        }
        RequestClose();
        return false;
    }

    void NodeGraphPanel::AnswerReload(EditorApplication& editor, Int32 choice, void*)
    {
        Array<EditorPanel*> panels;
        editor.FindPanels(TypeName, panels);
        for (EditorPanel* panel : panels)
        {
            auto* graph = static_cast<NodeGraphPanel*>(panel);
            if (!graph->m_askingReload)
            {
                continue;
            }
            graph->m_askingReload = false;
            if (choice == 1)
            {
                graph->ReloadFromDisk();
            }
        }
    }

    void NodeGraphPanel::AnswerClose(EditorApplication& editor, Int32 choice, void*)
    {
        Array<EditorPanel*> panels;
        editor.FindPanels(TypeName, panels);
        for (EditorPanel* panel : panels)
        {
            auto* graph = static_cast<NodeGraphPanel*>(panel);
            if (!graph->m_askingClose)
            {
                continue;
            }
            graph->m_askingClose = false;
            // 저장하지 못했으면 닫지 않는다 - 닫으면 고친 것이 사라진다.
            if ((choice == 0 && graph->Save()) || choice == 1)
            {
                editor.ClosePanel(*graph);
            }
        }
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
        return MoveItems(m_canvas.interaction.GetSelection(), delta);
    }

    Bool NodeGraphPanel::MoveItems(ArrayView<const UInt64> items, Vector2 delta)
    {
        auto moved = [items](UInt64 id)
        {
            for (const UInt64& item : items)
            {
                if (item.Get() == id.Get())
                {
                    return true;
                }
            }
            return false;
        };
        auto shifted = [delta](Vector2 point) { return Vector2{point.x.Get() + delta.x.Get(), point.y.Get() + delta.y.Get()}; };
        NodeGraph::Graph edited = m_document.graph;
        for (const UInt64& id : items)
        {
            if (const NodeGraph::Node* node = edited.FindNode(id))
            {
                edited.SetPosition(id, shifted(node->position));
            }
            else if (const NodeGraph::Comment* comment = edited.FindComment(id))
            {
                edited.SetCommentRect(id, shifted(comment->position), comment->size);
            }
        }
        // 두 끝이 다 가는 링크는 경유점도 데려간다 - 그림에서 미리 옮긴 것과 같다(`PlacedWaypoints`).
        for (const NodeGraph::Link& link : m_document.graph.GetLinks())
        {
            if (link.waypoints.IsEmpty() || !moved(link.from.node) || !moved(link.to.node))
            {
                continue;
            }
            Array<Vector2> points;
            for (const Vector2& point : link.waypoints)
            {
                points.Add(shifted(point));
            }
            edited.SetWaypoints(link.id, points.View());
        }
        return Commit("Move Nodes", edited);
    }

    Bool NodeGraphPanel::DeleteSelection()
    {
        NodeGraph::Graph edited = m_document.graph;
        for (const UInt64& id : m_canvas.interaction.GetSelection())
        {
            if (!edited.RemoveNode(id))
            {
                edited.RemoveComment(id);
            }
        }
        if (!Commit("Delete Nodes", edited))
        {
            return false;
        }
        m_canvas.interaction.ClearSelection();
        return true;
    }

    NodeGraph::CommentId NodeGraphPanel::AddComment(Vector2 at)
    {
        if (m_document.domain == nullptr)
        {
            return NodeGraph::CommentId(NodeGraph::InvalidId);
        }
        const NodeGraphView::Style& style = m_canvas.style;
        Vector2 position = at;
        Vector2 size{300.0f, 180.0f};
        // 고른 노드·주석이 있으면 그것을 여백을 두고 감싼다(언리얼 블루프린트의 C 와 같다).
        Bool any = false;
        Rect bounds;
        for (const UInt64& id : m_canvas.interaction.GetSelection())
        {
            Rect rect;
            if (const NodeGraph::Node* node = m_document.graph.FindNode(id))
            {
                rect = NodeGraphView::NodeBounds(*node, m_document.domain->FindNodeType(node->type.View()), style);
            }
            else if (const NodeGraph::Comment* comment = m_document.graph.FindComment(id))
            {
                rect = NodeGraphView::CommentBounds(*comment);
            }
            else
            {
                continue;
            }
            if (!any)
            {
                bounds = rect;
                any = true;
                continue;
            }
            bounds.min = Vector2{std::min(bounds.min.x.Get(), rect.min.x.Get()), std::min(bounds.min.y.Get(), rect.min.y.Get())};
            bounds.max = Vector2{std::max(bounds.max.x.Get(), rect.max.x.Get()), std::max(bounds.max.y.Get(), rect.max.y.Get())};
        }
        if (any)
        {
            const float padding = style.commentPadding.Get();
            position = Vector2{bounds.min.x.Get() - padding, bounds.min.y.Get() - padding - style.commentHeaderHeight.Get()};
            size = Vector2{bounds.max.x.Get() - bounds.min.x.Get() + padding * 2.0f,
                bounds.max.y.Get() - bounds.min.y.Get() + padding * 2.0f + style.commentHeaderHeight.Get()};
        }
        NodeGraph::Graph edited = m_document.graph;
        const NodeGraph::CommentId id = edited.AddComment(String(Loc::TextOr(LocKeys::NodeGraphComment, "Comment")), position, size,
            Color{0.32f, 0.42f, 0.58f, 0.55f});
        if (!Commit("Add Comment", edited))
        {
            return NodeGraph::CommentId(NodeGraph::InvalidId);
        }
        const UInt64 selected[] = {id};
        m_canvas.interaction.Select({selected, 1});
        return id;
    }

    Bool NodeGraphPanel::ResizeComment(NodeGraph::CommentId comment, Vector2 size)
    {
        NodeGraph::Graph edited = m_document.graph;
        const NodeGraph::Comment* found = edited.FindComment(comment);
        return found != nullptr && edited.SetCommentRect(comment, found->position, size) && Commit("Resize Comment", edited);
    }

    Bool NodeGraphPanel::SetCommentTitle(NodeGraph::CommentId comment, const String& title)
    {
        NodeGraph::Graph edited = m_document.graph;
        return edited.SetCommentTitle(comment, title) && Commit("Rename Comment", edited);
    }

    Bool NodeGraphPanel::SetCommentColor(NodeGraph::CommentId comment, const Color& color)
    {
        NodeGraph::Graph edited = m_document.graph;
        if (!edited.SetCommentColor(comment, color))
        {
            return false;
        }
        char key[48];
        std::snprintf(key, sizeof(key), "comment-color:%llu", static_cast<unsigned long long>(comment.Get()));
        return Commit("Comment Color", edited, key);
    }

    Bool NodeGraphPanel::AddWaypoint(NodeGraph::LinkId link, std::size_t index, Vector2 point)
    {
        NodeGraph::Graph edited = m_document.graph;
        return edited.InsertWaypoint(link, index, point) && Commit("Add Reroute Point", edited);
    }

    Bool NodeGraphPanel::MoveWaypoint(NodeGraph::LinkId link, std::size_t index, Vector2 point)
    {
        NodeGraph::Graph edited = m_document.graph;
        return edited.MoveWaypoint(link, index, point) && Commit("Move Reroute Point", edited);
    }

    Bool NodeGraphPanel::RemoveWaypoint(NodeGraph::LinkId link, std::size_t index)
    {
        NodeGraph::Graph edited = m_document.graph;
        return edited.RemoveWaypoint(link, index) && Commit("Remove Reroute Point", edited);
    }

    void NodeGraphPanel::ToggleMinimap()
    {
        m_canvas.style.showMinimap = !m_canvas.style.showMinimap;
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
        // 고른 주석도 담는다. 링크는 경유점째다(D-321).
        NodeGraph::Graph copied;
        copied.SetDomain(m_document.domain->GetName());
        for (const UInt64& id : m_canvas.interaction.GetSelection())
        {
            if (const NodeGraph::Node* node = m_document.graph.FindNode(id))
            {
                copied.InsertNode(*node);
            }
            else if (const NodeGraph::Comment* comment = m_document.graph.FindComment(id))
            {
                copied.InsertComment(*comment);
            }
        }
        for (const NodeGraph::Link& link : m_document.graph.GetLinks())
        {
            if (copied.FindNode(link.from.node) != nullptr && copied.FindNode(link.to.node) != nullptr
                && copied.Connect(*m_document.domain, link.from, link.to, link.id) == NodeGraph::ConnectResult::Connected)
            {
                copied.SetWaypoints(link.id, link.waypoints.View());
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
            || (copied.GetNodes().IsEmpty() && copied.GetComments().IsEmpty()))
        {
            return false;
        }
        // 복사한 묶음의 왼쪽 위를 `at` 에 놓는다. 번호는 새로 받고 묶음 안의 링크를 새 번호로 다시 잇는다.
        Float left = 1.0e30f;
        Float top = 1.0e30f;
        for (const NodeGraph::Node& node : copied.GetNodes())
        {
            left = std::min(left.Get(), node.position.x.Get());
            top = std::min(top.Get(), node.position.y.Get());
        }
        for (const NodeGraph::Comment& comment : copied.GetComments())
        {
            left = std::min(left.Get(), comment.position.x.Get());
            top = std::min(top.Get(), comment.position.y.Get());
        }
        auto place = [&](Vector2 point) { return Vector2{at.x.Get() + point.x.Get() - left.Get(), at.y.Get() + point.y.Get() - top.Get()}; };
        NodeGraph::Graph edited = m_document.graph;
        Array<NodeGraph::NodeId> fromIds;
        Array<NodeGraph::NodeId> toIds;
        Array<UInt64> pasted;
        for (const NodeGraph::Comment& comment : copied.GetComments())
        {
            pasted.Add(edited.AddComment(comment.title, place(comment.position), comment.size, comment.color));
        }
        for (const NodeGraph::Node& node : copied.GetNodes())
        {
            const NodeGraph::NodeId added = edited.AddNode(*m_document.domain, node.type.View(), place(node.position));
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
            pasted.Add(added);
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
            NodeGraph::LinkId created;
            if (from.Get() != NodeGraph::InvalidId && to.Get() != NodeGraph::InvalidId
                && edited.Connect(*m_document.domain, NodeGraph::PinRef{from, link.from.pin}, NodeGraph::PinRef{to, link.to.pin}, {}, &created)
                    == NodeGraph::ConnectResult::Connected)
            {
                Array<Vector2> points;
                for (const Vector2& point : link.waypoints)
                {
                    points.Add(place(point));
                }
                edited.SetWaypoints(created, points.View());
            }
        }
        if (pasted.IsEmpty() || !Commit(commandName, edited))
        {
            return false;
        }
        m_canvas.interaction.Select(pasted.View());
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
        for (const UInt64& id : m_canvas.interaction.GetSelection())
        {
            if (const NodeGraph::Node* node = m_document.graph.FindNode(id))
            {
                left = std::min(left.Get(), node->position.x.Get());
                top = std::min(top.Get(), node->position.y.Get());
            }
            else if (const NodeGraph::Comment* comment = m_document.graph.FindComment(id))
            {
                left = std::min(left.Get(), comment->position.x.Get());
                top = std::min(top.Get(), comment->position.y.Get());
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

    void NodeGraphPanel::FrameSelection()
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
            MoveItems(events.movedItems.View(), events.moveDelta);
        }
        if (events.waypointAdded)
        {
            AddWaypoint(events.waypointLink, events.waypointIndex, events.waypointPoint);
        }
        if (events.waypointMoved)
        {
            MoveWaypoint(events.waypointLink, events.waypointIndex, events.waypointPoint);
        }
        if (events.waypointRemoved)
        {
            RemoveWaypoint(events.waypointLink, events.waypointIndex);
        }
        if (events.commentResized)
        {
            ResizeComment(events.resizedComment, events.resizedSize);
        }
        if (events.connect)
        {
            Connect(events.connectFrom, events.connectTo);
        }
        if (events.disconnect)
        {
            Disconnect(events.disconnectLink);
        }
        // 미니맵 위의 우클릭은 메뉴를 열지 않는다 - 미니맵에는 고를 것이 없다.
        if ((events.contextMenu && events.contextHit.kind != NodeGraphView::HitKind::Minimap) || events.linkDropped)
        {
            m_menuFromDrop = events.linkDropped;
            m_menuPoint = events.linkDropped ? events.dropPoint : events.contextPoint;
            m_menuHit = events.linkDropped ? NodeGraphView::Hit{} : events.contextHit;
            m_dropPin = events.droppedPin;
            m_dropFromOutput = events.droppedFromOutput;
            // 우클릭한 노드·주석이 고름 밖이면 그것만 고른다 - 메뉴의 삭제·복제가 그것을 가리킨다.
            const UInt64 pointed = m_menuHit.kind == NodeGraphView::HitKind::Node ? m_menuHit.node
                : (m_menuHit.kind == NodeGraphView::HitKind::CommentHeader || m_menuHit.kind == NodeGraphView::HitKind::CommentResize)
                    ? m_menuHit.comment : UInt64(NodeGraph::InvalidId);
            if (pointed.Get() != NodeGraph::InvalidId && !m_canvas.interaction.IsSelected(pointed))
            {
                const UInt64 only[] = {pointed};
                m_canvas.interaction.Select({only, 1});
            }
            Widget::OpenContextMenu("##nodeGraphMenu");
        }
        if (events.nodeDoubleClicked || events.commentDoubleClicked)
        {
            // 노드를 두 번 누르면 그 노드를 보이는 인스펙터가 앞으로 온다(오브젝트를 두 번 누를 때와 같은 손짓).
            RevealInspector(true);
        }
    }

    Bool NodeGraphPanel::AcceptsDroppedPin(const NodeGraph::NodeTypeInfo& type) const
    {
        // 실제로 더하고 이어 본다 - 잇기 규칙(타입·넓히기·순환)이 모델 한 곳에만 있다.
        NodeGraph::Graph probe = m_document.graph;
        const NodeGraph::NodeId added = probe.AddNode(*m_document.domain, type.name.View(), m_menuPoint);
        if (added.Get() == NodeGraph::InvalidId)
        {
            return false;
        }
        ConnectDropped(probe, added);
        // 입력에 놓으면 그 입력의 옛 링크를 갈아 치우므로 링크 수로는 모른다 - 새 노드에 닿은 링크가 섰는지 본다.
        for (const NodeGraph::Link& link : probe.GetLinks())
        {
            if (link.from.node.Get() == added.Get() || link.to.node.Get() == added.Get())
            {
                return true;
            }
        }
        return false;
    }

    void NodeGraphPanel::DrawAddNodeMenu()
    {
        const NodeGraph::Domain& domain = *m_document.domain;
        const ArrayView<const NodeGraph::NodeTypeInfo> types = domain.GetNodeTypes();
        // 핀에서 끌어 놓았으면 그 핀에 이을 수 있는 종류만 보인다(언리얼 블루프린트의 "맥락에 맞게").
        Array<std::size_t> shown;
        Array<Widget::NodeMenuItem> items;
        for (std::size_t index = 0; index < types.Size(); ++index)
        {
            const NodeGraph::NodeTypeInfo& type = types[index];
            if (m_menuFromDrop && !AcceptsDroppedPin(type))
            {
                continue;
            }
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
            Widget::NodeMenuItem item;
            item.name = type.name.c_str();
            item.category = type.category.c_str();
            item.enabled = !taken;
            items.Add(item);
            shown.Add(index);
        }
        const Int32 chosen = Widget::NodeSearchMenu(m_menuFilter, m_menuHighlight, items.View(),
            Loc::TextOr(LocKeys::NodeGraphOnlyOne, "A graph holds only one of these"));
        if (chosen.Get() < 0)
        {
            return;
        }
        const NodeGraph::NodeTypeInfo& type = types[shown[static_cast<std::size_t>(chosen.Get())]];
        NodeGraph::Graph edited = m_document.graph;
        const NodeGraph::NodeId added = edited.AddNode(domain, type.name.View(), m_menuPoint);
        if (added.Get() == NodeGraph::InvalidId)
        {
            return;
        }
        if (m_menuFromDrop)
        {
            ConnectDropped(edited, added);
        }
        if (Commit("Add Node", edited))
        {
            const UInt64 selected[] = {added};
            m_canvas.interaction.Select({selected, 1});
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
            EditorActionUi::DrawItem("node_graph.comment", context);
            if (Widget::MenuItem(Loc::TextOr(LocKeys::NodeGraphDisconnectAll, "Break All Links")))
            {
                DisconnectNode(m_menuHit.node);
            }
            EditorActionUi::DrawItem("node_graph.delete", context);
        }
        else if (m_menuHit.kind == NodeGraphView::HitKind::CommentHeader || m_menuHit.kind == NodeGraphView::HitKind::CommentResize)
        {
            EditorActionUi::DrawItem("node_graph.copy", context);
            EditorActionUi::DrawItem("node_graph.duplicate", context);
            EditorActionUi::DrawItem("node_graph.delete", context);
        }
        else if (m_menuHit.kind == NodeGraphView::HitKind::Waypoint)
        {
            if (Widget::MenuItem(Loc::TextOr(LocKeys::NodeGraphRemoveWaypoint, "Remove Reroute Point")))
            {
                RemoveWaypoint(m_menuHit.link, m_menuHit.waypoint);
            }
        }
        else if (m_menuHit.kind == NodeGraphView::HitKind::Link)
        {
            if (Widget::MenuItem(Loc::TextOr(LocKeys::NodeGraphDisconnect, "Break Link")))
            {
                Disconnect(m_menuHit.link);
            }
        }
        else if (m_menuHit.kind != NodeGraphView::HitKind::Minimap)
        {
            // 빈 곳의 메뉴는 노드 찾기가 먼저다 - 열자마자 글자를 쳐서 노드를 더한다.
            DrawAddNodeMenu();
            if (!m_menuFromDrop)
            {
                ImGui::Separator();
                if (Widget::MenuItem(Loc::TextOr(LocKeys::NodeGraphAddComment, "Add Comment")))
                {
                    AddComment(m_menuPoint);
                }
                if (Widget::MenuItem(Loc::TextOr(LocKeys::NodeGraphPaste, "Paste"), nullptr, CanPaste(),
                        Loc::TextOr(LocKeys::NodeGraphNothingToPaste, "Nothing has been copied")))
                {
                    Paste(m_menuPoint);
                }
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
                RequestReload();
            }
            if (Widget::MenuItem(Loc::TextOr(LocKeys::NodeGraphClose, "Close")))
            {
                RequestClose();
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
            EditorActionUi::DrawItem("node_graph.comment", context);
            Widget::EndMenu();
        }
        if (Widget::BeginMenu(Loc::TextOr(LocKeys::MenuView, "View")))
        {
            EditorActionUi::DrawItem("node_graph.frame", context);
            Bool minimap = m_canvas.style.showMinimap;
            if (Widget::MenuToggle(Loc::TextOr(LocKeys::NodeGraphMinimap, "Minimap"), minimap))
            {
                ToggleMinimap();
            }
            Widget::EndMenu();
        }
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

        if (IsFocused())
        {
            g_focusSerial = UInt64(g_focusSerial.Get() + 1);
            m_focusSerial = g_focusSerial;
        }

        // 그래프가 창을 다 쓴다. 고른 노드의 값은 노드 인스펙터(같은 도크의 오른쪽)에 있다.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 canvasSize(std::max(64.0f, avail.x), std::max(64.0f, avail.y));
        NodeGraphView::Events events;
        // 입력 → 커맨드 → 그리기 차례다. 커맨드를 그리기 뒤에 넣으면 놓는 프레임에 노드가 옛 자리에 한 번 그려진다.
        Widget::BeginNodeGraphCanvas("##nodeGraph", m_document.graph, *m_document.domain, m_canvas, canvasSize, events);
        HandleEvents(events);
        if (m_frameRequested)
        {
            NodeGraphView::Frame(m_canvas.camera, m_canvas.style, m_document.graph, *m_document.domain, Vector2{canvasSize.x, canvasSize.y},
                m_canvas.interaction.GetSelection());
            m_frameRequested = false;
        }
        NodeGraphView::Events drawn;
        Widget::EndNodeGraphCanvas(m_document.graph, *m_document.domain, m_canvas, drawn);
        // 노드 안 값 칸은 그리면서 고쳐진다. 끄는 동안 프레임마다 오지만 같은 핀이면 커맨드 하나다(`SetInput` 의 합치기 키).
        if (drawn.inputEdited)
        {
            SetInput(drawn.editedNode, drawn.editedPin, drawn.editedValue);
        }
        DrawContextMenu();
    }
}
