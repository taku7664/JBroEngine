#include <JBro/NodeGraph/Domain.h>
#include <JBro/NodeGraph/Graph.h>
#include <JBro/NodeGraph/GraphYaml.h>
#include <JBro/NodeGraph/MathDomain.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

// 노드 그래프 모델의 시험(D-323, node-graph-plan §3 의 1 단계). 에디터 없이 잰다.
namespace
{
    using namespace JBro::NodeGraph;
    using JBro::Array;
    using JBro::String;
    using JBro::Vector2;

    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::Bool Near(float value, float expected)
    {
        return std::fabs(value - expected) < 0.0001f;
    }

    void TestConnectRules()
    {
        const JBro::OwnerPtr<Domain> domain = MakeMathDomain();
        Graph graph;
        graph.SetDomain(String(MathDomainName));
        const NodeId a = graph.AddNode(*domain, "Float", Vector2{0.0f, 0.0f});
        const NodeId b = graph.AddNode(*domain, "Float", Vector2{0.0f, 100.0f});
        const NodeId add = graph.AddNode(*domain, "Add", Vector2{200.0f, 0.0f});
        const NodeId v2 = graph.AddNode(*domain, "Vector2", Vector2{0.0f, 200.0f});
        Check(a.Get() != InvalidId && b.Get() != InvalidId && add.Get() != InvalidId && a.Get() != b.Get(), "nodes get distinct ids");
        Check(graph.AddNode(*domain, "Nope", Vector2{}).Get() == InvalidId, "an unknown node type is refused");
        Check(graph.FindNode(add)->inputs.Size() == 2 && Near(graph.FindNode(add)->inputs[1].values[0], 0.0f),
            "a new node takes its inputs' defaults");

        Check(graph.Connect(*domain, PinRef{a, 0}, PinRef{add, 0}) == ConnectResult::Connected, "float widens into a vector4 input");
        Check(graph.Connect(*domain, PinRef{add, 0}, PinRef{v2, 0}) == ConnectResult::TypeMismatch, "a vector4 does not narrow into a float");
        Check(graph.Connect(*domain, PinRef{add, 0}, PinRef{add, 1}) == ConnectResult::SameNode, "a node may not feed itself");
        Check(graph.Connect(*domain, PinRef{a, 3}, PinRef{add, 1}) == ConnectResult::MissingPin, "a pin past the outputs is missing");
        Check(graph.Connect(*domain, PinRef{JBro::UInt64(99u), 0}, PinRef{add, 1}) == ConnectResult::MissingNode, "an unknown node is missing");

        // b -> add.A 는 a -> add.A 를 갈아 치운다. 입력 하나에 링크 하나다.
        Check(graph.Connect(*domain, PinRef{b, 0}, PinRef{add, 0}) == ConnectResult::Connected, "a second link into an input is accepted");
        Check(graph.GetLinks().Size() == 1 && graph.FindLinkInto(PinRef{add, 0})->from.node.Get() == b.Get(), "and replaces the first");

        // 순환: add2(A <- add) 뒤에 add(B <- add2) 는 고리다.
        const NodeId add2 = graph.AddNode(*domain, "Add", Vector2{400.0f, 0.0f});
        Check(graph.Connect(*domain, PinRef{add, 0}, PinRef{add2, 0}) == ConnectResult::Connected, "add feeds add2");
        Check(graph.Connect(*domain, PinRef{add2, 0}, PinRef{add, 1}) == ConnectResult::Cycle, "closing the loop is refused");
        Check(graph.GetLinks().Size() == 2, "a refused link leaves the graph as it was");

        // 출력은 여러 입력에 나갈 수 있다.
        Check(graph.Connect(*domain, PinRef{b, 0}, PinRef{add2, 1}) == ConnectResult::Connected, "an output fans out");

        Check(graph.RemoveNode(b) && graph.GetLinks().Size() == 1, "removing a node takes its links with it");
        Check(graph.FindLinkInto(PinRef{add2, 0}) != nullptr, "but leaves the others");

        // 정해 준 링크 번호는 노드 번호와도 겹치면 안 된다.
        Check(graph.Connect(*domain, PinRef{a, 0}, PinRef{add, 1}, add) == ConnectResult::IdInUse, "a link id may not reuse a node id");
    }

