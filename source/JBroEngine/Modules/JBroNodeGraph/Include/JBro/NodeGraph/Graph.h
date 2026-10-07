#pragma once

#include <JBro/NodeGraph/Domain.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

#include <cstdint>
#include <string_view>

// 노드 그래프 모델이다(D-308, node-graph-plan §2.2). 에디터와 ImGui 를 모른다 - 셰이더 그래프 컴파일러도 이것을 읽는다.
//
// 노드와 링크는 그래프 안에서 늘 커지는 번호로 가리킨다. 핀은 노드 종류의 입력·출력 차례 번호다.
// 그래프는 늘 DAG 다 - `Connect` 가 순환을 만드는 잇기를 거절한다. 입력 하나에 링크는 하나다.
namespace JBro::NodeGraph
{
    using NodeId = UInt64;
    using LinkId = UInt64;
    inline constexpr std::uint64_t InvalidId = 0;

    struct Node
    {
        NodeId id = InvalidId;
        String type;
        // 그래프 공간의 왼쪽 위 자리다. y 는 아래로 커진다(화면과 같다).
        Vector2 position;
        // 입력마다 이어지지 않았을 때의 값이다. 종류의 입력 수와 같다.
        Array<PinValue> inputs;
    };

    struct PinRef
    {
        NodeId node = InvalidId;
        std::size_t pin = 0;

        Bool operator==(const PinRef& other) const noexcept
        {
            return node.Get() == other.node.Get() && pin == other.pin;
        }
    };

    // 출력 핀(`from`)에서 입력 핀(`to`)으로 간다.
    struct Link
    {
        LinkId id = InvalidId;
        PinRef from;
        PinRef to;
    };

    enum class ConnectResult : std::uint8_t
    {
        Connected,
        MissingNode,
        MissingPin,
        SameNode,
        TypeMismatch,
        Cycle,
        // 주어진 링크 번호를 이미 다른 링크가 쓴다.
        IdInUse,
    };

    class Graph final
    {
    public:
        const String& GetDomain() const noexcept { return m_domain; }
        void SetDomain(String domain) { m_domain = static_cast<String&&>(domain); }

        ArrayView<const Node> GetNodes() const noexcept { return m_nodes.View(); }
        ArrayView<const Link> GetLinks() const noexcept { return m_links.View(); }

        const Node* FindNode(NodeId id) const noexcept;
        const Link* FindLink(LinkId id) const noexcept;
        // 그 입력 핀에 들어오는 링크다. 없으면 널이다.
        const Link* FindLinkInto(PinRef input) const noexcept;

        // 종류의 기본값으로 노드를 더한다. 모르는 종류이거나 하나만 둘 종류가 이미 있으면 `InvalidId` 다.
        NodeId AddNode(const Domain& domain, std::string_view type, Vector2 position);
        // 노드를 그 번호 그대로 넣는다(읽기·붙여넣기·되돌리기). 번호가 비었거나 이미 있으면 거짓이다.
        // 입력 값의 수는 맞추지 않는다 - 모르는 종류의 노드도 그대로 남겨 다시 쓸 때 잃지 않게 한다.
        Bool InsertNode(Node node);
        // 노드와 그 노드에 닿은 링크를 지운다.
        Bool RemoveNode(NodeId id);

        // `from` 은 출력, `to` 는 입력이다. `to` 에 링크가 있으면 갈아 치운다. `id` 를 주면 그 번호로 만든다.
        ConnectResult Connect(const Domain& domain, PinRef from, PinRef to, LinkId id = LinkId{}, LinkId* created = nullptr);
        Bool Disconnect(LinkId id);

        Bool SetPosition(NodeId id, Vector2 position);
        Bool SetInput(NodeId id, std::size_t pin, const PinValue& value);

        // `from` → `to` 링크가 순환을 만드는가. `to` 에서 출력을 따라 `from` 에 닿으면 만든다.
        Bool WouldCreateCycle(NodeId from, NodeId to) const;
        // 입력을 먼저 계산할 차례로 모든 노드를 낸다(앞의 노드가 뒤의 노드의 입력이다).
        void TopologicalOrder(Array<NodeId>& order) const;

        // 다음에 매길 번호다. 노드와 링크가 함께 쓴다.
        UInt64 GetNextId() const noexcept { return m_nextId; }
        void SetNextId(UInt64 next) noexcept;

        void Clear();

    private:
        UInt64 TakeId();
        Node* FindNodeMutable(NodeId id) noexcept;

        String m_domain;
        Array<Node> m_nodes;
        Array<Link> m_links;
        UInt64 m_nextId = 1;
    };
}
