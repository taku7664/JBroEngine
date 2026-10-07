#include <JBro/NodeGraph/Graph.h>

namespace JBro::NodeGraph
{
    const Node* Graph::FindNode(NodeId id) const noexcept
    {
        return m_nodes.FindBy([id](const Node& node) { return node.id.Get() == id.Get(); });
    }

    Node* Graph::FindNodeMutable(NodeId id) noexcept
    {
        return m_nodes.FindBy([id](const Node& node) { return node.id.Get() == id.Get(); });
    }

    const Link* Graph::FindLink(LinkId id) const noexcept
    {
        return m_links.FindBy([id](const Link& link) { return link.id.Get() == id.Get(); });
    }

    const Link* Graph::FindLinkInto(PinRef input) const noexcept
    {
        return m_links.FindBy([input](const Link& link) { return link.to == input; });
    }

    UInt64 Graph::TakeId()
    {
        const UInt64 id = m_nextId;
        m_nextId = UInt64(m_nextId.Get() + 1);
        return id;
    }

    void Graph::SetNextId(UInt64 next) noexcept
    {
        // 있는 번호와 겹치지 않게 한다 - 읽은 파일의 `NextId` 가 낮아도 새 번호가 옛 노드를 덮지 않는다.
        std::uint64_t floor = next.Get() == 0 ? 1 : next.Get();
        for (const Node& node : m_nodes)
        {
            if (node.id.Get() >= floor)
            {
                floor = node.id.Get() + 1;
            }
        }
        for (const Link& link : m_links)
        {
            if (link.id.Get() >= floor)
            {
                floor = link.id.Get() + 1;
            }
        }
        m_nextId = UInt64(floor);
    }

    NodeId Graph::AddNode(const Domain& domain, std::string_view type, Vector2 position)
    {
        const NodeTypeInfo* info = domain.FindNodeType(type);
        if (info == nullptr)
        {
            return NodeId(InvalidId);
        }
        if (info->unique && m_nodes.FindBy([type](const Node& node) { return node.type.View() == type; }) != nullptr)
        {
            return NodeId(InvalidId);
        }

        Node node;
        node.id = TakeId();
        node.type = String(type);
        node.position = position;
        node.inputs.Reserve(info->inputs.Size());
        for (const PinInfo& pin : info->inputs)
        {
            node.inputs.Add(pin.defaultValue);
        }
        const NodeId id = node.id;
        m_nodes.Add(static_cast<Node&&>(node));
        return id;
    }

    Bool Graph::InsertNode(Node node)
    {
        if (node.id.Get() == InvalidId || FindNode(node.id) != nullptr || FindLink(node.id) != nullptr)
        {
            return false;
        }
        if (node.id.Get() >= m_nextId.Get())
        {
            m_nextId = UInt64(node.id.Get() + 1);
        }
        m_nodes.Add(static_cast<Node&&>(node));
        return true;
    }

    Bool Graph::RemoveNode(NodeId id)
    {
        const std::size_t index = m_nodes.IndexOfBy([id](const Node& node) { return node.id.Get() == id.Get(); });
        if (index == Array<Node>::InvalidIndex)
        {
            return false;
        }
        m_nodes.RemoveAt(index);
        m_links.RemoveAll([id](const Link& link)
            {
                return link.from.node.Get() == id.Get() || link.to.node.Get() == id.Get();
            });
        return true;
    }

    ConnectResult Graph::Connect(const Domain& domain, PinRef from, PinRef to, LinkId id, LinkId* created)
    {
        const Node* source = FindNode(from.node);
        const Node* target = FindNode(to.node);
        if (source == nullptr || target == nullptr)
        {
            return ConnectResult::MissingNode;
        }
        if (source->id.Get() == target->id.Get())
        {
            return ConnectResult::SameNode;
        }
        const NodeTypeInfo* sourceType = domain.FindNodeType(source->type.View());
        const NodeTypeInfo* targetType = domain.FindNodeType(target->type.View());
        if (sourceType == nullptr || targetType == nullptr
            || from.pin >= sourceType->outputs.Size() || to.pin >= targetType->inputs.Size())
        {
            return ConnectResult::MissingPin;
        }
        if (!domain.CanConvert(sourceType->outputs[from.pin].type, targetType->inputs[to.pin].type))
        {
            return ConnectResult::TypeMismatch;
        }
        if (WouldCreateCycle(from.node, to.node))
        {
            return ConnectResult::Cycle;
        }
        if (id.Get() != InvalidId && (FindLink(id) != nullptr || FindNode(id) != nullptr))
        {
            return ConnectResult::IdInUse;
        }

        m_links.RemoveAll([to](const Link& link) { return link.to == to; });

        Link link;
        if (id.Get() == InvalidId)
        {
            link.id = TakeId();
        }
        else
        {
            link.id = id;
            if (id.Get() >= m_nextId.Get())
            {
                m_nextId = UInt64(id.Get() + 1);
            }
        }
        link.from = from;
        link.to = to;
        if (created != nullptr)
        {
            *created = link.id;
        }
        m_links.Add(link);
        return ConnectResult::Connected;
    }