    void TestUniqueOutputAndInsert()
    {
        const JBro::OwnerPtr<Domain> domain = MakeMathDomain();
        Graph graph;
        Check(graph.AddNode(*domain, "Output", Vector2{}).Get() != InvalidId, "the first output node is added");
        Check(graph.AddNode(*domain, "Output", Vector2{}).Get() == InvalidId, "a second output node is refused");

        Node node;
        node.id = JBro::UInt64(40u);
        node.type = "Float";
        node.inputs.Add(PinValue{});
        Check(graph.InsertNode(node), "a node keeps the id it was inserted with");
        Check(false == graph.InsertNode(node), "the same id is not taken twice");
        Check(graph.GetNextId().Get() == 41, "inserting moves the next id past it");
        graph.SetNextId(JBro::UInt64(3u));
        Check(graph.GetNextId().Get() == 41, "a lower next id is lifted above the ids in use");
    }

    void TestTopologicalOrder()
    {
        const JBro::OwnerPtr<Domain> domain = MakeMathDomain();
        Graph graph;
        const NodeId output = graph.AddNode(*domain, "Output", Vector2{});
        const NodeId mul = graph.AddNode(*domain, "Multiply", Vector2{});
        const NodeId x = graph.AddNode(*domain, "Float", Vector2{});
        graph.Connect(*domain, PinRef{mul, 0}, PinRef{output, 0});
        graph.Connect(*domain, PinRef{x, 0}, PinRef{mul, 0});
        Array<NodeId> order;
        graph.TopologicalOrder(order);
        Check(order.Size() == 3, "every node is ordered");
        Check(order[0].Get() == x.Get() && order[1].Get() == mul.Get() && order[2].Get() == output.Get(),
            "inputs come before the nodes they feed, whatever order they were added in");
    }

    void TestEvaluate()
    {
        const JBro::OwnerPtr<Domain> domain = MakeMathDomain();
        Graph graph;
        const NodeId output = graph.AddNode(*domain, "Output", Vector2{});
        PinValue result;
        Check(EvaluateMath(graph, *domain, result) && Near(result.values[3], 1.0f) && Near(result.values[0], 0.0f),
            "an unconnected output gives its default");

        const NodeId color = graph.AddNode(*domain, "Color", Vector2{});
        const NodeId scale = graph.AddNode(*domain, "Float", Vector2{});
        const NodeId mul = graph.AddNode(*domain, "Multiply", Vector2{});
        PinValue half;
        half.values[0] = 0.5f;
        graph.SetInput(scale, 0, half);
        PinValue red;
        red.values[0] = 0.8f;
        graph.SetInput(color, 1, PinValue{});
        graph.SetInput(color, 0, red);
        graph.Connect(*domain, PinRef{color, 0}, PinRef{mul, 0});
        graph.Connect(*domain, PinRef{scale, 0}, PinRef{mul, 1});
        graph.Connect(*domain, PinRef{mul, 0}, PinRef{output, 0});
        Check(EvaluateMath(graph, *domain, result), "the graph evaluates");
        Check(Near(result.values[0], 0.4f) && Near(result.values[1], 0.0f) && Near(result.values[2], 0.5f) && Near(result.values[3], 0.5f),
            "color (0.8, 0, 1, 1) times a widened 0.5 is (0.4, 0, 0.5, 0.5)");

        const NodeId lerp = graph.AddNode(*domain, "Lerp", Vector2{});
        graph.Connect(*domain, PinRef{lerp, 0}, PinRef{output, 0});
        Check(EvaluateMath(graph, *domain, result) && Near(result.values[0], 0.5f), "lerp of 0 and 1 at its default 0.5 is 0.5");
        Check(EvaluateMathOutput(graph, *domain, mul, 0, result) && Near(result.values[0], 0.4f), "any output can be evaluated on its own");
    }

    void TestYamlRoundTrip()
    {
        const JBro::OwnerPtr<Domain> domain = MakeMathDomain();
        Graph graph;
        graph.SetDomain(String(MathDomainName));
        const NodeId output = graph.AddNode(*domain, "Output", Vector2{300.0f, 40.0f});
        const NodeId v3 = graph.AddNode(*domain, "Vector3", Vector2{-20.5f, 12.0f});
        PinValue z;
        z.values[0] = 0.25f;
        graph.SetInput(v3, 2, z);
        graph.Connect(*domain, PinRef{v3, 0}, PinRef{output, 0});
        const String text = WriteGraph(graph, domain.Get());

        String domainName;
        Check(ReadGraphDomain(text.View(), domainName) && domainName.View() == "Math", "the domain is read from the first key");

        Graph read;
        Array<String> problems;
        Check(ReadGraph(text.View(), domain.Get(), read, problems), "the written graph reads back");
        Check(problems.IsEmpty(), "with no problems");
        Check(read.GetNodes().Size() == 2 && read.GetLinks().Size() == 1, "with the same nodes and links");
        const Node* readV3 = read.FindNode(v3);
        Check(readV3 != nullptr && readV3->type.View() == "Vector3" && Near(readV3->position.x, -20.5f) && Near(readV3->inputs[2].values[0], 0.25f),
            "a node keeps its id, type, place and input values");
        Check(read.GetLinks()[0].id.Get() == graph.GetLinks()[0].id.Get(), "a link keeps its id");
        Check(read.GetNextId().Get() == graph.GetNextId().Get(), "and the next id carries over");
        Check(WriteGraph(read, domain.Get()) == text, "writing it again gives the same text");
    }

