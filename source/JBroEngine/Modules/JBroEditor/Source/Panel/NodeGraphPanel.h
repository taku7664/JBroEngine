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
#include <JBro/Types/Int.h>
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
        // 마지막으로 포커스를 가진(없으면 마지막으로 연) 그래프 창이다. 노드 인스펙터가 이것을 보인다. 없으면 널이다.
        static NodeGraphPanel* FindActive(EditorApplication& editor);

        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        Bool OnCreate(EditorApplication& editor) override;
        // 저장하지 않은 것이 있으면 닫지 않고 묻는다(탭의 닫기 단추).
        Bool OnCloseRequested() override;
        Bool HasUnsavedChanges() const override;
        Bool SaveChanges() override;
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
        // 파일을 다시 읽는다. 저장하지 않은 것은 버린다 - 메뉴는 이것을 바로 부르지 않고 `RequestReload` 로 먼저 묻는다.
        Bool ReloadFromDisk();
        // 저장하지 않은 것이 있으면 묻고, 없으면 곧바로 다시 읽거나 닫는다.
        void RequestReload();
        void RequestClose();
        // 노드 인스펙터를 연다(없으면 만든다). `focus` 면 앞으로 꺼낸다.
        void RevealInspector(Bool focus);

        // ── 편집. 모두 커맨드 하나이고, 바뀐 것이 없으면 커맨드를 쌓지 않고 거짓이다. ──
        NodeGraph::NodeId AddNode(std::string_view type, Vector2 position);
        Bool Connect(NodeGraph::PinRef from, NodeGraph::PinRef to);
        Bool Disconnect(NodeGraph::LinkId link);
        Bool DisconnectNode(NodeGraph::NodeId node);
        Bool MoveSelection(Vector2 delta);
        // 노드·주석을 함께 옮긴다. 두 끝이 다 옮겨 가는 링크는 경유점도 따라간다(D-321).
        Bool MoveItems(ArrayView<const UInt64> items, Vector2 delta);
        // 고른 것(노드·주석)을 지운다.
        Bool DeleteSelection();
        // ── 주석(D-321) ── 고른 노드가 있으면 그것을 감싸고, 없으면 `at` 에 기본 크기로 선다. 만든 주석을 고른다.
        NodeGraph::CommentId AddComment(Vector2 at);
        Bool ResizeComment(NodeGraph::CommentId comment, Vector2 size);
        Bool SetCommentTitle(NodeGraph::CommentId comment, const String& title);
        // 색 칸을 끄는 동안은 커맨드 하나로 합친다.
        Bool SetCommentColor(NodeGraph::CommentId comment, const Color& color);
        // ── 경유점(D-321) ──
        Bool AddWaypoint(NodeGraph::LinkId link, std::size_t index, Vector2 point);
        Bool MoveWaypoint(NodeGraph::LinkId link, std::size_t index, Vector2 point);
        Bool RemoveWaypoint(NodeGraph::LinkId link, std::size_t index);
        // 미니맵을 켜고 끈다(창마다, 저장하지 않는다).
        void ToggleMinimap();
        Bool DuplicateSelection();
        // 같은 칸을 끄는 동안의 값 바꾸기는 커맨드 하나로 합친다.
        Bool SetInput(NodeGraph::NodeId node, std::size_t pin, const NodeGraph::PinValue& value);
        Bool CopySelection();
        Bool Paste(Vector2 at);
        Bool CanPaste() const;
        Bool Undo();
        Bool Redo();
        // 고른 노드(없으면 모든 노드)에 맞춰 화면을 부드럽게 옮긴다(캔버스 뷰의 맞추기와 같다).
        void FrameSelection();

    private:
        // 그래프를 고친 사본으로 커맨드를 만든다. 사본이 지금과 같으면 쌓지 않는다.
        Bool Commit(const char* name, const NodeGraph::Graph& edited, const char* mergeKey = nullptr);
        String SelectionText() const;
        Bool PasteText(const char* text, Vector2 at, const char* commandName);
        void HandleEvents(const NodeGraphView::Events& events);
        // 노드 찾기 메뉴를 그린다. 핀에서 끌어 놓았으면 그 핀에 이을 수 있는 종류만 보인다(D-321).
        void DrawAddNodeMenu();
        void DrawContextMenu();
        // 그 종류의 노드를 더했을 때 끌어 놓은 핀에 이어지는가.
        Bool AcceptsDroppedPin(const NodeGraph::NodeTypeInfo& type) const;
        void UpdateTitle();
        static void AnswerReload(EditorApplication& editor, Int32 choice, void* user);
        static void AnswerClose(EditorApplication& editor, Int32 choice, void* user);
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
        // 노드 찾기 메뉴의 글자와 밝힌 줄이다. 메뉴가 열릴 때마다 비운다.
        String m_menuFilter;
        Int32 m_menuHighlight = 0;
        NodeGraph::PinRef m_dropPin;
        Bool m_dropFromOutput = false;
        Bool m_frameRequested = false;
        // 묻는 창을 띄워 놓고 답을 기다리는 중이다. 답은 패널을 번호로 찾는다(묻는 동안 패널이 닫힐 수 있다).
        Bool m_askingReload = false;
        Bool m_askingClose = false;
        // 포커스를 가질 때마다 커지는 번호다. 가장 큰 창이 노드 인스펙터의 대상이다.
        UInt64 m_focusSerial = 0;
    };
}