    Bool Graph::Disconnect(LinkId id)
    {
        const std::size_t index = m_links.IndexOfBy([id](const Link& link) { return link.id.Get() == id.Get(); });
        if (index == Array<Link>::InvalidIndex)
        {
            return false;
        }
        m_links.RemoveAt(index);
        return true;
    }

    Bool Graph::SetPosition(NodeId id, Vector2 position)
    {
        Node* node = FindNodeMutable(id);
        if (node == nullptr)
        {
            return false;
        }
        node->position = position;
        return true;
    }

    Bool Graph::SetInput(NodeId id, std::size_t pin, const PinValue& value)
    {
        Node* node = FindNodeMutable(id);
        if (node == nullptr || pin >= node->inputs.Size())
        {
            return false;
        }
        node->inputs[pin] = value;
        return true;
    }

    Bool Graph::WouldCreateCycle(NodeId from, NodeId to) const
    {
        if (from.Get() == to.Get())
        {
            return true;
        }
        // `to` 에서 출력 쪽으로 퍼져 `from` 에 닿는지 본다. 편집 때만 부르므로 할당을 아끼지 않는다.
        Array<NodeId> pending;
        Array<NodeId> visited;
        pending.Add(to);
        while (!pending.IsEmpty())
        {
            const NodeId current = pending.Last();
            pending.RemoveAt(pending.Size() - 1);
            if (current.Get() == from.Get())
            {
                return true;
            }
            if (visited.FindBy([current](NodeId id) { return id.Get() == current.Get(); }) != nullptr)
            {
                continue;
            }
            visited.Add(current);
            for (const Link& link : m_links)
            {
                if (link.from.node.Get() == current.Get())
                {
                    pending.Add(link.to.node);
                }
            }
        }
        return false;
    }

    void Graph::TopologicalOrder(Array<NodeId>& order) const
    {
        order.Clear();
        order.Reserve(m_nodes.Size());
        // 들어오는 링크 수를 센 뒤, 0 이 된 노드를 그래프에 둔 차례대로 꺼낸다 - 같은 그래프는 늘 같은 차례를 낸다.
        Array<std::size_t> remaining;
        remaining.Resize(m_nodes.Size());
        for (const Link& link : m_links)
        {
            const std::size_t index = m_nodes.IndexOfBy([&link](const Node& node) { return node.id.Get() == link.to.node.Get(); });
            if (index != Array<Node>::InvalidIndex)
            {
                ++remaining[index];
            }
        }
        Array<Bool> emitted;
        emitted.Resize(m_nodes.Size());
        Bool progressed = true;
        while (progressed)
        {
            progressed = false;
            for (std::size_t index = 0; index < m_nodes.Size(); ++index)
            {
                if (emitted[index] || remaining[index] != 0)
                {
                    continue;
                }
                emitted[index] = true;
                progressed = true;
                const NodeId id = m_nodes[index].id;
                order.Add(id);
                for (const Link& link : m_links)
                {
                    if (link.from.node.Get() != id.Get())
                    {
                        continue;
                    }
                    const std::size_t target = m_nodes.IndexOfBy([&link](const Node& node) { return node.id.Get() == link.to.node.Get(); });
                    if (target != Array<Node>::InvalidIndex && remaining[target] != 0)
                    {
                        --remaining[target];
                    }
                }
            }
        }
    }

    void Graph::Clear()
    {
        m_domain.clear();
        m_nodes.Clear();
        m_links.Clear();
        m_nextId = UInt64(1);
    }
}