    // **주석과 경유점**(D-321). 둘 다 그래프의 것이라 YAML 로 왕복하고, 번호는 노드·링크와 한 줄을 쓴다.
    // 경유점은 잇기의 뜻을 바꾸지 않는다 - 갈아 치운 링크는 경유점 없이 선다.
    void TestCommentsAndWaypoints()
    {
        const JBro::OwnerPtr<Domain> domain = MakeMathDomain();
        Graph graph;
        graph.SetDomain(String(MathDomainName));
        const NodeId constant = graph.AddNode(*domain, "Float", Vector2{0.0f, 0.0f});
        const NodeId output = graph.AddNode(*domain, "Output", Vector2{400.0f, 0.0f});
        LinkId link;
        Check(graph.Connect(*domain, PinRef{constant, 0}, PinRef{output, 0}, LinkId{}, &link) == ConnectResult::Connected, "the probe link connects");
        Check(graph.InsertWaypoint(link, 0, Vector2{200.0f, 40.0f}) && graph.InsertWaypoint(link, 1, Vector2{300.0f, 80.0f})
                && graph.InsertWaypoint(link, 1, Vector2{250.0f, 60.0f}),
            "waypoints go in at the place asked for");
        Check(!graph.InsertWaypoint(link, 9, Vector2{}) && !graph.MoveWaypoint(link, 3, Vector2{}) && !graph.RemoveWaypoint(link, 3),
            "and not past the end");
        const Link* stored = graph.FindLink(link);
        Check(stored->waypoints.Size() == 3 && Near(stored->waypoints[1].x, 250.0f) && Near(stored->waypoints[2].x, 300.0f),
            "in order from the output to the input");
        Check(graph.MoveWaypoint(link, 0, Vector2{210.0f, 30.0f}) && graph.RemoveWaypoint(link, 2) && graph.FindLink(link)->waypoints.Size() == 2,
            "they move and go away by their place");

        JBro::Color color;
        color.R = 0.2f;
        color.G = 0.4f;
        color.B = 0.6f;
        color.A = 0.5f;
        const CommentId comment = graph.AddComment(String("Inputs"), Vector2{-30.0f, -50.0f}, Vector2{260.0f, 140.0f}, color);
        Check(comment.Get() != InvalidId && comment.Get() != link.Get() && comment.Get() != output.Get(), "a comment takes a number from the same line");
        Check(!graph.SetCommentRect(comment, Vector2{}, Vector2{0.0f, 10.0f}), "a comment never shrinks to nothing");
        Node clash;
        clash.id = comment;
        clash.type = String("Float");
        Check(!graph.InsertNode(clash), "a node cannot take a comment's number");

        const String text = WriteGraph(graph, domain.Get());
        Check(text.find("Comments:") != String::npos && text.find("Waypoints:") != String::npos, "both are written");
        Graph read;
        Array<String> problems;
        Check(ReadGraph(text.View(), domain.Get(), read, problems) && problems.IsEmpty(), "and read back with no problems");
        const Comment* readComment = read.FindComment(comment);
        Check(readComment != nullptr && readComment->title.View() == "Inputs" && Near(readComment->position.y, -50.0f)
                && Near(readComment->size.x, 260.0f) && Near(readComment->color.B, 0.6f) && Near(readComment->color.A, 0.5f),
            "a comment keeps its title, place, size and color");
        Check(read.FindLink(link) != nullptr && read.FindLink(link)->waypoints.Size() == 2 && Near(read.FindLink(link)->waypoints[0].x, 210.0f),
            "a link keeps its waypoints");
        Check(WriteGraph(read, domain.Get()) == text, "writing it again gives the same text");
        Check(read.GetNextId().Get() > comment.Get(), "and the next number is past the comment");

        // 주석이 없는 그래프는 키를 적지 않는다 - 주석 전에 쓴 파일과 같은 글자다.
        Graph plain;
        plain.SetDomain(String(MathDomainName));
        plain.AddNode(*domain, "Float", Vector2{});
        Check(WriteGraph(plain, domain.Get()).find("Comments") == String::npos, "a graph with no comments writes no comment key");

        // 입력을 다른 출력으로 갈아 치우면 새 링크다. 옛 링크의 경유점은 따라오지 않는다.
        const NodeId other = graph.AddNode(*domain, "Float", Vector2{0.0f, 200.0f});
        LinkId replaced;
        graph.Connect(*domain, PinRef{other, 0}, PinRef{output, 0}, LinkId{}, &replaced);
        Check(graph.FindLink(link) == nullptr && graph.FindLink(replaced)->waypoints.IsEmpty(), "a replacing link starts straight");
        Check(graph.RemoveComment(comment) && graph.FindComment(comment) == nullptr, "a comment goes away");
    }

