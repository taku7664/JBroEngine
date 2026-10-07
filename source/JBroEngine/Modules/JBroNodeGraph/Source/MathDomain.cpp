#include <JBro/NodeGraph/MathDomain.h>

#include <cmath>
#include <initializer_list>

namespace JBro::NodeGraph
{
    namespace
    {
        enum MathPin : std::uint32_t
        {
            PinFloat = 0,
            PinVector2 = 1,
            PinVector3 = 2,
            PinVector4 = 3,
        };

        PinInfo MakePin(const char* name, std::uint32_t type, Float x = 0.0f, Float y = 0.0f, Float z = 0.0f, Float w = 0.0f)
        {
            PinInfo pin;
            pin.name = name;
            pin.type = PinType(type);
            pin.defaultValue.values[0] = x;
            pin.defaultValue.values[1] = y;
            pin.defaultValue.values[2] = z;
            pin.defaultValue.values[3] = w;
            return pin;
        }

        NodeTypeInfo MakeNode(const char* name, const char* category, std::initializer_list<PinInfo> inputs, std::initializer_list<PinInfo> outputs)
        {
            NodeTypeInfo node;
            node.name = name;
            node.category = category;
            for (const PinInfo& pin : inputs)
            {
                node.inputs.Add(pin);
            }
            for (const PinInfo& pin : outputs)
            {
                node.outputs.Add(pin);
            }
            return node;
        }

        // 링크로 온 값을 받는 쪽 타입에 맞춘다. `Float` 는 모든 칸에 퍼뜨리고 벡터는 그대로 둔다(남는 칸은 0 이다).
        PinValue Widen(const PinValue& value, PinType from)
        {
            if (from.Get() != PinFloat)
            {
                return value;
            }
            PinValue widened;
            for (Float& component : widened.values)
            {
                component = value.values[0];
            }
            return widened;
        }

        class Evaluator
        {
        public:
            Evaluator(const Graph& graph, const Domain& domain)
                : m_graph(graph), m_domain(domain)
            {
            }

            Bool Output(NodeId id, std::size_t output, PinValue& result, std::size_t depth = 0)
            {
                // 그래프는 DAG 라 깊이가 노드 수를 넘지 않는다. 넘으면 모델이 깨진 것이다.
                if (depth > m_graph.GetNodes().Size())
                {
                    return false;
                }
                const Node* node = m_graph.FindNode(id);
                const NodeTypeInfo* type = node == nullptr ? nullptr : m_domain.FindNodeType(node->type.View());
                if (type == nullptr || output >= type->outputs.Size())
                {
                    return false;
                }
                PinValue inputs[4];
                for (std::size_t pin = 0; pin < type->inputs.Size() && pin < 4; ++pin)
                {
                    if (!Input(*node, pin, inputs[pin], depth))
                    {
                        return false;
                    }
                }

                const std::string_view name = type->name.View();
                result = PinValue{};
                if (name == "Float")
                {
                    result.values[0] = inputs[0].values[0];
                }
                else if (name == "Vector2" || name == "Vector3" || name == "Color")
                {
                    for (std::size_t component = 0; component < type->inputs.Size(); ++component)
                    {
                        result.values[component] = inputs[component].values[0];
                    }
                }
                else if (name == "Lerp")
                {
                    const float t = inputs[2].values[0].Get();
                    for (std::size_t component = 0; component < 4; ++component)
                    {
                        const float a = inputs[0].values[component].Get();
                        const float b = inputs[1].values[component].Get();
                        result.values[component] = a + (b - a) * t;
                    }
                }
                else
                {
                    for (std::size_t component = 0; component < 4; ++component)
                    {
                        const float a = inputs[0].values[component].Get();
                        const float b = inputs[1].values[component].Get();
                        float value = 0.0f;
                        if (name == "Add")
                        {
                            value = a + b;
                        }
                        else if (name == "Subtract")
                        {
                            value = a - b;
                        }
                        else if (name == "Multiply")
                        {
                            value = a * b;
                        }
                        else if (name == "Divide")
                        {
                            value = b == 0.0f ? 0.0f : a / b;
                        }
                        else if (name == "Sin")
                        {
                            value = std::sin(a);
                        }
                        result.values[component] = value;
                    }
                }
                return true;
            }

