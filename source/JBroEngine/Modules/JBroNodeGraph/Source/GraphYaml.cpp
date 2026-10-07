#include <JBro/NodeGraph/GraphYaml.h>

#include <JBro/Core/Yaml.h>

#include <charconv>
#include <cstring>

namespace JBro::NodeGraph
{
    namespace
    {
        // 핀 타입이 쓰는 칸 수다. 모르는 종류의 노드나 모르는 핀은 네 칸을 다 적어 값을 잃지 않는다.
        std::size_t ComponentsOf(const Domain* domain, const NodeTypeInfo* type, std::size_t pin)
        {
            if (domain == nullptr || type == nullptr || pin >= type->inputs.Size())
            {
                return 4;
            }
            const PinTypeInfo* info = domain->FindPinType(type->inputs[pin].type);
            if (info == nullptr || info->components.Get() == 0 || info->components.Get() > 4)
            {
                return 4;
            }
            return info->components.Get();
        }

        Bool ParseFloat(const char* text, Float& value)
        {
            if (text == nullptr)
            {
                return false;
            }
            const std::size_t length = std::strlen(text);
            float parsed = 0.0f;
            const std::from_chars_result read = std::from_chars(text, text + length, parsed);
            if (read.ec != std::errc{} || read.ptr != text + length)
            {
                return false;
            }
            value = parsed;
            return true;
        }

        Bool FindId(const YamlDocument& document, UInt32 node, const char* key, UInt64& id)
        {
            Int64 value = 0;
            if (!document.FindInt(node, key, value) || value.Get() <= 0)
            {
                return false;
            }
            id = UInt64(static_cast<std::uint64_t>(value.Get()));
            return true;
        }

        // 그 키가 시퀀스면 그것을, 아니면(빈 시퀀스는 빈 스칼라로 읽힌다) 무효를 낸다.
        UInt32 FindSequence(const YamlDocument& document, UInt32 node, const char* key)
        {
            const UInt32 found = document.Find(node, key);
            if (found.Get() == YamlDocument::InvalidNode.Get() || document.GetKind(found) != YamlKind::Sequence)
            {
                return YamlDocument::InvalidNode;
            }
            return found;
        }
    }

    String WriteGraph(const Graph& graph, const Domain* domain)
    {
        YamlWriter writer;
        writer.WriteString("Domain", graph.GetDomain().c_str());
        writer.WriteInt("NextId", static_cast<std::int64_t>(graph.GetNextId().Get()));

        writer.BeginSequence("Nodes");
        for (const Node& node : graph.GetNodes())
        {
            const NodeTypeInfo* type = domain == nullptr ? nullptr : domain->FindNodeType(node.type.View());
            writer.BeginMap(nullptr);
            writer.WriteInt("Id", static_cast<std::int64_t>(node.id.Get()));
            writer.WriteString("Type", node.type.c_str());
            writer.WriteFloat("X", node.position.x);
            writer.WriteFloat("Y", node.position.y);
            if (!node.inputs.IsEmpty())
            {
                writer.BeginSequence("Inputs");
                for (std::size_t pin = 0; pin < node.inputs.Size(); ++pin)
                {
                    writer.BeginMap(nullptr);
                    if (type != nullptr && pin < type->inputs.Size())
                    {
                        writer.WriteString("Pin", type->inputs[pin].name.c_str());
                    }
                    else
                    {
                        // 모르는 종류다 - 이름을 모르니 차례로 적고, 읽을 때도 차례로 받는다.
                        writer.WriteInt("Index", static_cast<std::int64_t>(pin));
                    }
                    writer.BeginSequence("Value");
                    const std::size_t components = ComponentsOf(domain, type, pin);
                    for (std::size_t component = 0; component < components; ++component)
                    {
                        writer.WriteFloatItem(node.inputs[pin].values[component]);
                    }
                    writer.EndSequence();
                    writer.EndMap();
                }
                writer.EndSequence();
            }
            writer.EndMap();
        }
        writer.EndSequence();

        writer.BeginSequence("Links");
        for (const Link& link : graph.GetLinks())
        {
            const Node* from = graph.FindNode(link.from.node);
            const Node* to = graph.FindNode(link.to.node);
            const NodeTypeInfo* fromType = (domain == nullptr || from == nullptr) ? nullptr : domain->FindNodeType(from->type.View());
            const NodeTypeInfo* toType = (domain == nullptr || to == nullptr) ? nullptr : domain->FindNodeType(to->type.View());
            if (fromType == nullptr || toType == nullptr
                || link.from.pin >= fromType->outputs.Size() || link.to.pin >= toType->inputs.Size())
            {
                // 그래프의 링크는 `Connect` 만 만들므로 여기에 오지 않는다.
                continue;
            }
            writer.BeginMap(nullptr);
            writer.WriteInt("Id", static_cast<std::int64_t>(link.id.Get()));
            writer.WriteInt("FromNode", static_cast<std::int64_t>(link.from.node.Get()));
            writer.WriteString("FromPin", fromType->outputs[link.from.pin].name.c_str());
            writer.WriteInt("ToNode", static_cast<std::int64_t>(link.to.node.Get()));
            writer.WriteString("ToPin", toType->inputs[link.to.pin].name.c_str());
            writer.EndMap();
        }
        writer.EndSequence();
        return writer.GetText();
    }

