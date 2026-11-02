#include <JBro/NodeGraph/ShaderDomain.h>

#include <cstdio>
#include <initializer_list>
#include <string_view>

namespace JBro::NodeGraph
{
    namespace
    {
        enum ShaderPin : std::uint32_t
        {
            PinFloat = 0,
            PinVector2 = 1,
            PinVector3 = 2,
            PinVector4 = 3,
        };

        // 텍스처 자리의 상한이다(t1..t6, `MaxMaterialTextures`). 이 모듈은 에셋 모듈을 모른다.
        constexpr std::size_t MaxTextures = 6;

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

        const char* TypeName(std::uint32_t type)
        {
            switch (type)
            {
            case PinFloat:
                return "float";
            case PinVector2:
                return "float2";
            case PinVector3:
                return "float3";
            default:
                return "float4";
            }
        }

        // `value`(타입 `from`)를 `to` 로 옮기는 식이다.
        String Convert(const String& value, std::uint32_t from, std::uint32_t to)
        {
            String out;
            if (from == to)
            {
                return value;
            }
            if (from == PinFloat)
            {
                out = "(";
                out.append(value.c_str());
                out.append(to == PinVector2 ? ").xx" : to == PinVector3 ? ").xxx" : ").xxxx");
                return out;
            }
            if (to < from)
            {
                out = "(";
                out.append(value.c_str());
                out.append(to == PinFloat ? ").x" : to == PinVector2 ? ").xy" : ").xyz");
                return out;
            }
            out = TypeName(to);
            out.append("(");
            out.append(value.c_str());
            out.append(to == PinVector4 && from == PinVector2 ? ", 0.0f, 0.0f)" : ", 0.0f)");
            return out;
        }

        String Literal(const PinValue& value, std::uint32_t type)
        {
            char text[160] = {};
            switch (type)
            {
            case PinFloat:
                std::snprintf(text, sizeof(text), "%.6ff", static_cast<double>(value.values[0].Get()));
                break;
            case PinVector2:
                std::snprintf(text, sizeof(text), "float2(%.6ff, %.6ff)", static_cast<double>(value.values[0].Get()), static_cast<double>(value.values[1].Get()));
                break;
            case PinVector3:
                std::snprintf(text, sizeof(text), "float3(%.6ff, %.6ff, %.6ff)", static_cast<double>(value.values[0].Get()), static_cast<double>(value.values[1].Get()),
                    static_cast<double>(value.values[2].Get()));
                break;
            default:
                std::snprintf(text, sizeof(text), "float4(%.6ff, %.6ff, %.6ff, %.6ff)", static_cast<double>(value.values[0].Get()), static_cast<double>(value.values[1].Get()),
                    static_cast<double>(value.values[2].Get()), static_cast<double>(value.values[3].Get()));
                break;
            }
            return String(text);
        }

        String LocalName(NodeId id, std::size_t output)
        {
            char text[64] = {};
            std::snprintf(text, sizeof(text), "n%llu_%zu", static_cast<unsigned long long>(id.Get()), output);
            return String(text);
        }

        // 라벨을 식별자로 다듬는다. 영문자·숫자·밑줄만 남기고, 비었거나 숫자로 시작하면 노드 번호로 시작한다.
        String ParameterName(const Node& node)
        {
            String name;
            for (const char c : node.label.View())
            {
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')
                {
                    name.push_back(c);
                }
            }
            if (name.empty() || (name[0] >= '0' && name[0] <= '9'))
            {
                char prefix[32] = {};
                std::snprintf(prefix, sizeof(prefix), "param%llu", static_cast<unsigned long long>(node.id.Get()));
                String prefixed(prefix);
                prefixed.append(name.c_str());
                return prefixed;
            }
            return name;
        }

        Bool EndsWithColor(const String& name)
        {
            const std::string_view view = name.View();
            return view.size() >= 5 && (view.substr(view.size() - 5) == "Color" || view.substr(view.size() - 5) == "color");
        }