            Bool Input(const Node& node, std::size_t pin, PinValue& value, std::size_t depth)
            {
                const Link* link = m_graph.FindLinkInto(PinRef{node.id, pin});
                if (link == nullptr)
                {
                    value = pin < node.inputs.Size() ? node.inputs[pin] : PinValue{};
                    return true;
                }
                PinValue source;
                if (!Output(link->from.node, link->from.pin, source, depth + 1))
                {
                    return false;
                }
                const Node* from = m_graph.FindNode(link->from.node);
                const NodeTypeInfo* fromType = m_domain.FindNodeType(from->type.View());
                value = Widen(source, fromType->outputs[link->from.pin].type);
                return true;
            }

        private:
            const Graph& m_graph;
            const Domain& m_domain;
        };
    }

    OwnerPtr<Domain> MakeMathDomain()
    {
        OwnerPtr<Domain> domain = MakeOwnerPtr<Domain>(String(MathDomainName));
        domain->AddPinType(PinTypeInfo{String("Float"), Color{0.55f, 0.85f, 0.35f, 1.0f}, UInt32(1u)});
        domain->AddPinType(PinTypeInfo{String("Vector2"), Color{0.35f, 0.80f, 0.85f, 1.0f}, UInt32(2u)});
        domain->AddPinType(PinTypeInfo{String("Vector3"), Color{0.95f, 0.80f, 0.30f, 1.0f}, UInt32(3u)});
        domain->AddPinType(PinTypeInfo{String("Vector4"), Color{0.85f, 0.45f, 0.85f, 1.0f}, UInt32(4u)});
        for (std::uint32_t from = PinFloat; from <= PinVector4; ++from)
        {
            for (std::uint32_t to = from + 1; to <= PinVector4; ++to)
            {
                domain->AddConversion(PinType(from), PinType(to));
            }
        }

        domain->AddNodeType(MakeNode("Float", "Constant", {MakePin("Value", PinFloat)}, {MakePin("Result", PinFloat)}));
        domain->AddNodeType(MakeNode("Vector2", "Constant", {MakePin("X", PinFloat), MakePin("Y", PinFloat)}, {MakePin("Result", PinVector2)}));
        domain->AddNodeType(MakeNode("Vector3", "Constant",
            {MakePin("X", PinFloat), MakePin("Y", PinFloat), MakePin("Z", PinFloat)}, {MakePin("Result", PinVector3)}));
        domain->AddNodeType(MakeNode("Color", "Constant",
            {MakePin("R", PinFloat, 1.0f), MakePin("G", PinFloat, 1.0f), MakePin("B", PinFloat, 1.0f), MakePin("A", PinFloat, 1.0f)},
            {MakePin("Result", PinVector4)}));
        for (const char* name : {"Add", "Subtract", "Multiply", "Divide"})
        {
            const Float identity = (std::string_view(name) == "Multiply" || std::string_view(name) == "Divide") ? 1.0f : 0.0f;
            domain->AddNodeType(MakeNode(name, "Math",
                {MakePin("A", PinVector4), MakePin("B", PinVector4, identity, identity, identity, identity)}, {MakePin("Result", PinVector4)}));
        }
        domain->AddNodeType(MakeNode("Sin", "Math", {MakePin("A", PinVector4)}, {MakePin("Result", PinVector4)}));
        domain->AddNodeType(MakeNode("Lerp", "Math",
            {MakePin("A", PinVector4), MakePin("B", PinVector4, 1.0f, 1.0f, 1.0f, 1.0f), MakePin("T", PinFloat, 0.5f)}, {MakePin("Result", PinVector4)}));

        NodeTypeInfo output = MakeNode("Output", "", {MakePin("Color", PinVector4, 0.0f, 0.0f, 0.0f, 1.0f)}, {});
        output.unique = true;
        domain->AddNodeType(static_cast<NodeTypeInfo&&>(output));
        return domain;
    }

    Bool EvaluateMathOutput(const Graph& graph, const Domain& domain, NodeId node, std::size_t output, PinValue& result)
    {
        Evaluator evaluator(graph, domain);
        return evaluator.Output(node, output, result);
    }

    Bool EvaluateMath(const Graph& graph, const Domain& domain, PinValue& result)
    {
        const Node* output = nullptr;
        for (const Node& node : graph.GetNodes())
        {
            if (node.type.View() == "Output")
            {
                output = &node;
                break;
            }
        }
        if (output == nullptr)
        {
            return false;
        }
        Evaluator evaluator(graph, domain);
        return evaluator.Input(*output, 0, result, 0);
    }
}