    Bool ReadGraphDomain(std::string_view text, String& domain)
    {
        YamlDocument document;
        YamlError error;
        if (!document.Parse(text.data(), text.size(), error))
        {
            return false;
        }
        return document.FindScalar(document.GetRoot(), "Domain", domain) && !domain.empty();
    }

    Bool ReadGraph(std::string_view text, const Domain* domain, Graph& graph, Array<String>& problems)
    {
        graph.Clear();
        YamlDocument document;
        YamlError error;
        if (!document.Parse(text.data(), text.size(), error))
        {
            problems.Add(String("line ") + std::to_string(error.line) + ": " + error.message);
            return false;
        }
        const UInt32 root = document.GetRoot();
        if (root.Get() == YamlDocument::InvalidNode.Get() || document.GetKind(root) != YamlKind::Map)
        {
            problems.Add(String("the graph is not a map"));
            return false;
        }

        String domainName;
        document.FindScalar(root, "Domain", domainName);
        graph.SetDomain(domainName);
        if (domain != nullptr && domainName.View() != domain->GetName().View())
        {
            problems.Add(String("the graph names domain '") + domainName + "' but was read with '" + domain->GetName() + "'");
        }

        const UInt32 nodes = FindSequence(document, root, "Nodes");
        if (nodes.Get() != YamlDocument::InvalidNode.Get())
        {
            for (std::size_t index = 0; index < document.GetCount(nodes); ++index)
            {
                const UInt32 entry = document.GetElement(nodes, index);
                Node node;
                if (!FindId(document, entry, "Id", node.id))
                {
                    problems.Add(String("a node has no id"));
                    continue;
                }
                document.FindScalar(entry, "Type", node.type);
                Float x = 0.0f;
                Float y = 0.0f;
                document.FindFloat(entry, "X", x);
                document.FindFloat(entry, "Y", y);
                node.position = Vector2{x, y};

                const NodeTypeInfo* type = domain == nullptr ? nullptr : domain->FindNodeType(node.type.View());
                if (type != nullptr)
                {
                    for (const PinInfo& pin : type->inputs)
                    {
                        node.inputs.Add(pin.defaultValue);
                    }
                }
                else
                {
                    problems.Add(String("node ") + std::to_string(node.id.Get()) + " has unknown type '" + node.type + "'");
                }

                const UInt32 inputs = FindSequence(document, entry, "Inputs");
                if (inputs.Get() != YamlDocument::InvalidNode.Get())
                {
                    for (std::size_t inputIndex = 0; inputIndex < document.GetCount(inputs); ++inputIndex)
                    {
                        const UInt32 input = document.GetElement(inputs, inputIndex);
                        std::size_t pin = InvalidPinIndex;
                        String pinName;
                        Int64 pinIndex = -1;
                        if (type != nullptr && document.FindScalar(input, "Pin", pinName))
                        {
                            pin = Domain::FindInput(*type, pinName.View());
                        }
                        else if (type == nullptr && document.FindInt(input, "Index", pinIndex) && pinIndex.Get() >= 0)
                        {
                            pin = static_cast<std::size_t>(pinIndex.Get());
                            if (node.inputs.Size() <= pin)
                            {
                                node.inputs.Resize(pin + 1);
                            }
                        }
                        if (pin == InvalidPinIndex || pin >= node.inputs.Size())
                        {
                            problems.Add(String("node ") + std::to_string(node.id.Get()) + " has unknown input '" + pinName + "'");
                            continue;
                        }
                        const UInt32 value = FindSequence(document, input, "Value");
                        if (value.Get() == YamlDocument::InvalidNode.Get())
                        {
                            continue;
                        }
                        for (std::size_t component = 0; component < document.GetCount(value) && component < 4; ++component)
                        {
                            Float parsed = 0.0f;
                            if (ParseFloat(document.GetText(document.GetElement(value, component)), parsed))
                            {
                                node.inputs[pin].values[component] = parsed;
                            }
                        }
                    }
                }

                const NodeId id = node.id;
                if (!graph.InsertNode(static_cast<Node&&>(node)))
                {
                    problems.Add(String("node id ") + std::to_string(id.Get()) + " is used twice");
                }
            }
        }

        const UInt32 links = FindSequence(document, root, "Links");
        if (links.Get() != YamlDocument::InvalidNode.Get())
        {
            for (std::size_t index = 0; index < document.GetCount(links); ++index)
            {
                const UInt32 entry = document.GetElement(links, index);
                UInt64 id;
                UInt64 fromNode;
                UInt64 toNode;
                String fromPin;
                String toPin;
                if (!FindId(document, entry, "Id", id) || !FindId(document, entry, "FromNode", fromNode)
                    || !FindId(document, entry, "ToNode", toNode)
                    || !document.FindScalar(entry, "FromPin", fromPin) || !document.FindScalar(entry, "ToPin", toPin))
                {
                    problems.Add(String("a link is missing a field"));
                    continue;
                }
                const Node* from = graph.FindNode(fromNode);
                const Node* to = graph.FindNode(toNode);
                const NodeTypeInfo* fromType = (domain == nullptr || from == nullptr) ? nullptr : domain->FindNodeType(from->type.View());
                const NodeTypeInfo* toType = (domain == nullptr || to == nullptr) ? nullptr : domain->FindNodeType(to->type.View());
                const std::size_t fromIndex = fromType == nullptr ? InvalidPinIndex : Domain::FindOutput(*fromType, fromPin.View());
                const std::size_t toIndex = toType == nullptr ? InvalidPinIndex : Domain::FindInput(*toType, toPin.View());
                if (fromIndex == InvalidPinIndex || toIndex == InvalidPinIndex)
                {
                    problems.Add(String("link ") + std::to_string(id.Get()) + " points at a missing pin");
                    continue;
                }
                const ConnectResult result = graph.Connect(*domain, PinRef{fromNode, fromIndex}, PinRef{toNode, toIndex}, id);
                if (result != ConnectResult::Connected)
                {
                    problems.Add(String("link ") + std::to_string(id.Get()) + " was rejected");
                }
            }
        }

        Int64 nextId = 0;
        document.FindInt(root, "NextId", nextId);
        graph.SetNextId(UInt64(nextId.Get() > 0 ? static_cast<std::uint64_t>(nextId.Get()) : 1));
        return true;
    }
}