        // 자리마다 다른 입력을 같은 이름으로 쓰게 하는 머리다. 자리에 없는 입력은 정해진 값이다.
        constexpr const char* DomainHeader = R"hlsl(#if defined(JBRO_MATERIAL_SPRITE)
#define JBRO_GRAPH_INPUT JBroSpriteInput
float2 JBroGraphUV(JBroSpriteInput input) { return input.uv; }
float4 JBroGraphTint(JBroSpriteInput input) { return input.tint; }
float4 JBroGraphSource(JBroSpriteInput input) { return JBroSampleSprite(input); }
float3 JBroGraphNormal(JBroSpriteInput input) { return float3(0.0f, 0.0f, 1.0f); }
float3 JBroGraphWorldPosition(JBroSpriteInput input) { return float3(0.0f, 0.0f, 0.0f); }
float3 JBroGraphLight(JBroSpriteInput input) { return float3(1.0f, 1.0f, 1.0f); }
#elif defined(JBRO_MATERIAL_MESH)
#define JBRO_GRAPH_INPUT JBroMeshInput
float2 JBroGraphUV(JBroMeshInput input) { return input.uv; }
float4 JBroGraphTint(JBroMeshInput input) { return input.tint; }
float4 JBroGraphSource(JBroMeshInput input) { return float4(1.0f, 1.0f, 1.0f, 1.0f); }
float3 JBroGraphNormal(JBroMeshInput input) { return normalize(input.normal); }
float3 JBroGraphWorldPosition(JBroMeshInput input) { return input.worldPosition; }
float3 JBroGraphLight(JBroMeshInput input) { return JBroMeshLight(input.normal, input.worldPosition); }
#else
#define JBRO_GRAPH_INPUT JBroPostProcessInput
float2 JBroGraphUV(JBroPostProcessInput input) { return JBroUV(input); }
float4 JBroGraphTint(JBroPostProcessInput input) { return float4(1.0f, 1.0f, 1.0f, 1.0f); }
float4 JBroGraphSource(JBroPostProcessInput input) { return JBroLoadSource(input); }
float3 JBroGraphNormal(JBroPostProcessInput input) { return float3(0.0f, 0.0f, 1.0f); }
float3 JBroGraphWorldPosition(JBroPostProcessInput input) { return float3(0.0f, 0.0f, 0.0f); }
float3 JBroGraphLight(JBroPostProcessInput input) { return float3(1.0f, 1.0f, 1.0f); }
#endif
)hlsl";
    }

    OwnerPtr<Domain> MakeShaderDomain()
    {
        OwnerPtr<Domain> domain = MakeOwnerPtr<Domain>(String(ShaderDomainName));
        domain->AddPinType({String("Float"), Color{0.62f, 0.78f, 0.95f, 1.0f}, 1});
        domain->AddPinType({String("Vector2"), Color{0.55f, 0.9f, 0.55f, 1.0f}, 2});
        domain->AddPinType({String("Vector3"), Color{0.95f, 0.8f, 0.4f, 1.0f}, 3});
        domain->AddPinType({String("Vector4"), Color{0.95f, 0.55f, 0.75f, 1.0f}, 4});
        // 모두 서로 잇는다 - HLSL 처럼 넓히고 좁힌다(`Convert`).
        for (std::uint32_t from = 0; from < 4; ++from)
        {
            for (std::uint32_t to = 0; to < 4; ++to)
            {
                if (from != to)
                {
                    domain->AddConversion(PinType(from), PinType(to));
                }
            }
        }
        const PinInfo a = MakePin("A", PinVector4);
        const PinInfo b = MakePin("B", PinVector4);
        const PinInfo vectorOut = MakePin("Out", PinVector4);
        domain->AddNodeType(MakeNode("UV", "Input", {}, {MakePin("UV", PinVector2)}));
        domain->AddNodeType(MakeNode("Tint", "Input", {}, {MakePin("Color", PinVector4)}));
        domain->AddNodeType(MakeNode("Source", "Input", {}, {MakePin("Color", PinVector4)}));
        domain->AddNodeType(MakeNode("Time", "Input", {}, {MakePin("Seconds", PinFloat)}));
        domain->AddNodeType(MakeNode("Normal", "Input", {}, {MakePin("Normal", PinVector3)}));
        domain->AddNodeType(MakeNode("WorldPosition", "Input", {}, {MakePin("Position", PinVector3)}));
        domain->AddNodeType(MakeNode("Light", "Input", {}, {MakePin("Light", PinVector3)}));
        domain->AddNodeType(MakeNode("Float", "Constant", {MakePin("Value", PinFloat)}, {MakePin("Out", PinFloat)}));
        domain->AddNodeType(MakeNode("Vector2", "Constant", {MakePin("Value", PinVector2)}, {MakePin("Out", PinVector2)}));
        domain->AddNodeType(MakeNode("Vector3", "Constant", {MakePin("Value", PinVector3)}, {MakePin("Out", PinVector3)}));
        domain->AddNodeType(MakeNode("Color", "Constant", {MakePin("Value", PinVector4, 1.0f, 1.0f, 1.0f, 1.0f)}, {MakePin("Out", PinVector4)}));
        NodeTypeInfo floatParameter = MakeNode("FloatParameter", "Parameter", {MakePin("Default", PinFloat, 1.0f)}, {MakePin("Out", PinFloat)});
        floatParameter.hasLabel = true;
        domain->AddNodeType(static_cast<NodeTypeInfo&&>(floatParameter));
        NodeTypeInfo colorParameter = MakeNode("ColorParameter", "Parameter", {MakePin("Default", PinVector4, 1.0f, 1.0f, 1.0f, 1.0f)}, {MakePin("Out", PinVector4)});
        colorParameter.hasLabel = true;
        domain->AddNodeType(static_cast<NodeTypeInfo&&>(colorParameter));
        NodeTypeInfo textureParameter = MakeNode("TextureParameter", "Parameter", {MakePin("UV", PinVector2)}, {MakePin("Color", PinVector4)});
        textureParameter.hasLabel = true;
        domain->AddNodeType(static_cast<NodeTypeInfo&&>(textureParameter));
        domain->AddNodeType(MakeNode("Add", "Math", {a, b}, {vectorOut}));
        domain->AddNodeType(MakeNode("Subtract", "Math", {a, b}, {vectorOut}));
        domain->AddNodeType(MakeNode("Multiply", "Math", {MakePin("A", PinVector4, 1.0f, 1.0f, 1.0f, 1.0f), MakePin("B", PinVector4, 1.0f, 1.0f, 1.0f, 1.0f)}, {vectorOut}));
        domain->AddNodeType(MakeNode("Divide", "Math", {a, MakePin("B", PinVector4, 1.0f, 1.0f, 1.0f, 1.0f)}, {vectorOut}));
        domain->AddNodeType(MakeNode("Lerp", "Math", {a, b, MakePin("T", PinFloat, 0.5f)}, {vectorOut}));
        domain->AddNodeType(MakeNode("Power", "Math", {a, MakePin("Exponent", PinFloat, 1.0f)}, {vectorOut}));
        domain->AddNodeType(MakeNode("Sin", "Math", {a}, {vectorOut}));
        domain->AddNodeType(MakeNode("Cos", "Math", {a}, {vectorOut}));
        domain->AddNodeType(MakeNode("Saturate", "Math", {a}, {vectorOut}));
        domain->AddNodeType(MakeNode("Dot", "Math", {MakePin("A", PinVector3), MakePin("B", PinVector3)}, {MakePin("Out", PinFloat)}));
        domain->AddNodeType(MakeNode("Combine", "Vector", {MakePin("X", PinFloat), MakePin("Y", PinFloat), MakePin("Z", PinFloat), MakePin("W", PinFloat, 1.0f)}, {vectorOut}));
        domain->AddNodeType(MakeNode("Split", "Vector", {a}, {MakePin("X", PinFloat), MakePin("Y", PinFloat), MakePin("Z", PinFloat), MakePin("W", PinFloat)}));
        NodeTypeInfo output = MakeNode("Output", "", {MakePin("Color", PinVector4, 1.0f, 1.0f, 1.0f, 1.0f)}, {});
        output.unique = true;
        domain->AddNodeType(static_cast<NodeTypeInfo&&>(output));
        return domain;
    }

    Bool GenerateShaderHlsl(const Graph& graph, const Domain& domain, String& hlsl, String& error)
    {
        hlsl.clear();
        error.clear();
        const Node* output = nullptr;
        for (const Node& node : graph.GetNodes())
        {
            if (node.type.View() == "Output")
            {
                output = &node;
            }
        }
        if (output == nullptr)
        {
            error = "the shader graph has no Output node";
            return false;
        }
        // 입력을 먼저 셈할 차례에서 결과에 닿는 노드만 쓴다.
        Array<NodeId> order;
        graph.TopologicalOrder(order);
        Array<NodeId> reached;
        reached.Add(output->id);
        for (std::size_t scan = 0; scan < reached.Size(); ++scan)
        {
            const NodeId id = reached[scan];
            for (const Link& link : graph.GetLinks())
            {
                if (link.to.node.Get() != id.Get())
                {
                    continue;
                }
                Bool known = false;
                for (const NodeId other : reached)
                {
                    known = known || other.Get() == link.from.node.Get();
                }
                if (false == known)
                {
                    reached.Add(link.from.node);
                }
            }
        }
        const auto isReached = [&](NodeId id) {
            for (const NodeId other : reached)
            {
                if (other.Get() == id.Get())
                {
                    return true;
                }
            }
            return false;
        };

        String cbuffer;
        String textures;
        String body;
        Array<String> names;
        const auto addName = [&](const String& name) {
            for (const String& used : names)
            {
                if (used == name)
                {
                    error = "two parameters are named ";
                    error.append(name.c_str());
                    return false;
                }
            }
            names.Add(name);
            return true;
        };
        std::size_t textureCount = 0;
        for (const NodeId id : order)
        {
            if (false == isReached(id))
            {
                continue;
            }
            const Node* node = graph.FindNode(id);
            const NodeTypeInfo* type = node == nullptr ? nullptr : domain.FindNodeType(node->type.View());
            if (type == nullptr)
            {
                error = "the shader graph has an unknown node type";
                if (node != nullptr)
                {
                    error.append(": ");
                    error.append(node->type.c_str());
                }
                return false;
            }
            // 입력마다 식을 만든다: 이어졌으면 앞 노드의 지역 변수를 옮기고, 아니면 칸의 값이다.
            Array<String> inputs;
            for (std::size_t pin = 0; pin < type->inputs.Size(); ++pin)
            {
                const std::uint32_t wanted = type->inputs[pin].type.Get();
                const Link* link = graph.FindLinkInto(PinRef{id, pin});
                if (link != nullptr)
                {
                    const Node* from = graph.FindNode(link->from.node);
                    const NodeTypeInfo* fromType = from == nullptr ? nullptr : domain.FindNodeType(from->type.View());
                    if (fromType == nullptr || link->from.pin >= fromType->outputs.Size())
                    {
                        error = "a link starts at a pin that does not exist";
                        return false;
                    }
                    inputs.Add(Convert(LocalName(link->from.node, link->from.pin), fromType->outputs[link->from.pin].type.Get(), wanted));
                }
                else
                {
                    inputs.Add(Literal(pin < node->inputs.Size() ? node->inputs[pin] : type->inputs[pin].defaultValue, wanted));
                }
            }
            const std::string_view name = type->name.View();
            const auto emit = [&](std::size_t out, const String& expression) {
                body.append("    const ");
                body.append(TypeName(type->outputs[out].type.Get()));
                body.append(" ");
                body.append(LocalName(id, out).c_str());
                body.append(" = ");
                body.append(expression.c_str());
                body.append(";\n");
            };
            const auto call = [&](const char* function, std::size_t count) {
                String text(function);
                text.append("(");
                for (std::size_t at = 0; at < count; ++at)
                {
                    if (at != 0)
                    {
                        text.append(", ");
                    }
                    text.append(inputs[at].c_str());
                }
                text.append(")");
                return text;
            };
            const auto binary = [&](const char* op) {
                String text("(");
                text.append(inputs[0].c_str());
                text.append(op);
                text.append(inputs[1].c_str());
                text.append(")");
                return text;
            };
            if (name == "Output")
            {
                body.append("    return ");
                body.append(inputs[0].c_str());
                body.append(";\n");
            }
            else if (name == "UV")
            {
                emit(0, String("JBroGraphUV(input)"));
            }
            else if (name == "Tint")
            {
                emit(0, String("JBroGraphTint(input)"));
            }
            else if (name == "Source")
            {
                emit(0, String("JBroGraphSource(input)"));
            }
            else if (name == "Time")
            {
                emit(0, String("JBroSeconds()"));
            }
            else if (name == "Normal")
            {
                emit(0, String("JBroGraphNormal(input)"));
            }
            else if (name == "WorldPosition")
            {
                emit(0, String("JBroGraphWorldPosition(input)"));
            }
            else if (name == "Light")
            {
                emit(0, String("JBroGraphLight(input)"));
            }
            else if (name == "Float" || name == "Vector2" || name == "Vector3" || name == "Color")
            {
                emit(0, inputs[0]);
            }
            else if (name == "FloatParameter")
            {
                const String variable = ParameterName(*node);
                if (false == addName(variable))
                {
                    return false;
                }
                cbuffer.append("    float ");
                cbuffer.append(variable.c_str());
                cbuffer.append(" = ");
                cbuffer.append(Literal(node->inputs.Size() != 0 ? node->inputs[0] : PinValue{}, PinFloat).c_str());
                cbuffer.append(";\n");
                emit(0, variable);
            }
            else if (name == "ColorParameter")
            {
                // 이름이 Color 로 끝나야 인스펙터가 색 칸으로 보인다.
                String variable = ParameterName(*node);
                if (false == EndsWithColor(variable))
                {
                    variable.append("Color");
                }
                if (false == addName(variable))
                {
                    return false;
                }
                cbuffer.append("    float4 ");
                cbuffer.append(variable.c_str());
                cbuffer.append(" = ");
                cbuffer.append(Literal(node->inputs.Size() != 0 ? node->inputs[0] : PinValue{}, PinVector4).c_str());
                cbuffer.append(";\n");
                emit(0, variable);
            }
            else if (name == "TextureParameter")
            {
                const String variable = ParameterName(*node);
                if (false == addName(variable))
                {
                    return false;
                }
                ++textureCount;
                if (textureCount > MaxTextures)
                {
                    error = "a shader graph takes six textures at most";
                    return false;
                }
                char declaration[160] = {};
                std::snprintf(declaration, sizeof(declaration), "Texture2D %s : register(t%zu);\n", variable.c_str(), textureCount);
                textures.append(declaration);
                String sample(variable);
                sample.append(".Sample(JBroLinearRepeat, ");
                sample.append(inputs[0].c_str());
                sample.append(")");
                emit(0, sample);
            }
            else if (name == "Add")
            {
                emit(0, binary(" + "));
            }
            else if (name == "Subtract")
            {
                emit(0, binary(" - "));
            }
            else if (name == "Multiply")
            {
                emit(0, binary(" * "));
            }
            else if (name == "Divide")
            {
                emit(0, binary(" / "));
            }
            else if (name == "Lerp")
            {
                emit(0, call("lerp", 3));
            }
            else if (name == "Power")
            {
                emit(0, call("pow", 2));
            }
            else if (name == "Sin")
            {
                emit(0, call("sin", 1));
            }
            else if (name == "Cos")
            {
                emit(0, call("cos", 1));
            }
            else if (name == "Saturate")
            {
                emit(0, call("saturate", 1));
            }
            else if (name == "Dot")
            {
                emit(0, call("dot", 2));
            }
            else if (name == "Combine")
            {
                emit(0, call("float4", 4));
            }
            else if (name == "Split")
            {
                static const char* const Suffixes[4] = {".x", ".y", ".z", ".w"};
                for (std::size_t out = 0; out < 4; ++out)
                {
                    String component("(");
                    component.append(inputs[0].c_str());
                    component.append(")");
                    component.append(Suffixes[out]);
                    emit(out, component);
                }
            }
            else
            {
                error = "the shader graph has a node this generator does not know: ";
                error.append(node->type.c_str());
                return false;
            }
        }
        hlsl = "// Generated from a shader graph. Edit the graph, not this file: saving the graph writes it again.\n#include \"JBro/Material.hlsli\"\n\n";
        if (false == cbuffer.empty())
        {
            hlsl.append("cbuffer Material : register(b1)\n{\n");
            hlsl.append(cbuffer.c_str());
            hlsl.append("};\n\n");
        }
        if (false == textures.empty())
        {
            hlsl.append(textures.c_str());
            hlsl.append("\n");
        }
        hlsl.append(DomainHeader);
        hlsl.append("\nfloat4 PSMain(JBRO_GRAPH_INPUT input) : SV_TARGET\n{\n");
        hlsl.append(body.c_str());
        hlsl.append("}\n");
        return true;
    }
}