    void TestYamlProblems()
    {
        const JBro::OwnerPtr<Domain> domain = MakeMathDomain();
        const char* text =
            "Domain: Math\n"
            "NextId: 9\n"
            "Nodes:\n"
            "  - Id: 1\n"
            "    Type: Float\n"
            "    X: 0\n"
            "    Y: 0\n"
            "    Inputs:\n"
            "      - Pin: Value\n"
            "        Value:\n"
            "          - 3\n"
            "      - Pin: Gone\n"
            "        Value:\n"
            "          - 7\n"
            "  - Id: 2\n"
            "    Type: Mystery\n"
            "    X: 10\n"
            "    Y: 0\n"
            "    Inputs:\n"
            "      - Index: 1\n"
            "        Value:\n"
            "          - 1\n"
            "          - 2\n"
            "          - 3\n"
            "          - 4\n"
            "  - Id: 3\n"
            "    Type: Output\n"
            "    X: 20\n"
            "    Y: 0\n"
            "Links:\n"
            "  - Id: 4\n"
            "    FromNode: 1\n"
            "    FromPin: Result\n"
            "    ToNode: 3\n"
            "    ToPin: Color\n"
            "  - Id: 5\n"
            "    FromNode: 2\n"
            "    FromPin: Out\n"
            "    ToNode: 3\n"
            "    ToPin: Color\n";
        Graph graph;
        Array<String> problems;
        Check(ReadGraph(text, domain.Get(), graph, problems), "a graph with soft problems still reads");
        Check(problems.Size() == 3, "unknown input, unknown type and the link to it are each reported");
        Check(graph.GetNodes().Size() == 3, "the unknown node type is kept");
        Check(graph.GetLinks().Size() == 1 && graph.GetLinks()[0].id.Get() == 4, "only the link that can be resolved is made");
        Check(Near(graph.FindNode(JBro::UInt64(1u))->inputs[0].values[0], 3.0f), "the known input is read");
        const Node* mystery = graph.FindNode(JBro::UInt64(2u));
        Check(mystery->inputs.Size() == 2 && Near(mystery->inputs[1].values[3], 4.0f), "the unknown node keeps its values by index");

        // 모르는 노드의 값은 다시 쓸 때 잃지 않는다.
        const String written = WriteGraph(graph, domain.Get());
        Graph again;
        Array<String> moreProblems;
        Check(ReadGraph(written.View(), domain.Get(), again, moreProblems), "the rewritten graph reads");
        const Node* kept = again.FindNode(JBro::UInt64(2u));
        Check(kept != nullptr && kept->inputs.Size() == 2 && Near(kept->inputs[1].values[2], 3.0f), "and still carries the unknown node's values");

        Graph broken;
        Array<String> brokenProblems;
        Check(false == ReadGraph("Nodes: [", domain.Get(), broken, brokenProblems) || false == brokenProblems.IsEmpty(),
            "broken yaml is refused or reported");
    }

    void TestRegistry()
    {
        DomainRegistry registry;
        Check(registry.Add(MakeMathDomain()), "a domain is registered");
        Check(false == registry.Add(MakeMathDomain()), "the same name is not registered twice");
        Check(registry.Find("Math") != nullptr && registry.Find("Shader") == nullptr, "domains are found by name");
    }
}

JBro::Int32 RunNodeGraphTests()
{
    TestConnectRules();
    TestUniqueOutputAndInsert();
    TestTopologicalOrder();
    TestEvaluate();
    TestYamlRoundTrip();
    TestYamlProblems();
    TestCommentsAndWaypoints();
    TestRegistry();
    std::cout << "Node graph tests passed.\n";
    return 0;
}
