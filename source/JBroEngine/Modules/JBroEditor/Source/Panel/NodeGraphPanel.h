#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/NodeGraph/NodeGraphView.h>
#include <JBro/Editor/Widget/NodeGraphCanvas.h>
#include <JBro/NodeGraph/Domain.h>
#include <JBro/NodeGraph/Graph.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

#include <string_view>

namespace JBro
{
    class EditorApplication;

    // 그래프 창 하나가 고치는 것이다. 커맨드는 이것을 고치고, 패널과 함께 죽는다 - 스택이 패널 안에 있어 커맨드가 이것보다 오래 살지 않는다.
    struct NodeGraphDocument
    {
        NodeGraph::Graph graph;
        const NodeGraph::Domain* domain = nullptr;
        Array<String> problems;
        // 글자로 고칠 때마다 는다. 패널이 고름을 다시 맞추는 데 쓴다.
        UInt64 changes = 0;

        // 그래프를 글자에서 다시 짓는다. 읽지 못하면 그래프를 그대로 두고 거짓이다.
        Bool Apply(const String& text);
    };

    // **노드 그래프 한 장**이다(D-308, node-graph-plan §2.3). `.jgraph` 마다 서는 비고유 패널이고 뿌리의 "노드 그래프" 도크에 속한다.
    //
    //   파일 ▸ 저장 / 디스크에서 다시 읽기 / 닫기      편집 ▸ 실행 취소 / 다시 실행 / 복사 / 붙여넣기 / 복제 / 삭제 / 전체 보기
    //   ┌──────────────── 그래프 ────────────────┬── 고른 노드 ──┐
    //   │                                         │ Multiply       │
    //   │                                         │ A    연결됨    │
    //   │                                         │ B    [1][1]..  │
    //   │                                         │── 미리 보기 ──│
    //   └─────────────────────────────────────────┴────────────────┘
    //
    // **편집은 모두 커맨드이고 스택은 이 창의 것이다**(사용자 확인). 이 창에 포커스가 있으면 실행 취소·다시 실행 단축키가 이 스택을 돌린다.
    // 커맨드는 바꾸기 전후의 그래프 글자를 떠 둔다. 저장은 따로다 - 탭 이름 끝의 `*` 가 저장 안 됨이다.
    class NodeGraphPanel final : public InstancePanel
    {
    public:
        static constexpr const char* TypeName = "NodeGraph";
        static constexpr const char* DockAreaName = "NodeGraph";

        // 패널 행동(실행 취소·복사·삭제 …)을 행동 표에 올린다. 에디터를 켜기 전에 한 번 부른다.
        static void RegisterActions();

        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        Bool OnCreate(EditorApplication& editor) override;
        Bool HasMenuBar() const override;
        void OnMenuBar() override;
        void OnDraw() override;

        // 이 패널에 그 그래프를 싣는다. `.jgraph` 가 아니거나, 읽지 못하거나, 도메인을 모르면 거짓이다.
        Bool Show(AssetId asset);
        AssetId GetAsset() const { return m_asset; }
        const NodeGraph::Graph& GetGraph() const { return m_document.graph; }
        const NodeGraph::Domain* GetDomain() const { return m_document.domain; }
        ArrayView<const String> GetProblems() const { return m_document.problems.View(); }
        Widget::NodeGraphCanvasState& GetCanvasState() { return m_canvas; }
        EditorCommandManager& GetCommands() { return m_commands; }

        Bool IsDirty() const;
        Bool Save();
        Bool ReloadFromDisk();

        // ── 편집. 모두 커맨드 하나이고, 바뀐 것이 없으면 커맨드를 쌓지 않고 거짓이다. ──
        NodeGraph::NodeId AddNode(std::string_view type, Vector2 position);
        Bool Connect(NodeGraph::PinRef from, NodeGraph::PinRef to);
        Bool Disconnect(NodeGraph::LinkId link);
        Bool DisconnectNode(NodeGraph::NodeId node);
        Bool MoveSelection(Vector2 delta);
        Bool DeleteSelection();
        Bool DuplicateSelection();
        // 같은 칸을 끄는 동안의 값 바꾸기는 커맨드 하나로 합친다.
        Bool SetInput(NodeGraph::NodeId node, std::size_t pin, const NodeGraph::PinValue& value);
        Bool CopySelection();
        Bool Paste(Vector2 at);
        Bool CanPaste() const;
        Bool Undo();
        Bool Redo();
        void FrameAll();

    private:
        // 그래프를 고친 사본으로 커맨드를 만든다. 사본이 지금과 같으면 쌓지 않는다.
        Bool Commit(const char* name, const NodeGraph::Graph& edited, const char* mergeKey = nullptr);
        String SelectionText() const;
        Bool PasteText(const char* text, Vector2 at, const char* commandName);
        void HandleEvents(const NodeGraphView::Events& events);
        void DrawAddNodeMenu();
        void DrawContextMenu();
        void DrawSidebar(Float width);
        void DrawPreview();
        void UpdateTitle();
        void SyncSelection();
        // 더한 노드에서 끌어 놓은 핀에 맞는 첫 핀을 찾아 잇는다.
        void ConnectDropped(NodeGraph::Graph& graph, NodeGraph::NodeId added) const;

        EditorApplication* m_editor = nullptr;
        AssetId m_asset;
        String m_path;
        String m_title;
        String m_savedText;
        // 문서가 스택보다 먼저 선다 - 스택(과 그 안의 커맨드)이 먼저 죽는다.
        NodeGraphDocument m_document;
        EditorCommandManager m_commands;
        Widget::NodeGraphCanvasState m_canvas;
        UInt64 m_seenChanges = 0;
        UInt64 m_dirtyRevision = static_cast<std::uint64_t>(-1);
        Bool m_dirty = false;
        String m_message;
        Bool m_messageIsError = false;

        // 우클릭 메뉴를 연 자리와 그 아래의 것, 그리고 핀에서 끌어 놓았으면 그 핀이다.
        Vector2 m_menuPoint;
        NodeGraphView::Hit m_menuHit;
        Bool m_menuFromDrop = false;
        NodeGraph::PinRef m_dropPin;
        Bool m_dropFromOutput = false;
        Bool m_frameRequested = false;
    };
}
